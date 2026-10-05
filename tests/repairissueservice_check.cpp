#include "../src/repairissueservice.h"

#include <QCoreApplication>
#include <QVariantList>

#include <cassert>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QVariantMap spec = RepairIssueService::toolSpec();
    const QVariantMap function = spec.value(QStringLiteral("function")).toMap();
    assert(spec.value(QStringLiteral("type")).toString() == QStringLiteral("function"));
    assert(function.value(QStringLiteral("name")).toString() == QStringLiteral("update_issue"));
    const QVariantMap schema = function.value(QStringLiteral("parameters")).toMap();
    assert(schema.value(QStringLiteral("additionalProperties")).toBool() == false);
    assert(schema.value(QStringLiteral("required")).toList().size() == 4);

    QVariantMap state{
        {QStringLiteral("evidence"), QVariantMap{{QStringLiteral("E-1"), QVariantMap{}}}},
        {QStringLiteral("issues"), QVariantMap{}}
    };
    QVariantMap args{
        {QStringLiteral("issue_id"), QStringLiteral("rtl-clock")},
        {QStringLiteral("title"), QStringLiteral("Clock gating" )},
        {QStringLiteral("hypothesis"), QStringLiteral("Clock enable is not propagated")},
        {QStringLiteral("evidence_ids"), QVariantList{QStringLiteral("E-1")}},
        {QStringLiteral("proposed_change"), QStringLiteral("Trace enable fanout")},
        {QStringLiteral("expected_result"), QStringLiteral("DRC no longer reports the clock")},
        {QStringLiteral("status"), QStringLiteral("ready_to_patch")}
    };

    QVariantMap response = RepairIssueService::updateIssue(args, state);
    assert(response.value(QStringLiteral("ok")).toBool());
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    assert(result.value(QStringLiteral("status")).toString() == QStringLiteral("recorded"));
    assert(result.value(QStringLiteral("updated")).toBool());
    assert(result.value(QStringLiteral("state")).toMap() == state);
    assert(state.value(QStringLiteral("active_issue")).toString() == QStringLiteral("rtl-clock"));

    QVariantMap invalidArgs = args;
    invalidArgs.insert(QStringLiteral("evidence_ids"), QVariantList{QStringLiteral("E-missing")});
    response = RepairIssueService::updateIssue(invalidArgs, state);
    result = response.value(QStringLiteral("result")).toMap();
    assert(result.value(QStringLiteral("status")).toString() == QStringLiteral("invalid_evidence"));
    assert(result.value(QStringLiteral("unknown_ids")).toList() == QVariantList{QStringLiteral("E-missing")});
    assert(!result.value(QStringLiteral("updated")).toBool());

    QVariantMap incompleteArgs = args;
    incompleteArgs.remove(QStringLiteral("expected_result"));
    response = RepairIssueService::updateIssue(incompleteArgs, state);
    result = response.value(QStringLiteral("result")).toMap();
    assert(result.value(QStringLiteral("status")).toString() == QStringLiteral("incomplete_hypothesis"));
    assert(result.contains(QStringLiteral("reason")));

    for (int i = 0; i < 14; ++i) {
        args.insert(QStringLiteral("hypothesis"), QStringLiteral("hypothesis-%1").arg(i));
        response = RepairIssueService::updateIssue(args, state);
        result = response.value(QStringLiteral("result")).toMap();
        assert(result.value(QStringLiteral("status")).toString() == QStringLiteral("recorded"));
    }
    const QVariantMap storedIssue = state.value(QStringLiteral("issues")).toMap()
                                        .value(QStringLiteral("rtl-clock")).toMap();
    assert(storedIssue.value(QStringLiteral("history")).toList().size() == 12);
    assert(storedIssue.value(QStringLiteral("history")).toList().first().toMap()
               .value(QStringLiteral("hypothesis")).toString() == QStringLiteral("hypothesis-1"));
    assert(storedIssue.value(QStringLiteral("history")).toList().last().toMap()
               .value(QStringLiteral("hypothesis")).toString() == QStringLiteral("hypothesis-12"));
    return 0;
}
