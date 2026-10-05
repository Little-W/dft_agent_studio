#include "evaluationdatasetservice.h"
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

#include <algorithm>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QByteArray fileSha256(const QString &path, QString *error) {
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

QString jsonText(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isBool())
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    return QString::fromUtf8(value.isObject() ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
                                               : QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}

bool truthy(const QJsonValue &value) {
    if (value.isUndefined() || value.isNull()) return false;
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble() != 0.0;
    if (value.isString()) return !value.toString().isEmpty();
    if (value.isArray()) return !value.toArray().isEmpty();
    if (value.isObject()) return !value.toObject().isEmpty();
    return false;
}

struct JsonlRead {
    QJsonArray rows;
    QString error;
};

JsonlRead readJsonl(const QString &path) {
    JsonlRead result;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("无法读取 %1：%2").arg(path, input.errorString());
        return result;
    }
    qsizetype lineNumber = 0;
    while (!input.atEnd()) {
        const QByteArray line = input.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            result.error = QStringLiteral("%1:%2 不是 JSON 对象：%3")
                .arg(path).arg(lineNumber).arg(parseError.errorString());
            return result;
        }
        result.rows.append(document.object());
    }
    if (input.error() != QFileDevice::NoError)
        result.error = QStringLiteral("读取 %1 失败：%2").arg(path, input.errorString());
    return result;
}

QJsonObject counterObject(const QHash<QString, int> &counts) {
    QJsonObject object;
    QStringList keys = counts.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys)
        object.insert(key, counts.value(key));
    return object;
}

QString joinedSorted(const QSet<QString> &values) {
    QStringList rows = values.values();
    std::sort(rows.begin(), rows.end());
    return rows.join(QStringLiteral(", "));
}
}

QVariantMap EvaluationDatasetService::buildHoldoutManifest(const QString &holdoutPath,
                                                            const QStringList &trainingFiles,
                                                            const QString &manifestPath,
                                                            const QStringList &trainingProjects,
                                                            const QString &datasetKind) {
    if (datasetKind != QLatin1String("holdout") && datasetKind != QLatin1String("development"))
        return failure(QStringLiteral("评测数据类型必须是 holdout 或 development"));
    const QFileInfo holdoutInfo(holdoutPath);
    const QString holdout = holdoutInfo.canonicalFilePath();
    if (holdout.isEmpty() || !holdoutInfo.isFile())
        return failure(QStringLiteral("保留任务文件不存在：%1").arg(holdoutPath));
    const QString manifest = QFileInfo(manifestPath).absoluteFilePath();
    if (QDir::cleanPath(holdout) == QDir::cleanPath(manifest))
        return failure(QStringLiteral("manifest 不能覆盖评测输入文件。"));

    const JsonlRead casesRead = readJsonl(holdout);
    if (!casesRead.error.isEmpty())
        return failure(casesRead.error);
    if (casesRead.rows.isEmpty())
        return failure(QStringLiteral("保留任务不能为空"));
    QSet<QString> caseIds;
    QHash<QString, int> policies;
    QHash<QString, int> projects;
    for (qsizetype index = 0; index < casesRead.rows.size(); ++index) {
        const QJsonObject item = casesRead.rows.at(index).toObject();
        const QString caseId = jsonText(item.value(QStringLiteral("id"))).trimmed();
        const QString policy = jsonText(item.value(QStringLiteral("training_policy"))).trimmed();
        const QString project = jsonText(item.value(QStringLiteral("project"))).trimmed();
        const QString user = jsonText(item.value(QStringLiteral("user"))).trimmed();
        if (caseId.isEmpty() || project.isEmpty() || user.isEmpty())
            return failure(QStringLiteral("%1:%2 缺少 id、project 或 user").arg(holdout).arg(index + 1));
        if (caseIds.contains(caseId))
            return failure(QStringLiteral("保留任务 id 重复：%1").arg(caseId));
        if (!supportedTrainingPolicies().contains(policy))
            return failure(QStringLiteral("%1:%2 不支持训练策略 %3").arg(holdout).arg(index + 1).arg(policy));
        if (!truthy(item.value(QStringLiteral("must_contain")))
            && !truthy(item.value(QStringLiteral("must_contain_any")))
            && !truthy(item.value(QStringLiteral("must_parse_json"))))
            return failure(QStringLiteral("%1:%2 缺少回答检查要求").arg(holdout).arg(index + 1));
        const QJsonValue alternatives = item.value(QStringLiteral("must_contain_any"));
        if (truthy(alternatives)) {
            if (!alternatives.isArray())
                return failure(QStringLiteral("%1:%2 must_contain_any 必须是非空字符串组列表").arg(holdout).arg(index + 1));
            for (const QJsonValue &groupValue : alternatives.toArray()) {
                if (!groupValue.isArray() || groupValue.toArray().isEmpty())
                    return failure(QStringLiteral("%1:%2 must_contain_any 必须是非空字符串组列表").arg(holdout).arg(index + 1));
                for (const QJsonValue &token : groupValue.toArray()) {
                    if (!token.isString() || token.toString().isEmpty())
                        return failure(QStringLiteral("%1:%2 must_contain_any 必须是非空字符串组列表").arg(holdout).arg(index + 1));
                }
            }
        }
        caseIds.insert(caseId);
        ++policies[policy];
        ++projects[project];
    }

    QSet<QString> trainingCaseIds;
    QJsonArray sources;
    for (const QString &trainingPath : trainingFiles) {
        const QFileInfo info(trainingPath);
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || !info.isFile())
            return failure(QStringLiteral("训练数据文件不存在：%1").arg(trainingPath));
        if (QDir::cleanPath(canonical) == QDir::cleanPath(manifest))
            return failure(QStringLiteral("manifest 不能覆盖训练输入文件。"));
        const JsonlRead rows = readJsonl(canonical);
        if (!rows.error.isEmpty())
            return failure(rows.error);
        for (const QJsonValue &rowValue : rows.rows) {
            const QJsonValue metadataValue = rowValue.toObject().value(QStringLiteral("metadata"));
            if (!metadataValue.isObject())
                continue;
            const QJsonValue caseValue = metadataValue.toObject().value(QStringLiteral("case"));
            if (truthy(caseValue))
                trainingCaseIds.insert(jsonText(caseValue));
        }
        QString hashError;
        const QByteArray digest = fileSha256(canonical, &hashError);
        if (!hashError.isEmpty())
            return failure(hashError);
        sources.append(QJsonObject{
            {QStringLiteral("file"), canonical},
            {QStringLiteral("sha256"), QString::fromLatin1(digest)},
            {QStringLiteral("rows"), rows.rows.size()},
        });
    }
    QSet<QString> caseOverlap = caseIds;
    caseOverlap.intersect(trainingCaseIds);
    if (!caseOverlap.isEmpty())
        return failure(QStringLiteral("训练数据与保留任务 id 重复：%1").arg(joinedSorted(caseOverlap)));

    QSet<QString> trainingProjectSet;
    for (const QString &project : trainingProjects) {
        if (!project.isEmpty())
            trainingProjectSet.insert(project);
    }
    QSet<QString> projectOverlap(projects.keyBegin(), projects.keyEnd());
    projectOverlap.intersect(trainingProjectSet);
    if (!projectOverlap.isEmpty())
        return failure(QStringLiteral("训练项目与保留项目重复：%1").arg(joinedSorted(projectOverlap)));

    QString hashError;
    const QByteArray holdoutDigest = fileSha256(holdout, &hashError);
    if (!hashError.isEmpty())
        return failure(hashError);
    const QString description = datasetKind == QLatin1String("development")
        ? QStringLiteral("开发任务用于调整训练方案，不参与最终验收。")
        : QStringLiteral("保留任务不参加训练，用于比较基础模型、第一层适配器和叠加适配器。");
    QStringList sortedTrainingProjects = trainingProjectSet.values();
    std::sort(sortedTrainingProjects.begin(), sortedTrainingProjects.end());
    QJsonArray trainingProjectArray;
    for (const QString &project : sortedTrainingProjects)
        trainingProjectArray.append(project);
    const QJsonObject record{
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"))},
        {QStringLiteral("dataset_kind"), datasetKind},
        {QStringLiteral("holdout"), holdout},
        {QStringLiteral("holdout_sha256"), QString::fromLatin1(holdoutDigest)},
        {QStringLiteral("cases"), casesRead.rows.size()},
        {QStringLiteral("training_policies"), counterObject(policies)},
        {QStringLiteral("projects"), counterObject(projects)},
        {QStringLiteral("training_case_ids"), trainingCaseIds.size()},
        {QStringLiteral("case_overlap"), QJsonArray{}},
        {QStringLiteral("training_projects"), trainingProjectArray},
        {QStringLiteral("project_overlap"), QJsonArray{}},
        {QStringLiteral("training_sources"), sources},
        {QStringLiteral("description"), description},
    };
    if (!QDir().mkpath(QFileInfo(manifest).absolutePath()))
        return failure(QStringLiteral("无法创建 manifest 目录。"));
    QSaveFile output(manifest);
    const QByteArray serialized = QJsonDocument(record).toJson(QJsonDocument::Indented) + '\n';
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)
        || output.write(serialized) != serialized.size() || !output.commit())
        return failure(QStringLiteral("写入 manifest 失败：%1").arg(output.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), record.toVariantMap()}};
}
