#include "agentepisodeservice.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {
bool require(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "Check failed: %s\n", message);
    return condition;
}
}

int main() {
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary data root is valid"))
        return 1;
    const QString dataRoot = temporary.filePath(QStringLiteral("studio_data"));
    const QVariantMap episode{
        {QStringLiteral("episode_id"), QStringLiteral("episode-1")},
        {QStringLiteral("project_id"), QStringLiteral("sha256")},
        {QStringLiteral("goal"), QStringLiteral("Repair the DRC issue.")},
        {QStringLiteral("answer"), QStringLiteral("DRC evidence reviewed.")},
        {QStringLiteral("tool_trace"), QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("read_file")}}}},
        {QStringLiteral("errors"), QVariantList{}},
    };
    const QVariantMap saved = AgentEpisodeService::persist(episode, dataRoot);
    const QVariantMap approved = AgentEpisodeService::recordFeedback(
        QStringLiteral("episode-1"), QStringLiteral("approved"), QStringLiteral("Evidence matches."), {}, dataRoot);
    const QVariantMap corrected = AgentEpisodeService::recordFeedback(
        QStringLiteral("episode-1"), QStringLiteral("corrected"), QStringLiteral("Tighten the conclusion."),
        QStringLiteral("Correction with exact report evidence."), dataRoot);
    const QVariantMap missingCorrection = AgentEpisodeService::recordFeedback(
        QStringLiteral("episode-1"), QStringLiteral("corrected"), {}, {}, dataRoot);
    const QString policyPath = temporary.filePath(QStringLiteral("policy.md"));
    QFile policy(policyPath);
    const bool policyWritten = policy.open(QIODevice::WriteOnly | QIODevice::Text)
        && policy.write("Use evidence only.\n") > 0;
    policy.close();
    const QString outputPath = temporary.filePath(QStringLiteral("training/sft.jsonl"));
    const QVariantMap exported = AgentEpisodeService::exportSft(
        QDir(dataRoot).filePath(QStringLiteral("feedback.jsonl")), policyPath, outputPath);
    QFile output(outputPath);
    QJsonObject row;
    if (output.open(QIODevice::ReadOnly | QIODevice::Text))
        row = QJsonDocument::fromJson(output.readLine()).object();
    QFile log(QDir(dataRoot).filePath(QStringLiteral("feedback.jsonl")));
    const bool feedbackCount = log.open(QIODevice::ReadOnly | QIODevice::Text)
        && log.readAll().count('\n') == 2;

    const bool passed = require(saved.value(QStringLiteral("ok")).toBool(), "native runtime episode is persisted")
        && require(approved.value(QStringLiteral("ok")).toBool() && corrected.value(QStringLiteral("ok")).toBool(),
                   "approved and corrected reviews are recorded")
        && require(!missingCorrection.value(QStringLiteral("ok")).toBool(),
                   "corrected verdict requires corrected response text")
        && require(policyWritten && feedbackCount && exported.value(QStringLiteral("ok")).toBool(),
                   "latest reviewed episodes export into SFT JSONL")
        && require(exported.value(QStringLiteral("result")).toMap().value(QStringLiteral("accepted")).toInt() == 1
                       && exported.value(QStringLiteral("result")).toMap().value(QStringLiteral("superseded")).toInt() == 1,
                   "latest feedback supersedes prior review records")
        && require(row.value(QStringLiteral("messages")).toArray().size() == 3
                       && row.value(QStringLiteral("messages")).toArray().last().toObject()
                           .value(QStringLiteral("content")).toString() == QStringLiteral("Correction with exact report evidence."),
                   "SFT export uses the corrected answer and system policy");
    return passed ? 0 : 1;
}
