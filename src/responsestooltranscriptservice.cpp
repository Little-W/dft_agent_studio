#include "responsestooltranscriptservice.h"

#include "responsestooltranscript.h"
#include "studiopaths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <memory>

namespace {
struct Entry {
    std::shared_ptr<ResponsesToolTranscript> transcript;
    QDateTime lastAccess;
};

QHash<QString, Entry> &transcripts() {
    static QHash<QString, Entry> value;
    return value;
}

QMutex &transcriptMutex() {
    static QMutex value;
    return value;
}

QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QVariantList variantList(const QJsonArray &array) {
    QVariantList values;
    values.reserve(array.size());
    for (const QJsonValue &item : array)
        values.append(item.toVariant());
    return values;
}

QJsonArray jsonArray(const QVariantList &values) {
    QJsonArray array;
    for (const QVariant &item : values)
        array.append(QJsonValue::fromVariant(item));
    return array;
}

QString keyFor(const QVariantMap &arguments) {
    return arguments.value(QStringLiteral("session_key")).toString().trimmed();
}

void pruneExpired() {
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-3'600);
    for (auto it = transcripts().begin(); it != transcripts().end();) {
        if (it->lastAccess < cutoff)
            it = transcripts().erase(it);
        else
            ++it;
    }
    while (transcripts().size() > 64) {
        auto oldest = transcripts().begin();
        for (auto it = transcripts().begin(); it != transcripts().end(); ++it)
            if (it->lastAccess < oldest->lastAccess)
                oldest = it;
        transcripts().erase(oldest);
    }
}

bool safeArtifactRoot(const QString &requestedPath, const QString &agentRoot, QString *canonicalRoot) {
    const QString dataRoot = studioDataRoot(agentRoot);
    const QString threadRoot = QDir(dataRoot).filePath(QStringLiteral("agent_runtime/threads"));
    const QString normalized = QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath());
    const QString canonicalThreadRoot = QFileInfo(threadRoot).canonicalFilePath();
    if (canonicalThreadRoot.isEmpty() || normalized.isEmpty()
        || !normalized.startsWith(canonicalThreadRoot + QDir::separator()))
        return false;
    const QString parent = QFileInfo(normalized).absolutePath();
    const QString canonicalParent = QFileInfo(parent).canonicalFilePath();
    if (canonicalParent.isEmpty()
        || (canonicalParent != canonicalThreadRoot
            && !canonicalParent.startsWith(canonicalThreadRoot + QDir::separator())))
        return false;
    *canonicalRoot = normalized;
    return true;
}
}

bool ResponsesToolTranscriptService::supports(const QString &action) {
    return action == QStringLiteral("responses_context_compaction_plan")
        || action == QStringLiteral("responses_transcript_begin")
        || action == QStringLiteral("responses_transcript_append")
        || action == QStringLiteral("responses_transcript_build")
        || action == QStringLiteral("responses_transcript_bound_output")
        || action == QStringLiteral("responses_transcript_checkpoint")
        || action == QStringLiteral("responses_transcript_close");
}

QVariantMap ResponsesToolTranscriptService::dispatch(const QString &action, const QVariantMap &arguments,
                                                      const QString &agentRoot) {
    if (!supports(action))
        return failure(QStringLiteral("Unsupported Responses transcript action."));
    if (action == QStringLiteral("responses_context_compaction_plan")) {
        bool contextValid = false;
        bool percentValid = false;
        bool outputValid = false;
        bool inputValid = false;
        const int contextWindow = arguments.value(QStringLiteral("context_window")).toInt(&contextValid);
        const int percent = arguments.value(QStringLiteral("effective_context_percent")).toInt(&percentValid);
        const int outputTokens = arguments.value(QStringLiteral("maximum_output_tokens")).toInt(&outputValid);
        const int currentTokens = arguments.value(QStringLiteral("current_input_tokens")).toInt(&inputValid);
        const int fixedPromptTokens = ResponsesToolTranscript::tokenEstimate(
            QJsonValue::fromVariant(arguments.value(QStringLiteral("instructions"))))
            + ResponsesToolTranscript::tokenEstimate(jsonArray(arguments.value(QStringLiteral("tools")).toList()));
        ResponsesContextCompactionPolicy::Allocation allocation;
        QString error;
        const int configuredLimit = arguments.value(QStringLiteral("auto_compact_token_limit"), 0).toInt();
        const int safetyTokens = qBound(0, arguments.value(QStringLiteral("reserved_tokens"), 512).toInt(), 2'000'000);
        if (!contextValid || !percentValid || !outputValid || !inputValid || currentTokens < 0
            || !ResponsesContextCompactionPolicy::allocate(contextWindow, percent, configuredLimit,
                outputTokens, safetyTokens, fixedPromptTokens, &allocation, &error))
            return failure(error.isEmpty() ? QStringLiteral("Responses context compaction request is invalid.") : error);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("context_window"), allocation.contextWindow},
            {QStringLiteral("effective_context_window"), allocation.effectiveContextWindow},
            {QStringLiteral("fixed_prompt_tokens"), allocation.fixedPromptTokens},
            {QStringLiteral("maximum_output_tokens"), allocation.outputReserveTokens},
            {QStringLiteral("reserved_tokens"), allocation.safetyReserveTokens},
            {QStringLiteral("hard_input_budget"), allocation.hardInputBudget},
            {QStringLiteral("compact_at_tokens"), allocation.compactAtTokens},
            {QStringLiteral("should_compact"), ResponsesContextCompactionPolicy::shouldCompact(currentTokens, allocation)},
        }}};
    }
    const QString key = keyFor(arguments);
    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9_.:-]{1,200}$"));
    if (!validKey.match(key).hasMatch())
        return failure(QStringLiteral("Responses transcript session key is invalid."));

    if (action == QStringLiteral("responses_transcript_begin")) {
        QString artifactRoot;
        if (!safeArtifactRoot(arguments.value(QStringLiteral("artifact_root")).toString(), agentRoot,
                              &artifactRoot))
            return failure(QStringLiteral("Responses transcript artifact directory is outside the Studio thread store."));
        const QJsonArray initial = jsonArray(arguments.value(QStringLiteral("initial")).toList());
        const auto transcript = std::make_shared<ResponsesToolTranscript>(initial, artifactRoot);
        QMutexLocker locker(&transcriptMutex());
        pruneExpired();
        transcripts().insert(key, {transcript, QDateTime::currentDateTimeUtc()});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("session_key"), key}, {QStringLiteral("started"), true}}}};
    }

    std::shared_ptr<ResponsesToolTranscript> transcript;
    {
        QMutexLocker locker(&transcriptMutex());
        pruneExpired();
        auto it = transcripts().find(key);
        if (it == transcripts().end())
            return failure(QStringLiteral("Responses transcript session is not active."));
        if (action == QStringLiteral("responses_transcript_close")) {
            transcripts().erase(it);
            return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("closed"), true}}}};
        }
        it->lastAccess = QDateTime::currentDateTimeUtc();
        transcript = it->transcript;
    }
    if (action == QStringLiteral("responses_transcript_append")) {
        QString error;
        if (!transcript->appendRound(jsonArray(arguments.value(QStringLiteral("items")).toList()), &error))
            return failure(error);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("appended"), true}}}};
    }
    if (action == QStringLiteral("responses_transcript_build")) {
        bool budgetValid = false;
        int budget = arguments.value(QStringLiteral("input_budget")).toInt(&budgetValid);
        if (arguments.contains(QStringLiteral("context_window"))) {
            bool contextValid = false;
            bool percentValid = false;
            bool outputValid = false;
            const int contextWindow = arguments.value(QStringLiteral("context_window")).toInt(&contextValid);
            const int percent = arguments.value(QStringLiteral("effective_context_percent")).toInt(&percentValid);
            const int outputTokens = arguments.value(QStringLiteral("maximum_output_tokens")).toInt(&outputValid);
            if (!contextValid || !percentValid || !outputValid || contextWindow < 2'048
                || contextWindow > 2'000'000 || percent < 1 || percent > 100
                || outputTokens < 1 || outputTokens > contextWindow)
                return failure(QStringLiteral("Responses context allocation settings are invalid."));
            const int contextLimit = contextWindow * percent / 100;
            const int reserved = qBound(0, arguments.value(QStringLiteral("reserved_tokens"), 512).toInt(), contextWindow);
            const int promptTokens = ResponsesToolTranscript::tokenEstimate(
                QJsonValue::fromVariant(arguments.value(QStringLiteral("instructions"))))
                + ResponsesToolTranscript::tokenEstimate(jsonArray(arguments.value(QStringLiteral("tools")).toList()));
            budget = contextLimit - promptTokens - outputTokens - reserved;
            budgetValid = true;
        }
        QJsonArray items;
        QString error;
        if (!budgetValid || !transcript->build(budget, &items, &error,
                arguments.value(QStringLiteral("runtime_context")).toString()))
            return failure(error.isEmpty() ? QStringLiteral("Responses transcript input budget is invalid.") : error);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("items"), variantList(items)}, {QStringLiteral("input_budget"), budget}}}};
    }
    if (action == QStringLiteral("responses_transcript_checkpoint"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("items"), variantList(transcript->checkpoint())}}}};
    QString error;
    const QString output = transcript->boundOutput(
        arguments.value(QStringLiteral("call_id")).toString(),
        arguments.value(QStringLiteral("output")).toString(),
        arguments.value(QStringLiteral("maximum_characters"), 18'000).toInt(), &error);
    if (!error.isEmpty())
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("output"), output}}}};
}
