#include "studiocliturnservice.h"
#include "studiopaths.h"
#include "studiouserdata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTextStream>

#include <unistd.h>

namespace {
void writeLine(QTextStream &stream, const QJsonObject &object) {
    stream << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}

QJsonObject objectAt(const QJsonObject &source, const QString &key) {
    return source.value(key).toObject();
}

QByteArray readRequestLine() {
    QByteArray line;
    char byte = 0;
    while (true) {
        const ssize_t count = ::read(STDIN_FILENO, &byte, 1);
        if (count <= 0)
            return {};
        if (byte == '\n')
            return line;
        if (byte != '\r')
            line.append(byte);
        if (line.size() > 4 * 1024 * 1024)
            return {};
    }
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dft-agent-studio-agent"));
    QJsonParseError parseError{};
    const QByteArray bytes = readRequestLine();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    QTextStream output(stdout);
    if (!doc.isObject() || parseError.error != QJsonParseError::NoError) {
        writeLine(output, {{QStringLiteral("type"), QStringLiteral("error")},
                           {QStringLiteral("error"), QStringLiteral("stdin must contain one JSON request object.")}});
        return 2;
    }
    const QJsonObject input = doc.object();
    const QJsonObject model = objectAt(input, QStringLiteral("model"));
    StudioCliTurnService::Request request;
    request.agentRoot = input.value(QStringLiteral("agent_root")).toString().trimmed();
    if (request.agentRoot.isEmpty())
        request.agentRoot = studioFindAgentRoot();
    else
        request.agentRoot = QDir(request.agentRoot).absolutePath();
    QString dataError;
    if (!initializeStudioUserDataRoot(request.agentRoot, &dataError)) {
        writeLine(output, {{QStringLiteral("type"), QStringLiteral("error")},
                           {QStringLiteral("error"), dataError}});
        return 2;
    }
    request.project = objectAt(input, QStringLiteral("project")).toVariantMap();
    request.sessionId = input.value(QStringLiteral("session_id")).toString();
    request.createNewSession = input.value(QStringLiteral("new_session")).toBool();
    request.sessionName = input.value(QStringLiteral("session_name")).toString();
    request.goal = input.value(QStringLiteral("goal")).toString();
    request.permissionMode = input.value(QStringLiteral("permission_mode")).toString(QStringLiteral("workspace"));
    request.multiAgent = input.value(QStringLiteral("multi_agent")).toBool();
    request.baseUrl = model.value(QStringLiteral("base_url")).toString();
    request.apiKey = model.value(QStringLiteral("api_key")).toString();
    request.apiKeyFile = model.value(QStringLiteral("api_key_file")).toString();
    request.model = model.value(QStringLiteral("id")).toString();
    request.reasoningEffort = model.value(QStringLiteral("reasoning_effort")).toString(QStringLiteral("medium"));
    request.samplingOptions = model.value(QStringLiteral("sampling")).toObject();
    request.contextWindow = model.value(QStringLiteral("context_window")).toInt(65'536);
    request.effectiveContextPercent = model.value(QStringLiteral("effective_context_percent")).toInt(92);
    request.maximumOutputTokens = model.value(QStringLiteral("maximum_output_tokens")).toInt(4'096);
    const QJsonValue requestedToolRounds = model.contains(QStringLiteral("maximum_tool_rounds"))
        ? model.value(QStringLiteral("maximum_tool_rounds"))
        : input.value(QStringLiteral("maximum_tool_rounds"));
    request.maximumToolRounds = requestedToolRounds.toInt(500);
    request.timeoutMs = model.value(QStringLiteral("timeout_ms")).toInt(1'800'000);
    request.reconnectMaxAttempts = model.value(QStringLiteral("reconnect_max_attempts")).toInt(10);
    request.reconnectDelayMs = model.value(QStringLiteral("reconnect_delay_ms")).toInt(1'000);

    StudioCliTurnService turn;
    QObject::connect(&turn, &StudioCliTurnService::activity, &app,
        [&output](const QString &sessionId, const QVariantMap &event) {
            writeLine(output, {{QStringLiteral("type"), QStringLiteral("activity")},
                {QStringLiteral("session_id"), sessionId}, {QStringLiteral("event"), QJsonObject::fromVariantMap(event)}});
        });
    QObject::connect(&turn, &StudioCliTurnService::completed, &app,
        [&app, &output](const QString &sessionId, const QVariantMap &result) {
            writeLine(output, {{QStringLiteral("type"), QStringLiteral("completed")},
                {QStringLiteral("session_id"), sessionId}, {QStringLiteral("result"), QJsonObject::fromVariantMap(result)}});
            app.exit(0);
        });
    QObject::connect(&turn, &StudioCliTurnService::failed, &app,
        [&app, &output](const QString &sessionId, const QVariantMap &error) {
            writeLine(output, {{QStringLiteral("type"), QStringLiteral("failed")},
                {QStringLiteral("session_id"), sessionId}, {QStringLiteral("error"), QJsonObject::fromVariantMap(error)}});
            app.exit(3);
        });
    const QVariantMap started = turn.start(request);
    if (!started.value(QStringLiteral("ok")).toBool()) {
        writeLine(output, {{QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("error"), started.value(QStringLiteral("message")).toString()}});
        return 2;
    }
    writeLine(output, {{QStringLiteral("type"), QStringLiteral("started")},
        {QStringLiteral("session_id"), started.value(QStringLiteral("session_id")).toString()},
        {QStringLiteral("turn_id"), started.value(QStringLiteral("turn_id")).toString()}});

    auto *stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QByteArray controlBuffer;
    QObject::connect(stdinNotifier, &QSocketNotifier::activated, &app,
        [&turn, &output, stdinNotifier, &controlBuffer](QSocketDescriptor, QSocketNotifier::Type) {
            char buffer[4096];
            const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count <= 0) {
                stdinNotifier->setEnabled(false);
                return;
            }
            controlBuffer.append(buffer, static_cast<qsizetype>(count));
            while (true) {
                const qsizetype newline = controlBuffer.indexOf('\n');
                if (newline < 0)
                    break;
                const QByteArray line = controlBuffer.left(newline).trimmed();
                controlBuffer.remove(0, newline + 1);
                if (line.isEmpty())
                    continue;
                const QJsonDocument commandDoc = QJsonDocument::fromJson(line);
                const QJsonObject command = commandDoc.object();
                const QString type = command.value(QStringLiteral("type")).toString();
                if (type.isEmpty())
                    continue;
                if (type == QStringLiteral("cancel")) {
                    writeLine(output, {{QStringLiteral("type"), QStringLiteral("control_result")},
                        {QStringLiteral("command"), type}, {QStringLiteral("ok"), true},
                        {QStringLiteral("message"), QStringLiteral("cancel requested")}});
                    QMetaObject::invokeMethod(&turn, &StudioCliTurnService::cancel, Qt::QueuedConnection);
                    continue;
                }
                bool ok = true;
                QString message;
                QVariantMap decision;
                if (type == QStringLiteral("steer")) {
                    ok = turn.steer(command.value(QStringLiteral("message")).toString());
                    message = ok ? QStringLiteral("steering accepted") : QStringLiteral("no running turn to steer");
                } else if (type == QStringLiteral("approve") || type == QStringLiteral("deny")) {
                    decision = turn.decideTool(command.value(QStringLiteral("request_id")).toString(),
                                               type == QStringLiteral("approve"));
                    ok = decision.value(QStringLiteral("ok")).toBool();
                    message = decision.value(QStringLiteral("message")).toString();
                } else if (type == QStringLiteral("pause")) {
                    turn.pause();
                    message = QStringLiteral("pause requested");
                } else if (type == QStringLiteral("resume")) {
                    turn.resume();
                    message = QStringLiteral("resume requested");
                } else {
                    ok = false;
                    message = QStringLiteral("unsupported control command");
                }
                QJsonObject result{{QStringLiteral("type"), QStringLiteral("control_result")},
                    {QStringLiteral("command"), type}, {QStringLiteral("ok"), ok},
                    {QStringLiteral("message"), message}};
                if (!decision.isEmpty())
                    result.insert(QStringLiteral("result"), QJsonObject::fromVariantMap(decision));
                writeLine(output, result);
            }
        });
    return app.exec();
}
