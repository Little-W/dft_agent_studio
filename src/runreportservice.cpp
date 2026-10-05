#include "runreportservice.h"

#include "studiopaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QIODevice>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <zlib.h>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QString evidenceValue(const QJsonValue &value, const QString &fallback) {
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (value.isBool())
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    return QString::fromUtf8(QJsonDocument(value.isObject() ? QJsonDocument(value.toObject())
                                                           : QJsonDocument(value.toArray()))
                                 .toJson(QJsonDocument::Compact));
}

bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool storedThreadMatches(const QString &path, const QString &threadId, const QString &rootThreadId,
                         const QString &projectId, const QString &workspace) {
    static const QByteArray magic("DFTTHR1\0", 8);
    constexpr qsizetype maximumBytes = 32 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes)
        return false;
    const QByteArray packed = file.readAll();
    if (!packed.startsWith(magic))
        return false;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed.constData() + magic.size()));
    stream.avail_in = static_cast<uInt>(packed.size() - magic.size());
    if (inflateInit(&stream) != Z_OK)
        return false;
    QByteArray decoded;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const qsizetype produced = chunk.size() - static_cast<qsizetype>(stream.avail_out);
        if (decoded.size() + produced > maximumBytes) {
            inflateEnd(&stream);
            return false;
        }
        decoded.append(chunk.constData(), produced);
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END)
        return false;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(decoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject thread = document.object();
    return thread.value(QStringLiteral("id")).toString() == threadId
        && thread.value(QStringLiteral("project_id")).toString() == projectId
        && QFileInfo(thread.value(QStringLiteral("workspace")).toString()).canonicalFilePath() == workspace
        && (threadId == rootThreadId
                ? thread.value(QStringLiteral("parent_thread_id")).toString().isEmpty()
                : thread.value(QStringLiteral("parent_thread_id")).toString() == rootThreadId);
}

QString withExecutionEvidence(QString markdown, const QJsonObject &evidence) {
    markdown += QStringLiteral("\n\n### 本轮受控运行记录（运行时生成）\n");
    if (!evidence.value(QStringLiteral("current_turn_run")).toBool()) {
        markdown += QStringLiteral("- 本回合没有新的受控 DFT 运行；历史证据不能证明本回合目标已完成。\n");
        return markdown;
    }
    const QJsonObject drc = evidence.value(QStringLiteral("drc")).toObject();
    const QJsonObject atpg = evidence.value(QStringLiteral("atpg")).toObject();
    const QJsonObject execution = evidence.value(QStringLiteral("execution")).toObject();
    const QJsonValue coverage = atpg.value(QStringLiteral("coverage_percent"));
    const QJsonValue target = atpg.value(QStringLiteral("target_percent"));
    const QJsonValue patterns = atpg.value(QStringLiteral("patterns"));
    markdown += QStringLiteral("- Run：`%1`\n").arg(evidenceValue(evidence.value(QStringLiteral("run_id")), QStringLiteral("未提供")));
    markdown += QStringLiteral("- 运行/交叉验证状态：`%1`；目标达成标记：`%2`；干净完成：`%3`\n")
        .arg(evidenceValue(evidence.value(QStringLiteral("status")), QStringLiteral("unknown")),
             evidenceValue(evidence.value(QStringLiteral("objective_met")), QStringLiteral("无数据")),
             evidenceValue(execution.value(QStringLiteral("completed_cleanly")), QStringLiteral("False")));
    markdown += QStringLiteral("- DFT DRC：`%1`，允许上限：`%2`\n")
        .arg(evidenceValue(drc.value(QStringLiteral("observed_violations")), QStringLiteral("无数据")),
             evidenceValue(drc.value(QStringLiteral("maximum_allowed")), QStringLiteral("无数据")));
    markdown += QStringLiteral("- ATPG coverage：`%1`%，目标：`%2`%；patterns：`%3`\n")
        .arg(evidenceValue(coverage, QStringLiteral("无数据")),
             evidenceValue(target, QStringLiteral("无数据")),
             evidenceValue(patterns, QStringLiteral("无数据")));
    markdown += QStringLiteral("- 本回合记录到 `%1` 次 DFT 执行，涉及 `%2` 个不同 workspace。执行次数和独立 workspace 数是运行时统计，不由报告文字推断。\n")
        .arg(evidenceValue(evidence.value(QStringLiteral("execution_count")), QStringLiteral("0")),
             evidenceValue(evidence.value(QStringLiteral("independent_execution_count")), QStringLiteral("0")));
    const QString reviewKind = evidence.value(QStringLiteral("evidence_review_kind")).toString();
    const QString reviewPasses = evidenceValue(evidence.value(QStringLiteral("evidence_review_passes")), QStringLiteral("0"));
    if (reviewKind == QLatin1String("same_execution_evidence_review")) {
        markdown += QStringLiteral("- 证据复核：同一执行结果复核 `%1` 轮；这不是额外的独立 EDA 运行。\n")
            .arg(reviewPasses);
    } else if (!reviewKind.isEmpty()) {
        markdown += QStringLiteral("- 证据复核类型：`%1`；复核轮数：`%2`。\n").arg(reviewKind, reviewPasses);
    }
    markdown += QStringLiteral("- 证据目录：`%1`\n")
        .arg(evidenceValue(evidence.value(QStringLiteral("workspace")), QStringLiteral("未提供")));
    const QJsonArray evidenceRuns = evidence.value(QStringLiteral("evidence_runs")).toArray();
    if (!evidenceRuns.isEmpty()) {
        markdown += QStringLiteral("- 本回合执行清单：\n");
        for (qsizetype i = 0; i < qMin<qsizetype>(16, evidenceRuns.size()); ++i) {
            const QJsonObject run = evidenceRuns.at(i).toObject();
            const QJsonObject runAtpg = run.value(QStringLiteral("atpg")).toObject();
            const QJsonObject runDrc = run.value(QStringLiteral("drc")).toObject();
            markdown += QStringLiteral("  - `%1`；status=`%2`；workspace=`%3`；coverage=`%4`%；DRC violations=`%5`；evidence=`%6`\n")
                .arg(evidenceValue(run.value(QStringLiteral("run_id")), QStringLiteral("unknown")),
                     evidenceValue(run.value(QStringLiteral("status")), QStringLiteral("unknown")),
                     evidenceValue(run.value(QStringLiteral("workspace")), QStringLiteral("unknown")),
                     evidenceValue(runAtpg.value(QStringLiteral("coverage_percent")), QStringLiteral("n/a")),
                     evidenceValue(runDrc.value(QStringLiteral("observed_violations")), QStringLiteral("n/a")),
                     evidenceValue(run.value(QStringLiteral("evidence_file")), QStringLiteral("n/a")));
        }
    }
    const QJsonArray blockers = atpg.value(QStringLiteral("scan_chain_blockers")).toArray();
    if (!blockers.isEmpty()) {
        QJsonArray bounded;
        for (qsizetype i = 0; i < qMin<qsizetype>(12, blockers.size()); ++i)
            bounded.append(blockers.at(i));
        markdown += QStringLiteral("- ATPG 阻断链：`%1`\n")
            .arg(QString::fromUtf8(QJsonDocument(bounded).toJson(QJsonDocument::Compact)));
    }
    const QJsonArray errors = execution.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty()) {
        markdown += QStringLiteral("- 运行错误：\n");
        for (qsizetype i = 0; i < qMin<qsizetype>(8, errors.size()); ++i)
            markdown += QStringLiteral("  - %1\n").arg(evidenceValue(errors.at(i), QString{}).left(500));
    }
    if (evidence.value(QStringLiteral("evidence_file")).isString())
        markdown += QStringLiteral("- 证据记录：`%1`\n").arg(evidence.value(QStringLiteral("evidence_file")).toString());
    return markdown;
}
}

QVariantMap RunReportService::save(const QVariantMap &project, const QVariantMap &payload,
                                  const QString &agentRoot) {
    QString sessionId = payload.value(QStringLiteral("session_id")).toString().trimmed();
    const QString projectId = payload.value(QStringLiteral("project_id")).toString().trimmed();
    const QString title = payload.value(QStringLiteral("title")).toString().simplified();
    const QString category = payload.value(QStringLiteral("category")).toString().trimmed();
    QString markdown = payload.value(QStringLiteral("markdown")).toString().trimmed();
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    if (!safeId.match(sessionId).hasMatch() || !safeId.match(projectId).hasMatch())
        return failure(QStringLiteral("根会话或项目 ID 无效，无法归档报告。"));
    if (title.isEmpty() || title.size() > 160)
        return failure(QStringLiteral("报告标题必须为 1 至 160 个字符。"));
    if (category != QStringLiteral("run_report") && category != QStringLiteral("design_summary"))
        return failure(QStringLiteral("报告分类必须是 run_report 或 design_summary。"));
    if (markdown.isEmpty() || markdown.toUtf8().size() > 1'000'000)
        return failure(QStringLiteral("报告正文为空或超过 1 MB。"));

    const QString configuredProjectId = project.value(QStringLiteral("id")).toString().trimmed();
    const QString workspace = QFileInfo(studioAbsolutePath(
        project.value(QStringLiteral("root")).toString(), agentRoot)).canonicalFilePath();
    if (projectId != configuredProjectId || workspace.isEmpty())
        return failure(QStringLiteral("当前项目与报告归属不匹配。"));
    const QString threadsRoot = QDir(studioDataRoot(agentRoot)).filePath(QStringLiteral("agent_runtime/threads"));
    const QString canonicalThreadsRoot = QFileInfo(threadsRoot).canonicalFilePath();
    QString groupPath;
    QString metadataPath;
    const auto inspectGroup = [&](const QString &candidate) {
        const QFileInfo candidateInfo(candidate);
        const QString canonicalCandidate = candidateInfo.canonicalFilePath();
        const QString candidateMetadata = QDir(candidate).filePath(QStringLiteral("session.json"));
        const QFileInfo candidateMetadataInfo(candidateMetadata);
        if (!candidateInfo.isDir() || candidateInfo.isSymLink()
            || canonicalCandidate.isEmpty()
            || !canonicalCandidate.startsWith(canonicalThreadsRoot + QDir::separator())
            || !candidateMetadataInfo.isFile() || candidateMetadataInfo.isSymLink())
            return false;
        QFile candidateMetadataFile(candidateMetadata);
        if (!candidateMetadataFile.open(QIODevice::ReadOnly) || candidateMetadataFile.size() > 2 * 1024 * 1024)
            return false;
        QJsonParseError candidateError{};
        const QJsonDocument candidateDocument = QJsonDocument::fromJson(candidateMetadataFile.readAll(), &candidateError);
        if (candidateError.error != QJsonParseError::NoError || !candidateDocument.isObject())
            return false;
        const QJsonObject candidateMetadataObject = candidateDocument.object();
        const QString candidateRootId = candidateMetadataObject.value(QStringLiteral("root_thread_id")).toString();
        bool ownsThread = false;
        bool memberFound = false;
        const QJsonArray members = candidateMetadataObject.value(QStringLiteral("threads")).toArray();
        for (const QJsonValue &memberValue : members) {
            if (memberValue.toObject().value(QStringLiteral("id")).toString() == sessionId) {
                const QString threadFile = QDir(canonicalCandidate).filePath(sessionId + QStringLiteral(".thread.bin"));
                const QFileInfo threadInfo(threadFile);
                memberFound = true;
                ownsThread = threadInfo.isFile() && !threadInfo.isSymLink()
                    && threadInfo.canonicalFilePath().startsWith(canonicalCandidate + QDir::separator())
                    && storedThreadMatches(threadFile, sessionId, candidateRootId, projectId, workspace);
                break;
            }
        }
        if (!memberFound && candidateRootId == sessionId) {
            const QString rootFile = QDir(canonicalCandidate).filePath(sessionId + QStringLiteral(".thread.bin"));
            const QFileInfo rootInfo(rootFile);
            ownsThread = rootInfo.isFile() && !rootInfo.isSymLink()
                && rootInfo.canonicalFilePath().startsWith(canonicalCandidate + QDir::separator())
                && storedThreadMatches(rootFile, sessionId, candidateRootId, projectId, workspace);
        }
        if (!ownsThread)
            return false;
        groupPath = canonicalCandidate;
        metadataPath = candidateMetadata;
        return true;
    };
    if (canonicalThreadsRoot.isEmpty())
        return failure(QStringLiteral("Studio 会话存储目录不可用。"));
    const QString directGroup = QDir(threadsRoot).filePath(sessionId);
    if (!inspectGroup(directGroup)) {
        QDirIterator groups(threadsRoot, {QStringLiteral("session.json")}, QDir::Files,
                            QDirIterator::Subdirectories);
        while (groups.hasNext()) {
            const QString candidate = QFileInfo(groups.next()).absolutePath();
            if (inspectGroup(candidate))
                break;
        }
    }
    if (groupPath.isEmpty())
        return failure(QStringLiteral("根会话目录不可用或超出 Studio 会话存储范围。"));
    const QString canonicalGroup = groupPath;
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly) || metadataFile.size() > 2 * 1024 * 1024)
        return failure(QStringLiteral("根会话索引无法读取。"));
    QJsonParseError parseError{};
    const QJsonDocument metadataDocument = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !metadataDocument.isObject())
        return failure(QStringLiteral("根会话索引格式无效。"));
    const QJsonObject metadata = metadataDocument.object();
    sessionId = metadata.value(QStringLiteral("root_thread_id")).toString();
    if (sessionId.isEmpty()
        || metadata.value(QStringLiteral("project_id")).toString() != projectId
        || QFileInfo(metadata.value(QStringLiteral("workspace")).toString()).canonicalFilePath() != workspace)
        return failure(QStringLiteral("当前根会话与项目不匹配，拒绝归档报告。"));

    const QJsonValue evidenceValue = QJsonValue::fromVariant(payload.value(QStringLiteral("execution_evidence")));
    const bool hasEvidence = evidenceValue.isObject();
    const QJsonObject evidence = evidenceValue.toObject();
    if (category == QStringLiteral("run_report") && hasEvidence)
        markdown = withExecutionEvidence(markdown, evidence);
    const QByteArray markdownBytes = (markdown + QLatin1Char('\n')).toUtf8();
    if (markdownBytes.isEmpty() || markdownBytes.size() > 1'000'000)
        return failure(QStringLiteral("添加运行时证据后，报告正文超过 1 MB。"));

    const QString reportDirectory = QDir(canonicalGroup).filePath(QStringLiteral("run_reports"));
    if (!QDir().mkpath(reportDirectory))
        return failure(QStringLiteral("无法创建报告目录。"));
    const QString canonicalReportDirectory = QFileInfo(reportDirectory).canonicalFilePath();
    if (canonicalReportDirectory.isEmpty()
        || !canonicalReportDirectory.startsWith(canonicalGroup + QDir::separator()))
        return failure(QStringLiteral("报告目录超出根会话存储范围。"));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString reportId = QUuid::createUuid().toString(QUuid::Id128);
    const QString stem = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'_"))
        + reportId.left(10);
    const QString markdownName = stem + QStringLiteral(".md");
    const QString markdownPath = QDir(canonicalReportDirectory).filePath(markdownName);
    const QString recordPath = QDir(canonicalReportDirectory).filePath(stem + QStringLiteral(".json"));
    QString error;
    if (!writeAtomic(markdownPath, markdownBytes, &error))
        return failure(error);
    QJsonObject record{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("report_id"), reportId},
        {QStringLiteral("title"), title},
        {QStringLiteral("category"), category},
        {QStringLiteral("project_id"), projectId},
        {QStringLiteral("project_name"), payload.value(QStringLiteral("project_name")).toString().trimmed().left(160)},
        {QStringLiteral("session_id"), sessionId},
        {QStringLiteral("session_title"), metadata.value(QStringLiteral("name")).toString().trimmed().left(160)},
        {QStringLiteral("created_at"), now},
        {QStringLiteral("updated_at"), now},
        {QStringLiteral("report_file"), markdownName},
    };
    if (hasEvidence)
        record.insert(QStringLiteral("execution_evidence"), evidence);
    const QByteArray recordBytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (!writeAtomic(recordPath, recordBytes, &error)) {
        QFile::remove(markdownPath);
        return failure(error);
    }
    QVariantMap result = record.toVariantMap();
    result.insert(QStringLiteral("path"), markdownPath);
    result.insert(QStringLiteral("size"), markdownBytes.size());
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

QVariantMap RunReportService::list(const QVariantMap &project, const QVariantMap &payload,
                                   const QString &agentRoot) {
    const QString projectId = project.value(QStringLiteral("id")).toString().trimmed();
    const QString workspace = QFileInfo(studioAbsolutePath(
        project.value(QStringLiteral("root")).toString(), agentRoot)).canonicalFilePath();
    const QString requestedSession = payload.value(QStringLiteral("session_id")).toString().trimmed();
    const QString requestedCategory = payload.value(QStringLiteral("category")).toString().trimmed();
    if (projectId.isEmpty() || workspace.isEmpty())
        return failure(QStringLiteral("当前项目不可用于查询报告。"));
    if (!requestedCategory.isEmpty() && requestedCategory != QStringLiteral("run_report")
        && requestedCategory != QStringLiteral("design_summary"))
        return failure(QStringLiteral("报告分类必须是 run_report 或 design_summary。"));

    const QString threadsRoot = QDir(studioDataRoot(agentRoot)).filePath(
        QStringLiteral("agent_runtime/threads"));
    const QFileInfo threadsInfo(threadsRoot);
    const QString canonicalThreadsRoot = threadsInfo.canonicalFilePath();
    if (!threadsInfo.isDir() || threadsInfo.isSymLink() || canonicalThreadsRoot.isEmpty())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("reports"), QVariantList{}}}}};

    QVariantList reports;
    QDirIterator metadataFiles(threadsRoot, {QStringLiteral("session.json")}, QDir::Files,
                               QDirIterator::Subdirectories);
    while (metadataFiles.hasNext()) {
        const QString metadataPath = metadataFiles.next();
        const QFileInfo metadataInfo(metadataPath);
        const QString canonicalGroup = QFileInfo(metadataInfo.absolutePath()).canonicalFilePath();
        if (metadataInfo.isSymLink() || canonicalGroup.isEmpty()
            || !canonicalGroup.startsWith(canonicalThreadsRoot + QDir::separator()))
            continue;
        QFile metadataFile(metadataPath);
        if (!metadataFile.open(QIODevice::ReadOnly) || metadataFile.size() > 2 * 1024 * 1024)
            continue;
        QJsonParseError metadataError{};
        const QJsonDocument metadataDocument = QJsonDocument::fromJson(metadataFile.readAll(), &metadataError);
        if (metadataError.error != QJsonParseError::NoError || !metadataDocument.isObject())
            continue;
        const QJsonObject metadata = metadataDocument.object();
        const QString rootId = metadata.value(QStringLiteral("root_thread_id")).toString();
        if (rootId.isEmpty() || metadata.value(QStringLiteral("project_id")).toString() != projectId
            || QFileInfo(metadata.value(QStringLiteral("workspace")).toString()).canonicalFilePath() != workspace
            || (!requestedSession.isEmpty() && requestedSession != rootId))
            continue;

        const QString reportsPath = QDir(canonicalGroup).filePath(QStringLiteral("run_reports"));
        const QFileInfo reportsInfo(reportsPath);
        const QString canonicalReports = reportsInfo.canonicalFilePath();
        if (!reportsInfo.isDir() || reportsInfo.isSymLink() || canonicalReports.isEmpty()
            || !canonicalReports.startsWith(canonicalGroup + QDir::separator()))
            continue;
        QDirIterator records(canonicalReports, {QStringLiteral("*.json")}, QDir::Files,
                             QDirIterator::NoIteratorFlags);
        while (records.hasNext()) {
            const QFileInfo recordInfo(records.next());
            const QString canonicalRecord = recordInfo.canonicalFilePath();
            if (recordInfo.isSymLink() || canonicalRecord.isEmpty()
                || !canonicalRecord.startsWith(canonicalReports + QDir::separator()))
                continue;
            QFile recordFile(canonicalRecord);
            if (!recordFile.open(QIODevice::ReadOnly) || recordFile.size() > 256 * 1024)
                continue;
            QJsonParseError recordError{};
            const QJsonDocument recordDocument = QJsonDocument::fromJson(recordFile.readAll(), &recordError);
            if (recordError.error != QJsonParseError::NoError || !recordDocument.isObject())
                continue;
            const QJsonObject record = recordDocument.object();
            const QString category = record.value(QStringLiteral("category")).toString();
            const QString filename = record.value(QStringLiteral("report_file")).toString();
            static const QRegularExpression safeFilename(QStringLiteral("^[A-Za-z0-9._-]{1,180}\\.md$"));
            if (record.value(QStringLiteral("report_id")).toString().isEmpty()
                || record.value(QStringLiteral("session_id")).toString() != rootId
                || record.value(QStringLiteral("project_id")).toString() != projectId
                || (category != QStringLiteral("run_report") && category != QStringLiteral("design_summary"))
                || (!requestedCategory.isEmpty() && category != requestedCategory)
                || !safeFilename.match(filename).hasMatch())
                continue;
            const QFileInfo markdownInfo(QDir(canonicalReports).filePath(filename));
            const QString canonicalMarkdown = markdownInfo.canonicalFilePath();
            if (!markdownInfo.isFile() || markdownInfo.isSymLink() || markdownInfo.size() > 1'000'000
                || canonicalMarkdown.isEmpty()
                || !canonicalMarkdown.startsWith(canonicalReports + QDir::separator()))
                continue;
            QVariantMap item = record.toVariantMap();
            item.insert(QStringLiteral("path"), canonicalMarkdown);
            item.insert(QStringLiteral("size"), markdownInfo.size());
            reports.append(item);
        }
    }
    std::sort(reports.begin(), reports.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("updated_at")).toString()
            > right.toMap().value(QStringLiteral("updated_at")).toString();
    });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("reports"), reports}}}};
}

QVariantMap RunReportService::read(const QVariantMap &project, const QVariantMap &payload,
                                   const QString &agentRoot) {
    const QString reportId = payload.value(QStringLiteral("report_id")).toString().trimmed();
    if (reportId.isEmpty() || reportId.size() > 128)
        return failure(QStringLiteral("报告 ID 不可用。"));
    const QVariantMap listed = list(project, payload, agentRoot);
    if (!listed.value(QStringLiteral("ok")).toBool())
        return listed;
    const QVariantList reports = listed.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("reports")).toList();
    for (const QVariant &value : reports) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("report_id")).toString() != reportId)
            continue;
        QFile file(record.value(QStringLiteral("path")).toString());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1'000'000)
            return failure(QStringLiteral("报告正文无法读取。"));
        QVariantMap result = record;
        result.insert(QStringLiteral("markdown"), QString::fromUtf8(file.readAll()));
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
    }
    return failure(QStringLiteral("找不到当前项目下的报告。"));
}
