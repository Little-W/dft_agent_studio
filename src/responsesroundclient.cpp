#include "responsesroundclient.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {
QUrl responsesUrl(QString baseUrl) {
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith('/'))
        baseUrl.chop(1);
    if (baseUrl.isEmpty())
        return {};
    if (!baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
        baseUrl += QStringLiteral("/v1");
    QUrl url(baseUrl + QStringLiteral("/responses"));
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
        return {};
    return url;
}

QUrl chatCompletionsUrl(QString baseUrl) {
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith('/'))
        baseUrl.chop(1);
    if (baseUrl.isEmpty())
        return {};
    if (!baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
        baseUrl += QStringLiteral("/v1");
    const QUrl url(baseUrl + QStringLiteral("/chat/completions"));
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
        return {};
    return url;
}
}

ResponsesRoundClient::ResponsesRoundClient(QObject *parent)
    : QObject(parent), m_timeout(new QTimer(this)), m_retryTimer(new QTimer(this)) {
    m_timeout->setSingleShot(true);
    m_retryTimer->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
    connect(m_retryTimer, &QTimer::timeout, this, &ResponsesRoundClient::postAttempt);
}

bool ResponsesRoundClient::running() const {
    return m_reply != nullptr || m_retryTimer->isActive();
}

void ResponsesRoundClient::start(const QString &baseUrl, const QString &apiKey,
                                 const QJsonObject &payload, int timeoutMs,
                                 int reconnectMaxAttempts, int reconnectDelayMs) {
    if (running()) {
        failBeforeStart(QStringLiteral("A Responses request is already running."));
        return;
    }
    const QUrl url = responsesUrl(baseUrl);
    if (!url.isValid() || !payload.contains(QStringLiteral("model"))
        || !payload.value(QStringLiteral("model")).isString()
        || payload.value(QStringLiteral("model")).toString().trimmed().isEmpty()
        || !payload.value(QStringLiteral("input")).isArray()) {
        failBeforeStart(QStringLiteral("Responses request configuration is incomplete."));
        return;
    }
    m_url = url;
    m_chatCompletions = false;
    m_apiKey = apiKey.trimmed().toUtf8();
    m_requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_timeoutMs = qBound(1'000, timeoutMs, 1'800'000);
    m_reconnectMaxAttempts = qBound(1, reconnectMaxAttempts, 20);
    m_reconnectDelayMs = qBound(100, reconnectDelayMs, 60'000);
    m_attempt = 0;
    m_cancelled = false;
    postAttempt();
}

void ResponsesRoundClient::startChatCompletions(const QString &baseUrl, const QString &apiKey,
                                                const QJsonObject &payload, int timeoutMs,
                                                int reconnectMaxAttempts, int reconnectDelayMs) {
    if (running()) {
        failBeforeStart(QStringLiteral("A model request is already running."));
        return;
    }
    const QUrl url = chatCompletionsUrl(baseUrl);
    if (!url.isValid() || !payload.value(QStringLiteral("model")).isString()
        || payload.value(QStringLiteral("model")).toString().trimmed().isEmpty()
        || !payload.value(QStringLiteral("messages")).isArray()) {
        failBeforeStart(QStringLiteral("Chat Completions request configuration is incomplete."));
        return;
    }
    m_url = url;
    m_chatCompletions = true;
    m_apiKey = apiKey.trimmed().toUtf8();
    m_requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_timeoutMs = qBound(1'000, timeoutMs, 1'800'000);
    m_reconnectMaxAttempts = qBound(1, reconnectMaxAttempts, 20);
    m_reconnectDelayMs = qBound(100, reconnectDelayMs, 60'000);
    m_attempt = 0;
    m_cancelled = false;
    postAttempt();
}

void ResponsesRoundClient::startEndpoint(const QString &endpointUrl, const QString &apiKey,
                                         const QJsonObject &payload, int timeoutMs) {
    if (running()) {
        failBeforeStart(QStringLiteral("A Responses request is already running."));
        return;
    }
    const QUrl url(endpointUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || !payload.contains(QStringLiteral("model")) || !payload.value(QStringLiteral("model")).isString()) {
        failBeforeStart(QStringLiteral("Native API request configuration is incomplete."));
        return;
    }
    m_url = url;
    m_chatCompletions = false;
    m_apiKey = apiKey.trimmed().toUtf8();
    m_requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_timeoutMs = qBound(1'000, timeoutMs, 1'800'000);
    m_reconnectMaxAttempts = 1;
    m_reconnectDelayMs = 100;
    m_attempt = 0;
    m_cancelled = false;
    postAttempt();
}

void ResponsesRoundClient::postAttempt() {
    if (m_reply || !m_url.isValid())
        return;
    m_pending.clear();
    m_nonStreamBody.clear();
    m_events = {};
    m_chatToolCalls.clear();
    m_chatOutputText.clear();
    m_chatReasoningText.clear();
    m_visibleContent.clear();
    m_lastContentFragment.clear();
    m_visibleReasoning.clear();
    m_lastReasoningFragment.clear();
    m_chatUsage = {};
    m_sawSseFrame = false;
    m_sawTerminalResponse = false;
    ++m_attempt;
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "text/event-stream, application/json");
    request.setTransferTimeout(m_timeoutMs);
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey);
    m_reply = m_network.post(request, m_requestBody);
    connect(m_reply, &QNetworkReply::readyRead, this, &ResponsesRoundClient::consumeAvailable);
    connect(m_reply, &QNetworkReply::finished, this, &ResponsesRoundClient::finish);
    m_timeout->start(m_timeoutMs);
}

void ResponsesRoundClient::cancel() {
    const bool waitingToRetry = m_retryTimer->isActive();
    m_cancelled = true;
    m_retryTimer->stop();
    if (m_reply)
        m_reply->abort();
    else if (waitingToRetry)
        emit completed({}, 0, QStringLiteral("Responses request cancelled."));
}

void ResponsesRoundClient::consumeAvailable() {
    if (!m_reply)
        return;
    const QByteArray bytes = m_reply->readAll();
    const QByteArray contentType = m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
    if (!contentType.contains("text/event-stream")) {
        m_nonStreamBody.append(bytes);
        return;
    }
    m_pending.append(bytes);
    while (true) {
        const qsizetype newline = m_pending.indexOf('\n');
        if (newline < 0)
            break;
        QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        consumeLine(std::move(line));
    }
}

void ResponsesRoundClient::consumeLine(QByteArray line) {
    if (line.startsWith("data:")) {
        m_sawSseFrame = true;
        QByteArray data = line.mid(5).trimmed();
        if (data == "[DONE]")
            return;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (!document.isObject())
            return;
        const QJsonObject event = document.object();
        if (m_chatCompletions)
            consumeChatChunk(event);
        else
            emitEvent(event);
    } else if (!line.isEmpty() && !line.startsWith(':')) {
        // Some compatible gateways return JSON-lines despite an SSE header.
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (document.isObject()) {
            const QJsonObject event = document.object();
            if (m_chatCompletions)
                consumeChatChunk(event);
            else
                emitEvent(event);
        }
    }
}

void ResponsesRoundClient::consumeChatChunk(const QJsonObject &chunk) {
    if (chunk.value(QStringLiteral("usage")).isObject())
        m_chatUsage = chunk.value(QStringLiteral("usage")).toObject();
    const QJsonArray choices = chunk.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
    const QString content = normalizeStreamFragment(
        QStringLiteral("content"), delta.value(QStringLiteral("content")).toString());
    if (!content.isEmpty()) {
        m_chatOutputText.append(content);
        emitEvent({{QStringLiteral("type"), QStringLiteral("response.output_text.delta")},
                   {QStringLiteral("delta"), content}}, false);
    }
    const QString reasoning = normalizeStreamFragment(QStringLiteral("reasoning"),
        delta.value(QStringLiteral("reasoning_content")).toString(
        delta.value(QStringLiteral("reasoning")).toString()));
    if (!reasoning.isEmpty()) {
        m_chatReasoningText.append(reasoning);
        emitEvent({{QStringLiteral("type"), QStringLiteral("response.reasoning_summary_text.delta")},
                   {QStringLiteral("delta"), reasoning}}, false);
    }
    for (const QJsonValue &callValue : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject call = callValue.toObject();
        const int index = call.value(QStringLiteral("index")).toInt(-1);
        if (index < 0)
            continue;
        QJsonObject accumulated = m_chatToolCalls.value(index);
        const QJsonObject function = call.value(QStringLiteral("function")).toObject();
        if (!call.value(QStringLiteral("id")).toString().isEmpty())
            accumulated.insert(QStringLiteral("id"), call.value(QStringLiteral("id")));
        if (!function.value(QStringLiteral("name")).toString().isEmpty())
            accumulated.insert(QStringLiteral("name"), function.value(QStringLiteral("name")));
        QString arguments = accumulated.value(QStringLiteral("arguments")).toString();
        arguments.append(function.value(QStringLiteral("arguments")).toString());
        accumulated.insert(QStringLiteral("arguments"), arguments);
        m_chatToolCalls.insert(index, accumulated);
    }
}

QString ResponsesRoundClient::normalizeStreamFragment(const QString &channel, const QString &value) {
    if (value.isEmpty())
        return {};
    QString *visible = channel == QLatin1String("reasoning") ? &m_visibleReasoning : &m_visibleContent;
    QString *last = channel == QLatin1String("reasoning") ? &m_lastReasoningFragment : &m_lastContentFragment;
    if (value == *last || value == *visible)
        return {};
    const QString delta = !visible->isEmpty() && value.startsWith(*visible)
        ? value.mid(visible->size()) : value;
    *last = value;
    if (!delta.isEmpty())
        *visible += delta;
    return delta;
}

QJsonObject ResponsesRoundClient::chatCompletedEvent(const QJsonObject &response) const {
    QJsonArray output;
    if (!m_chatOutputText.isEmpty()) {
        output.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("message")},
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QJsonArray{QJsonObject{
                {QStringLiteral("type"), QStringLiteral("output_text")},
                {QStringLiteral("text"), m_chatOutputText},
            }}},
        });
    }
    for (auto it = m_chatToolCalls.cbegin(); it != m_chatToolCalls.cend(); ++it) {
        const QJsonObject call = it.value();
        output.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function_call")},
            {QStringLiteral("call_id"), call.value(QStringLiteral("id"))},
            {QStringLiteral("name"), call.value(QStringLiteral("name"))},
            {QStringLiteral("arguments"), call.value(QStringLiteral("arguments"))},
        });
    }
    QJsonObject usage{
        {QStringLiteral("input_tokens"), m_chatUsage.value(QStringLiteral("prompt_tokens"))},
        {QStringLiteral("output_tokens"), m_chatUsage.value(QStringLiteral("completion_tokens"))},
    };
    if (m_chatUsage.contains(QStringLiteral("prompt_tokens")))
        usage.insert(QStringLiteral("total_tokens"), m_chatUsage.value(QStringLiteral("total_tokens")));
    QJsonObject completed{
        {QStringLiteral("type"), QStringLiteral("response.completed")},
        {QStringLiteral("response"), QJsonObject{
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("output_text"), m_chatOutputText},
            {QStringLiteral("output"), output},
            {QStringLiteral("usage"), usage},
        }},
    };
    if (!response.isEmpty()) {
        QJsonObject normalized = completed.value(QStringLiteral("response")).toObject();
        normalized.insert(QStringLiteral("id"), response.value(QStringLiteral("id")));
        normalized.insert(QStringLiteral("model"), response.value(QStringLiteral("model")));
        completed.insert(QStringLiteral("response"), normalized);
    }
    return completed;
}

void ResponsesRoundClient::emitChatCompletionResponse(const QJsonObject &response) {
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    int toolIndex = 0;
    for (const QJsonValue &choiceValue : choices) {
        const QJsonObject choice = choiceValue.toObject();
        QJsonObject message = choice.value(QStringLiteral("message")).toObject();
        if (message.isEmpty())
            message = choice.value(QStringLiteral("delta")).toObject();
        if (message.isEmpty())
            continue;
        QString reasoning = message.value(QStringLiteral("reasoning_content")).toString(
            message.value(QStringLiteral("reasoning")).toString());
        if (!reasoning.isEmpty()) {
            reasoning = normalizeStreamFragment(QStringLiteral("reasoning"), reasoning);
            if (!reasoning.isEmpty()) {
                m_chatReasoningText.append(reasoning);
                emitEvent({{QStringLiteral("type"), QStringLiteral("response.reasoning_summary_text.delta")},
                           {QStringLiteral("delta"), reasoning}}, false);
            }
        }
        const QJsonValue content = message.value(QStringLiteral("content"));
        QString contentText = content.toString();
        if (content.isArray()) {
            for (const QJsonValue &partValue : content.toArray()) {
                const QJsonObject part = partValue.toObject();
                const QString type = part.value(QStringLiteral("type")).toString();
                if ((type.isEmpty() || type == QLatin1String("text") || type == QLatin1String("output_text"))
                    && part.value(QStringLiteral("text")).isString())
                    contentText.append(part.value(QStringLiteral("text")).toString());
            }
        }
        if (!contentText.isEmpty()) {
            contentText = normalizeStreamFragment(QStringLiteral("content"), contentText);
            if (!contentText.isEmpty()) {
                m_chatOutputText.append(contentText);
                emitEvent({{QStringLiteral("type"), QStringLiteral("response.output_text.delta")},
                           {QStringLiteral("delta"), contentText}}, false);
            }
        }
        for (const QJsonValue &callValue : message.value(QStringLiteral("tool_calls")).toArray()) {
            const QJsonObject call = callValue.toObject();
            const QJsonObject function = call.value(QStringLiteral("function")).toObject();
            QJsonValue arguments = function.value(QStringLiteral("arguments"));
            if (arguments.isObject())
                arguments = QString::fromUtf8(QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact));
            else if (!arguments.isString())
                arguments = QStringLiteral("{}");
            m_chatToolCalls.insert(toolIndex++, QJsonObject{
                {QStringLiteral("id"), call.value(QStringLiteral("id"))},
                {QStringLiteral("name"), function.value(QStringLiteral("name"))},
                {QStringLiteral("arguments"), arguments},
            });
        }
    }
    m_chatUsage = response.value(QStringLiteral("usage")).toObject();
    emitEvent(chatCompletedEvent(response));
}

void ResponsesRoundClient::emitEvent(const QJsonObject &event, bool normalizeDelta) {
    QJsonObject normalized = event;
    const QString type = normalized.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("response.completed") || type == QLatin1String("response.failed")
        || type == QLatin1String("response.incomplete") || type == QLatin1String("response.cancelled"))
        m_sawTerminalResponse = true;
    QString channel;
    if (type == QLatin1String("response.output_text.delta"))
        channel = QStringLiteral("content");
    else if (type == QLatin1String("response.reasoning_text.delta")
             || type == QLatin1String("response.reasoning_summary_text.delta"))
        channel = QStringLiteral("reasoning");
    if (normalizeDelta && !channel.isEmpty()) {
        const QString delta = normalizeStreamFragment(channel,
            normalized.value(QStringLiteral("delta")).toString());
        if (delta.isEmpty())
            return;
        normalized.insert(QStringLiteral("delta"), delta);
    }
    m_events.append(normalized);
    emit eventReceived(normalized);
}

void ResponsesRoundClient::finish() {
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    consumeAvailable();
    if (!m_pending.isEmpty()) {
        QByteArray line = m_pending;
        m_pending.clear();
        if (line.endsWith('\r'))
            line.chop(1);
        consumeLine(std::move(line));
    }
    m_timeout->stop();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
    if (!m_sawSseFrame && statusCode < 400 && !m_nonStreamBody.isEmpty()
        && !contentType.contains("text/event-stream")) {
        const auto emitResponseObject = [this](QJsonObject response) {
            if (response.value(QStringLiteral("response")).isObject()
                && !response.contains(QStringLiteral("output"))
                && !response.contains(QStringLiteral("choices")))
                response = response.value(QStringLiteral("response")).toObject();
            if (m_chatCompletions || response.value(QStringLiteral("choices")).isArray()) {
                emitChatCompletionResponse(response);
                return;
            }
            const QString type = response.value(QStringLiteral("type")).toString();
            const QJsonObject event = type.startsWith(QStringLiteral("response."))
                ? response
                : QJsonObject{{QStringLiteral("type"), QStringLiteral("response.completed")},
                              {QStringLiteral("response"), response}};
            emitEvent(event);
        };
        QByteArray body = m_nonStreamBody;
        if (body.startsWith(QByteArrayLiteral("\xef\xbb\xbf")))
            body.remove(0, 3);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (document.isObject()) {
            emitResponseObject(document.object());
        } else {
            QJsonArray framedEvents;
            const QList<QByteArray> lines = body.split('\n');
            for (QByteArray line : lines) {
                line = line.trimmed();
                if (line.startsWith("data:"))
                    line = line.mid(5).trimmed();
                else if (line.startsWith("event:") || line.startsWith("id:") || line.startsWith("retry:")
                         || line.startsWith(':'))
                    continue;
                if (line.isEmpty() || line == "[DONE]")
                    continue;
                QJsonParseError lineError;
                const QJsonDocument framed = QJsonDocument::fromJson(line, &lineError);
                if (framed.isObject())
                    framedEvents.append(framed.object());
            }
            if (!framedEvents.isEmpty()) {
                if (framedEvents.size() == 1
                    && framedEvents.first().toObject().value(QStringLiteral("choices")).isArray()) {
                    emitResponseObject(framedEvents.first().toObject());
                } else {
                    for (const QJsonValue &value : std::as_const(framedEvents)) {
                        const QJsonObject event = value.toObject();
                        emitEvent(event);
                    }
                }
            } else if (!m_chatCompletions) {
                const qsizetype objectStart = body.indexOf('{');
                const qsizetype objectEnd = body.lastIndexOf('}');
                if (objectStart >= 0 && objectEnd > objectStart) {
                    QJsonParseError wrappedError;
                    const QJsonDocument wrapped = QJsonDocument::fromJson(
                        body.mid(objectStart, objectEnd - objectStart + 1), &wrappedError);
                    if (wrapped.isObject()) {
                        emitResponseObject(wrapped.object());
                    } else if (networkError == QNetworkReply::NoError) {
                        emitEvent({{QStringLiteral("type"), QStringLiteral("response.invalid")},
                                   {QStringLiteral("detail"), wrappedError.errorString()}});
                    }
                } else if (networkError == QNetworkReply::NoError) {
                    emitEvent({{QStringLiteral("type"), QStringLiteral("response.invalid")},
                               {QStringLiteral("detail"), parseError.errorString()}});
                }
            }
        }
    }
    if (m_chatCompletions && m_sawSseFrame && statusCode >= 200 && statusCode < 300)
        emitEvent(chatCompletedEvent());
    QString error;
    if (networkError != QNetworkReply::NoError) {
        if (statusCode >= 400)
            error = QStringLiteral("%1 returned HTTP %2: %3")
                .arg(m_chatCompletions ? QStringLiteral("Chat Completions API") : QStringLiteral("Responses API"))
                .arg(statusCode).arg(QString::fromUtf8(m_nonStreamBody).trimmed());
        else
            error = QStringLiteral("%1 request failed: %2")
                .arg(m_chatCompletions ? QStringLiteral("Chat Completions") : QStringLiteral("Responses"), networkErrorText);
    }
    const QByteArray responseErrorBody = m_nonStreamBody.toLower();
    const bool malformedResponsesToolCall = !m_chatCompletions && statusCode == 500
        && responseErrorBody.contains("tool call arguments") && responseErrorBody.contains("json");
    const bool retryableStatus = statusCode == 408 || statusCode == 425 || statusCode == 429
        || (!malformedResponsesToolCall
            && (statusCode == 500 || statusCode == 502 || statusCode == 503 || statusCode == 504));
    const bool incompleteSuccessfulStream = statusCode >= 200 && statusCode < 300
        && m_sawSseFrame && !m_sawTerminalResponse;
    const bool retryableTransport = incompleteSuccessfulStream
        || (networkError != QNetworkReply::NoError
            && (statusCode == 0 || (statusCode >= 200 && statusCode < 300 && !m_sawTerminalResponse)));
    const bool retryable = retryableStatus || retryableTransport;
    const bool partialStream = m_sawSseFrame || !m_events.isEmpty();
    reply->deleteLater();
    m_reply = nullptr;
    if (!m_cancelled && retryable && m_attempt < m_reconnectMaxAttempts) {
        const QString reason = statusCode > 0
            ? QStringLiteral("HTTP %1").arg(statusCode)
            : networkErrorText;
        if (partialStream)
            emit streamReset();
        emit retrying(m_attempt, m_reconnectMaxAttempts, statusCode, reason);
        m_retryTimer->start(m_reconnectDelayMs);
        return;
    }
    if (error.isEmpty() && statusCode >= 200 && statusCode < 300 && m_attempt > 1)
        emit reconnected(m_attempt - 1);
    emit completed(m_events, statusCode, error);
}

void ResponsesRoundClient::failBeforeStart(const QString &error) {
    emit completed({}, 0, error);
}
