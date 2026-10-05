#include "responsesturnrunner.h"

#include <QMetaObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QUrl>

#include <exception>
#include <functional>

namespace {
QJsonObject findDftJobObject(const QJsonObject &root, const std::function<bool(const QJsonObject &)> &predicate,
                             int depth = 0) {
    if (predicate(root))
        return root;
    if (depth >= 8)
        return {};
    for (const QString &key : {QStringLiteral("result"), QStringLiteral("data"), QStringLiteral("payload")}) {
        const QJsonObject child = root.value(key).toObject();
        if (child.isEmpty())
            continue;
        const QJsonObject found = findDftJobObject(child, predicate, depth + 1);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

QJsonArray stringsFrom(const QJsonValue &value, int maximum = 8) {
    QJsonArray result;
    if (!value.isArray())
        return result;
    for (const QJsonValue &item : value.toArray()) {
        if (result.size() >= maximum)
            break;
        if (item.isString())
            result.append(item);
        else if (item.isObject()) {
            const QJsonObject object = item.toObject();
            const QString text = object.value(QStringLiteral("message")).toString(
                object.value(QStringLiteral("path")).toString());
            if (!text.isEmpty())
                result.append(text);
        }
    }
    return result;
}

QJsonObject compactDftJobResult(const QJsonObject &toolResult) {
    const QJsonObject job = findDftJobObject(toolResult, [](const QJsonObject &value) {
        return value.contains(QStringLiteral("job_id")) && value.contains(QStringLiteral("state"));
    });
    const QJsonObject payload = findDftJobObject(toolResult, [](const QJsonObject &value) {
        return value.contains(QStringLiteral("dft_analysis"))
            || (value.contains(QStringLiteral("verification")) && value.contains(QStringLiteral("execution")));
    });
    if (job.isEmpty() && payload.isEmpty())
        return toolResult;

    const QJsonObject execution = payload.value(QStringLiteral("execution")).toObject();
    const QJsonObject analysis = payload.value(QStringLiteral("dft_analysis")).toObject();
    const QJsonObject drc = analysis.value(QStringLiteral("drc")).toObject();
    const QJsonObject atpg = analysis.value(QStringLiteral("atpg")).toObject();
    const QJsonObject verification = payload.value(QStringLiteral("verification")).toObject();
    const QJsonObject confirmation = verification.value(QStringLiteral("confirmation")).toObject();
    QJsonObject compact{
        {QStringLiteral("job_id"), job.value(QStringLiteral("job_id"))},
        {QStringLiteral("operation"), job.value(QStringLiteral("operation"))},
        {QStringLiteral("job_state"), job.value(QStringLiteral("state"))},
        {QStringLiteral("status"), payload.value(QStringLiteral("status"))},
        {QStringLiteral("workspace"), execution.value(QStringLiteral("workspace"))},
        {QStringLiteral("flow_directory"), execution.value(QStringLiteral("flow_directory"))},
        {QStringLiteral("driver"), execution.value(QStringLiteral("driver"))},
        {QStringLiteral("execution_log"), execution.value(QStringLiteral("log"))},
        {QStringLiteral("execution_stdout"), execution.value(QStringLiteral("stdout"))},
        {QStringLiteral("first_attempt_stdout"), execution.value(QStringLiteral("first_attempt_stdout"))},
        {QStringLiteral("returncode"), execution.value(QStringLiteral("returncode"))},
        {QStringLiteral("attempt_count"), execution.value(QStringLiteral("attempt_count"))},
        {QStringLiteral("retry_reason"), execution.value(QStringLiteral("retry_reason"))},
        {QStringLiteral("first_attempt_log"), execution.value(QStringLiteral("first_attempt_log"))},
        {QStringLiteral("failure_category"), execution.value(QStringLiteral("failure_category"))},
        {QStringLiteral("next_action"), execution.value(QStringLiteral("next_action"))},
        {QStringLiteral("execution_errors"), stringsFrom(execution.value(QStringLiteral("errors")))},
        {QStringLiteral("drc"), QJsonObject{
            {QStringLiteral("passed"), drc.value(QStringLiteral("passed"))},
            {QStringLiteral("violations"), drc.value(QStringLiteral("observed_violations"))},
            {QStringLiteral("maximum_allowed"), drc.value(QStringLiteral("maximum_allowed"))},
            {QStringLiteral("report"), drc.value(QStringLiteral("report"))}}},
        {QStringLiteral("atpg"), QJsonObject{
            {QStringLiteral("coverage_percent"), atpg.value(QStringLiteral("coverage_percent"))},
            {QStringLiteral("target_percent"), atpg.value(QStringLiteral("target_percent"))},
            {QStringLiteral("patterns"), atpg.value(QStringLiteral("patterns"))},
            {QStringLiteral("total_faults"), atpg.value(QStringLiteral("total_faults"))},
            {QStringLiteral("tool"), atpg.value(QStringLiteral("tool"))},
            {QStringLiteral("blocking_errors"), stringsFrom(atpg.value(QStringLiteral("blocking_errors")))}}},
        {QStringLiteral("verification"), QJsonObject{
            {QStringLiteral("status"), verification.value(QStringLiteral("status"))},
            {QStringLiteral("verification_file"), verification.value(QStringLiteral("verification_file"))},
            {QStringLiteral("evidence_file"), verification.value(QStringLiteral("evidence_file"))},
            {QStringLiteral("rounds"), confirmation.value(QStringLiteral("rounds"))},
            {QStringLiteral("stable_snapshot"), confirmation.value(QStringLiteral("stable_snapshot"))}}},
        {QStringLiteral("evidence_file"), payload.value(QStringLiteral("evidence_file"))},
        {QStringLiteral("failed_checks"), stringsFrom(analysis.value(QStringLiteral("failed_checks")))}
    };
    if (compact.value(QStringLiteral("status")).toString().isEmpty())
        compact.insert(QStringLiteral("status"), verification.value(QStringLiteral("status")));
    if (compact.value(QStringLiteral("drc")).toObject().value(QStringLiteral("violations")).isUndefined())
        compact.remove(QStringLiteral("drc"));
    if (compact.value(QStringLiteral("atpg")).toObject().value(QStringLiteral("coverage_percent")).isUndefined())
        compact.remove(QStringLiteral("atpg"));
    return compact;
}

QString contentText(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    QStringList parts;
    for (const QJsonValue &partValue : value.toArray()) {
        const QJsonObject part = partValue.toObject();
        const QString type = part.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("input_text") || type == QStringLiteral("output_text")
            || type == QStringLiteral("text"))
            parts.append(part.value(QStringLiteral("text")).toString());
    }
    return parts.join(QString{});
}

QJsonArray chatMessages(const QString &instructions, const QJsonArray &input) {
    QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                   {QStringLiteral("content"), instructions}}};
    QJsonObject pendingAssistantTools;
    QJsonArray pendingCalls;
    auto flushCalls = [&] {
        if (pendingCalls.isEmpty())
            return;
        pendingAssistantTools.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        pendingAssistantTools.insert(QStringLiteral("content"), QJsonValue::Null);
        pendingAssistantTools.insert(QStringLiteral("tool_calls"), pendingCalls);
        messages.append(pendingAssistantTools);
        pendingAssistantTools = {};
        pendingCalls = {};
    };
    for (const QJsonValue &value : input) {
        const QJsonObject item = value.toObject();
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("function_call")) {
            const QString id = item.value(QStringLiteral("call_id")).toString(item.value(QStringLiteral("id")).toString());
            pendingCalls.append(QJsonObject{
                {QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{
                    {QStringLiteral("name"), item.value(QStringLiteral("name"))},
                    {QStringLiteral("arguments"), item.value(QStringLiteral("arguments"))},
                }},
            });
            continue;
        }
        flushCalls();
        if (type == QStringLiteral("function_call_output")) {
            messages.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("tool")},
                {QStringLiteral("tool_call_id"), item.value(QStringLiteral("call_id"))},
                {QStringLiteral("content"), item.value(QStringLiteral("output")).toString()},
            });
            continue;
        }
        if (type == QStringLiteral("message")) {
            QString role = item.value(QStringLiteral("role")).toString();
            if (role.isEmpty())
                role = QStringLiteral("user");
            const QString text = contentText(item.value(QStringLiteral("content")));
            if (role == QStringLiteral("assistant") && text.isEmpty())
                continue;
            messages.append(QJsonObject{{QStringLiteral("role"), role},
                                        {QStringLiteral("content"), text}});
            continue;
        }
        const QString role = item.value(QStringLiteral("role")).toString();
        if (!role.isEmpty())
            messages.append(QJsonObject{{QStringLiteral("role"), role},
                                        {QStringLiteral("content"), contentText(item.value(QStringLiteral("content")))}});
    }
    flushCalls();
    return messages;
}

QJsonArray chatTools(const QJsonArray &tools) {
    QJsonArray converted;
    for (const QJsonValue &value : tools) {
        QJsonObject tool = value.toObject();
        if (tool.value(QStringLiteral("function")).isObject()) {
            tool.insert(QStringLiteral("type"), QStringLiteral("function"));
            converted.append(tool);
            continue;
        }
        if (tool.value(QStringLiteral("type")).toString() != QStringLiteral("function"))
            continue;
        const QStringList fields{QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("parameters"), QStringLiteral("strict")};
        QJsonObject function;
        for (const QString &field : fields) {
            if (tool.contains(field))
                function.insert(field, tool.value(field));
        }
        converted.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"), function}});
    }
    return converted;
}

QJsonArray normalizeGatewayInput(const QJsonArray &input) {
    QJsonArray normalized;
    for (const QJsonValue &value : input) {
        if (!value.isObject()) {
            normalized.append(value);
            continue;
        }
        const QJsonObject item = value.toObject();
        const QString role = item.value(QStringLiteral("role")).toString();
        if (item.contains(QStringLiteral("type"))
            || (role != QStringLiteral("user") && role != QStringLiteral("developer")
                && role != QStringLiteral("assistant"))) {
            normalized.append(item);
            continue;
        }
        QString content;
        const QJsonValue rawContent = item.value(QStringLiteral("content"));
        if (rawContent.isString()) {
            content = rawContent.toString();
        } else if (rawContent.isArray()) {
            QStringList parts;
            for (const QJsonValue &part : rawContent.toArray()) {
                if (part.isString())
                    parts.append(part.toString());
                else if (part.isObject() && part.toObject().value(QStringLiteral("text")).isString())
                    parts.append(part.toObject().value(QStringLiteral("text")).toString());
            }
            content = parts.join(QLatin1Char('\n'));
        } else if (!rawContent.isUndefined() && !rawContent.isNull()) {
            content = QString::fromUtf8(QJsonDocument(QJsonArray{rawContent}).toJson(QJsonDocument::Compact));
            if (content.size() >= 2)
                content = content.mid(1, content.size() - 2);
        }
        normalized.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("message")},
            {QStringLiteral("role"), role == QStringLiteral("developer") ? QStringLiteral("system") : role},
            {QStringLiteral("content"), content},
        });
    }
    return normalized;
}

bool usesGatewayMessageShape(const QString &baseUrl) {
    QString path = QUrl(baseUrl.trimmed()).path().toLower();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path.endsWith(QStringLiteral("/api/v1"));
}
}

ResponsesTurnRunner::ResponsesTurnRunner(QObject *parent)
    : QObject(parent) {
    connect(&m_client, &ResponsesRoundClient::eventReceived,
            this, &ResponsesTurnRunner::modelEvent);
    connect(&m_client, &ResponsesRoundClient::completed,
            this, &ResponsesTurnRunner::handleRound);
    connect(&m_client, &ResponsesRoundClient::retrying, this,
        [this](int attempt, int maximum, int status, const QString &reason) {
            emit modelEvent({
                {QStringLiteral("type"), QStringLiteral("provider_reconnecting")},
                {QStringLiteral("attempt"), attempt},
                {QStringLiteral("max_attempts"), maximum},
                {QStringLiteral("status_code"), status},
                {QStringLiteral("reason"), reason},
            });
        });
    connect(&m_client, &ResponsesRoundClient::streamReset, this, [this] {
        emit modelEvent({{QStringLiteral("type"), QStringLiteral("provider_stream_reset")}});
    });
    connect(&m_client, &ResponsesRoundClient::reconnected, this, [this](int attempts) {
        emit modelEvent({
            {QStringLiteral("type"), QStringLiteral("provider_reconnected")},
            {QStringLiteral("attempts"), attempts},
        });
    });
}

bool ResponsesTurnRunner::running() const {
    return m_running;
}

bool ResponsesTurnRunner::paused() const {
    return m_paused;
}

void ResponsesTurnRunner::start(const Request &request, ToolExecutor executeTool) {
    if (m_running) {
        emit failed(QStringLiteral("A Responses turn is already running."), m_toolRounds);
        return;
    }
    if (request.model.trimmed().isEmpty() || request.input.isEmpty()) {
        emit failed(QStringLiteral("Responses turn requires a model and input."), 0);
        return;
    }
    m_request = request;
    if (m_request.protocol == QStringLiteral("responses")
        && m_request.baseUrl.trimmed() == m_gatewayChatModeBaseUrl)
        m_request.protocol = QStringLiteral("chat_completions");
    m_transcript = ResponsesToolTranscript(request.input, request.artifactRoot);
    m_executeTool = std::move(executeTool);
    m_calls.clear();
    m_toolResults.clear();
    m_toolOutputs = {};
    m_pendingSteering.clear();
    m_lastEvents = {};
    m_answer.clear();
    m_toolRounds = 0;
    m_completedTools = 0;
    m_emptyOutputBudgetContinuations = 0;
    m_parallelBatch = false;
    m_paused = false;
    m_running = true;
    requestModelRound();
}

bool ResponsesTurnRunner::steer(const QString &message) {
    const QString bounded = message.trimmed().left(64'000);
    if (!m_running || bounded.isEmpty())
        return false;
    m_pendingSteering.append(bounded);
    return true;
}

void ResponsesTurnRunner::pause() {
    if (m_running)
        m_paused = true;
}

void ResponsesTurnRunner::resume() {
    if (!m_running || !m_paused)
        return;
    m_paused = false;
    requestModelRound();
}

void ResponsesTurnRunner::cancel() {
    if (!m_running)
        return;
    m_running = false;
    m_paused = false;
    m_client.cancel();
    emit failed(QStringLiteral("Responses turn cancelled."), m_toolRounds);
}

void ResponsesTurnRunner::requestModelRound() {
    if (!m_running || m_paused)
        return;
    const int contextWindow = qBound(2'048, m_request.contextWindow, 2'000'000);
    const int contextLimit = contextWindow * qBound(1, m_request.effectiveContextPercent, 100) / 100;
    const int inputBudget = contextLimit
        - ResponsesToolTranscript::tokenEstimate(m_request.instructions)
        - ResponsesToolTranscript::tokenEstimate(m_request.tools)
        - qBound(1, m_request.maximumOutputTokens, contextWindow)
        - 512;
    QJsonArray input;
    QString transcriptError;
    if (inputBudget <= 0 || !m_transcript.build(inputBudget, &input, &transcriptError)) {
        finishWithError(transcriptError.isEmpty()
            ? QStringLiteral("Instructions, tools, and output reservation exceed the context budget.")
            : transcriptError);
        return;
    }
    const bool chatCompletions = m_request.protocol == QStringLiteral("chat_completions");
    const QJsonArray requestInput = !chatCompletions && usesGatewayMessageShape(m_request.baseUrl)
        ? normalizeGatewayInput(input) : input;
    QJsonObject payload = chatCompletions
        ? QJsonObject{
            {QStringLiteral("model"), m_request.model},
            {QStringLiteral("messages"), chatMessages(m_request.instructions, input)},
            {QStringLiteral("tools"), chatTools(m_request.tools)},
            {QStringLiteral("tool_choice"), m_request.tools.isEmpty()
                 ? QJsonValue(QStringLiteral("none")) : QJsonValue(QStringLiteral("auto"))},
            {QStringLiteral("parallel_tool_calls"), true},
            {QStringLiteral("stream"), true},
            {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}},
            {QStringLiteral("max_tokens"), qBound(1, m_request.maximumOutputTokens, contextWindow)},
        }
        : QJsonObject{
            {QStringLiteral("model"), m_request.model},
            {QStringLiteral("instructions"), m_request.instructions},
            {QStringLiteral("input"), requestInput},
            {QStringLiteral("tools"), m_request.tools},
            {QStringLiteral("tool_choice"), m_request.tools.isEmpty()
                 ? QJsonValue(QStringLiteral("none")) : QJsonValue(QStringLiteral("auto"))},
            {QStringLiteral("parallel_tool_calls"), true},
            {QStringLiteral("store"), false},
            {QStringLiteral("stream"), true},
            {QStringLiteral("max_output_tokens"), qBound(1, m_request.maximumOutputTokens, contextWindow)},
        };
    for (auto it = m_request.samplingOptions.constBegin(); it != m_request.samplingOptions.constEnd(); ++it) {
        if (it.key() == QStringLiteral("temperature") || it.key() == QStringLiteral("top_p")
            || it.key() == QStringLiteral("top_k") || it.key() == QStringLiteral("min_p")
            || it.key() == QStringLiteral("repeat_penalty") || it.key() == QStringLiteral("repeat_last_n")
            || it.key() == QStringLiteral("dry_multiplier") || it.key() == QStringLiteral("presence_penalty")
            || it.key() == QStringLiteral("frequency_penalty"))
            payload.insert(it.key(), it.value());
    }
    const QString reasoningEffort = m_request.reasoningEffort.trimmed().toLower();
    if (!chatCompletions && (reasoningEffort == QStringLiteral("low") || reasoningEffort == QStringLiteral("medium")
        || reasoningEffort == QStringLiteral("high") || reasoningEffort == QStringLiteral("xhigh")
        || reasoningEffort == QStringLiteral("max") || reasoningEffort == QStringLiteral("ultra")))
        payload.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), reasoningEffort}});
    if (!chatCompletions && m_request.baseUrl.startsWith(QStringLiteral("https://api.openai.com/")))
        payload.insert(QStringLiteral("include"), QJsonArray{QStringLiteral("reasoning.encrypted_content")});
    if (chatCompletions)
        m_client.startChatCompletions(m_request.baseUrl, m_request.apiKey, payload, m_request.timeoutMs,
                                      m_request.reconnectMaxAttempts, m_request.reconnectDelayMs);
    else
        m_client.start(m_request.baseUrl, m_request.apiKey, payload, m_request.timeoutMs,
                       m_request.reconnectMaxAttempts, m_request.reconnectDelayMs);
}

void ResponsesTurnRunner::handleRound(const QJsonArray &events, int statusCode, const QString &error) {
    if (!m_running)
        return;
    QString gatewayPath = QUrl(m_request.baseUrl.trimmed()).path().toLower();
    while (gatewayPath.endsWith(QLatin1Char('/')))
        gatewayPath.chop(1);
    const bool malformedResponsesToolCall = statusCode == 500
        && error.contains(QStringLiteral("tool call arguments"), Qt::CaseInsensitive)
        && error.contains(QStringLiteral("JSON"), Qt::CaseInsensitive);
    if (m_request.protocol == QStringLiteral("responses")
        && (statusCode == 404 || statusCode == 405 || statusCode == 501 || malformedResponsesToolCall)
        && gatewayPath.endsWith(QStringLiteral("/api/v1"))) {
        m_gatewayChatModeBaseUrl = m_request.baseUrl.trimmed();
        m_request.protocol = QStringLiteral("chat_completions");
        emit modelEvent({
            {QStringLiteral("type"), QStringLiteral("provider_route_fallback")},
            {QStringLiteral("provider"), QStringLiteral("api")},
            {QStringLiteral("from_route"), QStringLiteral("/responses")},
            {QStringLiteral("to_route"), QStringLiteral("/chat/completions")},
            {QStringLiteral("status_code"), statusCode},
            {QStringLiteral("reason"), malformedResponsesToolCall
                ? QStringLiteral("Responses 工具参数无法解析，切换兼容接口重试")
                : QStringLiteral("Responses 路由不可用，切换兼容接口")},
        });
        requestModelRound();
        return;
    }
    if (!error.isEmpty()) {
        finishWithError(error);
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        finishWithError(QStringLiteral("Responses API returned HTTP %1.").arg(statusCode));
        return;
    }
    m_lastEvents = events;
    bool sawCompletedResponse = false;
    for (const QJsonValue &value : events) {
        if (value.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("response.completed")) {
            sawCompletedResponse = true;
            const QString status = value.toObject().value(QStringLiteral("response")).toObject()
                .value(QStringLiteral("status")).toString();
            if (!status.isEmpty() && status != QStringLiteral("completed")) {
                finishWithError(QStringLiteral("Responses turn ended with status: %1").arg(status));
                return;
            }
            break;
        }
    }
    if (!sawCompletedResponse) {
        finishWithError(QStringLiteral("Responses stream ended without a completed response."));
        return;
    }
    m_calls = functionCalls(events);
    m_answer = outputText(events);
    if (!m_calls.isEmpty() || !m_answer.trimmed().isEmpty())
        m_emptyOutputBudgetContinuations = 0;
    if (m_calls.isEmpty()) {
        if (!m_pendingSteering.isEmpty()) {
            const QJsonObject completedEvent = [&] {
                for (const QJsonValue &value : events) {
                    const QJsonObject event = value.toObject();
                    if (event.value(QStringLiteral("type")).toString() == QStringLiteral("response.completed"))
                        return event;
                }
                return QJsonObject{};
            }();
            QJsonArray responseOutput = completedEvent.value(QStringLiteral("response")).toObject()
                .value(QStringLiteral("output")).toArray();
            if (responseOutput.isEmpty() && !m_answer.isEmpty())
                responseOutput.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), m_answer}});
            QString transcriptError;
            if (!responseOutput.isEmpty() && !m_transcript.appendRound(responseOutput, &transcriptError)) {
                finishWithError(transcriptError);
                return;
            }
            if (!appendPendingSteering())
                return;
            requestModelRound();
            return;
        }
        if (m_answer.trimmed().isEmpty()) {
            int outputTokens = 0;
            for (const QJsonValue &value : events) {
                const QJsonObject event = value.toObject();
                if (event.value(QStringLiteral("type")).toString() != QLatin1String("response.completed"))
                    continue;
                outputTokens = event.value(QStringLiteral("response")).toObject()
                    .value(QStringLiteral("usage")).toObject().value(QStringLiteral("output_tokens")).toInt();
                break;
            }
            const int outputLimit = qMax(1, m_request.maximumOutputTokens);
            if (outputTokens >= outputLimit * 9 / 10) {
                if (m_emptyOutputBudgetContinuations >= 1) {
                    emit modelEvent({
                        {QStringLiteral("type"), QStringLiteral("output_budget_stalled")},
                        {QStringLiteral("output_tokens"), outputTokens},
                        {QStringLiteral("output_token_limit"), outputLimit},
                        {QStringLiteral("continuations"), m_emptyOutputBudgetContinuations},
                    });
                    finishWithError(QStringLiteral(
                        "The model repeatedly exhausted its output budget without a tool call or answer. "
                        "The turn stopped to prevent an unproductive reasoning loop; continue with a concrete next action."));
                    return;
                }
                ++m_emptyOutputBudgetContinuations;
                const QString continuation = QStringLiteral(
                    "The previous model response used %1 of %2 output tokens without issuing a tool call or a user-facing answer. "
                    "Continue this same task from the existing conversation now. Do not repeat or extend analysis without acting; "
                    "take the next concrete tool action, or provide a concise result if the task is complete.")
                    .arg(outputTokens).arg(outputLimit);
                QString transcriptError;
                if (!m_transcript.appendRound(QJsonArray{QJsonObject{
                        {QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), continuation},
                    }}, &transcriptError)) {
                    finishWithError(transcriptError);
                    return;
                }
                emit modelEvent({
                    {QStringLiteral("type"), QStringLiteral("output_budget_continuation")},
                    {QStringLiteral("output_tokens"), outputTokens},
                    {QStringLiteral("output_token_limit"), outputLimit},
                });
                if (m_paused)
                    return;
                requestModelRound();
                return;
            }
            finishWithError(QStringLiteral("The model returned an empty response without a tool call."));
            return;
        }
        m_running = false;
        emit completed(m_answer, m_toolRounds);
        return;
    }
    const int maximumRounds = qBound(0, m_request.maximumToolRounds, 1'000);
    if (m_toolRounds >= maximumRounds) {
        finishWithError(QStringLiteral("Tool round limit reached before the model completed the turn."));
        return;
    }
    if (!m_executeTool) {
        finishWithError(QStringLiteral("The model requested a tool but no tool executor is configured."));
        return;
    }
    ++m_toolRounds;
    m_toolOutputs = {};
    m_toolResults = QVector<QJsonObject>(m_calls.size());
    m_completedTools = 0;
    m_parallelBatch = m_calls.size() > 1;
    for (const FunctionCall &call : std::as_const(m_calls)) {
        if (call.name != QStringLiteral("read_file")
            && call.name != QStringLiteral("read_project_excerpt")
            && call.name != QStringLiteral("search_project_text")) {
            m_parallelBatch = false;
            break;
        }
    }
    if (m_parallelBatch) {
        for (int index = 0; index < m_calls.size(); ++index)
            executeNextTool(index);
    } else {
        executeNextTool(0);
    }
}

void ResponsesTurnRunner::executeNextTool(int index) {
    if (!m_running || index < 0 || index >= m_calls.size())
        return;
    const FunctionCall call = m_calls.at(index);
    emit toolStarted(call.name, call.id, call.arguments);
    bool toolAllowed = false;
    for (const QJsonValue &value : std::as_const(m_request.tools)) {
        const QJsonObject tool = value.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString(
            tool.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString());
        if (name == call.name) {
            toolAllowed = true;
            break;
        }
    }
    if (!toolAllowed) {
        finishTool(index, call, QJsonObject{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("The model requested an undeclared tool.")},
        });
        return;
    }
    if (!call.argumentError.isEmpty()) {
        finishTool(index, call, QJsonObject{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), call.argumentError},
        });
        return;
    }
    const QPointer<ResponsesTurnRunner> guard(this);
    try {
        m_executeTool(call.name, call.arguments, call.id,
            [guard, index, call](QJsonObject result) {
                if (!guard)
                    return;
                QMetaObject::invokeMethod(guard, [guard, index, call, result = std::move(result)]() mutable {
                    if (!guard || !guard->m_running)
                        return;
                    guard->finishTool(index, call, result);
                }, Qt::QueuedConnection);
            });
    } catch (const std::exception &exception) {
        finishTool(index, call, QJsonObject{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QString::fromUtf8(exception.what())},
        });
    } catch (...) {
        finishTool(index, call, QJsonObject{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Tool executor raised an unknown exception.")},
        });
    }
}

void ResponsesTurnRunner::finishTool(int index, const FunctionCall &call, const QJsonObject &result) {
    if (index < 0 || index >= m_toolResults.size()
        || m_toolResults.at(index).contains(QStringLiteral("type")))
        return;
    emit toolFinished(call.name, call.id, result);
    const QJsonObject modelResult = call.name == QLatin1String("wait_dft_job")
        ? compactDftJobResult(result) : result;
    QString transcriptError;
    const QString toolOutputText = m_transcript.boundOutput(
        call.id, QString::fromUtf8(QJsonDocument(modelResult).toJson(QJsonDocument::Compact)),
        18'000, &transcriptError);
    if (!transcriptError.isEmpty()) {
        finishWithError(transcriptError);
        return;
    }
    m_toolResults[index] = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function_call_output")},
        {QStringLiteral("call_id"), call.id},
        {QStringLiteral("output"), toolOutputText},
    };
    ++m_completedTools;
    if (m_completedTools < m_calls.size() && m_parallelBatch)
        return;
    if (m_completedTools < m_calls.size()) {
        executeNextTool(index + 1);
        return;
    }
    for (const QJsonObject &output : std::as_const(m_toolResults))
        m_toolOutputs.append(output);
    continueAfterTools();
}

void ResponsesTurnRunner::continueAfterTools() {
    const QJsonObject completedEvent = [&] {
        for (const QJsonValue &value : m_lastEvents) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() == QStringLiteral("response.completed"))
                return event;
        }
        return QJsonObject{};
    }();
    const QJsonArray output = completedEvent.value(QStringLiteral("response")).toObject()
        .value(QStringLiteral("output")).toArray();
    QJsonArray roundItems;
    if (!output.isEmpty()) {
        for (const QJsonValue &item : output)
            roundItems.append(item);
    } else {
        for (const FunctionCall &item : std::as_const(m_calls)) {
            roundItems.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call")},
                {QStringLiteral("call_id"), item.id},
                {QStringLiteral("name"), item.name},
                {QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(item.arguments).toJson(QJsonDocument::Compact))},
            });
        }
    }
    for (const QJsonValue &item : std::as_const(m_toolOutputs))
        roundItems.append(item);
    QString transcriptError;
    if (!m_transcript.appendRound(roundItems, &transcriptError)) {
        finishWithError(transcriptError);
        return;
    }
    if (!appendPendingSteering())
        return;
    if (m_paused)
        return;
    requestModelRound();
}

bool ResponsesTurnRunner::appendPendingSteering() {
    while (!m_pendingSteering.isEmpty()) {
        const QString message = m_pendingSteering.takeFirst();
        QString error;
        if (!m_transcript.appendRound(QJsonArray{QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"), message},
            }}, &error)) {
            finishWithError(error);
            return false;
        }
        emit modelEvent({
            {QStringLiteral("type"), QStringLiteral("user_steering_received")},
            {QStringLiteral("text"), message},
        });
    }
    return true;
}

void ResponsesTurnRunner::finishWithError(const QString &error) {
    m_running = false;
    m_paused = false;
    emit failed(error, m_toolRounds);
}

QVector<ResponsesTurnRunner::FunctionCall> ResponsesTurnRunner::functionCalls(const QJsonArray &events) {
    QJsonArray output;
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("response.completed"))
            continue;
        output = event.value(QStringLiteral("response")).toObject().value(QStringLiteral("output")).toArray();
        break;
    }
    if (output.isEmpty()) {
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() == QStringLiteral("response.output_item.done"))
                output.append(event.value(QStringLiteral("item")));
        }
    }
    QVector<FunctionCall> calls;
    for (const QJsonValue &value : std::as_const(output)) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString() != QStringLiteral("function_call"))
            continue;
        const QString callId = item.value(QStringLiteral("call_id")).toString(item.value(QStringLiteral("id")).toString());
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        const QJsonValue argumentValue = item.value(QStringLiteral("arguments"));
        QJsonObject arguments;
        QString argumentError;
        if (argumentValue.isObject()) {
            arguments = argumentValue.toObject();
        } else if (argumentValue.isString()) {
            QJsonParseError parseError;
            const QJsonDocument parsed = QJsonDocument::fromJson(argumentValue.toString().toUtf8(), &parseError);
            if (parsed.isObject())
                arguments = parsed.object();
            else
                argumentError = QStringLiteral("Function arguments are invalid JSON: %1").arg(parseError.errorString());
        } else {
            argumentError = QStringLiteral("Function arguments must be a JSON object.");
        }
        if (!callId.isEmpty() && !name.isEmpty())
            calls.append({callId, name, arguments, argumentError});
    }
    return calls;
}

QString ResponsesTurnRunner::outputText(const QJsonArray &events) {
    QString deltas;
    QString completedText;
    QString doneText;
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const QString type = event.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response.output_text.delta"))
            deltas.append(event.value(QStringLiteral("delta")).toString());
        else if (type == QStringLiteral("response.output_text.done"))
            doneText = event.value(QStringLiteral("text")).toString();
        else if (type == QStringLiteral("response.completed")) {
            const QJsonObject response = event.value(QStringLiteral("response")).toObject();
            if (response.value(QStringLiteral("output_text")).isString())
                completedText = response.value(QStringLiteral("output_text")).toString();
            if (completedText.isEmpty()) {
                QStringList parts;
                for (const QJsonValue &itemValue : response.value(QStringLiteral("output")).toArray()) {
                    const QJsonObject item = itemValue.toObject();
                    if (item.value(QStringLiteral("type")).toString() != QLatin1String("message"))
                        continue;
                    for (const QJsonValue &partValue : item.value(QStringLiteral("content")).toArray()) {
                        const QJsonObject part = partValue.toObject();
                        if (part.value(QStringLiteral("type")).toString() == QLatin1String("output_text"))
                            parts.append(part.value(QStringLiteral("text")).toString());
                    }
                }
                completedText = parts.join(QString{});
            }
        }
    }
    if (!completedText.isEmpty())
        return completedText.trimmed();
    if (!doneText.isEmpty())
        return doneText.trimmed();
    return deltas.trimmed();
}
