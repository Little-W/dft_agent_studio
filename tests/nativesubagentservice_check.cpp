#include "nativesubagentservice.h"

#include "sessioncatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

namespace {
bool fail(const char *reason) {
    std::fprintf(stderr, "native-subagent check: %s\n", reason);
    return false;
}

class FakeResponsesServer final : public QObject {
public:
    explicit FakeResponsesServer(QObject *parent = nullptr) : QObject(parent) {
        QObject::connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                buffers.insert(socket, {});
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray &buffer = buffers[socket];
                    buffer.append(socket->readAll());
                    const qsizetype separator = buffer.indexOf("\r\n\r\n");
                    if (separator < 0)
                        return;
                    const qsizetype lengthHeader = buffer.toLower().indexOf("content-length:");
                    if (lengthHeader < 0)
                        return;
                    const qsizetype lineEnd = buffer.indexOf("\r\n", lengthHeader);
                    const qsizetype length = buffer.mid(lengthHeader + 15,
                        lineEnd - lengthHeader - 15).trimmed().toLongLong();
                    const qsizetype bodyStart = separator + 4;
                    if (buffer.size() - bodyStart < length)
                        return;
                    const QJsonObject request = QJsonDocument::fromJson(
                        buffer.mid(bodyStart, length)).object();
                    requests.append(request);
                    const QJsonArray input = request.value(QStringLiteral("input")).toArray();
                    bool hasToolOutput = false;
                    for (const QJsonValue &item : input)
                        hasToolOutput = hasToolOutput || item.toObject().value(QStringLiteral("type")).toString()
                            == QStringLiteral("function_call_output");
                    const QJsonObject response = hasToolOutput
                        ? QJsonObject{{QStringLiteral("id"), QStringLiteral("fake-final")},
                                      {QStringLiteral("output_text"), QStringLiteral("fake child result")}}
                        : QJsonObject{{QStringLiteral("id"), QStringLiteral("fake-tool-call")},
                                      {QStringLiteral("output"), QJsonArray{QJsonObject{
                                          {QStringLiteral("type"), QStringLiteral("function_call")},
                                          {QStringLiteral("call_id"), QStringLiteral("fake-call")},
                                          {QStringLiteral("name"), QStringLiteral("fake_tool")},
                                          {QStringLiteral("arguments"), QStringLiteral("{}")},
                                      }}}};
                    const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                    socket->write(QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    buffers.remove(socket);
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    bool start() { return server.listen(QHostAddress::LocalHost); }
    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    }
    QTcpServer server;
    QHash<QTcpSocket *, QByteArray> buffers;
    QList<QJsonObject> requests;
};

bool waitForResults(NativeSubagentService &service, int expected, QList<QJsonObject> *results) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&service, &NativeSubagentService::resultReady, &loop,
        [results, expected, &loop](const QString &, const QJsonObject &) {
            if (results->size() >= expected)
                loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(8'000);
    if (results->size() < expected)
        loop.exec();
    return results->size() >= expected;
}
}

bool nativeSubagentServiceCheck() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail("temporary directory unavailable");
    const QString agentRoot = temporary.path() + QStringLiteral("/agent");
    const QString workspace = temporary.path() + QStringLiteral("/project");
    if (!QDir().mkpath(agentRoot) || !QDir().mkpath(workspace))
        return fail("workspace directory creation failed");
    QVariantMap project{{QStringLiteral("id"), QStringLiteral("subagent-fixture")},
                        {QStringLiteral("root"), workspace}};
    const QVariantMap root = SessionCatalog::dispatch(QStringLiteral("session_new"), project,
        {{QStringLiteral("name"), QStringLiteral("subagent parent")}}, agentRoot);
    if (!root.value(QStringLiteral("ok")).toBool())
        return fail("root session creation failed");
    const QString parentId = root.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("session")).toMap().value(QStringLiteral("id")).toString();

    FakeResponsesServer server;
    if (!server.start())
        return fail("fake Responses server failed to listen");
    NativeSubagentService service;
    service.setMaximumParallelTurns(2);
    QList<QJsonObject> results;
    int fakeToolCalls = 0;
    QObject::connect(&service, &NativeSubagentService::resultReady, &service,
        [&results](const QString &, const QJsonObject &result) { results.append(result); });
    const auto makeTask = [&](const QString &goal) {
        NativeSubagentService::Task task;
        task.project = project;
        task.agentRoot = agentRoot;
        task.parentSessionId = parentId;
        task.task = goal;
        task.role = QStringLiteral("researcher");
        task.specialty = QStringLiteral("source audit");
        task.turn.baseUrl = server.baseUrl();
        task.turn.apiKey = QStringLiteral("fake-key");
        task.turn.model = QStringLiteral("fixture-model");
        task.turn.maximumToolRounds = 3;
        task.turn.timeoutMs = 3'000;
        task.turn.reconnectMaxAttempts = 1;
        task.turn.tools = QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                        {QStringLiteral("name"), QStringLiteral("fake_tool")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                        {QStringLiteral("name"), QStringLiteral("spawn_subagent")}},
        };
        return task;
    };
    const auto fakeExecutor = [&fakeToolCalls](const QString &name, const QJsonObject &,
                                               const QString &, ResponsesTurnRunner::ToolResult done) {
        if (name != QStringLiteral("fake_tool")) {
            done(QJsonObject{{QStringLiteral("success"), false},
                             {QStringLiteral("error"), QStringLiteral("unexpected tool")}});
            return;
        }
        ++fakeToolCalls;
        done(QJsonObject{{QStringLiteral("success"), true},
                         {QStringLiteral("result"), QStringLiteral("fixture evidence")}});
    };

    service.submit(makeTask(QStringLiteral("Inspect module alpha and report evidence.")), fakeExecutor);
    service.submit(makeTask(QStringLiteral("Inspect module beta and report evidence.")), fakeExecutor);
    if (!waitForResults(service, 2, &results) || fakeToolCalls != 2 || service.activeTurnCount() != 0) {
        std::fprintf(stderr, "native-subagent check: parallel turns; results=%zu tools=%d active=%d requests=%zu\n",
                     static_cast<size_t>(results.size()), fakeToolCalls, service.activeTurnCount(),
                     static_cast<size_t>(server.requests.size()));
        return false;
    }
    QString alphaChild;
    QString betaChild;
    QJsonObject alphaThread;
    for (const QJsonObject &result : std::as_const(results)) {
        const QString childId = result.value(QStringLiteral("subagent_id")).toString();
        const QVariantMap childLoad = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), project,
            {{QStringLiteral("thread_id"), childId}}, agentRoot);
        const QJsonObject childThread = QJsonObject::fromVariantMap(childLoad.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("thread")).toMap());
        const QString goal = childThread.value(QStringLiteral("subagent_task_goal")).toString();
        if (goal.contains(QStringLiteral("alpha"))) {
            alphaChild = childId;
            alphaThread = childThread;
        } else if (goal.contains(QStringLiteral("beta"))) {
            betaChild = childId;
        }
    }
    if (alphaChild.isEmpty() || betaChild.isEmpty() || alphaChild == betaChild)
        return fail("parallel tasks did not receive distinct child sessions");

    const QVariantMap childrenResult = SessionCatalog::dispatch(QStringLiteral("session_children"), project,
        {{QStringLiteral("thread_id"), parentId}}, agentRoot);
    if (!childrenResult.value(QStringLiteral("ok")).toBool()
        || childrenResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("children")).toList().size() != 2)
        return fail("root session child listing did not persist both children");
    if (alphaThread.value(QStringLiteral("parent_thread_id")).toString() != parentId
        || alphaThread.value(QStringLiteral("turn_records")).toArray().isEmpty()
        || alphaThread.value(QStringLiteral("conversation")).toArray().size() != 2)
        return fail("first child thread did not persist completed turn state");

    results.clear();
    service.submit(makeTask(QStringLiteral("Inspect module alpha and report evidence.")), fakeExecutor);
    if (!waitForResults(service, 1, &results)
        || results.first().value(QStringLiteral("subagent_id")).toString() != alphaChild) {
        std::fprintf(stderr, "native-subagent check: follow-up; results=%zu tools=%d active=%d requests=%zu\n",
                     static_cast<size_t>(results.size()), fakeToolCalls, service.activeTurnCount(),
                     static_cast<size_t>(server.requests.size()));
        return false;
    }
    if (fakeToolCalls != 3 || server.requests.size() < 6)
        return fail("follow-up did not use the same child and complete a second Responses turn");
    for (const QJsonObject &request : std::as_const(server.requests)) {
        for (const QJsonValue &tool : request.value(QStringLiteral("tools")).toArray())
            if (tool.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("spawn_subagent"))
                return fail("child inherited recursive subagent tools");
    }
    return true;
}

#ifdef NATIVE_SUBAGENT_SERVICE_STANDALONE
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return nativeSubagentServiceCheck() ? 0 : 1;
}
#endif
