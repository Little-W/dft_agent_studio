#include "sessioncatalog.h"
#include "runreportservice.h"
#include "studiocliturnservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimer>

#include <cstdio>

namespace {
bool check(bool condition, const char *message) {
    if (condition)
        return true;
    std::fprintf(stderr, "Check failed: %s\n", message);
    return false;
}

bool runOneFakeTurn(const QString &agentRoot, const QString &workspace, const QString &sessionId,
                    bool createNew, const QString &goal, QString *createdId, QString *answer,
                    bool saveReport = false, QVariantMap *reportToolEvent = nullptr,
                    bool localLlama = false) {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return check(false, "fake Responses server listens");
    int responseNumber = 0;
    QString requestLine;
    QObject::connect(&server, &QTcpServer::newConnection, &server,
        [&server, &responseNumber, &requestLine, saveReport, localLlama] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
            [socket, &responseNumber, &requestLine, saveReport, localLlama] {
            QByteArray received = socket->property("requestBytes").toByteArray();
            received += socket->readAll();
            socket->setProperty("requestBytes", received);
            const qsizetype boundary = received.indexOf("\r\n\r\n");
            if (boundary < 0)
                return;
            requestLine = received.left(boundary).split('\n').first().trimmed();
            qsizetype contentLength = 0;
            for (const QByteArray &line : received.left(boundary).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
            }
            if (received.size() - boundary - 4 < contentLength)
                return;
            if (socket->property("responded").toBool())
                return;
            socket->setProperty("responded", true);
            QByteArray body;
            if (saveReport && responseNumber++ == 0) {
                const QJsonObject arguments{
                    {QStringLiteral("title"), QStringLiteral("CLI report")},
                    {QStringLiteral("category"), QStringLiteral("run_report")},
                    {QStringLiteral("markdown"), QStringLiteral("# Verified report\n")},
                };
                const QJsonObject call{
                    {QStringLiteral("type"), QStringLiteral("function_call")},
                    {QStringLiteral("call_id"), QStringLiteral("call-save-report")},
                    {QStringLiteral("name"), QStringLiteral("save_run_report")},
                    {QStringLiteral("arguments"), QString::fromUtf8(
                        QJsonDocument(arguments).toJson(QJsonDocument::Compact))},
                };
                const QJsonObject event{
                    {QStringLiteral("type"), QStringLiteral("response.completed")},
                    {QStringLiteral("response"), QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("cli-report-call")},
                        {QStringLiteral("status"), QStringLiteral("completed")},
                        {QStringLiteral("output"), QJsonArray{call}},
                    }},
                };
                body = QByteArrayLiteral("data: ")
                    + QJsonDocument(event).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n\n");
            } else if (localLlama) {
                body = QByteArrayLiteral(
                    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"native cli answer\"},\"finish_reason\":null}]}\n\n"
                    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
                    "data: [DONE]\n\n");
            } else {
                body = QByteArrayLiteral(
                    "data: {\"type\":\"response.output_text.delta\",\"delta\":\"native cli answer\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"cli-response\",\"status\":\"completed\",\"output_text\":\"native cli answer\"}}\n\n");
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    StudioCliTurnService::Request request;
    request.project = {{QStringLiteral("id"), QStringLiteral("cli-check")},
        {QStringLiteral("name"), QStringLiteral("CLI integration")},
        {QStringLiteral("root"), workspace}};
    if (localLlama) {
        request.project.insert(QStringLiteral("modelProvider"), QStringLiteral("legacy"));
        request.project.insert(QStringLiteral("modelRuntime"), QStringLiteral("llama_cpp"));
    }
    request.agentRoot = agentRoot;
    request.sessionId = sessionId;
    request.createNewSession = createNew;
    request.sessionName = QStringLiteral("CLI integration test");
    request.goal = goal;
    request.permissionMode = QStringLiteral("full_access");
    request.multiAgent = true;
    request.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    request.model = QStringLiteral("fake-model");
    request.contextWindow = 65'536;
    request.maximumOutputTokens = 512;
    request.reconnectMaxAttempts = 0;

    StudioCliTurnService service;
    bool completed = false;
    QVariantMap result;
    QObject::connect(&service, &StudioCliTurnService::activity, &service,
        [reportToolEvent](const QString &, const QVariantMap &event) {
            if (reportToolEvent && event.value(QStringLiteral("name")).toString() == QStringLiteral("save_run_report"))
                *reportToolEvent = event;
        });
    QObject::connect(&service, &StudioCliTurnService::completed, &service,
        [&](const QString &, const QVariantMap &value) { completed = true; result = value; });
    QObject::connect(&service, &StudioCliTurnService::failed, &service,
        [&](const QString &, const QVariantMap &value) { result = value; });
    const QVariantMap started = service.start(request);
    if (!started.value(QStringLiteral("ok")).toBool()) {
        std::fprintf(stderr, "Turn start failed: %s\n", qPrintable(started.value(QStringLiteral("message")).toString()));
        return false;
    }
    if (createdId)
        *createdId = started.value(QStringLiteral("session_id")).toString();
    const QString activeSessionId = started.value(QStringLiteral("session_id")).toString();
    const QVariantMap runningProgress = SessionCatalog::dispatch(QStringLiteral("session_progress"), request.project,
        {{QStringLiteral("thread_id"), activeSessionId}}, agentRoot);
    const QVariantMap runningSession = runningProgress.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("session")).toMap();
    const bool runningStatusPersisted = runningProgress.value(QStringLiteral("ok")).toBool()
        && runningSession.value(QStringLiteral("execution_status")).toString() == QStringLiteral("running")
        && runningSession.value(QStringLiteral("execution_state")).toString() == QStringLiteral("agent_thinking");
    QEventLoop loop;
    QObject::connect(&service, &StudioCliTurnService::completed, &loop, &QEventLoop::quit);
    QObject::connect(&service, &StudioCliTurnService::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
    loop.exec();
    if (answer)
        *answer = result.value(QStringLiteral("answer")).toString();
    const QVariantMap finalProgress = SessionCatalog::dispatch(QStringLiteral("session_progress"), request.project,
        {{QStringLiteral("thread_id"), activeSessionId}}, agentRoot);
    const QVariantMap finalSession = finalProgress.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("session")).toMap();
    if (!completed)
        std::fprintf(stderr, "CLI turn diagnostic: result=%s answer=%s\n",
            qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact))),
            answer ? qPrintable(*answer) : "");
    const QString expectedPath = localLlama ? QStringLiteral("POST /v1/chat/completions")
                                            : QStringLiteral("POST /v1/responses");
    return check(runningStatusPersisted, "CLI marks the root session running before model/tool work")
        && check(finalProgress.value(QStringLiteral("ok")).toBool()
                     && finalSession.value(QStringLiteral("execution_status")).toString() == QStringLiteral("completed")
                     && finalSession.value(QStringLiteral("progress")).toInt() == 100,
                 "CLI persists completed session status and final progress")
        && check(requestLine.startsWith(expectedPath), "CLI selects the configured provider route")
        && check(completed, "native CLI turn reaches completed state")
        && check(result.value(QStringLiteral("status")).toString() == QStringLiteral("completed"),
                 "completed turn result reports success");
}

bool runExecutableControlTest(const QString &agentRoot, const QString &workspace) {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return check(false, "control protocol fake server listens");
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            QByteArray received = socket->property("requestBytes").toByteArray();
            received += socket->readAll();
            socket->setProperty("requestBytes", received);
            const qsizetype boundary = received.indexOf("\r\n\r\n");
            if (boundary < 0)
                return;
            qsizetype contentLength = 0;
            for (const QByteArray &line : received.left(boundary).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
            }
            if (received.size() - boundary - 4 < contentLength || socket->property("responseScheduled").toBool())
                return;
            socket->setProperty("responseScheduled", true);
            QTimer::singleShot(2'000, socket, [socket] {
                if (socket->state() == QAbstractSocket::UnconnectedState)
                    return;
                const QByteArray body = "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"cancel\",\"status\":\"completed\",\"output_text\":\"late\"}}\n\n";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    });

#ifndef DFT_STUDIO_AGENT_CLI_PATH
    return check(false, "agent CLI binary path is configured for control protocol test");
#else
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOME"), agentRoot);
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(DFT_STUDIO_AGENT_CLI_PATH));
    if (!process.waitForStarted(5'000))
        return check(false, "native agent CLI process starts");
    const QJsonObject request{
        {QStringLiteral("agent_root"), agentRoot},
        {QStringLiteral("project"), QJsonObject{{QStringLiteral("id"), QStringLiteral("cli-control")},
            {QStringLiteral("name"), QStringLiteral("CLI control")},
            {QStringLiteral("root"), workspace}, {QStringLiteral("goal"), QStringLiteral("test goal")}}},
        {QStringLiteral("new_session"), true}, {QStringLiteral("goal"), QStringLiteral("cancel this turn")},
        {QStringLiteral("permission_mode"), QStringLiteral("full_access")},
        {QStringLiteral("model"), QJsonObject{{QStringLiteral("base_url"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort())},
            {QStringLiteral("id"), QStringLiteral("fake-model")},
            {QStringLiteral("reconnect_max_attempts"), 0}}},
    };
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    process.waitForBytesWritten(2'000);
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains("\"type\":\"started\"") && timer.elapsed() < 10'000) {
        if (process.waitForReadyRead(500))
            output += process.readAllStandardOutput();
    }
    if (!check(output.contains("\"type\":\"started\""), "CLI emits started before model completion")) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    process.write("{\"type\":\"cancel\"}\n");
    process.waitForBytesWritten(2'000);
    timer.restart();
    while (!output.contains("\"type\":\"failed\"") && timer.elapsed() < 10'000) {
        if (process.waitForReadyRead(500))
            output += process.readAllStandardOutput();
    }
    process.waitForFinished(5'000);
    if (!(output.contains("\"type\":\"control_result\"")
          && output.contains("\"command\":\"cancel\"")
          && output.contains("\"type\":\"failed\"")))
        std::fprintf(stderr, "Control CLI exit=%d output=%s stderr=%s\n", process.exitCode(),
            output.constData(), process.readAllStandardError().constData());
    return check(output.contains("\"type\":\"control_result\"")
                     && output.contains("\"command\":\"cancel\"")
                     && output.contains("\"type\":\"failed\""),
                 "stdin cancel command is handled asynchronously and exits with structured failure");
#endif
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "temporary Studio root is created"))
        return 1;
    const QString agentRoot = temporary.path();
    const QString workspace = QDir(agentRoot).filePath(QStringLiteral("project"));
    const QString isolatedHome = QDir(agentRoot).filePath(QStringLiteral("home"));
    if (!QDir().mkpath(workspace) || !QDir().mkpath(isolatedHome))
        return 1;
    qputenv("HOME", QFile::encodeName(isolatedHome));

    QString sessionId;
    QString answer;
    QVariantMap reportToolEvent;
    if (!runOneFakeTurn(agentRoot, workspace, {}, true, QStringLiteral("first goal"), &sessionId, &answer,
                        true, &reportToolEvent))
        return 1;
    if (!check(!sessionId.isEmpty() && answer == QStringLiteral("native cli answer"),
               "new turn returns session identity and model answer"))
        return 1;
    QVariantMap project{{QStringLiteral("id"), QStringLiteral("cli-check")},
        {QStringLiteral("root"), workspace}};
    const QVariantMap loaded = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), project,
        {{QStringLiteral("thread_id"), sessionId}}, agentRoot);
    if (!check(loaded.value(QStringLiteral("ok")).toBool(), "completed root session reloads"))
        return 1;
    const QVariantMap thread = loaded.value(QStringLiteral("result")).toMap().value(QStringLiteral("thread")).toMap();
    if (!check(thread.value(QStringLiteral("conversation")).toList().size() == 2
                   && thread.value(QStringLiteral("turn_records")).toList().size() == 1,
               "user/assistant conversation and turn record persist"))
        return 1;
    const QVariantMap reportList = RunReportService::list(project,
        {{QStringLiteral("session_id"), sessionId}}, agentRoot);
    const QVariantList reports = reportList.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("reports")).toList();
    if (!reportList.value(QStringLiteral("ok")).toBool() || reports.size() != 1) {
        std::fprintf(stderr, "Report archive diagnostic: session=%s list=%s tool=%s\n",
            qPrintable(sessionId),
            qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(reportList).toJson(QJsonDocument::Compact))),
            qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(reportToolEvent).toJson(QJsonDocument::Compact))));
    }
    if (!check(reportList.value(QStringLiteral("ok")).toBool() && reports.size() == 1
                   && reports.first().toMap().value(QStringLiteral("session_id")).toString() == sessionId,
               "CLI runtime archives reports under the active root session"))
        return 1;

    if (!runOneFakeTurn(agentRoot, workspace, sessionId, false, QStringLiteral("follow-up goal"), nullptr, &answer))
        return 1;
    const QVariantMap resumed = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), project,
        {{QStringLiteral("thread_id"), sessionId}}, agentRoot);
    const QVariantMap resumedThread = resumed.value(QStringLiteral("result")).toMap().value(QStringLiteral("thread")).toMap();
    if (!check(resumed.value(QStringLiteral("ok")).toBool()
                     && resumedThread.value(QStringLiteral("conversation")).toList().size() == 4
                     && resumedThread.value(QStringLiteral("turn_records")).toList().size() == 2,
                 "resumed turn appends to the existing session without losing prior history"))
        return 1;
    if (!runOneFakeTurn(agentRoot, workspace, {}, true, QStringLiteral("local llama.cpp route"), nullptr,
                        &answer, false, nullptr, true))
        return 1;
    return runExecutableControlTest(agentRoot, workspace) ? 0 : 1;
}
