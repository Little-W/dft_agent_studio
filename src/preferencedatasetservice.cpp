#include "preferencedatasetservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <utility>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

bool validMessages(const QJsonValue &value, const QString &key, const QString &dataset,
                   qint64 lineNumber, QJsonArray *messages, QString *error) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        *error = QStringLiteral("%1:%2 缺少 %3").arg(dataset).arg(lineNumber).arg(key);
        return false;
    }
    *messages = value.toArray();
    for (const QJsonValue &messageValue : std::as_const(*messages)) {
        if (!messageValue.isObject()
            || messageValue.toObject().value(QStringLiteral("content")).toString().trimmed().isEmpty()) {
            *error = QStringLiteral("%1:%2 的 %3 消息无效").arg(dataset).arg(lineNumber).arg(key);
            return false;
        }
    }
    return true;
}

QByteArray sha256(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!digest.addData(&file)) {
        *error = file.errorString();
        return {};
    }
    return digest.result().toHex();
}
}

QVariantMap PreferenceDatasetService::buildManifest(const QString &datasetPath, const QString &manifestPath) {
    const QFileInfo datasetInfo(datasetPath);
    if (!datasetInfo.isFile() || datasetInfo.isSymLink())
        return failure(QStringLiteral("偏好数据文件不存在或不是普通文件：%1").arg(datasetPath));
    if (manifestPath.trimmed().isEmpty())
        return failure(QStringLiteral("偏好数据清单输出路径不能为空。"));

    QFile dataset(datasetInfo.absoluteFilePath());
    if (!dataset.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(QStringLiteral("无法读取偏好数据：%1").arg(dataset.errorString()));

    QSet<QString> cases;
    qint64 rows = 0;
    qint64 lineNumber = 0;
    while (!dataset.atEnd()) {
        const QByteArray line = dataset.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("%1:%2 不是有效 JSON 对象：%3")
                .arg(datasetInfo.absoluteFilePath()).arg(lineNumber).arg(parseError.errorString()));
        const QJsonObject row = document.object();
        QJsonArray prompt;
        QJsonArray chosen;
        QJsonArray rejected;
        QString error;
        if (!validMessages(row.value(QStringLiteral("prompt")), QStringLiteral("prompt"),
                           datasetInfo.absoluteFilePath(), lineNumber, &prompt, &error)
            || !validMessages(row.value(QStringLiteral("chosen")), QStringLiteral("chosen"),
                              datasetInfo.absoluteFilePath(), lineNumber, &chosen, &error)
            || !validMessages(row.value(QStringLiteral("rejected")), QStringLiteral("rejected"),
                              datasetInfo.absoluteFilePath(), lineNumber, &rejected, &error))
            return failure(error);
        if (prompt.last().toObject().value(QStringLiteral("role")).toString() != QStringLiteral("user"))
            return failure(QStringLiteral("%1:%2 prompt 必须以 user 结束").arg(datasetInfo.absoluteFilePath()).arg(lineNumber));
        if (chosen.last().toObject().value(QStringLiteral("role")).toString() != QStringLiteral("assistant")
            || rejected.last().toObject().value(QStringLiteral("role")).toString() != QStringLiteral("assistant"))
            return failure(QStringLiteral("%1:%2 chosen 和 rejected 必须是 assistant 回答")
                .arg(datasetInfo.absoluteFilePath()).arg(lineNumber));
        if (chosen == rejected)
            return failure(QStringLiteral("%1:%2 chosen 与 rejected 不能相同")
                .arg(datasetInfo.absoluteFilePath()).arg(lineNumber));

        const QJsonObject metadata = row.value(QStringLiteral("metadata")).toObject();
        const QString source = metadata.value(QStringLiteral("source")).toString().trimmed();
        const QString caseId = metadata.value(QStringLiteral("case")).toString().trimmed();
        if (source.isEmpty())
            return failure(QStringLiteral("%1:%2 缺少来源").arg(datasetInfo.absoluteFilePath()).arg(lineNumber));
        if (caseId.isEmpty())
            return failure(QStringLiteral("%1:%2 缺少 case").arg(datasetInfo.absoluteFilePath()).arg(lineNumber));
        if (cases.contains(caseId))
            return failure(QStringLiteral("偏好样本 case 重复：%1").arg(caseId));
        cases.insert(caseId);
        ++rows;
    }
    if (rows == 0)
        return failure(QStringLiteral("偏好数据不能为空"));

    QString digestError;
    const QByteArray digest = sha256(datasetInfo.absoluteFilePath(), &digestError);
    if (digest.isEmpty())
        return failure(QStringLiteral("无法计算偏好数据 SHA-256：%1").arg(digestError));
    const QString absoluteManifest = QFileInfo(manifestPath).absoluteFilePath();
    const QVariantMap record{
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"))},
        {QStringLiteral("dataset"), datasetInfo.canonicalFilePath()},
        {QStringLiteral("dataset_sha256"), QString::fromLatin1(digest)},
        {QStringLiteral("pairs"), rows},
        {QStringLiteral("review_status"), QStringLiteral("supervisor_reviewed")},
        {QStringLiteral("training_status"), QStringLiteral("等待两层监督微调通过保留任务后再试验 DPO")},
    };
    if (!QDir().mkpath(QFileInfo(absoluteManifest).absolutePath()))
        return failure(QStringLiteral("无法创建偏好清单目录。"));
    QSaveFile output(absoluteManifest);
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(record)).toJson(QJsonDocument::Indented) + '\n';
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)
        || output.write(bytes) != bytes.size() || !output.commit())
        return failure(QStringLiteral("无法写入偏好数据清单：%1").arg(output.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), record}};
}
