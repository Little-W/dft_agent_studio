#include "nativetoolregistryservice.h"

#include <QDateTime>
#include <QMetaType>
#include <QMutexLocker>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <exception>

namespace {

bool hasString(const QVariantList &values, const QString &needle) {
    return std::any_of(values.cbegin(), values.cend(), [&needle](const QVariant &value) {
        return value.toString() == needle;
    });
}

QVariant strictSchemaValue(const QVariant &value);

QVariantMap strictSchemaMap(const QVariantMap &schema) {
    QVariantMap normalized = schema;
    const QVariant propertiesValue = schema.value(QStringLiteral("properties"));
    if (propertiesValue.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap properties = propertiesValue.toMap();
        const QVariantList required = schema.value(QStringLiteral("required")).toList();
        QVariantMap strictProperties;
        QVariantList strictRequired;
        for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
            const QVariant item = strictSchemaValue(it.value());
            if (hasString(required, it.key())) {
                strictProperties.insert(it.key(), item);
            } else {
                strictProperties.insert(it.key(), QVariantMap{
                    {QStringLiteral("anyOf"), QVariantList{item, QVariantMap{{QStringLiteral("type"), QStringLiteral("null")}}}}
                });
            }
            strictRequired.append(it.key());
        }
        normalized.insert(QStringLiteral("properties"), strictProperties);
        normalized.insert(QStringLiteral("required"), strictRequired);
        normalized.insert(QStringLiteral("additionalProperties"), false);
    }

    for (const QString &key : {QStringLiteral("items")}) {
        if (schema.value(key).metaType().id() == QMetaType::QVariantMap)
            normalized.insert(key, strictSchemaValue(schema.value(key)));
    }
    for (const QString &key : {QStringLiteral("anyOf"), QStringLiteral("oneOf"), QStringLiteral("allOf")}) {
        const QVariant value = schema.value(key);
        if (value.metaType().id() != QMetaType::QVariantList)
            continue;
        QVariantList alternatives;
        for (const QVariant &item : value.toList())
            alternatives.append(strictSchemaValue(item));
        normalized.insert(key, alternatives);
    }
    return normalized;
}

QVariant strictSchemaValue(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap)
        return strictSchemaMap(value.toMap());
    return value;
}

QVariant normalizeOptionalNulls(const QVariant &value, const QVariantMap &schema) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap object = value.toMap();
        const QVariantMap properties = schema.value(QStringLiteral("properties")).toMap();
        const QVariantList required = schema.value(QStringLiteral("required")).toList();
        QVariantMap normalized;
        for (auto it = object.cbegin(); it != object.cend(); ++it) {
            const bool optionalNull = properties.contains(it.key())
                && !hasString(required, it.key())
                && (!it.value().isValid() || it.value().isNull());
            if (optionalNull)
                continue;
            const QVariant childSchema = properties.value(it.key());
            normalized.insert(it.key(), childSchema.metaType().id() == QMetaType::QVariantMap
                                ? normalizeOptionalNulls(it.value(), childSchema.toMap())
                                : it.value());
        }
        return normalized;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariant itemSchema = schema.value(QStringLiteral("items"));
        if (itemSchema.metaType().id() != QMetaType::QVariantMap)
            return value;
        QVariantList normalized;
        for (const QVariant &item : value.toList())
            normalized.append(normalizeOptionalNulls(item, itemSchema.toMap()));
        return normalized;
    }
    return value;
}

bool isJsonType(const QVariant &value, const QString &expected) {
    const int type = value.metaType().id();
    if (expected == QStringLiteral("null"))
        return !value.isValid() || value.isNull();
    if (!value.isValid() || value.isNull())
        return false;
    if (expected == QStringLiteral("object"))
        return type == QMetaType::QVariantMap;
    if (expected == QStringLiteral("array"))
        return type == QMetaType::QVariantList;
    if (expected == QStringLiteral("string"))
        return type == QMetaType::QString;
    if (expected == QStringLiteral("boolean"))
        return type == QMetaType::Bool;
    const bool integer = type == QMetaType::Int || type == QMetaType::UInt
        || type == QMetaType::LongLong || type == QMetaType::ULongLong;
    const bool number = integer || type == QMetaType::Double || type == QMetaType::Float;
    if (expected == QStringLiteral("integer"))
        return integer;
    if (expected == QStringLiteral("number"))
        return number;
    return true;
}

QString childPath(const QString &path, const QString &key) {
    return path + QLatin1Char('.') + key;
}

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

QVariantMap failureEnvelope(const QString &message, const QString &requestId = {}) {
    QVariantMap result{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    if (!requestId.isEmpty())
        result.insert(QStringLiteral("request_id"), requestId);
    return result;
}

bool failedResult(const QVariantMap &result) {
    if (result.contains(QStringLiteral("ok")) && !result.value(QStringLiteral("ok")).toBool())
        return true;
    const QString status = result.value(QStringLiteral("status")).toString();
    const QVariant error = result.value(QStringLiteral("error"));
    const bool hasError = error.metaType().id() == QMetaType::QString
        ? !error.toString().isEmpty()
        : (error.isValid() && !error.isNull() && error.toBool());
    return hasError
        || status == QStringLiteral("failed")
        || status == QStringLiteral("blocked")
        || status == QStringLiteral("rejected")
        || status == QStringLiteral("cross_check_blocked")
        || status == QStringLiteral("needs_source_read")
        || status == QStringLiteral("tool_compatibility_edit_blocked")
        || status == QStringLiteral("input_manifest_incomplete")
        || status == QStringLiteral("skill_required")
        || status == QStringLiteral("evidence_required")
        || status == QStringLiteral("approval_required")
        || status == QStringLiteral("approval_rejected")
        || status == QStringLiteral("unchanged_inputs")
        || status == QStringLiteral("no_completed_result")
        || status == QStringLiteral("repeated_action_blocked")
        || status == QStringLiteral("repeated_action_stalled");
}

QString failureMessage(const QVariantMap &result) {
    for (const QString &key : {QStringLiteral("error"), QStringLiteral("message"), QStringLiteral("reason")}) {
        const QString value = result.value(key).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return result.value(QStringLiteral("status")).toString();
}

bool validateValue(const QVariant &value, const QVariantMap &schema,
                   const QString &path, QString *error) {
    const QVariant anyOfValue = schema.value(QStringLiteral("anyOf"));
    if (anyOfValue.metaType().id() == QMetaType::QVariantList) {
        bool matched = false;
        for (const QVariant &candidate : anyOfValue.toList()) {
            QString ignored;
            if (candidate.metaType().id() == QMetaType::QVariantMap
                && validateValue(value, candidate.toMap(), path, &ignored)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return fail(error, path + QStringLiteral(" does not match any allowed type"));
        return true;
    }

    const QVariant oneOfValue = schema.value(QStringLiteral("oneOf"));
    if (oneOfValue.metaType().id() == QMetaType::QVariantList) {
        int matches = 0;
        for (const QVariant &candidate : oneOfValue.toList()) {
            QString ignored;
            if (candidate.metaType().id() == QMetaType::QVariantMap
                && validateValue(value, candidate.toMap(), path, &ignored))
                ++matches;
        }
        if (matches != 1)
            return fail(error, path + QStringLiteral(" must match exactly one allowed schema"));
    }

    const QVariant allOfValue = schema.value(QStringLiteral("allOf"));
    if (allOfValue.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &candidate : allOfValue.toList()) {
            if (candidate.metaType().id() == QMetaType::QVariantMap
                && !validateValue(value, candidate.toMap(), path, error))
                return false;
        }
    }

    const QString expected = schema.value(QStringLiteral("type")).toString();
    if (!expected.isEmpty() && !isJsonType(value, expected))
        return fail(error, path + QStringLiteral(" has the wrong type; expected ") + expected);

    const QVariant enumValue = schema.value(QStringLiteral("enum"));
    if (enumValue.metaType().id() == QMetaType::QVariantList && !enumValue.toList().contains(value)) {
        QStringList choices;
        for (const QVariant &candidate : enumValue.toList())
            choices.append(candidate.toString());
        return fail(error, path + QStringLiteral(" must be one of: ") + choices.join(QStringLiteral(", ")));
    }

    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap object = value.toMap();
        const QVariantMap properties = schema.value(QStringLiteral("properties")).toMap();
        const QVariantList required = schema.value(QStringLiteral("required")).toList();
        for (const QVariant &requiredValue : required) {
            const QString key = requiredValue.toString();
            if (!object.contains(key))
                return fail(error, childPath(path, key) + QStringLiteral(" is required"));
        }
        if (schema.value(QStringLiteral("additionalProperties")).isValid()
            && !schema.value(QStringLiteral("additionalProperties")).toBool()) {
            for (auto it = object.cbegin(); it != object.cend(); ++it) {
                if (!properties.contains(it.key()))
                    return fail(error, childPath(path, it.key()) + QStringLiteral(" is not allowed"));
            }
        }
        for (auto it = object.cbegin(); it != object.cend(); ++it) {
            const QVariant child = properties.value(it.key());
            if (child.metaType().id() == QMetaType::QVariantMap
                && !validateValue(it.value(), child.toMap(), childPath(path, it.key()), error))
                return false;
        }
    }

    if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariantList array = value.toList();
        const int minItems = schema.value(QStringLiteral("minItems"), -1).toInt();
        const int maxItems = schema.value(QStringLiteral("maxItems"), -1).toInt();
        if (minItems >= 0 && array.size() < minItems)
            return fail(error, path + QStringLiteral(" has fewer than %1 items").arg(minItems));
        if (maxItems >= 0 && array.size() > maxItems)
            return fail(error, path + QStringLiteral(" has more than %1 items").arg(maxItems));
        const QVariant itemSchema = schema.value(QStringLiteral("items"));
        if (itemSchema.metaType().id() == QMetaType::QVariantMap) {
            for (qsizetype i = 0; i < array.size(); ++i) {
                if (!validateValue(array.at(i), itemSchema.toMap(),
                                   path + QStringLiteral("[%1]").arg(i), error))
                    return false;
            }
        }
    }

    if (value.metaType().id() == QMetaType::QString) {
        const qsizetype length = value.toString().size();
        const int minLength = schema.value(QStringLiteral("minLength"), -1).toInt();
        const int maxLength = schema.value(QStringLiteral("maxLength"), -1).toInt();
        if (minLength >= 0 && length < minLength)
            return fail(error, path + QStringLiteral(" is shorter than %1 characters").arg(minLength));
        if (maxLength >= 0 && length > maxLength)
            return fail(error, path + QStringLiteral(" is longer than %1 characters").arg(maxLength));
    }

    if (isJsonType(value, QStringLiteral("number"))) {
        const double number = value.toDouble();
        if (schema.contains(QStringLiteral("minimum")) && number < schema.value(QStringLiteral("minimum")).toDouble())
            return fail(error, path + QStringLiteral(" is below minimum"));
        if (schema.contains(QStringLiteral("maximum")) && number > schema.value(QStringLiteral("maximum")).toDouble())
            return fail(error, path + QStringLiteral(" is above maximum"));
    }
    return true;
}

} // namespace

bool NativeToolRegistryService::registerTool(const QString &name, const QString &description,
                                             const QVariantMap &parameters, QString *error,
                                             const QString &risk, bool requiresApproval,
                                             ToolHandler handler) {
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty())
        return fail(error, QStringLiteral("Tool name must not be empty"));
    if (risk != QStringLiteral("read") && risk != QStringLiteral("execute")
        && risk != QStringLiteral("change"))
        return fail(error, QStringLiteral("Unknown tool risk: ") + risk);
    if (parameters.value(QStringLiteral("type")).toString() != QStringLiteral("object"))
        return fail(error, QStringLiteral("Tool parameters schema must be an object schema"));
    QMutexLocker locker(&m_mutex);
    if (m_tools.contains(normalizedName))
        return fail(error, QStringLiteral("Tool name is already registered: ") + normalizedName);
    m_tools.insert(normalizedName, ToolDefinition{description, parameters, risk, requiresApproval,
                                                   std::move(handler)});
    return true;
}

void NativeToolRegistryService::setEventCallback(EventCallback callback) {
    QMutexLocker locker(&m_mutex);
    m_eventCallback = std::move(callback);
}

void NativeToolRegistryService::setExecutionCallbacks(BeforeExecute before, AfterExecute after) {
    QMutexLocker locker(&m_mutex);
    m_beforeExecute = std::move(before);
    m_afterExecute = std::move(after);
}

QVariantMap NativeToolRegistryService::request(const QString &name, const QVariantMap &arguments,
                                               const QString &reason,
                                               const QString &providerCallId,
                                               std::optional<bool> requiresApprovalOverride) {
    ToolDefinition spec;
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_tools.constFind(name);
        if (it == m_tools.cend())
            return failureEnvelope(QStringLiteral("Unregistered DFT tool: ") + name);
        spec = it.value();
    }

    QString validationError;
    const QVariantMap normalized = normalizedArguments(arguments, spec.parameters);
    if (!validateValue(normalized, spec.parameters, QStringLiteral("arguments"), &validationError))
        return failureEnvelope(validationError);

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    id.remove(QLatin1Char('-'));
    const bool requiresApproval = requiresApprovalOverride.value_or(spec.requiresApproval);
    const QString approvalState = requiresApproval ? QStringLiteral("pending")
                                                    : QStringLiteral("not_required");
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
    const QVariantMap requestValue{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), normalized},
        {QStringLiteral("requested_at"), timestamp},
        {QStringLiteral("risk"), spec.risk},
        {QStringLiteral("requires_approval"), requiresApproval},
        {QStringLiteral("approval_state"), approvalState},
        {QStringLiteral("reason"), reason.trimmed()},
        {QStringLiteral("provider_call_id"), providerCallId}
    };
    {
        QMutexLocker locker(&m_mutex);
        m_requests.insert(id, requestValue);
    }
    emitEvent({
        {QStringLiteral("event"), requiresApproval ? QStringLiteral("tool_approval_requested")
                                                    : QStringLiteral("tool_call")},
        {QStringLiteral("request_id"), id},
        {QStringLiteral("provider_call_id"), providerCallId},
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), normalized},
        {QStringLiteral("risk"), spec.risk},
        {QStringLiteral("reason"), reason.trimmed()}
    });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("request"), requestValue}};
}

QVariantList NativeToolRegistryService::pendingRequests() const {
    QVariantList pending;
    QMutexLocker locker(&m_mutex);
    for (auto it = m_requests.cbegin(); it != m_requests.cend(); ++it) {
        if (it.value().value(QStringLiteral("approval_state")).toString() == QStringLiteral("pending"))
            pending.append(it.value());
    }
    return pending;
}

QVariantMap NativeToolRegistryService::decide(const QString &requestId, bool approved) {
    QVariantMap requestValue;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_requests.find(requestId);
        if (it == m_requests.end())
            return failureEnvelope(QStringLiteral("Unknown tool request: ") + requestId);
        if (it.value().value(QStringLiteral("approval_state")).toString() != QStringLiteral("pending"))
            return failureEnvelope(QStringLiteral("Tool request is not awaiting approval"), requestId);
        it.value().insert(QStringLiteral("approval_state"),
                          approved ? QStringLiteral("approved") : QStringLiteral("rejected"));
        requestValue = it.value();
    }
    emitEvent({
        {QStringLiteral("event"), QStringLiteral("tool_approval_decided")},
        {QStringLiteral("request_id"), requestId},
        {QStringLiteral("provider_call_id"), requestValue.value(QStringLiteral("provider_call_id"))},
        {QStringLiteral("name"), requestValue.value(QStringLiteral("name"))},
        {QStringLiteral("approved"), approved}
    });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("request"), requestValue}};
}

QVariantMap NativeToolRegistryService::execute(const QString &requestId) {
    QVariantMap requestValue;
    ToolDefinition spec;
    BeforeExecute before;
    AfterExecute after;
    {
        QMutexLocker locker(&m_mutex);
        const auto requestIt = m_requests.constFind(requestId);
        if (requestIt == m_requests.cend())
            return failureEnvelope(QStringLiteral("Unknown tool request: ") + requestId, requestId);
        requestValue = requestIt.value();
        const QString approvalState = requestValue.value(QStringLiteral("approval_state")).toString();
        if (approvalState == QStringLiteral("pending"))
            return failureEnvelope(QStringLiteral("Tool request is still awaiting approval"), requestId);
        if (approvalState == QStringLiteral("rejected"))
            return failureEnvelope(QStringLiteral("Tool request was rejected"), requestId);
        const auto specIt = m_tools.constFind(requestValue.value(QStringLiteral("name")).toString());
        if (specIt == m_tools.cend())
            return failureEnvelope(QStringLiteral("Tool definition was removed"), requestId);
        spec = specIt.value();
        before = m_beforeExecute;
        after = m_afterExecute;
    }

    QVariantMap result;
    try {
        std::optional<QVariantMap> blocked;
        if (before)
            blocked = before(requestValue);
        if (blocked.has_value()) {
            result = *blocked;
        } else if (!spec.handler) {
            throw std::runtime_error("No handler registered for tool");
        } else {
            QVariantMap handlerArguments = requestValue.value(QStringLiteral("arguments")).toMap();
            handlerArguments.insert(QStringLiteral("__native_approval_granted"),
                requestValue.value(QStringLiteral("approval_state")).toString() == QStringLiteral("approved"));
            result = spec.handler(handlerArguments);
        }
        if (after)
            result = after(requestValue, result);
    } catch (const std::exception &exception) {
        result = {{QStringLiteral("status"), QStringLiteral("failed")},
                  {QStringLiteral("error"), QStringLiteral("RuntimeError: ") + QString::fromUtf8(exception.what())}};
        if (after) {
            try {
                result = after(requestValue, result);
            } catch (...) {
            }
        }
        const QVariantMap event{
            {QStringLiteral("event"), QStringLiteral("tool_result")},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("provider_call_id"), requestValue.value(QStringLiteral("provider_call_id"))},
            {QStringLiteral("name"), requestValue.value(QStringLiteral("name"))},
            {QStringLiteral("failed"), true},
            {QStringLiteral("result"), result}
        };
        emitEvent(event);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("request_id"), requestId},
                {QStringLiteral("result"), result},
                {QStringLiteral("error"), result.value(QStringLiteral("error"))}};
    } catch (...) {
        result = {{QStringLiteral("status"), QStringLiteral("failed")},
                  {QStringLiteral("error"), QStringLiteral("RuntimeError: unknown tool exception")}};
        if (after) {
            try {
                result = after(requestValue, result);
            } catch (...) {
            }
        }
        emitEvent({
            {QStringLiteral("event"), QStringLiteral("tool_result")},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("provider_call_id"), requestValue.value(QStringLiteral("provider_call_id"))},
            {QStringLiteral("name"), requestValue.value(QStringLiteral("name"))},
            {QStringLiteral("failed"), true},
            {QStringLiteral("result"), result}
        });
        return {{QStringLiteral("ok"), false}, {QStringLiteral("request_id"), requestId},
                {QStringLiteral("result"), result},
                {QStringLiteral("error"), result.value(QStringLiteral("error"))}};
    }

    {
        QMutexLocker locker(&m_mutex);
        m_results.insert(requestId, result);
    }
    emitEvent({
        {QStringLiteral("event"), QStringLiteral("tool_result")},
        {QStringLiteral("request_id"), requestId},
        {QStringLiteral("provider_call_id"), requestValue.value(QStringLiteral("provider_call_id"))},
        {QStringLiteral("name"), requestValue.value(QStringLiteral("name"))},
        {QStringLiteral("failed"), failedResult(result)},
        {QStringLiteral("result"), result}
    });
    const bool failed = failedResult(result);
    QVariantMap response{{QStringLiteral("ok"), !failed},
                         {QStringLiteral("request_id"), requestId},
                         {QStringLiteral("result"), result}};
    if (failed)
        response.insert(QStringLiteral("error"), failureMessage(result));
    return response;
}

QVariantMap NativeToolRegistryService::completedResults() const {
    QMutexLocker locker(&m_mutex);
    QVariantMap completed;
    for (auto it = m_results.cbegin(); it != m_results.cend(); ++it)
        completed.insert(it.key(), it.value());
    return completed;
}

QVariantList NativeToolRegistryService::completedCalls() const {
    QMutexLocker locker(&m_mutex);
    QVariantList calls;
    for (auto it = m_results.cbegin(); it != m_results.cend(); ++it) {
        const auto requestIt = m_requests.constFind(it.key());
        if (requestIt == m_requests.cend())
            continue;
        calls.append(QVariantMap{
            {QStringLiteral("request_id"), it.key()},
            {QStringLiteral("provider_call_id"), requestIt.value().value(QStringLiteral("provider_call_id"))},
            {QStringLiteral("name"), requestIt.value().value(QStringLiteral("name"))},
            {QStringLiteral("arguments"), requestIt.value().value(QStringLiteral("arguments"))},
            {QStringLiteral("result"), it.value()}
        });
    }
    return calls;
}

void NativeToolRegistryService::emitEvent(const QVariantMap &event) const {
    EventCallback callback;
    {
        QMutexLocker locker(&m_mutex);
        callback = m_eventCallback;
    }
    if (callback)
        callback(event);
}

QVariantList NativeToolRegistryService::responsesToolDefinitions() const {
    QMutexLocker locker(&m_mutex);
    QVariantList definitions;
    for (auto it = m_tools.cbegin(); it != m_tools.cend(); ++it) {
        definitions.append(QVariantMap{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("name"), it.key()},
            {QStringLiteral("description"), it.value().description},
            {QStringLiteral("parameters"), responsesStrictSchema(it.value().parameters)},
            {QStringLiteral("strict"), true}
        });
    }
    return definitions;
}

QVariantMap NativeToolRegistryService::responsesStrictSchema(const QVariantMap &schema) {
    return strictSchemaMap(schema);
}

QVariantMap NativeToolRegistryService::normalizedArguments(const QVariantMap &arguments,
                                                             const QVariantMap &schema) {
    return normalizeOptionalNulls(arguments, schema).toMap();
}

bool NativeToolRegistryService::validateArguments(const QVariantMap &arguments,
                                                   const QVariantMap &schema,
                                                   QString *error) {
    if (schema.value(QStringLiteral("type")).toString() != QStringLiteral("object"))
        return fail(error, QStringLiteral("Tool parameters schema must be an object schema"));
    const QVariantMap normalized = normalizedArguments(arguments, schema);
    return validateValue(normalized, schema, QStringLiteral("arguments"), error);
}
