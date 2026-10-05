#include "trainingshardingservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject message(const QString &role, const QString &content) {
    return {{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

class FakeLlamaServer final : public QObject {
public:
    FakeLlamaServer() {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_buffers.insert(socket, {});
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray &buffer = m_buffers[socket];
                    buffer.append(socket->readAll());
                    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const qsizetype lengthStart = buffer.indexOf("Content-Length:");
                    if (lengthStart < 0)
                        return;
                    const qsizetype valueStart = lengthStart + qsizetype(sizeof("Content-Length:") - 1);
                    const qsizetype valueEnd = buffer.indexOf("\r\n", valueStart);
                    const int contentLength = buffer.mid(valueStart, valueEnd - valueStart).trimmed().toInt();
                    if (buffer.size() < headerEnd + 4 + contentLength)
                        return;
                    const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
                    const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
                    QJsonParseError parseError{};
                    const QJsonObject request = QJsonDocument::fromJson(body, &parseError).object();
                    QJsonObject response;
                    if (requestLine.contains("/apply-template")) {
                        m_sawGenerationPrompt = m_sawGenerationPrompt
                            || request.value(QStringLiteral("add_generation_prompt")).toBool();
                        m_templateRequests.append(request);
                        QStringList pieces;
                        for (const QJsonValue &value : request.value(QStringLiteral("messages")).toArray()) {
                            const QJsonObject row = value.toObject();
                            pieces.append(row.value(QStringLiteral("role")).toString());
                            pieces.append(row.value(QStringLiteral("content")).toString());
                        }
                        response.insert(QStringLiteral("prompt"), pieces.join(QLatin1Char(' ')));
                    } else if (requestLine.contains("/tokenize")) {
                        const QString content = request.value(QStringLiteral("content")).toString();
                        const int count = qMax(1, content.toUtf8().size() / 12 + 1);
                        QJsonArray tokens;
                        for (int index = 0; index < count; ++index)
                            tokens.append(index);
                        response.insert(QStringLiteral("tokens"), tokens);
                    } else {
                        response.insert(QStringLiteral("error"), QStringLiteral("unknown route"));
                    }
                    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
                    socket->write(QByteArray::number(payload.size()));
                    socket->write("\r\n\r\n");
                    socket->write(payload);
                    socket->disconnectFromHost();
                    m_buffers.remove(socket);
                });
            }
        });
        require(m_server.listen(QHostAddress::LocalHost), "fake tokenizer server listens");
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    bool sawGenerationPrompt() const { return m_sawGenerationPrompt; }
    const QList<QJsonObject> &templateRequests() const { return m_templateRequests; }

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<QJsonObject> m_templateRequests;
    bool m_sawGenerationPrompt = false;
};

QJsonObject validToolTrajectory() {
    QJsonArray messages{
        message(QStringLiteral("system"), QStringLiteral("system")),
        message(QStringLiteral("user"), QStringLiteral("start")),
        QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), QStringLiteral("first decision")},
                    {QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("call-1")},
                        {QStringLiteral("type"), QStringLiteral("function")}}}}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                    {QStringLiteral("tool_call_id"), QStringLiteral("call-1")},
                    {QStringLiteral("content"), QStringLiteral("first result")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), QStringLiteral("second decision")},
                    {QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("call-2")},
                        {QStringLiteral("type"), QStringLiteral("function")}}}}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                    {QStringLiteral("tool_call_id"), QStringLiteral("call-2")},
                    {QStringLiteral("content"), QStringLiteral("second result")}},
        message(QStringLiteral("assistant"), QStringLiteral("final answer")),
    };
    return {{QStringLiteral("messages"), messages},
            {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("case"), QStringLiteral("tool-case")},
                {QStringLiteral("training_policy"), QStringLiteral("role_reinforcement_v1")}}}};
}

QByteArray jsonl(const QJsonObject &row) {
    return QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory available");
    FakeLlamaServer server;
    const QString dataset = temporary.filePath(QStringLiteral("input.jsonl"));
    require(write(dataset, jsonl(validToolTrajectory())), "tool trajectory fixture written");
    const QVariantMap prepared = TrainingShardingService::prepare(dataset,
        temporary.filePath(QStringLiteral("stages")), server.url(), {60, 120}, true);
    require(prepared.value(QStringLiteral("ok")).toBool(), "native length-stage preparation succeeds");
    const QVariantMap result = prepared.value(QStringLiteral("result")).toMap();
    const QJsonObject report = QJsonObject::fromVariantMap(result.value(QStringLiteral("report")).toMap());
    require(report.value(QStringLiteral("anomaly_count")).toInt() == 0,
            "complete tool trajectory has no preparation anomalies");
    const QJsonObject integrity = report.value(QStringLiteral("integrity")).toObject();
    require(integrity.value(QStringLiteral("assistant_answers_preserved")).toBool()
                && integrity.value(QStringLiteral("tool_pairs_complete")).toBool()
                && integrity.value(QStringLiteral("no_assistant_truncation")).toBool(),
            "assistant targets and tool call/result pairs are preserved");
    const QJsonObject outputFiles = report.value(QStringLiteral("output_files")).toObject();
    require(!outputFiles.isEmpty(), "stage JSONL outputs are created");
    QByteArray renderedCorpora;
    for (const QString &stage : outputFiles.keys()) {
        const QJsonObject stageOutput = outputFiles.value(stage).toObject();
        const QString outputPath = stageOutput.value(QStringLiteral("path")).toString();
        QFile output(outputPath);
        require(output.open(QIODevice::ReadOnly), "stage output is readable");
        while (!output.atEnd()) {
            const QJsonObject row = QJsonDocument::fromJson(output.readLine()).object();
            const QJsonObject control = row.value(QStringLiteral("training_control")).toObject();
            require(!control.value(QStringLiteral("supervised_assistant_turns")).toArray().isEmpty(),
                    "each written stage row has explicit supervised assistant turns");
            const QJsonArray messages = row.value(QStringLiteral("messages")).toArray();
            for (qsizetype index = 0; index < messages.size(); ++index) {
                const QJsonObject message = messages.at(index).toObject();
                if (message.value(QStringLiteral("role")).toString() != QLatin1String("assistant")
                    || !message.contains(QStringLiteral("tool_calls")))
                    continue;
                const QString id = message.value(QStringLiteral("tool_calls")).toArray().first().toObject()
                    .value(QStringLiteral("id")).toString();
                require(index + 1 < messages.size()
                            && messages.at(index + 1).toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")
                            && messages.at(index + 1).toObject().value(QStringLiteral("tool_call_id")).toString() == id,
                        "tool action and its result remain adjacent in a segment");
            }
        }
        const QJsonObject corpusMeta = stageOutput.value(QStringLiteral("full_sequence_corpus")).toObject();
        const QString corpusPath = corpusMeta.value(QStringLiteral("path")).toString();
        QFile corpus(corpusPath);
        require(corpus.open(QIODevice::ReadOnly), "rendered native training corpus is readable");
        const QByteArray corpusText = corpus.readAll();
        require(corpusMeta.value(QStringLiteral("objective")).toString()
                    == QStringLiteral("full_sequence_next_token"),
                "rendered corpus declares its full-sequence objective");
        require(!corpusMeta.value(QStringLiteral("assistant_only_loss_mask")).toBool(),
                "rendered corpus marks assistant-only loss masking as unsupported");
        renderedCorpora += corpusText;
    }
    require(renderedCorpora.contains("first decision") && renderedCorpora.contains("first result"),
            "rendered corpora retain assistant and tool content across split stages");
    QString observedTrainingPrompts;
    for (const QJsonObject &request : server.templateRequests()) {
        for (const QJsonValue &value : request.value(QStringLiteral("messages")).toArray())
            observedTrainingPrompts += value.toObject().value(QStringLiteral("content")).toString() + QLatin1Char('\n');
    }
    require(observedTrainingPrompts.contains(QStringLiteral("隔离工作副本中的 RTL 可编辑"))
                && observedTrainingPrompts.contains(QStringLiteral("只有新鲜证据确认 RTL 设计缺陷后才修复 RTL"))
                && !observedTrainingPrompts.contains(QStringLiteral("不得修改 HDL、原工程或工艺库")),
            "autonomous repair training allows evidence-backed RTL fixes in an isolated copy");
    const QJsonObject corpusObjective = report.value(QStringLiteral("native_text_corpus")).toObject();
    require(corpusObjective.value(QStringLiteral("objective")).toString()
                == QStringLiteral("full_sequence_next_token")
                && !corpusObjective.value(QStringLiteral("assistant_only_loss_mask")).toBool(),
            "training report distinguishes native full-sequence training from assistant-only SFT");

    const QString preflightReportPath = temporary.filePath(QStringLiteral("reports/preflight.json"));
    QJsonObject preflightRow = validToolTrajectory();
    QJsonArray preflightMessages = preflightRow.value(QStringLiteral("messages")).toArray();
    QJsonObject longUserMessage = preflightMessages.at(1).toObject();
    longUserMessage.insert(QStringLiteral("content"), QStringLiteral("start ") + QString(2'000, QLatin1Char('x')));
    preflightMessages[1] = longUserMessage;
    preflightRow.insert(QStringLiteral("messages"), preflightMessages);
    const QString preflightDataset = temporary.filePath(QStringLiteral("preflight.jsonl"));
    require(write(preflightDataset, jsonl(preflightRow)), "preflight dataset fixture written");
    const QVariantMap preflight = TrainingShardingService::preflight(
        preflightDataset, preflightReportPath, server.url(), 128);
    require(preflight.value(QStringLiteral("ok")).toBool(), "native training preflight completes");
    const QVariantMap preflightResult = preflight.value(QStringLiteral("result")).toMap();
    const QJsonObject preflightReport = QJsonObject::fromVariantMap(
        preflightResult.value(QStringLiteral("report")).toMap());
    require(preflightReport.value(QStringLiteral("rows")).toInt() == 1
                && preflightReport.value(QStringLiteral("assistant_decisions")).toInt() == 3,
            "preflight audits every assistant decision rather than only the final answer");
    require(preflightReport.value(QStringLiteral("status")).toString() == QStringLiteral("failed")
                && preflightReport.value(QStringLiteral("overlong_examples")).toInt() > 0
                && preflightResult.value(QStringLiteral("exit_code")).toInt() == 2,
            "preflight fails the requested maximum without truncating any decision");
    require(preflightReport.value(QStringLiteral("truncation_allowed")).toBool() == false
                && preflightReport.value(QStringLiteral("recommended_max_length")).toInt() >= 128
                && preflightReport.value(QStringLiteral("longest_examples")).toArray().size() == 3,
            "preflight reports safe power-of-two length and ranked decisions");
    require(QFileInfo::exists(preflightReportPath) && server.sawGenerationPrompt(),
            "preflight atomically writes its report and tokenizes prompts with generation markers");

    QJsonObject invalid = validToolTrajectory();
    QJsonArray invalidMessages = invalid.value(QStringLiteral("messages")).toArray();
    QJsonObject badResult = invalidMessages.at(3).toObject();
    badResult.insert(QStringLiteral("tool_call_id"), QStringLiteral("wrong-id"));
    invalidMessages[3] = badResult;
    invalid.insert(QStringLiteral("messages"), invalidMessages);
    require(write(dataset, jsonl(invalid)), "invalid trajectory fixture rewritten");
    const QVariantMap rejected = TrainingShardingService::prepare(dataset,
        temporary.filePath(QStringLiteral("invalid-out")), server.url(), {60, 120}, true);
    require(rejected.value(QStringLiteral("ok")).toBool()
                && rejected.value(QStringLiteral("result")).toMap().value(QStringLiteral("anomaly_count")).toInt() == 1,
            "mismatched tool call/result IDs are reported as anomalies");
    require(QDir(temporary.filePath(QStringLiteral("invalid-out"))).entryList({QStringLiteral("stage_*.jsonl")}).isEmpty(),
            "invalid source rows do not produce official stage files");

    const auto rejectsSchema = [&](const QJsonObject &row, const QString &name) {
        require(write(dataset, jsonl(row)), "schema fixture written");
        const QVariantMap response = TrainingShardingService::prepare(dataset,
            temporary.filePath(name), server.url(), {60, 120}, true);
        require(response.value(QStringLiteral("ok")).toBool()
                    && response.value(QStringLiteral("result")).toMap()
                           .value(QStringLiteral("anomaly_count")).toInt() == 1,
                "legacy SFT schema constraint is reported as an anomaly");
    };
    QJsonObject shortRow = validToolTrajectory();
    QJsonArray shortMessages = shortRow.value(QStringLiteral("messages")).toArray();
    while (shortMessages.size() > 2)
        shortMessages.removeLast();
    shortRow.insert(QStringLiteral("messages"), shortMessages);
    rejectsSchema(shortRow, QStringLiteral("short-row-out"));

    QJsonObject missingSystem = validToolTrajectory();
    QJsonArray noSystemMessages = missingSystem.value(QStringLiteral("messages")).toArray();
    QJsonObject firstMessage = noSystemMessages.first().toObject();
    firstMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    noSystemMessages[0] = firstMessage;
    missingSystem.insert(QStringLiteral("messages"), noSystemMessages);
    rejectsSchema(missingSystem, QStringLiteral("missing-system-out"));

    QJsonObject incompleteControl = validToolTrajectory();
    incompleteControl.insert(QStringLiteral("training_control"), QJsonObject{});
    rejectsSchema(incompleteControl, QStringLiteral("incomplete-control-out"));

    QJsonObject emptyTools = validToolTrajectory();
    emptyTools.insert(QStringLiteral("tools"), QJsonArray{});
    rejectsSchema(emptyTools, QStringLiteral("empty-tools-out"));

    QJsonObject uncontrolledToolEnding = validToolTrajectory();
    QJsonArray toolEndingMessages = uncontrolledToolEnding.value(QStringLiteral("messages")).toArray();
    toolEndingMessages.removeLast();
    uncontrolledToolEnding.insert(QStringLiteral("messages"), toolEndingMessages);
    rejectsSchema(uncontrolledToolEnding, QStringLiteral("uncontrolled-tool-ending-out"));
    return 0;
}
