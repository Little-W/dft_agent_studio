#include "studionativeapi.h"

#include "runreportservice.h"
#include "sessioncatalog.h"
#include "configureddftflowservice.h"
#include "studiostorageservice.h"
#include "studiotoolservice.h"

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QVariantMap projectFrom(const QVariantMap &params) {
    QVariantMap project = params.value(QStringLiteral("project")).toMap();
    if (project.isEmpty()) {
        project.insert(QStringLiteral("id"), params.value(QStringLiteral("project_id")));
        project.insert(QStringLiteral("root"), params.value(QStringLiteral("workspace")));
    }
    return project;
}
}

bool StudioNativeApi::supports(const QString &method) {
    return method == QStringLiteral("session/list") || method == QStringLiteral("session/list_all")
        || method == QStringLiteral("session/lookup")
        || method == QStringLiteral("session/new") || method == QStringLiteral("session/read")
        || method == QStringLiteral("session/save") || method == QStringLiteral("session/progress")
        || method == QStringLiteral("session/children") || method == QStringLiteral("session/archive")
        || method == QStringLiteral("session/delete") || method == QStringLiteral("project/list")
        || method == QStringLiteral("project/read") || method == QStringLiteral("project/update")
        || method == QStringLiteral("project/readiness")
        || method == QStringLiteral("settings/read") || method == QStringLiteral("model/list")
        || method == QStringLiteral("model/update") || method == QStringLiteral("model/activate")
        || method == QStringLiteral("model/run-config")
        || method == QStringLiteral("capability/set") || method == QStringLiteral("storage/resolve-path")
        || method == QStringLiteral("storage/remap-record") || method == QStringLiteral("storage/diagnose")
        || method == QStringLiteral("recovery/diagnose")
        || method == QStringLiteral("report/list") || method == QStringLiteral("report/read")
        || method == QStringLiteral("report/save");
}

QVariantMap StudioNativeApi::dispatch(const QString &method, const QVariantMap &params,
                                      const QString &agentRoot) {
    if (!supports(method))
        return failure(QStringLiteral("原生 Studio 存储 API 不支持该方法。"));
    const QVariantMap project = projectFrom(params);
    QVariantMap payload = params;
    payload.remove(QStringLiteral("project"));
    payload.remove(QStringLiteral("workspace"));
    if (method == QStringLiteral("session/lookup"))
        return SessionCatalog::dispatch(QStringLiteral("session_runtime_lookup"), {}, payload, agentRoot);
    if (method == QStringLiteral("session/list") || method == QStringLiteral("session/list_all"))
        return SessionCatalog::dispatch(method == QStringLiteral("session/list_all")
            ? QStringLiteral("sessions_all") : QStringLiteral("sessions"), project, payload, agentRoot);
    if (method == QStringLiteral("session/new"))
        return SessionCatalog::dispatch(QStringLiteral("session_new"), project, payload, agentRoot);
    if (method == QStringLiteral("session/read")) {
        QVariantMap loaded = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), project,
                                                       payload, agentRoot);
        if (!loaded.value(QStringLiteral("ok")).toBool())
            return loaded;
        QVariantMap result = loaded.value(QStringLiteral("result")).toMap();
        const QVariantMap thread = result.value(QStringLiteral("thread")).toMap();
        result.insert(QStringLiteral("conversation"), thread.value(QStringLiteral("conversation")));
        result.insert(QStringLiteral("turns"), thread.value(QStringLiteral("turn_records")));
        const QVariantMap events = SessionCatalog::dispatch(QStringLiteral("session_runtime_events"),
            project, payload, agentRoot);
        if (!events.value(QStringLiteral("ok")).toBool())
            return events;
        result.insert(QStringLiteral("events"), events.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("events")));
        loaded.insert(QStringLiteral("result"), result);
        return loaded;
    }
    if (method == QStringLiteral("session/save"))
        return SessionCatalog::dispatch(QStringLiteral("session_runtime_save"), project, payload, agentRoot);
    if (method == QStringLiteral("session/progress"))
        return SessionCatalog::dispatch(QStringLiteral("session_progress"), project, payload, agentRoot);
    if (method == QStringLiteral("session/children"))
        return SessionCatalog::dispatch(QStringLiteral("session_children"), project, payload, agentRoot);
    if (method == QStringLiteral("session/archive"))
        return SessionCatalog::dispatch(payload.value(QStringLiteral("archived"), true).toBool()
            ? QStringLiteral("session_archive") : QStringLiteral("session_restore"), project, payload, agentRoot);
    if (method == QStringLiteral("session/delete"))
        return SessionCatalog::dispatch(QStringLiteral("session_delete"), project, payload, agentRoot);
    if (method == QStringLiteral("report/list"))
        return RunReportService::list(project, payload, agentRoot);
    if (method == QStringLiteral("report/read"))
        return RunReportService::read(project, payload, agentRoot);
    if (method == QStringLiteral("report/save"))
        return RunReportService::save(project, payload, agentRoot);
    if (method == QStringLiteral("project/list"))
        return StudioToolService::dispatch(QStringLiteral("list_studio_projects"), project, payload, agentRoot);
    if (method == QStringLiteral("project/read"))
        return StudioToolService::dispatch(QStringLiteral("inspect_studio_project"), project, payload, agentRoot);
    if (method == QStringLiteral("project/readiness")) {
        QVariantMap projectResponse = StudioToolService::dispatch(
            QStringLiteral("inspect_studio_project"), project, payload, agentRoot);
        if (!projectResponse.value(QStringLiteral("ok")).toBool())
            return projectResponse;
        const QVariantMap projectData = projectResponse.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("project")).toMap();
        return ConfiguredDftFlowService::readiness(projectData, {}, agentRoot);
    }
    if (method == QStringLiteral("project/update"))
        return StudioToolService::dispatch(QStringLiteral("update_studio_project"), project, payload, agentRoot);
    if (method == QStringLiteral("settings/read"))
        return StudioToolService::dispatch(QStringLiteral("inspect_studio_settings"), project, payload, agentRoot);
    if (method == QStringLiteral("model/list"))
        return StudioToolService::listModels(payload.value(QStringLiteral("refresh")).toBool(), agentRoot);
    if (method == QStringLiteral("model/update"))
        return StudioToolService::dispatch(QStringLiteral("update_studio_model"), project, payload, agentRoot);
    if (method == QStringLiteral("model/activate"))
        return StudioToolService::dispatch(QStringLiteral("set_studio_active_model"), project, payload, agentRoot);
    if (method == QStringLiteral("capability/set"))
        return StudioToolService::dispatch(QStringLiteral("set_studio_capability"), project, payload, agentRoot);
    if (method == QStringLiteral("storage/resolve-path"))
        return StudioStorageService::resolvePath(payload.value(QStringLiteral("path")).toString(), agentRoot);
    if (method == QStringLiteral("model/run-config"))
        return StudioStorageService::modelRunConfig(payload.value(QStringLiteral("model_id")).toString(), agentRoot);
    if (method == QStringLiteral("storage/remap-record"))
        return StudioStorageService::remapRecord(payload.value(QStringLiteral("value")), agentRoot);
    if (method == QStringLiteral("storage/diagnose") || method == QStringLiteral("recovery/diagnose"))
        return StudioStorageService::diagnose(agentRoot);
    return failure(QStringLiteral("原生 Studio 存储 API 不支持该方法。"));
}
