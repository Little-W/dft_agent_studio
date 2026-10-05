#include "studiocliturnservice.h"
#include "studiopaths.h"
#include "studiouserdata.h"
#include "studionativeapi.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QPointer>
#include <QSocketNotifier>
#include <QTextStream>
#include <QVariantMap>

#include <cstdio>
#include <unistd.h>

#ifndef DFT_AGENT_STUDIO_VERSION
#define DFT_AGENT_STUDIO_VERSION "0.1.0"
#endif

namespace {
constexpr qsizetype MaximumLineBytes = 4 * 1024 * 1024;
constexpr int MaximumActiveTurns = 16;

void stderrMessage(QtMsgType, const QMessageLogContext &, const QString &message) {
    const QByteArray bytes = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", bytes.constData());
    std::fflush(stderr);
}

bool validId(const QJsonValue &id) {
    return id.isNull() || id.isString() || id.isDouble();
}

QJsonObject rpcError(const QJsonValue &id, int code, const QString &message,
                     const QJsonValue &data = {}) {
    QJsonObject error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!data.isUndefined())
        error.insert(QStringLiteral("data"), data);
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id}, {QStringLiteral("error"), error}};
}

bool managementMethod(const QString &method) {
    static const QSet<QString> methods{
        QStringLiteral("project/list"), QStringLiteral("project/read"),
        QStringLiteral("project/readiness"), QStringLiteral("project/update"),
        QStringLiteral("model/list"), QStringLiteral("model/activate"),
        QStringLiteral("model/run-config"), QStringLiteral("session/list"),
        QStringLiteral("session/read"), QStringLiteral("session/archive"),
        QStringLiteral("session/delete"), QStringLiteral("session/new"),
        QStringLiteral("report/list"), QStringLiteral("report/read"),
        QStringLiteral("report/save"),
    };
    return methods.contains(method);
}

QString statusMessage(const QVariantMap &value) {
    QString message = value.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = value.value(QStringLiteral("error")).toString();
    return message.isEmpty() ? QStringLiteral("Native Studio operation failed.") : message;
}

class StudioServer final : public QObject {
public:
    explicit StudioServer(QString root, QObject *parent = nullptr)
        : QObject(parent), m_agentRoot(std::move(root)), m_output(stdout) {}

    void consume(const QByteArray &bytes) {
        m_input.append(bytes);
        while (true) {
            const qsizetype newline = m_input.indexOf('\n');
            if (newline < 0) {
                if (m_input.size() > MaximumLineBytes) {
                    m_input.clear();
                    write(rpcError(QJsonValue(QJsonValue::Null), -32700,
                                   QStringLiteral("JSON-RPC line exceeds 4 MiB.")));
                }
                return;
            }
            QByteArray line = m_input.left(newline);
            m_input.remove(0, newline + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.size() > MaximumLineBytes) {
                write(rpcError(QJsonValue(QJsonValue::Null), -32700,
                               QStringLiteral("JSON-RPC line exceeds 4 MiB.")));
                continue;
            }
            if (!line.trimmed().isEmpty())
                processLine(line);
        }
    }

private:
    QString m_agentRoot;
    QTextStream m_output;
    QByteArray m_input;
    QHash<QString, QPointer<StudioCliTurnService>> m_turnsById;
    QHash<QString, QPointer<StudioCliTurnService>> m_turnsBySession;

    void write(const QJsonObject &object) {
        m_output << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
    }

    void reply(const QJsonValue &id, const QJsonValue &result) {
        write({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
               {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
    }

    void notify(const QString &method, const QJsonObject &params) {
        write({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
               {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
    }

    void processLine(const QByteArray &line) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            write(rpcError(QJsonValue(QJsonValue::Null), -32700,
                           QStringLiteral("Parse error: %1").arg(parseError.errorString())));
            return;
        }
        const QJsonObject request = document.object();
        const QJsonValue id = request.value(QStringLiteral("id"));
        const bool hasId = request.contains(QStringLiteral("id"));
        if (request.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")
            || !request.value(QStringLiteral("method")).isString()
            || (hasId && !validId(id))) {
            if (hasId)
                write(rpcError(id, -32600, QStringLiteral("Invalid JSON-RPC request.")));
            return;
        }
        const QString method = request.value(QStringLiteral("method")).toString();
        if (method.size() > 128) {
            if (hasId)
                write(rpcError(id, -32600, QStringLiteral("Method name exceeds 128 characters.")));
            return;
        }
        const QJsonValue paramsValue = request.value(QStringLiteral("params"));
        if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
            if (hasId)
                write(rpcError(id, -32602, QStringLiteral("params must be a JSON object.")));
            return;
        }
        const QJsonObject params = paramsValue.toObject();
        dispatch(method, params, id, hasId);
    }

    void dispatch(const QString &method, const QJsonObject &params,
                  const QJsonValue &id, bool hasId) {
        if (method == QStringLiteral("initialize")) {
            const QString requestedRoot = params.value(QStringLiteral("agent_root")).toString().trimmed();
            if (!requestedRoot.isEmpty())
                m_agentRoot = QDir(requestedRoot).absolutePath();
            if (hasId) {
                reply(id, QJsonObject{
                    {QStringLiteral("protocolVersion"), QStringLiteral("2.0")},
                    {QStringLiteral("serverInfo"), QJsonObject{
                        {QStringLiteral("name"), QStringLiteral("dft-agent-studio")},
                        {QStringLiteral("version"), QStringLiteral(DFT_AGENT_STUDIO_VERSION)}}},
                    {QStringLiteral("capabilities"), QJsonObject{
                        {QStringLiteral("turns"), true}, {QStringLiteral("live_steering"), true},
                        {QStringLiteral("tool_approval"), true}, {QStringLiteral("management"), true}}},
                });
            }
            return;
        }
        if (managementMethod(method)) {
            QVariantMap result = StudioNativeApi::dispatch(method, params.toVariantMap(), m_agentRoot);
            if (hasId)
                reply(id, QJsonObject::fromVariantMap(result));
            return;
        }
        if (method == QStringLiteral("turn/start")) {
            startTurn(params, id, hasId);
            return;
        }
        if (method == QStringLiteral("turn/steer") || method == QStringLiteral("turn/pause")
            || method == QStringLiteral("turn/resume") || method == QStringLiteral("turn/cancel")
            || method == QStringLiteral("turn/approve") || method == QStringLiteral("turn/deny")) {
            controlTurn(method, params, id, hasId);
            return;
        }
        if (hasId)
            write(rpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method)));
    }

    void startTurn(const QJsonObject &params, const QJsonValue &id, bool hasId) {
        if (m_turnsById.size() >= MaximumActiveTurns) {
            if (hasId)
                write(rpcError(id, -32010, QStringLiteral("Maximum active turn count (16) reached.")));
            return;
        }
        const QJsonObject model = params.value(QStringLiteral("model")).toObject();
        if (model.contains(QStringLiteral("api_key"))) {
            if (hasId)
                write(rpcError(id, -32602,
                    QStringLiteral("Do not send model.api_key; send model.api_key_file for native secret loading.")));
            return;
        }
        StudioCliTurnService::Request turnRequest;
        turnRequest.project = params.value(QStringLiteral("project")).toObject().toVariantMap();
        turnRequest.agentRoot = params.value(QStringLiteral("agent_root")).toString(m_agentRoot);
        if (turnRequest.agentRoot.trimmed().isEmpty())
            turnRequest.agentRoot = m_agentRoot;
        turnRequest.sessionId = params.value(QStringLiteral("session_id")).toString();
        turnRequest.createNewSession = params.value(QStringLiteral("new_session")).toBool();
        turnRequest.sessionName = params.value(QStringLiteral("session_name")).toString();
        turnRequest.goal = params.value(QStringLiteral("goal")).toString();
        turnRequest.permissionMode = params.value(QStringLiteral("permission_mode")).toString(QStringLiteral("approval"));
        turnRequest.multiAgent = params.value(QStringLiteral("multi_agent")).toBool();
        for (const QJsonValue &value : params.value(QStringLiteral("disabled_tools")).toArray()) {
            if (value.isString() && value.toString().size() <= 128)
                turnRequest.disabledTools.append(value.toString());
        }
        turnRequest.baseUrl = model.value(QStringLiteral("base_url")).toString();
        turnRequest.apiKeyFile = model.value(QStringLiteral("api_key_file")).toString();
        turnRequest.model = model.value(QStringLiteral("id")).toString();
        turnRequest.reasoningEffort = model.value(QStringLiteral("reasoning_effort")).toString(QStringLiteral("medium"));
        turnRequest.samplingOptions = model.value(QStringLiteral("sampling")).toObject();
        turnRequest.contextWindow = model.value(QStringLiteral("context_window")).toInt(65'536);
        turnRequest.effectiveContextPercent = model.value(QStringLiteral("effective_context_percent")).toInt(92);
        turnRequest.maximumOutputTokens = model.value(QStringLiteral("maximum_output_tokens")).toInt(4'096);
        turnRequest.maximumToolRounds = model.value(QStringLiteral("maximum_tool_rounds")).toInt(500);
        turnRequest.timeoutMs = model.value(QStringLiteral("timeout_ms")).toInt(1'800'000);
        turnRequest.reconnectMaxAttempts = model.value(QStringLiteral("reconnect_max_attempts")).toInt(10);
        turnRequest.reconnectDelayMs = model.value(QStringLiteral("reconnect_delay_ms")).toInt(1'000);
        if (turnRequest.goal.size() > 100'000 || turnRequest.sessionId.size() > 128
            || turnRequest.sessionName.size() > 120 || turnRequest.baseUrl.size() > 2'048
            || turnRequest.apiKeyFile.size() > 2'048 || turnRequest.model.size() > 256
            || turnRequest.samplingOptions.size() > 32) {
            if (hasId)
                write(rpcError(id, -32602, QStringLiteral("Turn request exceeds a field size limit.")));
            return;
        }
        if (turnRequest.agentRoot.trimmed().isEmpty())
            turnRequest.agentRoot = m_agentRoot;
        auto *service = new StudioCliTurnService(this);
        const QVariantMap started = service->start(turnRequest);
        if (!started.value(QStringLiteral("ok")).toBool()) {
            service->deleteLater();
            if (hasId)
                write(rpcError(id, -32010, statusMessage(started)));
            return;
        }
        const QString sessionId = started.value(QStringLiteral("session_id")).toString();
        const QString turnId = started.value(QStringLiteral("turn_id")).toString();
        if (turnId.isEmpty() || sessionId.isEmpty()) {
            service->cancel();
            service->deleteLater();
            if (hasId)
                write(rpcError(id, -32010, QStringLiteral("Native turn did not return valid identifiers.")));
            return;
        }
        m_turnsById.insert(turnId, service);
        m_turnsBySession.insert(sessionId, service);
        connect(service, &StudioCliTurnService::activity, this,
            [this](const QString &sid, const QVariantMap &event) {
                notify(QStringLiteral("turn/activity"), QJsonObject{
                    {QStringLiteral("session_id"), sid},
                    {QStringLiteral("event"), QJsonObject::fromVariantMap(event)}});
            });
        connect(service, &StudioCliTurnService::completed, this,
            [this, service](const QString &sid, const QVariantMap &result) {
                notify(QStringLiteral("turn/final"), QJsonObject{
                    {QStringLiteral("session_id"), sid},
                    {QStringLiteral("result"), QJsonObject::fromVariantMap(result)}});
                removeTurn(service);
            });
        connect(service, &StudioCliTurnService::failed, this,
            [this, service](const QString &sid, const QVariantMap &result) {
                notify(QStringLiteral("turn/final"), QJsonObject{
                    {QStringLiteral("session_id"), sid},
                    {QStringLiteral("result"), QJsonObject::fromVariantMap(result)}});
                removeTurn(service);
            });
        if (hasId)
            reply(id, QJsonObject::fromVariantMap(started));
        else
            notify(QStringLiteral("turn/started"), QJsonObject::fromVariantMap(started));
    }

    void controlTurn(const QString &method, const QJsonObject &params,
                     const QJsonValue &id, bool hasId) {
        QString key = params.value(QStringLiteral("turn_id")).toString().trimmed();
        QPointer<StudioCliTurnService> service = key.isEmpty()
            ? m_turnsBySession.value(params.value(QStringLiteral("session_id")).toString())
            : m_turnsById.value(key);
        if (!service) {
            if (hasId)
                write(rpcError(id, -32011, QStringLiteral("No active turn matches the supplied identity.")));
            return;
        }
        QVariantMap result{{QStringLiteral("ok"), true}};
        if (method == QStringLiteral("turn/steer")) {
            const QString message = params.value(QStringLiteral("message")).toString();
            if (message.isEmpty() || message.size() > 100'000) {
                if (hasId)
                    write(rpcError(id, -32602, QStringLiteral("Steering message must contain 1 to 100000 characters.")));
                return;
            }
            const bool accepted = service->steer(message);
            result.insert(QStringLiteral("accepted"), accepted);
            if (!accepted)
                result.insert(QStringLiteral("message"), QStringLiteral("Turn is not currently accepting steering."));
        } else if (method == QStringLiteral("turn/pause")) {
            service->pause();
        } else if (method == QStringLiteral("turn/resume")) {
            service->resume();
        } else if (method == QStringLiteral("turn/cancel")) {
            service->cancel();
        } else {
            const QString requestId = params.value(QStringLiteral("request_id")).toString().trimmed();
            if (requestId.isEmpty() || requestId.size() > 128) {
                if (hasId)
                    write(rpcError(id, -32602, QStringLiteral("request_id must contain 1 to 128 characters.")));
                return;
            }
            result = service->decideTool(requestId, method == QStringLiteral("turn/approve"));
        }
        if (hasId)
            reply(id, QJsonObject::fromVariantMap(result));
    }

    void removeTurn(StudioCliTurnService *service) {
        for (auto it = m_turnsById.begin(); it != m_turnsById.end();) {
            if (it.value() == service)
                it = m_turnsById.erase(it);
            else
                ++it;
        }
        for (auto it = m_turnsBySession.begin(); it != m_turnsBySession.end();) {
            if (it.value() == service)
                it = m_turnsBySession.erase(it);
            else
                ++it;
        }
        service->deleteLater();
    }
};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dft-agent-studio-server"));
    qInstallMessageHandler(stderrMessage);
    QString agentRoot = studioFindAgentRoot();
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--agent-root"))
            agentRoot = QDir(QString::fromLocal8Bit(argv[i + 1])).absolutePath();
    }
    QString dataError;
    if (!initializeStudioUserDataRoot(agentRoot, &dataError)) {
        std::fprintf(stderr, "%s\n", dataError.toUtf8().constData());
        return 2;
    }
    StudioServer server(agentRoot, &app);
    auto *notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app,
        [&server, notifier](QSocketDescriptor, QSocketNotifier::Type) {
            char buffer[16 * 1024];
            const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count <= 0) {
                notifier->setEnabled(false);
                QCoreApplication::quit();
                return;
            }
            server.consume(QByteArray(buffer, static_cast<qsizetype>(count)));
        });
    return app.exec();
}
