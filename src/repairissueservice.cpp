#include "repairissueservice.h"

#include <QVariantList>

namespace {

QVariantList stringEnum(const QStringList &values) {
    QVariantList result;
    result.reserve(values.size());
    for (const QString &value : values)
        result.append(value);
    return result;
}

}

QVariantMap RepairIssueService::toolSpec() {
    const QVariantMap properties{
        {QStringLiteral("issue_id"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("title"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("hypothesis"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("evidence_ids"), QVariantMap{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}}}},
        {QStringLiteral("proposed_change"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("expected_result"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("next_action"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("status"), QVariantMap{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), stringEnum({QStringLiteral("investigating"),
                                                  QStringLiteral("ready_to_patch"),
                                                  QStringLiteral("blocked"),
                                                  QStringLiteral("hypothesis_rejected")})}}}
    };
    const QVariantMap parameters{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), stringEnum({QStringLiteral("issue_id"),
                                                  QStringLiteral("hypothesis"),
                                                  QStringLiteral("evidence_ids"),
                                                  QStringLiteral("status")})},
        {QStringLiteral("additionalProperties"), false}
    };
    const QString description = QStringLiteral(
        "记录当前问题的可证伪根因假设、已读取证据 ID、拟修改和预期验证。"
        "不能把假设当结论，也不能手工标记 verified。"
        "调查已有报告和真实源码后再记录；假设被反证时写 hypothesis_rejected 并说明原因。写记录本身不算工程进展。");
    return {{QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QVariantMap{
                 {QStringLiteral("name"), QStringLiteral("update_issue")},
                 {QStringLiteral("description"), description},
                 {QStringLiteral("parameters"), parameters}}}};
}

QVariantMap RepairIssueService::updateIssue(const QVariantMap &arguments, QVariantMap &state) {
    const QVariantList evidenceIds = arguments.value(QStringLiteral("evidence_ids")).toList();
    const QVariantMap evidence = state.value(QStringLiteral("evidence")).toMap();
    QVariantList unknown;
    for (const QVariant &evidenceId : evidenceIds) {
        if (!evidence.contains(evidenceId.toString()))
            unknown.append(evidenceId);
    }
    if (!unknown.isEmpty()) {
        const QVariantMap result{{QStringLiteral("status"), QStringLiteral("invalid_evidence")},
                                 {QStringLiteral("unknown_ids"), unknown},
                                 {QStringLiteral("updated"), false}};
        QVariantMap wrappedResult = result;
        wrappedResult.insert(QStringLiteral("state"), state);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), wrappedResult}};
    }

    if (arguments.value(QStringLiteral("status")).toString() == QStringLiteral("ready_to_patch")
        && (evidenceIds.isEmpty()
            || arguments.value(QStringLiteral("expected_result")).toString().isEmpty()
            || arguments.value(QStringLiteral("proposed_change")).toString().isEmpty())) {
    {
        const QVariantMap result{{QStringLiteral("status"), QStringLiteral("incomplete_hypothesis")},
                                 {QStringLiteral("updated"), false},
                                 {QStringLiteral("reason"), QStringLiteral("ready_to_patch 需要真实证据、具体改法和预期验证结果。")}};
        QVariantMap wrappedResult = result;
        wrappedResult.insert(QStringLiteral("state"), state);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), wrappedResult}};
    }
    }

    const QString issueId = arguments.value(QStringLiteral("issue_id")).toString();
    QVariantMap issues = state.value(QStringLiteral("issues")).toMap();
    const QVariantMap previous = issues.value(issueId).toMap();
    QVariantList history = previous.value(QStringLiteral("history")).toList();
    if (!previous.isEmpty()
        && previous.value(QStringLiteral("hypothesis")) != arguments.value(QStringLiteral("hypothesis"))) {
        QVariantMap previousEntry = previous;
        previousEntry.remove(QStringLiteral("history"));
        history.append(previousEntry);
    }
    while (history.size() > 12)
        history.removeFirst();

    QVariantMap issue = arguments;
    issue.insert(QStringLiteral("history"), history);
    issues.insert(issueId, issue);
    state.insert(QStringLiteral("issues"), issues);
    state.insert(QStringLiteral("active_issue"), issueId);

    const QVariantMap result{{QStringLiteral("status"), QStringLiteral("recorded")},
                             {QStringLiteral("issue"), issue},
                             {QStringLiteral("updated"), true},
                             {QStringLiteral("notice"), QStringLiteral("这是工作假设，不是已证明根因；记录不代表文件已修改。")}};
    QVariantMap wrappedResult = result;
    wrappedResult.insert(QStringLiteral("state"), state);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), wrappedResult}};
}
