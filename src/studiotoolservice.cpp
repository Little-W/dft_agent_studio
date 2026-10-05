#include "studiotoolservice.h"

#include "studiopaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
QMutex &dispatchMutex() {
    static QMutex mutex;
    return mutex;
}

QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

bool hasMarkdownPathMarkup(const QJsonValue &value, const QString &field, QString *badField) {
    static const QSet<QString> pathFields{
        QStringLiteral("root"), QStringLiteral("rtl_root"), QStringLiteral("related_documents"),
        QStringLiteral("library_dir"), QStringLiteral("filelist"), QStringLiteral("constraint_file"),
        QStringLiteral("atpg_cell_model_files"), QStringLiteral("macro_libraries"),
        QStringLiteral("test_model_files"), QStringLiteral("source_files"),
        QStringLiteral("source_support_files"), QStringLiteral("include_dirs"),
        QStringLiteral("pre_scripts"), QStringLiteral("post_scripts"),
        QStringLiteral("synthesis_output_dir"), QStringLiteral("dft_output_dir"),
        QStringLiteral("tessent_dofile")};
    if (pathFields.contains(field)) {
        const auto containsMarkup = [](const QJsonValue &candidate) {
            return candidate.isString() && candidate.toString().contains(QStringLiteral("**"));
        };
        if (containsMarkup(value)) {
            *badField = field;
            return true;
        }
        if (value.isArray()) {
            const QJsonArray paths = value.toArray();
            for (qsizetype index = 0; index < paths.size(); ++index) {
                if (containsMarkup(paths.at(index))) {
                    *badField = QStringLiteral("%1[%2]").arg(field).arg(index);
                    return true;
                }
            }
        }
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString child = field.isEmpty() ? it.key() : field + QLatin1Char('.') + it.key();
            if (hasMarkdownPathMarkup(it.value(), it.key(), badField)) {
                *badField = child;
                return true;
            }
        }
    } else if (value.isArray() && !pathFields.contains(field)) {
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (hasMarkdownPathMarkup(array.at(index), field, badField)) {
                *badField = QStringLiteral("%1[%2]").arg(field).arg(index);
                return true;
            }
        }
    }
    return false;
}

bool readObject(const QString &path, const QString &arrayKey, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 Studio 配置：%1 (%2)").arg(path, file.errorString());
        return false;
    }
    if (file.size() > 32 * 1024 * 1024) {
        *error = QStringLiteral("Studio 配置超过 32 MiB 安全上限：%1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value(arrayKey).isArray()) {
        *error = QStringLiteral("Studio 配置格式无效或缺少 %1 数组：%2").arg(arrayKey, path);
        return false;
    }
    *object = document.object();
    return true;
}

bool writeObject(const QString &path, const QJsonObject &object, QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        *error = QStringLiteral("无法创建 Studio 配置目录：%1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入 Studio 配置：%1 (%2)").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        *error = QStringLiteral("Studio 配置写入不完整：%1 (%2)").arg(path, file.errorString());
        return false;
    }
    if (!file.commit()) {
        *error = QStringLiteral("无法原子保存 Studio 配置：%1 (%2)").arg(path, file.errorString());
        return false;
    }
    return true;
}

int findById(const QJsonArray &rows, const QString &id) {
    for (qsizetype index = 0; index < rows.size(); ++index) {
        if (rows.at(index).isObject() && rows.at(index).toObject().value(QStringLiteral("id")).toString() == id)
            return static_cast<int>(index);
    }
    return -1;
}

QJsonObject deepMerge(QJsonObject target, const QJsonObject &source) {
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        if (it.value().isObject())
            target.insert(it.key(), deepMerge(target.value(it.key()).toObject(), it.value().toObject()));
        else
            target.insert(it.key(), it.value());
    }
    return target;
}

QJsonObject projectChange(QJsonObject project, const QJsonObject &changes, QStringList *applied, QString *error) {
    const QSet<QString> allowed{
        QStringLiteral("name"), QStringLiteral("kind"), QStringLiteral("root"), QStringLiteral("rtl_root"),
        QStringLiteral("top"), QStringLiteral("goal"), QStringLiteral("notes"), QStringLiteral("flow_profile"),
        QStringLiteral("minimum_coverage"), QStringLiteral("maximum_dft_drc_violations"),
        QStringLiteral("library_dir"), QStringLiteral("library_file"), QStringLiteral("library_profile"),
        QStringLiteral("metadata"), QStringLiteral("dft_execution"), QStringLiteral("flow_modules"),
        QStringLiteral("related_documents")};
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            *error = QStringLiteral("不允许修改的项目字段：%1").arg(it.key());
            return {};
        }
        QString badPathField;
        if (hasMarkdownPathMarkup(it.value(), it.key(), &badPathField)) {
            *error = QStringLiteral("项目设置路径 %1 含有 Markdown 强调标记 **。请先用 shell 验证原始文件路径，并以不带格式标记的字面路径重试；本次未保存任何设置。")
                .arg(badPathField);
            return {};
        }
    }
    const auto hasDerivedAnalyzeFormat = [](const QJsonValue &value) {
        const QJsonObject container = value.toObject();
        const QJsonObject execution = container.contains(QStringLiteral("dft_execution"))
            ? container.value(QStringLiteral("dft_execution")).toObject() : container;
        return execution.value(QStringLiteral("synthesis_configuration")).toObject()
            .contains(QStringLiteral("analyze_format"));
    };
    if (hasDerivedAnalyzeFormat(changes.value(QStringLiteral("metadata")))
        || hasDerivedAnalyzeFormat(changes.value(QStringLiteral("dft_execution")))) {
        *error = QStringLiteral(
            "synthesis_configuration.analyze_format 是运行证据中的派生值，不是可写项目设置；"
            "请设置 metadata.dft_execution.language 为 verilog 或 sverilog。"
            "No settings were saved.");
        return {};
    }
    QJsonObject metadata = project.value(QStringLiteral("metadata")).toObject();
    if (changes.value(QStringLiteral("metadata")).isObject()) {
        metadata = deepMerge(metadata, changes.value(QStringLiteral("metadata")).toObject());
        applied->append(QStringLiteral("metadata"));
    }
    for (const QString &key : {QStringLiteral("dft_execution"), QStringLiteral("flow_modules")}) {
        const QJsonValue value = changes.value(key);
        if (value.isObject()) {
            const QString metadataKey = key;
            metadata.insert(metadataKey, deepMerge(metadata.value(metadataKey).toObject(), value.toObject()));
            applied->append(QStringLiteral("metadata.%1").arg(key));
        } else if (!value.isUndefined()) {
            *error = QStringLiteral("%1 必须是 JSON 对象。").arg(key);
            return {};
        }
    }
    const QJsonValue documents = changes.value(QStringLiteral("related_documents"));
    if (!documents.isUndefined()) {
        if (!documents.isArray()) {
            *error = QStringLiteral("related_documents 必须是文档路径字符串数组。");
            return {};
        }
        QJsonArray unique;
        QSet<QString> seen;
        for (const QJsonValue &value : documents.toArray()) {
            if (!value.isString()) {
                *error = QStringLiteral("related_documents 必须只包含字符串路径。");
                return {};
            }
            const QString path = value.toString().trimmed();
            if (!path.isEmpty() && !seen.contains(path)) {
                seen.insert(path);
                unique.append(path);
            }
        }
        project.insert(QStringLiteral("related_documents"), unique);
        applied->append(QStringLiteral("related_documents"));
    }
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        if (it.key() == QStringLiteral("metadata") || it.key() == QStringLiteral("dft_execution")
            || it.key() == QStringLiteral("flow_modules") || it.key() == QStringLiteral("related_documents"))
            continue;
        project.insert(it.key(), it.value());
        applied->append(it.key());
    }
    project.insert(QStringLiteral("metadata"), metadata);
    return project;
}

QVariantMap readFailure(const QString &path, const QString &key) {
    QJsonObject object;
    QString error;
    if (!readObject(path, key, &object, &error))
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("document"), object}};
}

bool mutationAllowed(const QVariantMap &project, QVariantMap *denied) {
    QString mode = project.value(QStringLiteral("agentPermissionMode")).toString().trimmed().toLower();
    if (mode.isEmpty()) {
        QVariantMap execution = project.value(QStringLiteral("dftExecution")).toMap();
        if (execution.isEmpty())
            execution = project.value(QStringLiteral("dft_execution")).toMap();
        if (execution.isEmpty())
            execution = project.value(QStringLiteral("metadata")).toMap()
                            .value(QStringLiteral("dft_execution")).toMap();
        mode = execution.value(QStringLiteral("agent_permission_mode")).toString().trimmed().toLower();
    }
    if (mode == QStringLiteral("autonomous") || mode == QStringLiteral("full_access"))
        return true;
    *denied = {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("updated"), false},
        {QStringLiteral("awaiting_user_approval"), true},
        {QStringLiteral("reason"), QStringLiteral("当前为请求批准权限；请切换到自主迭代或完全访问后再修改 Studio 配置。")}}}};
    return false;
}
}

bool StudioToolService::supports(const QString &action) {
    return action == QStringLiteral("list_studio_projects")
        || action == QStringLiteral("inspect_studio_project")
        || action == QStringLiteral("inspect_studio_settings")
        || action == QStringLiteral("update_studio_project")
        || action == QStringLiteral("update_studio_model")
        || action == QStringLiteral("set_studio_active_model")
        || action == QStringLiteral("set_studio_capability");
}

QVariantMap StudioToolService::listModels(bool refresh, const QString &agentRoot) {
    QMutexLocker locker(&dispatchMutex());
    const QString path = studioConfigPath(QStringLiteral("models.json"), agentRoot);
    const QFileInfo info(path);
    if (info.isSymLink())
        return failure(QStringLiteral("Studio 模型目录不能是符号链接。"));
    QJsonObject document;
    QString error;
    if (!readObject(path, QStringLiteral("models"), &document, &error))
        return failure(error);

    QVariantList refreshErrors;
    if (refresh) {
        QJsonArray models = document.value(QStringLiteral("models")).toArray();
        QSet<QString> refreshedBases;
        for (qsizetype index = 0; index < models.size(); ++index) {
            if (!models.at(index).isObject())
                continue;
            QJsonObject model = models.at(index).toObject();
            QString base = model.value(QStringLiteral("api_base")).toString().trimmed();
            while (base.endsWith(QLatin1Char('/')))
                base.chop(1);
            if (model.value(QStringLiteral("provider")).toString() != QStringLiteral("api")
                || base.isEmpty() || refreshedBases.contains(base))
                continue;
            refreshedBases.insert(base);
            QUrl url(base + QStringLiteral("/models"));
            if (!url.isValid() || (url.scheme() != QStringLiteral("http")
                                   && url.scheme() != QStringLiteral("https"))
                || url.host().isEmpty()) {
                refreshErrors.append(QVariantMap{{QStringLiteral("api_base"), base},
                    {QStringLiteral("error"), QStringLiteral("模型 API 地址不是有效的 HTTP(S) URL。")}});
                continue;
            }

            QNetworkAccessManager manager;
            QNetworkRequest request(url);
            request.setRawHeader("Accept", "application/json");
            request.setTransferTimeout(15000);
            QNetworkReply *reply = manager.get(request);
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
                if (reply->isRunning())
                    reply->abort();
            });
            timeout.start(15000);
            if (reply->isRunning())
                loop.exec();
            timeout.stop();

            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray bytes = reply->readAll();
            QJsonParseError parseError{};
            const QJsonDocument payload = QJsonDocument::fromJson(bytes, &parseError);
            QStringList liveIds;
            if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && parseError.error == QJsonParseError::NoError && payload.isObject()) {
                for (const QJsonValue &entry : payload.object().value(QStringLiteral("data")).toArray()) {
                    const QString id = entry.toObject().value(QStringLiteral("id")).toString().trimmed();
                    if (!id.isEmpty() && !liveIds.contains(id))
                        liveIds.append(id);
                }
            }
            const QString replyError = reply->errorString();
            reply->deleteLater();
            if (liveIds.isEmpty()) {
                QString message;
                if (status < 200 || status >= 300)
                    message = QStringLiteral("HTTP %1").arg(status);
                else if (parseError.error != QJsonParseError::NoError || !payload.isObject())
                    message = QStringLiteral("模型 API 返回了无效 JSON。 ") + parseError.errorString();
                else if (!replyError.isEmpty())
                    message = replyError;
                else
                    message = QStringLiteral("模型 API 没有返回可用模型。");
                refreshErrors.append(QVariantMap{{QStringLiteral("api_base"), base},
                    {QStringLiteral("error"), message}});
                continue;
            }

            const QString oldId = model.value(QStringLiteral("id")).toString();
            if (!liveIds.contains(oldId)) {
                const QString replacement = liveIds.constFirst();
                model.insert(QStringLiteral("id"), replacement);
                model.insert(QStringLiteral("label"), replacement);
                if (document.value(QStringLiteral("active_model_id")).toString() == oldId)
                    document.insert(QStringLiteral("active_model_id"), replacement);
            }
            QJsonArray available;
            for (const QString &id : liveIds)
                available.append(id);
            model.insert(QStringLiteral("available_models"), available);
            models.replace(index, model);
        }
        document.insert(QStringLiteral("models"), models);
        if (!writeObject(path, document, &error))
            return failure(error);
    }

    QVariantList publicModels;
    for (const QJsonValue &value : document.value(QStringLiteral("models")).toArray()) {
        if (!value.isObject())
            continue;
        QJsonObject model = value.toObject();
        model.remove(QStringLiteral("api_key"));
        model.remove(QStringLiteral("api_key_file"));
        publicModels.append(model.toVariantMap());
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("active_model_id"), document.value(QStringLiteral("active_model_id")).toString()},
        {QStringLiteral("models"), publicModels}, {QStringLiteral("refresh_errors"), refreshErrors}}}};
}

QVariantMap StudioToolService::dispatch(const QString &action, const QVariantMap &project,
                                        const QVariantMap &arguments, const QString &agentRoot) {
    QMutexLocker locker(&dispatchMutex());
    if (!supports(action))
        return failure(QStringLiteral("不支持的 Studio 工具操作。"));
    const QString projectsPath = studioConfigPath(QStringLiteral("projects.json"), agentRoot);
    const QString modelsPath = studioConfigPath(QStringLiteral("models.json"), agentRoot);
    const QString capabilitiesPath = studioConfigPath(QStringLiteral("gui_capabilities.json"), agentRoot);
    QString error;
    if (action == QStringLiteral("list_studio_projects") || action == QStringLiteral("inspect_studio_project")
        || action == QStringLiteral("update_studio_project")) {
        auto response = readFailure(projectsPath, QStringLiteral("projects"));
        if (!response.value(QStringLiteral("ok")).toBool())
            return response;
        QJsonObject document = response.value(QStringLiteral("document")).toJsonObject();
        QJsonArray projects = document.value(QStringLiteral("projects")).toArray();
        if (action == QStringLiteral("list_studio_projects")) {
            QVariantList summaries;
            for (const QJsonValue &value : projects) {
                if (!value.isObject())
                    continue;
                const QJsonObject row = value.toObject();
                const QString root = row.value(QStringLiteral("root")).toString();
                summaries.append(QVariantMap{
                    {QStringLiteral("id"), row.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("name"), row.value(QStringLiteral("name")).toString()},
                    {QStringLiteral("kind"), row.value(QStringLiteral("kind")).toString()},
                    {QStringLiteral("root"), root},
                    {QStringLiteral("exists"), !root.isEmpty() && QFileInfo(studioAbsolutePath(root, agentRoot)).isDir()},
                    {QStringLiteral("managed"), row.value(QStringLiteral("managed")).toBool()},
                    {QStringLiteral("goal"), row.value(QStringLiteral("goal")).toString().left(2000)},
                    {QStringLiteral("flow_profile"), row.value(QStringLiteral("flow_profile")).toString()}});
            }
            return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("registry_path"), projectsPath},
                {QStringLiteral("version"), document.value(QStringLiteral("version")).toInt(1)},
                {QStringLiteral("projects"), summaries}}}};
        }
        QString projectId = arguments.value(QStringLiteral("project_id")).toString().trimmed();
        if (projectId.isEmpty())
            projectId = project.value(QStringLiteral("id")).toString().trimmed();
        const int index = findById(projects, projectId);
        if (index < 0)
            return failure(QStringLiteral("找不到 Studio 项目：%1").arg(projectId));
        if (action == QStringLiteral("inspect_studio_project"))
            return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("project"), projects.at(index).toObject().toVariantMap()},
                {QStringLiteral("registry_path"), projectsPath}}}};
        QVariantMap denied;
        if (!mutationAllowed(project, &denied))
            return denied;
        const QVariant changesValue = arguments.value(QStringLiteral("changes"));
        const QJsonObject changes = QJsonObject::fromVariantMap(changesValue.toMap());
        if (changes.isEmpty())
            return failure(QStringLiteral("项目 changes 必须是非空 JSON 对象。"));
        const QJsonObject original = projects.at(index).toObject();
        QStringList applied;
        const QJsonObject updated = projectChange(original, changes, &applied, &error);
        if (!error.isEmpty())
            return failure(error);
        if (updated == original)
            return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("updated"), false}, {QStringLiteral("status"), QStringLiteral("no_change")},
                {QStringLiteral("project_id"), projectId}, {QStringLiteral("changed_fields"), QVariantList{}},
                {QStringLiteral("project"), original.toVariantMap()},
                {QStringLiteral("notice"), QStringLiteral("设置值与当前值相同；未写入平台注册表。")}}}};
        projects.replace(index, updated);
        document.insert(QStringLiteral("projects"), projects);
        if (!writeObject(projectsPath, document, &error))
            return failure(error);
        QVariantMap result{{QStringLiteral("updated"), true},
            {QStringLiteral("permission_mode"), project.value(QStringLiteral("agentPermissionMode"))},
            {QStringLiteral("project_id"), projectId}, {QStringLiteral("changed_fields"), applied},
            {QStringLiteral("registry_path"), projectsPath},
            {QStringLiteral("project"), updated.toVariantMap()},
            {QStringLiteral("notice"), QStringLiteral("项目设置已原子保存；当前项目上下文可以同步。")}};
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
    }

    if (action == QStringLiteral("inspect_studio_settings")) {
        auto modelsResponse = readFailure(modelsPath, QStringLiteral("models"));
        if (!modelsResponse.value(QStringLiteral("ok")).toBool())
            return modelsResponse;
        auto capabilitiesResponse = readFailure(capabilitiesPath, QStringLiteral("disabled"));
        if (!capabilitiesResponse.value(QStringLiteral("ok")).toBool())
            return capabilitiesResponse;
        QJsonObject modelsDocument = modelsResponse.value(QStringLiteral("document")).toJsonObject();
        const QJsonObject capabilitiesDocument = capabilitiesResponse.value(QStringLiteral("document")).toJsonObject();
        QVariantList models;
        for (const QJsonValue &value : modelsDocument.value(QStringLiteral("models")).toArray()) {
            if (!value.isObject())
                continue;
            QJsonObject safe = value.toObject();
            const bool configured = !safe.value(QStringLiteral("api_key")).toString().trimmed().isEmpty()
                || !safe.value(QStringLiteral("api_key_file")).toString().trimmed().isEmpty();
            if (safe.contains(QStringLiteral("api_key")))
                safe.insert(QStringLiteral("api_key"), configured ? QStringLiteral("<configured>") : QString{});
            safe.remove(QStringLiteral("api_key_file"));
            QVariantMap item = safe.toVariantMap();
            item.insert(QStringLiteral("api_key_configured"), configured);
            models.append(item);
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("models_path"), modelsPath},
            {QStringLiteral("active_model_id"), modelsDocument.value(QStringLiteral("active_model_id")).toString()},
            {QStringLiteral("models"), models}, {QStringLiteral("capabilities_path"), capabilitiesPath},
            {QStringLiteral("disabled_capabilities"), capabilitiesDocument.value(QStringLiteral("disabled")).toArray().toVariantList()}}}};
    }

    QVariantMap denied;
    if (!mutationAllowed(project, &denied))
        return denied;
    QJsonObject document;
    QString arrayKey = action == QStringLiteral("set_studio_capability")
        ? QStringLiteral("disabled") : QStringLiteral("models");
    const QString path = action == QStringLiteral("set_studio_capability") ? capabilitiesPath : modelsPath;
    if (!readObject(path, arrayKey, &document, &error))
        return failure(error);
    QJsonArray rows = document.value(arrayKey).toArray();
    if (action == QStringLiteral("update_studio_model")) {
        const QString modelId = arguments.value(QStringLiteral("model_id")).toString().trimmed();
        const int index = findById(rows, modelId);
        if (index < 0)
            return failure(QStringLiteral("找不到 Studio 模型：%1").arg(modelId));
        const QJsonObject changes = QJsonObject::fromVariantMap(arguments.value(QStringLiteral("changes")).toMap());
        if (changes.isEmpty())
            return failure(QStringLiteral("模型 changes 必须是非空 JSON 对象。"));
        if (changes.contains(QStringLiteral("id")))
            return failure(QStringLiteral("模型 ID 不可修改，请创建新的模型记录。"));
        QJsonObject updated = rows.at(index).toObject();
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
            if (it.value().isObject())
                return failure(QStringLiteral("模型修改 path 只能是模型记录的顶层字段名。"));
            updated.insert(it.key(), it.value());
        }
        rows.replace(index, updated);
        document.insert(arrayKey, rows);
        if (!writeObject(path, document, &error))
            return failure(error);
        QVariantList changed;
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
            changed.append(it.key());
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("updated"), true}, {QStringLiteral("permission_mode"), project.value(QStringLiteral("agentPermissionMode"))},
            {QStringLiteral("model_id"), modelId}, {QStringLiteral("changed_fields"), changed},
            {QStringLiteral("models_path"), path}}}};
    }
    if (action == QStringLiteral("set_studio_active_model")) {
        const QString modelId = arguments.value(QStringLiteral("model_id")).toString().trimmed();
        if (modelId.isEmpty() || findById(rows, modelId) < 0)
            return failure(QStringLiteral("找不到 Studio 模型：%1").arg(modelId));
        document.insert(QStringLiteral("active_model_id"), modelId);
        if (!writeObject(path, document, &error))
            return failure(error);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("updated"), true}, {QStringLiteral("permission_mode"), project.value(QStringLiteral("agentPermissionMode"))},
            {QStringLiteral("active_model_id"), modelId}, {QStringLiteral("models_path"), path}}}};
    }

    const QString capabilityId = arguments.value(QStringLiteral("capability_id")).toString().trimmed();
    if (capabilityId.isEmpty())
        return failure(QStringLiteral("capability_id 不能为空。"));
    const bool enabled = arguments.value(QStringLiteral("enabled"), true).toBool();
    QSet<QString> disabled;
    for (const QJsonValue &value : rows) {
        const QString item = value.toString().trimmed();
        if (!item.isEmpty() && item != capabilityId)
            disabled.insert(item);
    }
    if (!enabled)
        disabled.insert(capabilityId);
    QStringList sorted = disabled.values();
    std::sort(sorted.begin(), sorted.end());
    document.insert(arrayKey, QJsonArray::fromStringList(sorted));
    if (!writeObject(path, document, &error))
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("updated"), true}, {QStringLiteral("permission_mode"), project.value(QStringLiteral("agentPermissionMode"))},
        {QStringLiteral("capability_id"), capabilityId}, {QStringLiteral("enabled"), enabled},
        {QStringLiteral("capabilities_path"), path}}}};
}
