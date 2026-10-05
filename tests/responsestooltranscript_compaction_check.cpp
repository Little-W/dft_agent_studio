#include "responsestooltranscript.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QJsonArray toolRound(const QString &id, const QString &output) {
    return {QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                         {QStringLiteral("call_id"), id},
                         {QStringLiteral("name"), QStringLiteral("read_file")},
                         {QStringLiteral("arguments"), QStringLiteral("{}")} },
            QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                        {QStringLiteral("call_id"), id},
                        {QStringLiteral("output"), output}}};
}

QJsonArray skillRound(const QString &id, const QString &body) {
    return {QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                         {QStringLiteral("call_id"), id},
                         {QStringLiteral("name"), QStringLiteral("skills_read")},
                         {QStringLiteral("arguments"), QStringLiteral("{\"skill_id\":\"dft-drc-fix\"}")} },
            QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                        {QStringLiteral("call_id"), id},
                        {QStringLiteral("output"), body}}};
}

QJsonArray userRound(const QString &message) {
    return {QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), message}}};
}
}

int main() {
    check(ResponsesToolTranscript::tokenEstimate(QStringLiteral("中文测试")) >= 6,
          "CJK receives the Python-compatible conservative floor");
    check(ResponsesToolTranscript::tokenEstimate(QString(400, QLatin1Char('x'))) >= 150,
          "ASCII token estimate no longer undercounts the memory heuristic");

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary transcript root exists");
    ResponsesToolTranscript transcript(QJsonArray{QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QStringLiteral("Preserve this goal.")},
    }}, temporary.path());
    QString error;
    for (int i = 0; i < 5; ++i)
        check(transcript.appendRound(toolRound(QString::number(i), QString(700, QLatin1Char('r'))), &error),
              "durable complete tool round is appended");
    const QJsonArray before = transcript.checkpoint(4);
    QJsonArray built;
    check(!transcript.build(20, &built, &error, QStringLiteral("active repair state")),
          "fixed prompt overflow is rejected before compaction");
    check(transcript.checkpoint(4) == before,
          "failed preflight does not archive or discard existing rounds");

    ResponsesToolTranscript compacting({}, temporary.path());
    check(compacting.appendRound(userRound(QString(1600, QLatin1Char('a'))), &error),
          "old user message round is persisted");
    check(compacting.appendRound(userRound(QStringLiteral("Most recent user guidance")), &error),
          "recent user guidance round is persisted");
    check(compacting.appendRound(toolRound(QStringLiteral("later"), QString(800, QLatin1Char('z'))), &error),
          "tool evidence round is persisted");
    check(compacting.appendRound(skillRound(QStringLiteral("skill"),
              QStringLiteral("Loaded skill body marker: use evidence and compare plausible causes.")), &error),
          "dynamic skill output is persisted as a complete tool pair");
    check(!compacting.build(100, &built, &error), "tiny context forces the prior rounds into durable archive");
    check(compacting.installCompactionSummary(QStringLiteral("The agent inspected two artifacts; one issue remains."),
                                               256, &error),
          "caller-supplied model summary creates a durable compaction checkpoint");
    check(compacting.build(2500, &built, &error), "checkpoint and archived evidence index fit restored context");
    const QString builtText = QString::fromUtf8(QJsonDocument(built).toJson(QJsonDocument::Compact));
    check(builtText.contains(QStringLiteral("Most recent user guidance")),
          qPrintable(QStringLiteral("newest archived user guidance is retained: %1").arg(builtText.left(1500))));
    check(builtText.contains(QStringLiteral("Context summary")), "summary is visible to the provider");
    check(!builtText.contains(QString(1600, QLatin1Char('a'))),
          "old oversized user history is not duplicated into the provider request");
    check(builtText.contains(QStringLiteral("compaction_checkpoint")),
          "archive handoff identifies the durable compaction checkpoint");
    check(builtText.contains(QStringLiteral("Loaded skill body marker")),
          "archiving a dynamic skill read keeps its body available to the model");
    check(builtText.contains(QStringLiteral("read_file:")),
          "compacted context retains a concise observation of archived tool evidence");
    const QString checkpointPath = [&built]() {
        for (const QJsonValue &item : built) {
            const QString content = item.toObject().value(QStringLiteral("content")).toString();
            const int start = content.indexOf(QStringLiteral("<runtime_archive_index data_only=\"true\">\n"));
            if (start < 0)
                continue;
            const int jsonStart = content.indexOf(QLatin1Char('{'), start);
            const int jsonEnd = content.indexOf(QStringLiteral("\n</runtime_archive_index>"), jsonStart);
            const QJsonObject index = QJsonDocument::fromJson(content.mid(jsonStart, jsonEnd - jsonStart).toUtf8()).object();
            return index.value(QStringLiteral("compaction_checkpoint")).toString();
        }
        return QString();
    }();
    QFile checkpoint(checkpointPath);
    check(!checkpointPath.isEmpty() && checkpoint.open(QIODevice::ReadOnly),
          "checkpoint file survives process boundaries");
    const QJsonObject checkpointObject = QJsonDocument::fromJson(checkpoint.readAll()).object();
    check(checkpointObject.value(QStringLiteral("summary")).toString().contains(QStringLiteral("one issue remains")),
          "checkpoint stores the supplied summary for future restore");
    check(checkpointObject.value(QStringLiteral("retained_history_messages")).isArray(),
          "durable checkpoint records the role-aware retained history field");

    std::cout << "Responses transcript compaction checks passed\n";
    return 0;
}
