#include "../src/studionativeapi.h"
#include "../src/sessioncatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdlib>

namespace {
bool ok(const QVariantMap &response) {
    return response.value(QStringLiteral("ok")).toBool();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return EXIT_FAILURE;
    const QString projectRoot = QDir(temporary.path()).filePath(QStringLiteral("project"));
    const QString foreignRoot = QDir(temporary.path()).filePath(QStringLiteral("foreign"));
    if (!QDir().mkpath(projectRoot) || !QDir().mkpath(foreignRoot))
        return EXIT_FAILURE;
    const QVariantMap project{{QStringLiteral("id"), QStringLiteral("p-main")},
                              {QStringLiteral("root"), projectRoot}};
    const QVariantMap foreignProject{{QStringLiteral("id"), QStringLiteral("p-other")},
                                    {QStringLiteral("root"), foreignRoot}};
    const QString agentRoot = temporary.path();

    const QString modelPath = QDir(agentRoot).filePath(QStringLiteral("studio_data/config/models.json"));
    const QString projectPath = QDir(agentRoot).filePath(QStringLiteral("studio_data/config/projects.json"));
    if (!QDir().mkpath(QFileInfo(modelPath).absolutePath()))
        return EXIT_FAILURE;
    QFile modelFile(modelPath);
    const QJsonObject modelObject{
        {QStringLiteral("id"), QStringLiteral("m-native")},
        {QStringLiteral("provider"), QStringLiteral("api")},
        {QStringLiteral("api_base"), QStringLiteral("https://models.invalid/v1")},
        {QStringLiteral("api_key"), QStringLiteral("must-not-leak")},
        {QStringLiteral("api_key_file"), QStringLiteral("/secure/api.key")}};
    const QByteArray modelJson = QJsonDocument(QJsonObject{
        {QStringLiteral("active_model_id"), QStringLiteral("m-native")},
        {QStringLiteral("models"), QJsonArray{modelObject}}}).toJson(QJsonDocument::Compact);
    if (!modelFile.open(QIODevice::WriteOnly) || modelFile.write(modelJson) != modelJson.size())
        return EXIT_FAILURE;
    modelFile.close();
    const QVariantMap runConfigResponse = StudioNativeApi::dispatch(QStringLiteral("model/run-config"), {
        {QStringLiteral("model_id"), QStringLiteral("m-native")}}, agentRoot);
    const QVariantMap runModel = runConfigResponse.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("model")).toMap();
    if (!ok(runConfigResponse) || runModel.value(QStringLiteral("id")).toString() != QStringLiteral("m-native")
        || runModel.contains(QStringLiteral("api_key"))
        || runModel.value(QStringLiteral("api_key_file")).toString() != QStringLiteral("/secure/api.key"))
        return EXIT_FAILURE;

    QFile projectFile(projectPath);
    const QByteArray projectJson = QJsonDocument(QJsonObject{
        {QStringLiteral("projects"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("p-main")},
            {QStringLiteral("name"), QStringLiteral("Project Main")},
            {QStringLiteral("root"), projectRoot},
            {QStringLiteral("metadata"), QJsonObject{}}}}}}).toJson(QJsonDocument::Compact);
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write(projectJson) != projectJson.size())
        return EXIT_FAILURE;
    projectFile.close();
    const QVariantMap readiness = StudioNativeApi::dispatch(QStringLiteral("project/readiness"), {
        {QStringLiteral("project_id"), QStringLiteral("p-main")}}, agentRoot);
    const QVariantMap readinessResult = readiness.value(QStringLiteral("result")).toMap();
    if (!ok(readiness) || !readinessResult.contains(QStringLiteral("ready"))
        || readinessResult.value(QStringLiteral("ready")).toBool()
        || readinessResult.value(QStringLiteral("configuration_errors")).toList().isEmpty())
        return EXIT_FAILURE;

    if (!StudioNativeApi::supports(QStringLiteral("session/list"))
        || !StudioNativeApi::supports(QStringLiteral("session/lookup"))
        || StudioNativeApi::supports(QStringLiteral("turn/start")))
        return EXIT_FAILURE;
    const QVariantMap created = SessionCatalog::dispatch(QStringLiteral("session_new"), project,
        {{QStringLiteral("name"), QStringLiteral("Storage API test")}}, agentRoot);
    if (!ok(created))
        return EXIT_FAILURE;
    QVariantMap session = created.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("session")).toMap();
    const QString sessionId = session.value(QStringLiteral("id")).toString();
    if (sessionId.isEmpty())
        return EXIT_FAILURE;
    const QVariantMap lookup = StudioNativeApi::dispatch(QStringLiteral("session/lookup"), {
        {QStringLiteral("thread_id"), sessionId}}, agentRoot);
    const QVariantMap lookupThread = lookup.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("thread")).toMap();
    if (!ok(lookup) || lookupThread.value(QStringLiteral("project_id")).toString() != QStringLiteral("p-main")
        || lookupThread.value(QStringLiteral("workspace")).toString() != projectRoot)
        return EXIT_FAILURE;

    QVariantMap loaded = StudioNativeApi::dispatch(QStringLiteral("session/read"), {
        {QStringLiteral("project"), project}, {QStringLiteral("thread_id"), sessionId}}, agentRoot);
    if (!ok(loaded))
        return EXIT_FAILURE;
    QVariantMap thread = loaded.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("thread")).toMap();
    if (thread.value(QStringLiteral("id")).toString() != sessionId
        || !thread.value(QStringLiteral("parent_thread_id")).toString().isEmpty())
        return EXIT_FAILURE;
    QVariantList conversation = thread.value(QStringLiteral("conversation")).toList();
    conversation.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("assistant")},
                                    {QStringLiteral("text"), QStringLiteral("Native storage response")}});
    thread.insert(QStringLiteral("conversation"), conversation);
    thread.insert(QStringLiteral("updated_at"), QStringLiteral("2026-09-30T00:00:00.000Z"));
    const QVariantMap saved = SessionCatalog::dispatch(QStringLiteral("session_runtime_save"), project,
        {{QStringLiteral("thread"), thread}}, agentRoot);
    if (!ok(saved))
        return EXIT_FAILURE;

    QVariantMap child = thread;
    const QString childId = QStringLiteral("subagent-storage-review");
    child.insert(QStringLiteral("id"), childId);
    child.insert(QStringLiteral("name"), QStringLiteral("Storage review"));
    child.insert(QStringLiteral("parent_thread_id"), sessionId);
    child.insert(QStringLiteral("subagent_task_name"), QStringLiteral("review"));
    const QVariantMap savedChild = SessionCatalog::dispatch(QStringLiteral("session_runtime_save"), project,
        {{QStringLiteral("thread"), child}}, agentRoot);
    if (!ok(savedChild))
        return EXIT_FAILURE;
    const QVariantMap childLookup = StudioNativeApi::dispatch(QStringLiteral("session/lookup"), {
        {QStringLiteral("thread_id"), childId}}, agentRoot);
    if (!ok(childLookup)
        || childLookup.value(QStringLiteral("result")).toMap().value(QStringLiteral("thread")).toMap()
            .value(QStringLiteral("project_id")).toString() != QStringLiteral("p-main"))
        return EXIT_FAILURE;
    const QVariantMap children = SessionCatalog::dispatch(QStringLiteral("session_children"), project,
        {{QStringLiteral("thread_id"), sessionId}}, agentRoot);
    const QVariantList childRows = children.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("children")).toList();
    const QString rootThreadPath = QDir(temporary.path()).filePath(QStringLiteral(
        "studio_data/agent_runtime/threads/%1/%1.thread.bin").arg(sessionId));
    const QString childThreadPath = QDir(temporary.path()).filePath(QStringLiteral(
        "studio_data/agent_runtime/threads/%1/%2.thread.bin").arg(sessionId, childId));
    if (!ok(children) || childRows.size() != 1
        || childRows.first().toMap().value(QStringLiteral("parent_thread_id")).toString() != sessionId
        || !QFileInfo::exists(rootThreadPath) || !QFileInfo::exists(childThreadPath))
        return EXIT_FAILURE;

    const QVariantMap listed = StudioNativeApi::dispatch(QStringLiteral("session/list"), {
        {QStringLiteral("project"), project}}, agentRoot);
    const QVariantList sessions = listed.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("sessions")).toList();
    if (!ok(listed) || sessions.size() != 1
        || sessions.first().toMap().value(QStringLiteral("id")).toString() != sessionId)
        return EXIT_FAILURE;

    const QVariantMap reportSaved = StudioNativeApi::dispatch(QStringLiteral("report/save"), {
        {QStringLiteral("project"), project}, {QStringLiteral("session_id"), sessionId},
        {QStringLiteral("project_id"), QStringLiteral("p-main")},
        {QStringLiteral("project_name"), QStringLiteral("Project Main")},
        {QStringLiteral("title"), QStringLiteral("Storage API report")},
        {QStringLiteral("category"), QStringLiteral("design_summary")},
        {QStringLiteral("markdown"), QStringLiteral("# Verified\n\nNative report contents.")}}, agentRoot);
    if (!ok(reportSaved))
        return EXIT_FAILURE;
    const QVariantMap report = reportSaved.value(QStringLiteral("result")).toMap();
    const QString reportId = report.value(QStringLiteral("report_id")).toString();
    const QVariantMap reportList = StudioNativeApi::dispatch(QStringLiteral("report/list"), {
        {QStringLiteral("project"), project}, {QStringLiteral("category"), QStringLiteral("design_summary")}}, agentRoot);
    const QVariantList reports = reportList.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("reports")).toList();
    if (!ok(reportList) || reports.size() != 1
        || reports.first().toMap().value(QStringLiteral("report_id")).toString() != reportId)
        return EXIT_FAILURE;
    const QVariantMap reportRead = StudioNativeApi::dispatch(QStringLiteral("report/read"), {
        {QStringLiteral("project"), project}, {QStringLiteral("report_id"), reportId}}, agentRoot);
    const QVariantMap reportReadResult = reportRead.value(QStringLiteral("result")).toMap();
    if (!ok(reportRead) || !reportReadResult.value(QStringLiteral("markdown")).toString()
            .contains(QStringLiteral("Native report contents")))
        return EXIT_FAILURE;

    const QVariantMap wrongProjectRead = StudioNativeApi::dispatch(QStringLiteral("report/read"), {
        {QStringLiteral("project"), foreignProject}, {QStringLiteral("report_id"), reportId}}, agentRoot);
    if (ok(wrongProjectRead))
        return EXIT_FAILURE;
    const QVariantMap archived = StudioNativeApi::dispatch(QStringLiteral("session/archive"), {
        {QStringLiteral("project"), project}, {QStringLiteral("thread_id"), sessionId}}, agentRoot);
    if (!ok(archived))
        return EXIT_FAILURE;
    const QVariantMap archivedLoad = StudioNativeApi::dispatch(QStringLiteral("session/read"), {
        {QStringLiteral("project"), project}, {QStringLiteral("thread_id"), sessionId}}, agentRoot);
    if (!ok(archivedLoad))
        return EXIT_FAILURE;
    const QVariantMap archivedThread = archivedLoad.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("thread")).toMap();
    if (!archivedThread.value(QStringLiteral("archived")).toBool())
        return EXIT_FAILURE;
    const QVariantMap restored = StudioNativeApi::dispatch(QStringLiteral("session/archive"), {
        {QStringLiteral("project"), project}, {QStringLiteral("thread_id"), sessionId},
        {QStringLiteral("archived"), false}}, agentRoot);
    if (!ok(restored))
        return EXIT_FAILURE;

    QTcpServer modelServer;
    if (!modelServer.listen(QHostAddress::LocalHost, 0))
        return EXIT_FAILURE;
    QObject::connect(&modelServer, &QTcpServer::newConnection, &app, [&modelServer] {
        while (QTcpSocket *socket = modelServer.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                const QByteArray body = R"({"data":[{"id":"live-model-a"},{"id":"live-model-b"}]})";
                const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });
    const QJsonObject apiModel{
        {QStringLiteral("id"), QStringLiteral("stale-model")},
        {QStringLiteral("label"), QStringLiteral("Stale model")},
        {QStringLiteral("provider"), QStringLiteral("api")},
        {QStringLiteral("api_base"), QStringLiteral("http://127.0.0.1:%1/v1").arg(modelServer.serverPort())},
        {QStringLiteral("api_key"), QStringLiteral("secret-value")},
        {QStringLiteral("api_key_file"), QStringLiteral("/secret/key")}};
    const QByteArray refreshConfig = QJsonDocument(QJsonObject{
        {QStringLiteral("active_model_id"), QStringLiteral("stale-model")},
        {QStringLiteral("models"), QJsonArray{apiModel}}}).toJson(QJsonDocument::Compact);
    QFile refreshFile(modelPath);
    if (!refreshFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || refreshFile.write(refreshConfig) != refreshConfig.size())
        return EXIT_FAILURE;
    refreshFile.close();
    const QVariantMap refreshed = StudioNativeApi::dispatch(QStringLiteral("model/list"), {
        {QStringLiteral("refresh"), true}}, agentRoot);
    const QVariantMap refreshedResult = refreshed.value(QStringLiteral("result")).toMap();
    const QVariantList refreshedModels = refreshedResult.value(QStringLiteral("models")).toList();
    const QVariantMap refreshedModel = refreshedModels.value(0).toMap();
    if (!ok(refreshed) || refreshedResult.value(QStringLiteral("active_model_id")).toString()
            != QStringLiteral("live-model-a")
        || refreshedModel.value(QStringLiteral("id")).toString() != QStringLiteral("live-model-a")
        || refreshedModel.value(QStringLiteral("available_models")).toList()
            != QVariantList{QStringLiteral("live-model-a"), QStringLiteral("live-model-b")}
        || refreshedModel.contains(QStringLiteral("api_key")) || refreshedModel.contains(QStringLiteral("api_key_file"))
        || !refreshedResult.value(QStringLiteral("refresh_errors")).toList().isEmpty())
        return EXIT_FAILURE;

    const QString migratedRun = QDir(temporary.path()).filePath(
        QStringLiteral("studio_data/runs/legacy-run.log"));
    if (!QDir().mkpath(QFileInfo(migratedRun).absolutePath()))
        return EXIT_FAILURE;
    QFile migratedFile(migratedRun);
    if (!migratedFile.open(QIODevice::WriteOnly) || migratedFile.write("run") != 3)
        return EXIT_FAILURE;
    migratedFile.close();
    const QVariantMap resolvedPath = StudioNativeApi::dispatch(QStringLiteral("storage/resolve-path"), {
        {QStringLiteral("path"), QStringLiteral("artifacts/runs/legacy-run.log")}}, agentRoot);
    const QVariantMap storageDiagnosis = StudioNativeApi::dispatch(QStringLiteral("recovery/diagnose"), {}, agentRoot);
    const QVariantMap diagnosisResult = storageDiagnosis.value(QStringLiteral("result")).toMap();
    if (!ok(resolvedPath) || resolvedPath.value(QStringLiteral("path")).toString() != migratedRun
        || !ok(storageDiagnosis) || diagnosisResult.value(QStringLiteral("mode")).toString() != QStringLiteral("read_only")
        || diagnosisResult.value(QStringLiteral("session_count")).toInt() != 1)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
