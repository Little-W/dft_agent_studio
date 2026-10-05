#include "agenttoolclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#include <algorithm>

namespace {
QUrl completionUrl(QString baseUrl) {
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith('/'))
        baseUrl.chop(1);
    if (!baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
        baseUrl += QStringLiteral("/v1");
    return QUrl(baseUrl + QStringLiteral("/chat/completions"));
}

bool matchesType(const QJsonValue &value, const QString &type) {
    if (type == QStringLiteral("object")) return value.isObject();
    if (type == QStringLiteral("array")) return value.isArray();
    if (type == QStringLiteral("string")) return value.isString();
    if (type == QStringLiteral("number")) return value.isDouble();
    if (type == QStringLiteral("integer")) return value.isDouble() && value.toDouble() == qFloor(value.toDouble());
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("null")) return value.isNull();
    return true;
}

bool contains(const QJsonArray &array, const QJsonValue &value) {
    return std::any_of(array.cbegin(), array.cend(), [&value](const QJsonValue &item) {
        return item == value;
    });
}
}

AgentToolClient::AgentToolClient(QObject *parent)
    : QObject(parent), m_timeout(new QTimer(this)) {
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
    });
}

bool AgentToolClient::running() const {
    return m_reply != nullptr;
}

void AgentToolClient::start(const Request &request) {
    begin(request, false);
}

void AgentToolClient::startConversation(const Request &request) {
    begin(request, true);
}

void AgentToolClient::begin(const Request &request, bool conversationRequest) {
    if (running()) {
        const QString error = QStringLiteral("A function-call request is already running.");
        if (conversationRequest)
            emit conversationCompleted({}, error);
        else
            emit completed({}, error);
        return;
    }
    const QUrl url = completionUrl(request.baseUrl);
    if (!url.isValid() || url.host().isEmpty() || request.model.trimmed().isEmpty()
        || request.messages.isEmpty()
        || (conversationRequest ? request.tools.isEmpty() : request.toolName.trimmed().isEmpty())) {
        const QString error = QStringLiteral("Function-call request configuration is incomplete.");
        if (conversationRequest)
            emit conversationCompleted({}, error);
        else
            emit completed({}, error);
        return;
    }
    m_conversationRequest = conversationRequest;
    m_allowFinalAnswer = request.allowFinalAnswer;
    m_expectedToolName = request.toolName.trimmed();
    m_expectedParameters = request.parameters;
    m_expectedTools.clear();

    QJsonArray tools = request.tools;
    if (!conversationRequest) {
        const QJsonObject function{
            {QStringLiteral("name"), request.toolName},
            {QStringLiteral("description"), request.toolDescription},
            {QStringLiteral("parameters"), request.parameters},
        };
        tools = QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), function},
        }};
    } else {
        for (const QJsonValue &toolValue : tools) {
            const QJsonObject function = toolValue.toObject().value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty())
                m_expectedTools.insert(name, function.value(QStringLiteral("parameters")).toObject());
        }
        if (m_expectedTools.isEmpty()) {
            emit conversationCompleted({}, QStringLiteral("Function-call request contains no valid tools."));
            return;
        }
    }
    QJsonObject body{
        {QStringLiteral("model"), request.model},
        {QStringLiteral("messages"), request.messages},
        {QStringLiteral("history_message_count"), 0},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("tool_choice"), conversationRequest && request.allowFinalAnswer
            ? QJsonValue(QStringLiteral("auto")) : QJsonValue(QStringLiteral("required"))},
        {QStringLiteral("stream"), false},
        {QStringLiteral("temperature"), request.temperature},
        {QStringLiteral("max_tokens"), request.maxTokens},
    };
    if (!request.options.isEmpty())
        body.insert(QStringLiteral("options"), request.options);
    if (!request.agentOptions.isEmpty())
        body.insert(QStringLiteral("dft_agent"), request.agentOptions);

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!request.apiKey.trimmed().isEmpty())
        networkRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + request.apiKey.trimmed().toUtf8());
    m_reply = m_network.post(networkRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_timeout->stop();
        if (reply)
            finish(reply);
    });
    m_timeout->start(qBound(1'000, request.timeoutMs, 1'800'000));
}

void AgentToolClient::cancel() {
    if (m_reply)
        m_reply->abort();
}

void AgentToolClient::finish(QNetworkReply *reply) {
    const QByteArray bytes = reply->readAll();
    const auto networkError = reply->error();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    m_reply = nullptr;

    auto fail = [this](const QString &error) {
        if (m_conversationRequest)
            emit conversationCompleted({}, error);
        else
            emit completed({}, error);
    };
    if (networkError != QNetworkReply::NoError) {
        fail(QStringLiteral("Function-call request failed: %1").arg(reply->errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(bytes, &parseError);
    if (!response.isObject()) {
        fail(statusCode >= 400
            ? QStringLiteral("Function-call request failed: HTTP %1: %2").arg(statusCode).arg(QString::fromUtf8(bytes).trimmed())
            : QStringLiteral("Function-call API returned invalid JSON: %1").arg(parseError.errorString()));
        return;
    }
    const QJsonObject root = response.object();
    if (statusCode >= 400) {
        fail(QStringLiteral("Function-call request failed: HTTP %1: %2")
            .arg(statusCode).arg(QString::fromUtf8(bytes).trimmed()));
        return;
    }
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        fail(QStringLiteral("Function-call API returned no choices."));
        return;
    }
    const QJsonObject choice = choices.at(0).toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QJsonArray calls = message.value(QStringLiteral("tool_calls")).toArray();
    if (m_conversationRequest) {
        if (calls.isEmpty()) {
            if (choice.value(QStringLiteral("finish_reason")).toString() == QStringLiteral("stop")
                && m_allowFinalAnswer) {
                emit conversationCompleted(message, {});
                return;
            }
            fail(QStringLiteral("Function-call API returned neither a tool call nor a final answer."));
            return;
        }
        if (calls.size() > 8) {
            fail(QStringLiteral("Function-call API returned too many tool calls in one response."));
            return;
        }
        for (const QJsonValue &callValue : calls) {
            const QJsonObject function = callValue.toObject().value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            if (!m_expectedTools.contains(name)) {
                fail(QStringLiteral("Function-call API returned an unexpected tool name: %1").arg(name));
                return;
            }
            QJsonObject arguments;
            const QJsonValue argumentValue = function.value(QStringLiteral("arguments"));
            if (argumentValue.isObject()) {
                arguments = argumentValue.toObject();
            } else if (argumentValue.isString()) {
                QJsonParseError argumentError;
                const QJsonDocument parsed = QJsonDocument::fromJson(argumentValue.toString().toUtf8(), &argumentError);
                if (!parsed.isObject()) {
                    fail(QStringLiteral("Function-call arguments for %1 are not a valid JSON object.").arg(name));
                    return;
                }
                arguments = parsed.object();
            } else {
                fail(QStringLiteral("Function-call arguments for %1 must be an object.").arg(name));
                return;
            }
            QString validationError;
            if (!validate(arguments, m_expectedTools.value(name), QStringLiteral("arguments.%1").arg(name), &validationError)) {
                fail(validationError);
                return;
            }
        }
        emit conversationCompleted(message, {});
        return;
    }
    if (calls.size() != 1) {
        emit completed({}, QStringLiteral("Function-call API must return exactly one tool call."));
        return;
    }
    const QJsonObject call = calls.at(0).toObject();
    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    if (function.value(QStringLiteral("name")).toString() != m_expectedToolName) {
        emit completed({}, QStringLiteral("Function-call API returned an unexpected tool name."));
        return;
    }
    QJsonObject arguments;
    const QJsonValue argumentValue = function.value(QStringLiteral("arguments"));
    if (argumentValue.isObject()) {
        arguments = argumentValue.toObject();
    } else if (argumentValue.isString()) {
        QJsonParseError argumentError;
        const QJsonDocument parsed = QJsonDocument::fromJson(argumentValue.toString().toUtf8(), &argumentError);
        if (!parsed.isObject()) {
            emit completed({}, QStringLiteral("Function-call arguments are not a valid JSON object."));
            return;
        }
        arguments = parsed.object();
    } else {
        emit completed({}, QStringLiteral("Function-call arguments must be a JSON object."));
        return;
    }
    QString validationError;
    if (!validate(arguments, m_expectedParameters, QStringLiteral("arguments"), &validationError)) {
        emit completed({}, validationError);
        return;
    }
    emit completed(arguments, {});
}

bool AgentToolClient::validate(const QJsonValue &value, const QJsonObject &schema,
                               const QString &path, QString *error) {
    const QJsonArray alternatives = schema.value(QStringLiteral("anyOf")).toArray();
    if (!alternatives.isEmpty()) {
        for (const QJsonValue &candidate : alternatives) {
            QString ignored;
            if (validate(value, candidate.toObject(), path, &ignored))
                return true;
        }
        *error = QStringLiteral("%1 does not match any allowed schema type.").arg(path);
        return false;
    }
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && !matchesType(value, type)) {
        *error = QStringLiteral("%1 must be %2.").arg(path, type);
        return false;
    }
    if (schema.value(QStringLiteral("enum")).isArray()
        && !contains(schema.value(QStringLiteral("enum")).toArray(), value)) {
        *error = QStringLiteral("%1 is not an allowed value.").arg(path);
        return false;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
        for (const QJsonValue &field : required) {
            const QString name = field.toString();
            if (!object.contains(name)) {
                *error = QStringLiteral("%1 is missing required field %2.").arg(path, name);
                return false;
            }
        }
        if (schema.value(QStringLiteral("additionalProperties")).toBool(true)) {
            // Unknown values are allowed by this schema.
        } else {
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if (!properties.contains(it.key())) {
                    *error = QStringLiteral("%1 has unexpected field %2.").arg(path, it.key());
                    return false;
                }
            }
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (properties.value(it.key()).isObject()
                && !validate(it.value(), properties.value(it.key()).toObject(), path + '.' + it.key(), error))
                return false;
        }
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        const int minimum = schema.value(QStringLiteral("minItems")).toInt(-1);
        const int maximum = schema.value(QStringLiteral("maxItems")).toInt(-1);
        if (minimum >= 0 && array.size() < minimum) {
            *error = QStringLiteral("%1 must contain at least %2 items.").arg(path).arg(minimum);
            return false;
        }
        if (maximum >= 0 && array.size() > maximum) {
            *error = QStringLiteral("%1 must contain at most %2 items.").arg(path).arg(maximum);
            return false;
        }
        const QJsonObject itemSchema = schema.value(QStringLiteral("items")).toObject();
        for (qsizetype index = 0; index < array.size() && !itemSchema.isEmpty(); ++index) {
            if (!validate(array.at(index), itemSchema, QStringLiteral("%1[%2]").arg(path).arg(index), error))
                return false;
        }
    }
    return true;
}
