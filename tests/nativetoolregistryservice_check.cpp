#include "../src/nativetoolregistryservice.h"

#include <QCoreApplication>
#include <QVariantList>

#include <cassert>
#include <stdexcept>

namespace {

QVariantMap objectSchema() {
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QVariantMap{
            {QStringLiteral("mode"), QVariantMap{
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("enum"), QVariantList{QStringLiteral("read"), QStringLiteral("write")}}
            }},
            {QStringLiteral("count"), QVariantMap{
                {QStringLiteral("type"), QStringLiteral("integer")},
                {QStringLiteral("minimum"), 1}
            }},
            {QStringLiteral("note"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("options"), QVariantMap{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), QVariantMap{
                    {QStringLiteral("enabled"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}}
                }},
                {QStringLiteral("required"), QVariantList{QStringLiteral("enabled")}},
                {QStringLiteral("additionalProperties"), false}
            }}
        }},
        {QStringLiteral("required"), QVariantList{QStringLiteral("mode"), QStringLiteral("count"), QStringLiteral("options")}},
        {QStringLiteral("additionalProperties"), false}
    };
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    NativeToolRegistryService registry;
    QString error;
    const QVariantMap schema = objectSchema();
    assert(registry.registerTool(QStringLiteral("sample"), QStringLiteral("Sample tool"), schema, &error,
                                 QStringLiteral("read"), false,
                                 [](const QVariantMap &args) {
                                     return QVariantMap{{QStringLiteral("seen"), args.value(QStringLiteral("count"))}};
                                 }));
    assert(!registry.registerTool(QStringLiteral("sample"), QStringLiteral("duplicate"), schema, &error));

    const QVariantList definitions = registry.responsesToolDefinitions();
    assert(definitions.size() == 1);
    const QVariantMap definition = definitions.first().toMap();
    assert(definition.value(QStringLiteral("type")).toString() == QStringLiteral("function"));
    assert(definition.value(QStringLiteral("name")).toString() == QStringLiteral("sample"));
    assert(definition.value(QStringLiteral("strict")).toBool());

    const QVariantMap strict = definition.value(QStringLiteral("parameters")).toMap();
    const QVariantMap strictProperties = strict.value(QStringLiteral("properties")).toMap();
    assert(strict.value(QStringLiteral("additionalProperties")).toBool() == false);
    assert(strict.value(QStringLiteral("required")).toList().size() == 4);
    assert(strictProperties.value(QStringLiteral("note")).toMap().contains(QStringLiteral("anyOf")));
    const QVariantList optionalTypes = strictProperties.value(QStringLiteral("note")).toMap()
                                           .value(QStringLiteral("anyOf")).toList();
    assert(optionalTypes.size() == 2);
    assert(optionalTypes.first().toMap().value(QStringLiteral("type")).toString() == QStringLiteral("string"));
    assert(optionalTypes.last().toMap().value(QStringLiteral("type")).toString() == QStringLiteral("null"));
    assert(strictProperties.value(QStringLiteral("count")).toMap().value(QStringLiteral("type")).toString()
           == QStringLiteral("integer"));
    assert(strictProperties.value(QStringLiteral("mode")).toMap().value(QStringLiteral("enum")).toList().size() == 2);
    const QVariantMap strictOptions = strictProperties.value(QStringLiteral("options")).toMap();
    assert(strictOptions.value(QStringLiteral("required")).toList().size() == 1);
    assert(strictOptions.value(QStringLiteral("required")).toList().first().toString() == QStringLiteral("enabled"));
    assert(strictOptions.value(QStringLiteral("additionalProperties")).toBool() == false);
    assert(strictOptions.value(QStringLiteral("properties")).toMap().value(QStringLiteral("enabled")).toMap()
               .value(QStringLiteral("type")).toString() == QStringLiteral("boolean"));

    QVariantMap valid{
        {QStringLiteral("mode"), QStringLiteral("read")},
        {QStringLiteral("count"), 2},
        {QStringLiteral("options"), QVariantMap{{QStringLiteral("enabled"), true}}}
    };
    assert(NativeToolRegistryService::validateArguments(valid, schema, &error));

    QVariantMap optionalNull = valid;
    optionalNull.insert(QStringLiteral("note"), QVariant());
    assert(NativeToolRegistryService::validateArguments(optionalNull, schema, &error));
    assert(!NativeToolRegistryService::normalizedArguments(optionalNull, schema).contains(QStringLiteral("note")));

    QVariantMap missing = valid;
    missing.remove(QStringLiteral("count"));
    assert(!NativeToolRegistryService::validateArguments(missing, schema, &error));

    QVariantMap extra = valid;
    extra.insert(QStringLiteral("unexpected"), QStringLiteral("value"));
    assert(!NativeToolRegistryService::validateArguments(extra, schema, &error));

    QVariantMap badEnum = valid;
    badEnum.insert(QStringLiteral("mode"), QStringLiteral("execute"));
    assert(!NativeToolRegistryService::validateArguments(badEnum, schema, &error));
    assert(error.contains(QStringLiteral("read, write")));

    QVariantMap badType = valid;
    badType.insert(QStringLiteral("count"), QStringLiteral("2"));
    assert(!NativeToolRegistryService::validateArguments(badType, schema, &error));

    QVariantMap badNested = valid;
    badNested.insert(QStringLiteral("options"), QVariantMap{{QStringLiteral("enabled"), true},
                                                            {QStringLiteral("other"), 1}});
    assert(!NativeToolRegistryService::validateArguments(badNested, schema, &error));

    QVariantMap optionalSchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QVariantMap{{QStringLiteral("note"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}}}},
        {QStringLiteral("required"), QVariantList{}},
        {QStringLiteral("additionalProperties"), false}
    };
    assert(NativeToolRegistryService::validateArguments({}, optionalSchema, &error));

    QVariantList events;
    registry.setEventCallback([&events](const QVariantMap &event) { events.append(event); });
    const QVariantMap readRequest = registry.request(QStringLiteral("sample"), valid,
                                                      QStringLiteral(" inspect "), QStringLiteral("call-read"));
    assert(readRequest.value(QStringLiteral("ok")).toBool());
    const QVariantMap read = readRequest.value(QStringLiteral("request")).toMap();
    const QString readId = read.value(QStringLiteral("id")).toString();
    assert(readId.size() == 32);
    assert(read.value(QStringLiteral("risk")).toString() == QStringLiteral("read"));
    assert(!read.value(QStringLiteral("requires_approval")).toBool());
    assert(read.value(QStringLiteral("approval_state")).toString() == QStringLiteral("not_required"));
    assert(read.value(QStringLiteral("reason")).toString() == QStringLiteral("inspect"));
    assert(events.last().toMap().value(QStringLiteral("event")).toString() == QStringLiteral("tool_call"));
    QVariantMap executed = registry.execute(readId);
    assert(executed.value(QStringLiteral("ok")).toBool());
    assert(executed.value(QStringLiteral("result")).toMap().value(QStringLiteral("seen")).toInt() == 2);
    assert(events.last().toMap().value(QStringLiteral("event")).toString() == QStringLiteral("tool_result"));
    assert(!events.last().toMap().value(QStringLiteral("failed")).toBool());
    assert(registry.completedResults().contains(readId));
    assert(registry.completedCalls().size() == 1);

    int approvalHandlerCalls = 0;
    assert(registry.registerTool(QStringLiteral("change"), QStringLiteral("Change tool"), schema, &error,
                                 QStringLiteral("change"), true,
                                 [&approvalHandlerCalls](const QVariantMap &) {
                                     ++approvalHandlerCalls;
                                     return QVariantMap{{QStringLiteral("status"), QStringLiteral("done")}};
                                 }));
    const QVariantMap deniedRequest = registry.request(QStringLiteral("change"), valid, {}, QStringLiteral("call-denied"));
    const QVariantMap denied = deniedRequest.value(QStringLiteral("request")).toMap();
    const QString deniedId = denied.value(QStringLiteral("id")).toString();
    assert(denied.value(QStringLiteral("requires_approval")).toBool());
    assert(denied.value(QStringLiteral("approval_state")).toString() == QStringLiteral("pending"));
    assert(registry.pendingRequests().size() == 1);
    assert(!registry.execute(deniedId).value(QStringLiteral("ok")).toBool());
    assert(approvalHandlerCalls == 0);
    assert(registry.decide(deniedId, false).value(QStringLiteral("ok")).toBool());
    assert(registry.pendingRequests().isEmpty());
    assert(!registry.execute(deniedId).value(QStringLiteral("ok")).toBool());
    assert(!registry.decide(deniedId, true).value(QStringLiteral("ok")).toBool());
    assert(approvalHandlerCalls == 0);

    const QVariantMap approvedRequest = registry.request(QStringLiteral("change"), valid, {}, QStringLiteral("call-approved"));
    const QString approvedId = approvedRequest.value(QStringLiteral("request")).toMap().value(QStringLiteral("id")).toString();
    assert(registry.decide(approvedId, true).value(QStringLiteral("ok")).toBool());
    assert(registry.execute(approvedId).value(QStringLiteral("ok")).toBool());
    assert(registry.execute(approvedId).value(QStringLiteral("ok")).toBool());
    assert(approvalHandlerCalls == 2); // Matching DftToolApi: execution is not one-shot.
    assert(registry.completedCalls().size() == 2); // One latest result per request ID.

    assert(registry.registerTool(QStringLiteral("throws"), QStringLiteral("Failing tool"), schema, &error,
                                 QStringLiteral("execute"), false,
                                 [](const QVariantMap &) -> QVariantMap { throw std::runtime_error("boom"); }));
    const QVariantMap failingRequest = registry.request(QStringLiteral("throws"), valid);
    const QString failingId = failingRequest.value(QStringLiteral("request")).toMap().value(QStringLiteral("id")).toString();
    const int priorResultCount = registry.completedResults().size();
    const QVariantMap failure = registry.execute(failingId);
    assert(!failure.value(QStringLiteral("ok")).toBool());
    assert(failure.value(QStringLiteral("error")).toString().contains(QStringLiteral("boom")));
    assert(events.last().toMap().value(QStringLiteral("failed")).toBool());
    assert(!registry.completedResults().contains(failingId)); // Python records raised failures as events, not completed results.
    assert(registry.completedResults().size() == priorResultCount);

    for (const QString &status : {QStringLiteral("skill_required"), QStringLiteral("evidence_required"),
                                  QStringLiteral("approval_required"), QStringLiteral("approval_rejected"),
                                  QStringLiteral("repeated_action_blocked"), QStringLiteral("unchanged_inputs"),
                                  QStringLiteral("blocked"), QStringLiteral("rejected"),
                                  QStringLiteral("tool_compatibility_edit_blocked")}) {
        const QString toolName = QStringLiteral("status_") + status;
        assert(registry.registerTool(toolName, QStringLiteral("Status fixture"), schema, &error,
                                     QStringLiteral("read"), false,
                                     [status](const QVariantMap &) {
                                         return QVariantMap{{QStringLiteral("status"), status},
                                                            {QStringLiteral("action_completed"), false},
                                                            {QStringLiteral("message"), QStringLiteral("Required next step.")}};
                                     }));
        const QVariantMap request = registry.request(toolName, valid);
        const QString requestId = request.value(QStringLiteral("request")).toMap()
                                      .value(QStringLiteral("id")).toString();
        const QVariantMap response = registry.execute(requestId);
        assert(!response.value(QStringLiteral("ok")).toBool());
        assert(response.value(QStringLiteral("error")).toString() == QStringLiteral("Required next step."));
        assert(response.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString() == status);
        assert(events.last().toMap().value(QStringLiteral("failed")).toBool());
    }

    const int eventCount = events.size();
    assert(!registry.request(QStringLiteral("sample"), {{QStringLiteral("unknown"), 1}})
                .value(QStringLiteral("ok")).toBool());
    assert(events.size() == eventCount); // Invalid arguments are rejected before a tool-call event.
    return 0;
}
