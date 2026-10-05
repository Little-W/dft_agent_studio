#include "studiostorageservice.h"

#include "studiopaths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QVariantList>

namespace {
const QSet<QString> pathKeys{
    QStringLiteral("path"), QStringLiteral("paths"), QStringLiteral("file"),
    QStringLiteral("files"), QStringLiteral("workspace"), QStringLiteral("root"),
    QStringLiteral("patch"), QStringLiteral("directory"), QStringLiteral("platformProjectsPath"),
    QStringLiteral("platformModelsPath"), QStringLiteral("platformCapabilitiesPath"),
    QStringLiteral("modelApiKeyFile"), QStringLiteral("apiKeyFile"), QStringLiteral("sourceFile"),
    QStringLiteral("patchFile"), QStringLiteral("evidenceFile")};

QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QStringList mappings() {
    return {
        QStringLiteral("agent_runtime"), QStringLiteral("memory"), QStringLiteral("runs"),
        QStringLiteral("episodes"), QStringLiteral("iterations"), QStringLiteral("logs"),
        QStringLiteral("gui"), QStringLiteral("gui_runs"), QStringLiteral("runtime"),
        QStringLiteral("review.jsonl"), QStringLiteral("audit.jsonl")};
}

bool within(const QString &path, const QString &root) {
    return !path.isEmpty() && !root.isEmpty() && path.startsWith(root + QDir::separator());
}

QVariant remapValue(const QVariant &value, const QString &key, const QString &agentRoot,
                    QString *error) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap output;
        const QVariantMap input = value.toMap();
        for (auto it = input.cbegin(); it != input.cend(); ++it)
            output.insert(it.key(), remapValue(it.value(), it.key(), agentRoot, error));
        return output;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList output;
        for (const QVariant &item : value.toList())
            output.append(remapValue(item, key, agentRoot, error));
        return output;
    }
    if (value.metaType().id() != QMetaType::QString)
        return value;
    const QString text = value.toString();
    if (text.isEmpty() || text.contains(QLatin1Char('\n'))
        || !(pathKeys.contains(key) || key.endsWith(QStringLiteral("_path"))
             || key.endsWith(QStringLiteral("_file")) || key.endsWith(QStringLiteral("_root"))
             || key.endsWith(QStringLiteral("_dir")) || key.endsWith(QStringLiteral("_paths"))
             || key.endsWith(QStringLiteral("_files"))))
        return value;
    const QVariantMap resolved = StudioStorageService::resolvePath(text, agentRoot);
    if (!resolved.value(QStringLiteral("ok")).toBool()) {
        *error = resolved.value(QStringLiteral("message")).toString();
        return value;
    }
    return resolved.value(QStringLiteral("path"));
}
}

QVariantMap StudioStorageService::resolvePath(const QString &path, const QString &agentRoot) {
    const QString original = path.trimmed();
    if (original.isEmpty())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), original},
                {QStringLiteral("remapped"), false}};
    QString normalized = QDir::fromNativeSeparators(original);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.contains(QStringLiteral("..")))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), original},
                {QStringLiteral("remapped"), false}};

    const QString dataRoot = QFileInfo(studioDataRoot(agentRoot)).canonicalFilePath();
    if (dataRoot.isEmpty())
        return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), original},
                {QStringLiteral("remapped"), false}};
    QStringList roots{QDir(agentRoot).absolutePath()};
    const QString aliasesPath = QDir(dataRoot).filePath(QStringLiteral("_migration/path_aliases.json"));
    QFile aliases(aliasesPath);
    if (aliases.open(QIODevice::ReadOnly)) {
        if (aliases.size() > 1024 * 1024)
            return failure(QStringLiteral("Studio legacy path aliases 文件超过读取上限。"));
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(aliases.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("Studio legacy path aliases 文件格式无效。"));
        for (const QJsonValue &value : document.object().value(QStringLiteral("legacy_roots")).toArray()) {
            if (!value.isString())
                continue;
            const QString root = value.toString().trimmed();
            if (!root.isEmpty() && !roots.contains(root))
                roots.append(root);
        }
    }

    QStringList candidates{normalized.startsWith(QStringLiteral("./")) ? normalized.mid(2) : normalized};
    for (QString prefix : std::as_const(roots)) {
        while (prefix.endsWith(QLatin1Char('/')))
            prefix.chop(1);
        if (normalized.startsWith(prefix + QLatin1Char('/')))
            candidates.append(normalized.mid(prefix.size() + 1));
    }
    for (QString candidate : std::as_const(candidates)) {
        QString targetRelative;
        for (const QString &store : mappings()) {
            const QString old = QStringLiteral("artifacts/") + store;
            if (candidate == old || candidate.startsWith(old + QLatin1Char('/'))) {
                targetRelative = store + candidate.mid(old.size());
                break;
            }
        }
        for (const QString &name : {QStringLiteral("projects.json"), QStringLiteral("models.json"),
                                     QStringLiteral("gui_capabilities.json")}) {
            if (candidate == QStringLiteral("data/") + name)
                targetRelative = QStringLiteral("config/") + name;
        }
        if (candidate == QStringLiteral("data/feedback.jsonl"))
            targetRelative = QStringLiteral("feedback.jsonl");
        if (candidate.startsWith(QStringLiteral("artifacts/gui-"))
            && !candidate.mid(QStringLiteral("artifacts/").size()).contains(QLatin1Char('/'))
            && candidate.endsWith(QStringLiteral(".png")))
            targetRelative = QStringLiteral("gui/screenshots/") + QFileInfo(candidate).fileName();
        if (targetRelative.isEmpty())
            continue;
        const QString destination = QDir(dataRoot).filePath(targetRelative);
        const QFileInfo destinationInfo(destination);
        const QString canonical = destinationInfo.canonicalFilePath();
        if (!destinationInfo.exists() || canonical.isEmpty() || !within(canonical, dataRoot))
            continue;
        return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), destination},
                {QStringLiteral("remapped"), true}};
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), original},
            {QStringLiteral("remapped"), false}};
}

QVariantMap StudioStorageService::remapRecord(const QVariant &value, const QString &agentRoot) {
    QString error;
    const QVariant remapped = remapValue(value, {}, agentRoot, &error);
    if (!error.isEmpty())
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("value"), remapped}};
}

QVariantMap StudioStorageService::modelRunConfig(const QString &modelId, const QString &agentRoot) {
    if (modelId.trimmed().isEmpty())
        return failure(QStringLiteral("模型标识不可用。"));
    const QString path = studioConfigPath(QStringLiteral("models.json"), agentRoot);
    const QFileInfo info(path);
    QFile file(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 32 * 1024 * 1024
        || !file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("无法安全读取 Studio 模型目录。"));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return failure(QStringLiteral("Studio 模型目录格式无效。"));
    for (const QJsonValue &value : document.object().value(QStringLiteral("models")).toArray()) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("id")).toString() != modelId)
            continue;
        if (!model.value(QStringLiteral("enabled")).toBool(true))
            return failure(QStringLiteral("所选模型已停用。"));
        QJsonObject safe = model;
        safe.remove(QStringLiteral("api_key"));
        const QString keyFile = safe.value(QStringLiteral("api_key_file")).toString();
        if (!keyFile.isEmpty()) {
            const QVariantMap resolved = resolvePath(keyFile, agentRoot);
            if (resolved.value(QStringLiteral("ok")).toBool())
                safe.insert(QStringLiteral("api_key_file"), resolved.value(QStringLiteral("path")).toString());
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("model"), safe.toVariantMap()},
            {QStringLiteral("active_model_id"), document.object().value(QStringLiteral("active_model_id")).toString()},
            {QStringLiteral("models_path"), path}}}};
    }
    return failure(QStringLiteral("模型 %1 不在 Studio 模型目录中。").arg(modelId));
}

QVariantMap StudioStorageService::diagnose(const QString &agentRoot) {
    const QString root = QDir(agentRoot).absolutePath();
    const QString dataRoot = studioDataRoot(agentRoot);
    const QFileInfo dataInfo(dataRoot);
    if (dataInfo.exists() && !dataInfo.isDir())
        return failure(QStringLiteral("Studio 数据根路径不是目录。"));
    QVariantMap config;
    for (const QString &name : {QStringLiteral("projects.json"), QStringLiteral("models.json"),
                                QStringLiteral("gui_capabilities.json")}) {
        const QString path = studioConfigPath(name, agentRoot);
        QFile file(path);
        QString status = QStringLiteral("missing");
        int count = 0;
        if (file.open(QIODevice::ReadOnly)) {
            if (file.size() > 32 * 1024 * 1024) {
                status = QStringLiteral("invalid");
            } else {
                QJsonParseError error{};
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
                const QString key = name == QStringLiteral("projects.json") ? QStringLiteral("projects")
                    : name == QStringLiteral("models.json") ? QStringLiteral("models") : QStringLiteral("disabled");
                const QJsonValue entries = document.isObject() ? document.object().value(key) : QJsonValue{};
                if (error.error == QJsonParseError::NoError && entries.isArray()) {
                    count = entries.toArray().size();
                    status = count == 0 && key != QStringLiteral("disabled")
                        ? QStringLiteral("empty") : QStringLiteral("valid");
                } else {
                    status = QStringLiteral("invalid");
                }
            }
        }
        config.insert(name, QVariantMap{{QStringLiteral("path"), path},
            {QStringLiteral("status"), status}, {QStringLiteral("count"), count}});
    }

    const QString threadRoot = QDir(dataRoot).filePath(QStringLiteral("agent_runtime/threads"));
    QVariantList roots;
    int sessionCount = 0;
    int binaryCount = 0;
    int archivedCount = 0;
    QDirIterator metadataFiles(threadRoot, {QStringLiteral("session.json")}, QDir::Files,
                               QDirIterator::Subdirectories);
    while (metadataFiles.hasNext()) {
        const QString metadataPath = metadataFiles.next();
        QFile metadata(metadataPath);
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 2 * 1024 * 1024)
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
        if (!document.isObject())
            continue;
        const QJsonObject object = document.object();
        const QString id = object.value(QStringLiteral("root_thread_id")).toString();
        if (id.isEmpty())
            continue;
        ++sessionCount;
        roots.append(QVariantMap{{QStringLiteral("id"), id},
            {QStringLiteral("project_id"), object.value(QStringLiteral("project_id")).toString()},
            {QStringLiteral("workspace"), object.value(QStringLiteral("workspace")).toString()},
            {QStringLiteral("updated_at"), object.value(QStringLiteral("updated_at")).toString()}});
        QDirIterator binaries(QFileInfo(metadataPath).absolutePath(),
            {QStringLiteral("*.thread.bin")}, QDir::Files, QDirIterator::NoIteratorFlags);
        while (binaries.hasNext()) {
            binaries.next();
            ++binaryCount;
        }
        for (const QJsonValue &memberValue : object.value(QStringLiteral("threads")).toArray())
            archivedCount += memberValue.toObject().value(QStringLiteral("archived")).toBool() ? 1 : 0;
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("project_root"), root}, {QStringLiteral("configured_data_root"), dataRoot},
        {QStringLiteral("selected_data_root"), dataRoot}, {QStringLiteral("data_root_exists"), dataInfo.isDir()},
        {QStringLiteral("config"), config}, {QStringLiteral("session_count"), sessionCount},
        {QStringLiteral("binary_thread_count"), binaryCount}, {QStringLiteral("archived_thread_count"), archivedCount},
        {QStringLiteral("sessions"), roots}, {QStringLiteral("mode"), QStringLiteral("read_only")}}}};
}
