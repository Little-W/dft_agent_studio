#include "trainingdatasetservice.h"
#include "trainingpolicies.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QByteArray sha256(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            *error = QStringLiteral("读取 %1 失败：%2").arg(path, file.errorString());
            return {};
        }
        hash.addData(chunk);
    }
    return hash.result().toHex();
}

QJsonValue canonicalValue(const QJsonValue &value) {
    if (value.isObject()) {
        QJsonObject sorted;
        QStringList keys = value.toObject().keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &key : keys)
            sorted.insert(key, canonicalValue(value.toObject().value(key)));
        return sorted;
    }
    if (value.isArray()) {
        QJsonArray sorted;
        for (const QJsonValue &item : value.toArray())
            sorted.append(canonicalValue(item));
        return sorted;
    }
    return value;
}

QByteArray canonicalJson(const QJsonValue &value) {
    const QJsonValue canonical = canonicalValue(value);
    if (canonical.isObject())
        return QJsonDocument(canonical.toObject()).toJson(QJsonDocument::Compact);
    if (canonical.isArray())
        return QJsonDocument(canonical.toArray()).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonArray{canonical}).toJson(QJsonDocument::Compact).sliced(1).chopped(1);
}

QString rowPrefix(const QString &source, qsizetype line) {
    return QStringLiteral("%1:%2 ").arg(source).arg(line);
}

QString validateRow(const QJsonObject &row, const QString &source, qsizetype line) {
    const QJsonValue messagesValue = row.value(QStringLiteral("messages"));
    if (!messagesValue.isArray() || messagesValue.toArray().size() < 3)
        return rowPrefix(source, line) + QStringLiteral("缺少 system、user、assistant 三条消息");
    const QJsonArray messages = messagesValue.toArray();
    QStringList roles;
    roles.reserve(messages.size());
    for (const QJsonValue &value : messages) {
        if (!value.isObject())
            return rowPrefix(source, line) + QStringLiteral("消息必须是对象");
        roles.append(value.toObject().value(QStringLiteral("role")).toString());
    }
    if (roles.at(0) != QLatin1String("system") || roles.at(1) != QLatin1String("user")
        || roles.constLast() != QLatin1String("assistant"))
        return rowPrefix(source, line) + QStringLiteral("消息角色顺序错误");
    static const QSet<QString> allowedRoles{
        QStringLiteral("system"), QStringLiteral("user"), QStringLiteral("assistant"), QStringLiteral("tool")};
    for (qsizetype index = 0; index < messages.size(); ++index) {
        if (!allowedRoles.contains(roles.at(index)))
            return rowPrefix(source, line) + QStringLiteral("含有不支持的消息角色");
        const QJsonObject message = messages.at(index).toObject();
        const QString content = message.value(QStringLiteral("content")).toString();
        const QJsonValue calls = message.value(QStringLiteral("tool_calls"));
        const bool assistantCalls = roles.at(index) == QLatin1String("assistant")
            && calls.isArray() && !calls.toArray().isEmpty();
        if (content.trimmed().isEmpty() && !assistantCalls)
            return rowPrefix(source, line) + QStringLiteral("消息内容不能为空");
    }
    const QJsonValue tools = row.value(QStringLiteral("tools"));
    if (!tools.isUndefined() && !tools.isNull() && (!tools.isArray() || tools.toArray().isEmpty()))
        return rowPrefix(source, line) + QStringLiteral("tools 必须是非空列表");
    const QJsonValue metadataValue = row.value(QStringLiteral("metadata"));
    if (!metadataValue.isObject())
        return rowPrefix(source, line) + QStringLiteral("缺少训练记录信息");
    const QJsonObject metadata = metadataValue.toObject();
    const QString policy = metadata.value(QStringLiteral("training_policy")).toString();
    if (!supportedTrainingPolicies().contains(policy))
        return rowPrefix(source, line) + QStringLiteral("不支持训练策略 %1").arg(policy);
    if (metadata.value(QStringLiteral("training_category")).toString().trimmed().isEmpty())
        return rowPrefix(source, line) + QStringLiteral("缺少 training_category");
    if (metadata.value(QStringLiteral("source")).toString().trimmed().isEmpty())
        return rowPrefix(source, line) + QStringLiteral("缺少 source");
    return {};
}

QJsonObject counterObject(const QHash<QString, int> &counter) {
    QJsonObject output;
    QStringList keys = counter.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys)
        output.insert(key, counter.value(key));
    return output;
}

const QStringList kEvidenceVariants{
    QStringLiteral("canonical_review"), QStringLiteral("evidence_summary"),
    QStringLiteral("completion_decision"), QStringLiteral("handoff_note"),
    QStringLiteral("scope_guardrail")};
const QStringList kBlockedVariants{
    QStringLiteral("blocker_diagnosis"), QStringLiteral("repair_denial"),
    QStringLiteral("manual_escalation"), QStringLiteral("quality_gate_denial"),
    QStringLiteral("evidence_preservation")};

QJsonObject objectAt(const QJsonObject &object, const QString &key) {
    return object.value(key).toObject();
}

QString firstError(const QJsonObject &result) {
    const QJsonArray errors = objectAt(result, QStringLiteral("execution"))
                                  .value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty())
        return errors.first().toString(errors.first().toVariant().toString());
    const QJsonArray blockers = objectAt(result, QStringLiteral("cross_validation"))
                                    .value(QStringLiteral("blockers")).toArray();
    if (!blockers.isEmpty())
        return blockers.first().toString(blockers.first().toVariant().toString());
    return QStringLiteral("未发现可解析的首个工具错误；仍只能以记录状态为准。");
}

QJsonObject supervisorWarning(const QJsonObject &episode, const QJsonObject &result) {
    if (episode.value(QStringLiteral("supervisor_warning")).isObject())
        return episode.value(QStringLiteral("supervisor_warning")).toObject();
    if (result.value(QStringLiteral("supervisor_warning")).isObject())
        return result.value(QStringLiteral("supervisor_warning")).toObject();
    return objectAt(objectAt(result, QStringLiteral("source")), QStringLiteral("supervisor_warning"));
}

QString valueText(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return value.toVariant().toString();
}

QJsonObject resultFromEpisode(const QJsonObject &episode) {
    const QJsonArray trace = episode.value(QStringLiteral("tool_trace")).toArray();
    if (trace.isEmpty())
        return {};
    const QJsonValue value = trace.last().toObject().value(QStringLiteral("result"));
    if (!value.isString())
        return {};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject evidenceFacts(const QJsonObject &record) {
    const QJsonObject episode = objectAt(record, QStringLiteral("episode"));
    const QJsonObject result = resultFromEpisode(episode);
    const QJsonObject cross = objectAt(result, QStringLiteral("cross_validation"));
    const QJsonObject execution = objectAt(result, QStringLiteral("execution"));
    const QJsonObject warning = supervisorWarning(episode, result);
    const QJsonArray acceptance = result.value(QStringLiteral("acceptance")).toArray();
    QJsonValue coverage;
    for (const QJsonValue &item : acceptance) {
        const QJsonValue value = item.toObject().value(QStringLiteral("observed_percentage"));
        if (value.isDouble())
            coverage = value;
    }
    QString evidence = result.value(QStringLiteral("evidence_file")).toString();
    if (evidence.isEmpty())
        evidence = episode.value(QStringLiteral("evidence_file")).toString();
    return {
        {QStringLiteral("project"), episode.value(QStringLiteral("project_id")).toString(
             result.value(QStringLiteral("project")).toString(QStringLiteral("受控 DFT 项目")))},
        {QStringLiteral("scope"), episode.value(QStringLiteral("scope")).toString(QStringLiteral("dft"))},
        {QStringLiteral("status"), cross.value(QStringLiteral("status")).toString(
             result.value(QStringLiteral("conclusion")).toString(QStringLiteral("需按证据复核")))},
        {QStringLiteral("rounds"), cross.value(QStringLiteral("rounds"))},
        {QStringLiteral("stable_snapshot"), cross.value(QStringLiteral("stable_snapshot"))},
        {QStringLiteral("error_count"), execution.value(QStringLiteral("error_count"))},
        {QStringLiteral("first_error"), firstError(result)},
        {QStringLiteral("warning"), warning},
        {QStringLiteral("evidence_path"), evidence},
        {QStringLiteral("verification_path"), cross.value(QStringLiteral("verification_file")).toString()},
        {QStringLiteral("coverage"), coverage},
    };
}

QString evidencePrompt(const QJsonObject &facts, const QString &variant) {
    QStringList items{
        QStringLiteral("项目=%1").arg(facts.value(QStringLiteral("project")).toString()),
        QStringLiteral("受控状态=%1").arg(facts.value(QStringLiteral("status")).toString())};
    if (!facts.value(QStringLiteral("rounds")).isUndefined() && !facts.value(QStringLiteral("rounds")).isNull())
        items.append(QStringLiteral("交叉验证轮数=%1").arg(valueText(facts.value(QStringLiteral("rounds")))));
    if (!facts.value(QStringLiteral("stable_snapshot")).isUndefined() && !facts.value(QStringLiteral("stable_snapshot")).isNull())
        items.append(QStringLiteral("快照一致=%1").arg(valueText(facts.value(QStringLiteral("stable_snapshot")))));
    if (!facts.value(QStringLiteral("error_count")).isUndefined() && !facts.value(QStringLiteral("error_count")).isNull())
        items.append(QStringLiteral("解析错误数=%1").arg(valueText(facts.value(QStringLiteral("error_count")))));
    if (facts.value(QStringLiteral("coverage")).isDouble())
        items.append(QStringLiteral("报告覆盖率=%1%").arg(facts.value(QStringLiteral("coverage")).toDouble(), 0, 'f', 2));
    const QJsonObject warning = objectAt(facts, QStringLiteral("warning"));
    if (!warning.isEmpty())
        items.append(QStringLiteral("监管告警=%1").arg(warning.value(QStringLiteral("kind")).toString(QStringLiteral("manual_review"))));
    const QString evidence = facts.value(QStringLiteral("evidence_path")).toString();
    if (!evidence.isEmpty())
        items.append(QStringLiteral("证据=%1").arg(evidence));
    static const QHash<QString, QString> requests{
        {QStringLiteral("canonical_review"), QStringLiteral("请依据这份已审核记录给出受控结论。")},
        {QStringLiteral("evidence_summary"), QStringLiteral("请写一段仅基于证据的核查摘要，不能补充未执行事实。")},
        {QStringLiteral("completion_decision"), QStringLiteral("请判断是否可以称为完成或通过，并说明判断依据。")},
        {QStringLiteral("handoff_note"), QStringLiteral("请写给 DFT 工程师的交接说明，保留下一步和证据路径。")},
        {QStringLiteral("scope_guardrail"), QStringLiteral("请说明本条记录能证明什么、不能证明什么；禁止虚构修复或工程复核结论。")},
        {QStringLiteral("blocker_diagnosis"), QStringLiteral("请准确诊断该阻塞，状态不得写成 verified。")},
        {QStringLiteral("repair_denial"), QStringLiteral("请区分工具/配置阻塞与 RTL 设计缺陷，说明何种新鲜报告和源码证据足以支持隔离副本 RTL 修改，以及修改后的验证步骤。")},
        {QStringLiteral("manual_escalation"), QStringLiteral("请给监管工程师一条仅基于当前证据的人工升级建议。")},
        {QStringLiteral("quality_gate_denial"), QStringLiteral("请判断是否满足质量门槛；有阻塞、DRC 或缺失覆盖率时不得宣称达标。")},
        {QStringLiteral("evidence_preservation"), QStringLiteral("请说明应保留哪些证据以及为何必须重新运行而不是把本次写成通过。")},
    };
    return items.join(QStringLiteral("；")) + QStringLiteral("。") + requests.value(variant);
}

QString derivedAnswer(const QJsonObject &facts, const QString &variant) {
    const QString project = QStringLiteral("`%1`").arg(facts.value(QStringLiteral("project")).toString());
    const QString status = facts.value(QStringLiteral("status")).toString();
    const QString path = facts.value(QStringLiteral("evidence_path")).toString();
    const QString evidence = path.isEmpty() ? QStringLiteral("证据路径未在该 episode 中结构化记录。")
        : QStringLiteral("证据：`%1`。").arg(path);
    const QJsonObject warning = objectAt(facts, QStringLiteral("warning"));
    if (!warning.isEmpty()) {
        const QString diagnosis = warning.value(QStringLiteral("diagnosis")).toString(QStringLiteral("当前工具与源 RTL 存在兼容性问题。"));
        const QString suggestion = warning.value(QStringLiteral("suggestion")).toString(QStringLiteral("请由监管者人工审核并重新固定源版本。"));
        return QStringLiteral("核查结论：%1 当前状态为 `%2`，不能称为完成或 `verified`。监管告警为 `%3`：%4%5 应先查明工具与源代码的兼容性；不能用 RTL、pragma 或约束改动掩盖工具问题。若另有新鲜报告证实 RTL 存在独立设计缺陷，可在隔离副本修复并重新验证。%6")
            .arg(project, status, warning.value(QStringLiteral("kind")).toString(QStringLiteral("manual_review")), diagnosis, suggestion, evidence);
    }
    QString base;
    if (status == QLatin1String("verified")) {
        const QString rounds = valueText(facts.value(QStringLiteral("rounds")));
        const QString verification = rounds.isEmpty() ? QStringLiteral("交叉验证记录为 verified。")
            : QStringLiteral("两轮交叉验证（%1 轮）一致。").arg(rounds);
        const QString coverage = facts.value(QStringLiteral("coverage")).isDouble()
            ? QStringLiteral("报告记录的覆盖率为 %1%。").arg(facts.value(QStringLiteral("coverage")).toDouble(), 0, 'f', 2) : QString{};
        base = QStringLiteral("核查结论：%1 的受控执行状态为 `verified`。%2%3这只证明本次生成 flow 的记录可复核，不构成时序、物理实现或 ATPG 工程交付结论。%4")
            .arg(project, verification, coverage, evidence);
    } else {
        base = QStringLiteral("核查结论：%1 的受控执行状态为 `%2`，不能写成完成或通过。首个可解析阻塞信息：%3 应保留原始日志并查明根因；先排除工具、库、路径、约束和控制脚本问题，阅读具体 DRC 规则及 RTL 信号/逻辑。若新鲜报告与源码共同确认 RTL 设计缺陷，可在隔离副本修改 RTL，保存补丁并亲自重跑验证；若根因在配置则修配置，不要修改 RTL。%4")
            .arg(project, status, facts.value(QStringLiteral("first_error")).toString(), evidence);
    }
    if (variant == QLatin1String("handoff_note") || variant == QLatin1String("manual_escalation"))
        return base + QStringLiteral("下一步：仅在工程师批准的设计或约束版本重新固定后，启动新的隔离运行。");
    if (variant == QLatin1String("scope_guardrail") || variant == QLatin1String("repair_denial")
        || variant == QLatin1String("quality_gate_denial") || variant == QLatin1String("evidence_preservation"))
        return base + QStringLiteral("结论范围严格限于上述已记录证据，不能外推为芯片就绪或量产结论。");
    return base;
}
}

QVariantMap TrainingDatasetService::buildSupervisedMix(const QStringList &inputs,
                                                       const QString &outputPath,
                                                       const QString &manifestPath) {
    if (inputs.isEmpty() || outputPath.trimmed().isEmpty() || manifestPath.trimmed().isEmpty())
        return failure(QStringLiteral("必须提供至少一个输入文件、输出文件和 manifest。"));

    const QString outputAbsolute = QFileInfo(outputPath).absoluteFilePath();
    const QString manifestAbsolute = QFileInfo(manifestPath).absoluteFilePath();
    if (QDir::cleanPath(outputAbsolute) == QDir::cleanPath(manifestAbsolute))
        return failure(QStringLiteral("训练集与 manifest 不能使用同一个文件路径。"));
    QSet<QString> seen;
    QHash<QString, int> categories;
    QHash<QString, int> policies;
    QJsonArray sources;
    qsizetype totalRows = 0;
    const QFileInfo outputInfo(outputAbsolute);
    if (!QDir().mkpath(outputInfo.absolutePath()))
        return failure(QStringLiteral("无法创建训练集目录：%1").arg(outputInfo.absolutePath()));
    QSaveFile output(outputAbsolute);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("无法创建训练集：%1").arg(output.errorString()));

    for (const QString &inputValue : inputs) {
        const QFileInfo inputInfo(inputValue);
        const QString inputPath = inputInfo.canonicalFilePath();
        if (inputPath.isEmpty() || !inputInfo.isFile()) {
            output.cancelWriting();
            return failure(QStringLiteral("输入文件不存在：%1").arg(inputValue));
        }
        if (QDir::cleanPath(inputPath) == QDir::cleanPath(outputAbsolute)
            || QDir::cleanPath(inputPath) == QDir::cleanPath(manifestAbsolute)) {
            output.cancelWriting();
            return failure(QStringLiteral("输入文件不能与输出训练集或 manifest 重名：%1").arg(inputPath));
        }
        QFile input(inputPath);
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
            output.cancelWriting();
            return failure(QStringLiteral("无法读取 %1：%2").arg(inputPath, input.errorString()));
        }
        qsizetype sourceRows = 0;
        qsizetype lineNumber = 0;
        QCryptographicHash sourceHash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const QByteArray lineBytes = input.readLine();
            sourceHash.addData(lineBytes);
            ++lineNumber;
            if (lineBytes.trimmed().isEmpty())
                continue;
            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(lineBytes, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                output.cancelWriting();
                return failure(QStringLiteral("%1不是有效 JSON 对象：%2")
                                   .arg(rowPrefix(inputPath, lineNumber), parseError.errorString()));
            }
            const QJsonObject row = document.object();
            const QString invalid = validateRow(row, inputPath, lineNumber);
            if (!invalid.isEmpty()) {
                output.cancelWriting();
                return failure(invalid);
            }
            const QString canonical = QString::fromUtf8(canonicalJson(row));
            if (seen.contains(canonical))
                continue;
            seen.insert(canonical);
            const QByteArray serialized = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
            if (output.write(serialized) != serialized.size()) {
                output.cancelWriting();
                return failure(QStringLiteral("写入训练集失败：%1").arg(output.errorString()));
            }
            const QJsonObject metadata = row.value(QStringLiteral("metadata")).toObject();
            ++categories[metadata.value(QStringLiteral("training_category")).toString()];
            ++policies[metadata.value(QStringLiteral("training_policy")).toString()];
            ++sourceRows;
            ++totalRows;
        }
        if (input.error() != QFileDevice::NoError) {
            output.cancelWriting();
            return failure(QStringLiteral("读取 %1 失败：%2").arg(inputPath, input.errorString()));
        }
        sources.append(QJsonObject{
            {QStringLiteral("file"), inputPath},
            {QStringLiteral("sha256"), QString::fromLatin1(sourceHash.result().toHex())},
            {QStringLiteral("rows"), static_cast<qint64>(sourceRows)},
        });
    }

    if (totalRows == 0) {
        output.cancelWriting();
        return failure(QStringLiteral("没有可写入的监督训练样本"));
    }
    if (!output.commit())
        return failure(QStringLiteral("提交训练集失败：%1").arg(output.errorString()));
    QString digestError;
    const QByteArray datasetDigest = sha256(outputAbsolute, &digestError);
    if (!digestError.isEmpty())
        return failure(digestError);

    if (!QDir().mkpath(QFileInfo(manifestAbsolute).absolutePath()))
        return failure(QStringLiteral("无法创建 manifest 目录：%1").arg(QFileInfo(manifestAbsolute).absolutePath()));
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
    const QJsonObject record{
        {QStringLiteral("created_at"), timestamp},
        {QStringLiteral("dataset"), QFileInfo(outputAbsolute).canonicalFilePath()},
        {QStringLiteral("dataset_sha256"), QString::fromLatin1(datasetDigest)},
        {QStringLiteral("rows"), static_cast<qint64>(totalRows)},
        {QStringLiteral("categories"), counterObject(categories)},
        {QStringLiteral("training_policies"), counterObject(policies)},
        {QStringLiteral("sources"), sources},
        {QStringLiteral("description"), QStringLiteral("由监管者检查的问题反馈、角色要求、执行流程和 DFT 工具样本组成；不含未确认的成功结论。")},
    };
    QSaveFile manifest(manifestAbsolute);
    const QByteArray manifestBytes = QJsonDocument(record).toJson(QJsonDocument::Indented) + '\n';
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text)
        || manifest.write(manifestBytes) != manifestBytes.size() || !manifest.commit())
        return failure(QStringLiteral("写入 manifest 失败：%1").arg(manifest.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), record.toVariantMap()}};
}

QVariantMap TrainingDatasetService::buildEvidenceAugmentedSft(const QString &feedbackPath,
                                                              const QString &policyPath,
                                                              const QString &outputPath,
                                                              const QString &manifestPath) {
    const QFileInfo feedbackInfo(feedbackPath);
    const QFileInfo policyInfo(policyPath);
    if (!feedbackInfo.isFile())
        return failure(QStringLiteral("审核反馈文件不存在：%1").arg(feedbackPath));
    if (!policyInfo.isFile())
        return failure(QStringLiteral("Agent 运行策略文件不存在：%1").arg(policyPath));
    const QString outputAbsolute = QFileInfo(outputPath).absoluteFilePath();
    const QString manifestAbsolute = QFileInfo(manifestPath).absoluteFilePath();
    if (outputAbsolute.isEmpty() || manifestAbsolute.isEmpty()
        || QDir::cleanPath(outputAbsolute) == QDir::cleanPath(manifestAbsolute)
        || QDir::cleanPath(outputAbsolute) == QDir::cleanPath(feedbackInfo.absoluteFilePath())
        || QDir::cleanPath(manifestAbsolute) == QDir::cleanPath(feedbackInfo.absoluteFilePath())
        || QDir::cleanPath(outputAbsolute) == QDir::cleanPath(policyInfo.absoluteFilePath())
        || QDir::cleanPath(manifestAbsolute) == QDir::cleanPath(policyInfo.absoluteFilePath()))
        return failure(QStringLiteral("输出、manifest、反馈和策略必须使用不同文件路径。"));

    QFile feedback(feedbackInfo.absoluteFilePath());
    QFile policyFile(policyInfo.absoluteFilePath());
    if (!feedback.open(QIODevice::ReadOnly | QIODevice::Text) || !policyFile.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("无法读取审核反馈或运行策略文件。"));
    const QByteArray feedbackBytes = feedback.readAll();
    const QByteArray policyBytes = policyFile.readAll();
    const QString policy = QString::fromUtf8(policyBytes);
    if (feedback.error() != QFileDevice::NoError || policyFile.error() != QFileDevice::NoError)
        return failure(QStringLiteral("读取审核反馈或运行策略文件时发生错误。"));

    QVector<QJsonObject> latestRecords;
    QHash<QString, qsizetype> positions;
    const QList<QByteArray> lines = feedbackBytes.split('\n');
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lines.at(index).trimmed().isEmpty())
            continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(lines.at(index), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("反馈日志第 %1 行不是有效 JSON 对象：%2")
                               .arg(index + 1).arg(parseError.errorString()));
        const QJsonObject record = document.object();
        const QString episodeId = record.value(QStringLiteral("episode_id")).toString();
        if (episodeId.isEmpty())
            return failure(QStringLiteral("反馈日志第 %1 行缺少 episode_id。").arg(index + 1));
        if (positions.contains(episodeId))
            latestRecords[positions.value(episodeId)] = record;
        else {
            positions.insert(episodeId, latestRecords.size());
            latestRecords.append(record);
        }
    }

    QVector<QJsonObject> reviewed;
    for (const QJsonObject &record : latestRecords) {
        const QString verdict = record.value(QStringLiteral("verdict")).toString();
        if (verdict == QLatin1String("approved") || verdict == QLatin1String("corrected"))
            reviewed.append(record);
    }
    if (reviewed.isEmpty())
        return failure(QStringLiteral("没有当前已批准或已修正的审核反馈。"));
    if (!QDir().mkpath(QFileInfo(outputAbsolute).absolutePath())
        || !QDir().mkpath(QFileInfo(manifestAbsolute).absolutePath()))
        return failure(QStringLiteral("无法创建训练集或 manifest 目录。"));

    QSaveFile output(outputAbsolute);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("无法创建增强训练集：%1").arg(output.errorString()));
    QHash<QString, int> statusCounts;
    qsizetype structuredEpisodes = 0;
    qsizetype blockedExtraRows = 0;
    qsizetype rows = 0;
    for (const QJsonObject &record : reviewed) {
        const QJsonObject facts = evidenceFacts(record);
        const QString status = facts.value(QStringLiteral("status")).toString();
        ++statusCounts[status];
        const QJsonObject episode = objectAt(record, QStringLiteral("episode"));
        QString canonical = record.value(QStringLiteral("corrected_response")).toString().trimmed();
        if (canonical.isEmpty())
            canonical = episode.value(QStringLiteral("answer")).toString().trimmed();
        if (canonical.isEmpty())
            continue;
        const bool hasEvidence = status != QLatin1String("需按证据复核")
            || !objectAt(facts, QStringLiteral("warning")).isEmpty();
        QStringList variants = hasEvidence ? kEvidenceVariants : QStringList{QStringLiteral("canonical_review")};
        if (hasEvidence && status == QLatin1String("blocked")) {
            variants.append(kBlockedVariants);
            blockedExtraRows += kBlockedVariants.size();
        }
        if (hasEvidence)
            ++structuredEpisodes;
        const QString episodeId = record.value(QStringLiteral("episode_id")).toString();
        const QString verdict = record.value(QStringLiteral("verdict")).toString();
        for (const QString &variant : variants) {
            const QString answer = variant == QLatin1String("canonical_review") ? canonical : derivedAnswer(facts, variant);
            const QJsonObject row{
                {QStringLiteral("messages"), QJsonArray{
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), policy}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), evidencePrompt(facts, variant)}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), answer}},
                }},
                {QStringLiteral("metadata"), QJsonObject{
                    {QStringLiteral("source_episode_id"), episodeId},
                    {QStringLiteral("source_verdict"), verdict},
                    {QStringLiteral("variant"), variant},
                    {QStringLiteral("augmentation"), QStringLiteral("evidence_derived_rephrasing_not_new_execution")},
                    {QStringLiteral("status"), status},
                    {QStringLiteral("evidence_path"), facts.value(QStringLiteral("evidence_path"))},
                }}
            };
            const QByteArray serialized = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
            if (output.write(serialized) != serialized.size()) {
                output.cancelWriting();
                return failure(QStringLiteral("写入增强训练集失败：%1").arg(output.errorString()));
            }
            ++rows;
        }
    }
    if (rows == 0) {
        output.cancelWriting();
        return failure(QStringLiteral("审核记录中没有可用于训练的回答。"));
    }
    if (!output.commit())
        return failure(QStringLiteral("提交增强训练集失败：%1").arg(output.errorString()));
    QString hashError;
    const QByteArray outputHash = sha256(outputAbsolute, &hashError);
    if (!hashError.isEmpty())
        return failure(hashError);
    const QByteArray feedbackHash = QCryptographicHash::hash(feedbackBytes, QCryptographicHash::Sha256).toHex();
    const QByteArray policyHash = QCryptographicHash::hash(policyBytes, QCryptographicHash::Sha256).toHex();
    QJsonObject statusCountsJson;
    QStringList statuses = statusCounts.keys();
    std::sort(statuses.begin(), statuses.end());
    for (const QString &status : statuses)
        statusCountsJson.insert(status, statusCounts.value(status));
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
    const QJsonObject manifest{
        {QStringLiteral("created_at"), timestamp},
        {QStringLiteral("dataset"), QFileInfo(outputAbsolute).canonicalFilePath()},
        {QStringLiteral("dataset_sha256"), QString::fromLatin1(outputHash)},
        {QStringLiteral("reviewed_source_episodes"), reviewed.size()},
        {QStringLiteral("source_episodes_with_structured_evidence"), structuredEpisodes},
        {QStringLiteral("rows"), rows},
        {QStringLiteral("variants_per_structured_source_episode"), kEvidenceVariants.size()},
        {QStringLiteral("variant_names"), QJsonArray::fromStringList(kEvidenceVariants)},
        {QStringLiteral("blocked_variant_names"), QJsonArray::fromStringList(kBlockedVariants)},
        {QStringLiteral("blocked_extra_rows"), blockedExtraRows},
        {QStringLiteral("status_counts_by_source_episode"), statusCountsJson},
        {QStringLiteral("provenance"), QStringLiteral("Every row is an evidence-derived phrasing of one current human-reviewed episode; it is not a new DFT execution.")},
        {QStringLiteral("feedback_sha256"), QString::fromLatin1(feedbackHash)},
        {QStringLiteral("policy_sha256"), QString::fromLatin1(policyHash)},
    };
    QSaveFile manifestFile(manifestAbsolute);
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented) + '\n';
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || manifestFile.write(manifestBytes) != manifestBytes.size() || !manifestFile.commit())
        return failure(QStringLiteral("写入增强数据 manifest 失败：%1").arg(manifestFile.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), manifest.toVariantMap()}};
}
