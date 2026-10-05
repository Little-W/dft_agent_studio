#include "agentepisodeservice.h"

#include "nativememoryservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <utility>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QString timestamp() {
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
}

bool validEpisodeId(const QString &episodeId) {
    return QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,120}$")).match(episodeId).hasMatch();
}

QVariantMap readEpisode(const QString &episodeId, const QString &dataRoot, QString *pathOut = nullptr) {
    if (!validEpisodeId(episodeId))
        return failure(QStringLiteral("episode ID 格式无效。"));
    const QString path = QDir(dataRoot).filePath(QStringLiteral("episodes/%1.json").arg(episodeId));
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 64 * 1024 * 1024)
        return failure(QStringLiteral("找不到 episode，或 episode 不是可读的普通文件：%1").arg(path));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("无法读取 episode：%1").arg(file.errorString()));
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return failure(QStringLiteral("episode JSON 无效：%1").arg(parseError.errorString()));
    if (pathOut)
        *pathOut = path;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("episode"), document.object().toVariantMap()}};
}

bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QStringLiteral("无法创建输出目录。" );
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)
        || output.write(bytes) != bytes.size() || !output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}
}

QVariantMap AgentEpisodeService::persist(const QVariantMap &episodeValue, const QString &dataRoot) {
    QVariantMap episode = episodeValue;
    QString episodeId = episode.value(QStringLiteral("episode_id")).toString().trimmed();
    if (episodeId.isEmpty())
        episodeId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
            + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::Id128).left(12);
    if (!validEpisodeId(episodeId))
        return failure(QStringLiteral("episode ID 格式无效。"));
    episode.insert(QStringLiteral("episode_id"), episodeId);
    if (!episode.contains(QStringLiteral("timestamp")))
        episode.insert(QStringLiteral("timestamp"), timestamp());
    const QString path = QDir(dataRoot).filePath(QStringLiteral("episodes/%1.json").arg(episodeId));
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(episode)).toJson(QJsonDocument::Indented) + '\n';
    QString error;
    if (!writeAtomic(path, bytes, &error))
        return failure(QStringLiteral("保存 episode 失败：%1").arg(error));

    const QString projectId = episode.value(QStringLiteral("project_id"), QStringLiteral("global_shared")).toString();
    NativeMemoryService memory(projectId, QDir(dataRoot).filePath(QStringLiteral("memory/agent_memory.sqlite3")));
    if (memory.importEpisode(episode, path, &error) < 0)
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("episode_id"), episodeId}, {QStringLiteral("path"), path},
            {QStringLiteral("memory_warning"), error}}}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("episode_id"), episodeId}, {QStringLiteral("path"), path}}}};
}

QVariantMap AgentEpisodeService::recordFeedback(const QString &episodeId, const QString &verdict,
                                               const QString &note, const QString &correctedResponse,
                                               const QString &dataRoot) {
    static const QSet<QString> verdicts{QStringLiteral("approved"), QStringLiteral("corrected"), QStringLiteral("rejected")};
    if (!verdicts.contains(verdict))
        return failure(QStringLiteral("verdict 必须是 approved、corrected 或 rejected。"));
    if (verdict == QLatin1String("corrected") && correctedResponse.trimmed().isEmpty())
        return failure(QStringLiteral("corrected 审核必须提供 corrected_response。"));
    QString episodePath;
    const QVariantMap loaded = readEpisode(episodeId, dataRoot, &episodePath);
    if (!loaded.value(QStringLiteral("ok")).toBool())
        return loaded;
    const QVariantMap episode = loaded.value(QStringLiteral("episode")).toMap();
    const QVariantMap record{
        {QStringLiteral("timestamp"), timestamp()}, {QStringLiteral("episode_id"), episodeId},
        {QStringLiteral("verdict"), verdict}, {QStringLiteral("note"), note},
        {QStringLiteral("corrected_response"), correctedResponse}, {QStringLiteral("episode"), episode},
    };
    const QString feedbackPath = QDir(dataRoot).filePath(QStringLiteral("feedback.jsonl"));
    if (!QDir().mkpath(QFileInfo(feedbackPath).absolutePath()))
        return failure(QStringLiteral("无法创建审核反馈目录。"));
    QFile feedback(feedbackPath);
    if (!feedback.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return failure(QStringLiteral("无法追加审核反馈：%1").arg(feedback.errorString()));
    const QByteArray line = QJsonDocument(QJsonObject::fromVariantMap(record)).toJson(QJsonDocument::Compact) + '\n';
    if (feedback.write(line) != line.size() || !feedback.flush())
        return failure(QStringLiteral("写入审核反馈失败：%1").arg(feedback.errorString()));

    const QString projectId = episode.value(QStringLiteral("project_id"), QStringLiteral("global_shared")).toString();
    NativeMemoryService memory(projectId, QDir(dataRoot).filePath(QStringLiteral("memory/agent_memory.sqlite3")));
    QString memoryError;
    if (!memory.recordFeedback(episodeId, verdict, note, correctedResponse, feedbackPath, &memoryError))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("feedback_file"), feedbackPath}, {QStringLiteral("episode_id"), episodeId},
            {QStringLiteral("verdict"), verdict}, {QStringLiteral("memory_warning"), memoryError}}}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("feedback_file"), feedbackPath}, {QStringLiteral("episode_id"), episodeId},
        {QStringLiteral("verdict"), verdict}}}};
}

QVariantMap AgentEpisodeService::exportSft(const QString &feedbackPath, const QString &policyPath,
                                           const QString &outputPath) {
    QFile feedback(feedbackPath);
    QFile policy(policyPath);
    if (!feedback.open(QIODevice::ReadOnly | QIODevice::Text) || !policy.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(QStringLiteral("无法读取审核反馈或 Agent policy 文件。"));
    const QByteArray policyBytes = policy.readAll();
    if (policyBytes.trimmed().isEmpty())
        return failure(QStringLiteral("Agent policy 文件不能为空。"));
    QHash<QString, QJsonObject> latestByEpisode;
    QStringList order;
    qint64 superseded = 0;
    qint64 lineNumber = 0;
    while (!feedback.atEnd()) {
        const QByteArray line = feedback.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("反馈日志第 %1 行 JSON 无效：%2").arg(lineNumber).arg(parseError.errorString()));
        const QJsonObject record = document.object();
        const QString episodeId = record.value(QStringLiteral("episode_id")).toString().trimmed();
        if (episodeId.isEmpty())
            return failure(QStringLiteral("反馈日志第 %1 行缺少 episode_id。").arg(lineNumber));
        if (latestByEpisode.contains(episodeId))
            ++superseded;
        else
            order.append(episodeId);
        latestByEpisode.insert(episodeId, record);
    }
    if (latestByEpisode.isEmpty())
        return failure(QStringLiteral("尚无可导出的审核反馈。"));

    QSaveFile output(QFileInfo(outputPath).absoluteFilePath());
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())
        || !output.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("无法创建 SFT 输出：%1").arg(output.errorString()));
    qint64 accepted = 0;
    qint64 skipped = 0;
    for (const QString &episodeId : std::as_const(order)) {
        const QJsonObject record = latestByEpisode.value(episodeId);
        const QString verdict = record.value(QStringLiteral("verdict")).toString();
        if (verdict == QLatin1String("rejected")) {
            ++skipped;
            continue;
        }
        const QJsonObject episode = record.value(QStringLiteral("episode")).toObject();
        const QString goal = episode.value(QStringLiteral("goal")).toString().trimmed();
        QString answer = record.value(QStringLiteral("corrected_response")).toString().trimmed();
        if (answer.isEmpty())
            answer = episode.value(QStringLiteral("answer")).toString().trimmed();
        if (goal.isEmpty() || answer.isEmpty()) {
            ++skipped;
            continue;
        }
        const QJsonObject row{
            {QStringLiteral("messages"), QJsonArray{
                QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), QString::fromUtf8(policyBytes)}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), goal}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                            {QStringLiteral("content"), answer}},
            }},
            {QStringLiteral("metadata"), QJsonObject{
                {QStringLiteral("episode_id"), episodeId}, {QStringLiteral("verdict"), verdict},
                {QStringLiteral("review_note"), record.value(QStringLiteral("note"))},
            }},
        };
        const QByteArray bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        if (output.write(bytes) != bytes.size()) {
            output.cancelWriting();
            return failure(QStringLiteral("写入 SFT 数据失败：%1").arg(output.errorString()));
        }
        ++accepted;
    }
    if (accepted == 0) {
        output.cancelWriting();
        return failure(QStringLiteral("审核反馈中没有可用于 SFT 的答案。"));
    }
    if (!output.commit())
        return failure(QStringLiteral("提交 SFT 数据失败：%1").arg(output.errorString()));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("output_file"), QFileInfo(outputPath).absoluteFilePath()},
        {QStringLiteral("accepted"), accepted}, {QStringLiteral("skipped"), skipped},
        {QStringLiteral("superseded"), superseded}}}};
}
