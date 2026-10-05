#include "repairrunevidenceservice.h"

#include <QCoreApplication>
#include <QVariantList>

#include <cassert>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QVariantMap envelope{
        {QStringLiteral("executed"), true},
        {QStringLiteral("adapter_tag"), QStringLiteral("keep-me")},
        {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("execution"), QVariantMap{
                {QStringLiteral("selected_run_id"), QStringLiteral("run-42")},
                {QStringLiteral("workspace"), QStringLiteral("/tmp/work/run-42")},
            }},
        }},
    };
    const auto records = RepairRunEvidenceService::controlledRunPayloads(envelope);
    assert(records.size() == 1);
    assert(records.first().value(QStringLiteral("run_id")).toString() == QStringLiteral("run-42"));
    assert(records.first().value(QStringLiteral("workspace")).toString() == QStringLiteral("/tmp/work/run-42"));
    assert(records.first().value(QStringLiteral("adapter_tag")).toString() == QStringLiteral("keep-me"));

    const QVariantMap nativeFlowResult{
        {QStringLiteral("status"), QStringLiteral("verified")},
        {QStringLiteral("execution"), QVariantMap{
            {QStringLiteral("completed_cleanly"), true},
            {QStringLiteral("returncode"), 0},
            {QStringLiteral("workspace"), QStringLiteral("/tmp/work/native-run-7")},
        }},
        {QStringLiteral("verification"), QVariantMap{
            {QStringLiteral("status"), QStringLiteral("verified")},
        }},
    };
    const auto nativeRecords = RepairRunEvidenceService::controlledRunPayloads(nativeFlowResult);
    assert(nativeRecords.size() == 1);
    assert(nativeRecords.first().value(QStringLiteral("workspace")).toString()
           == QStringLiteral("/tmp/work/native-run-7"));
    assert(nativeRecords.first().value(QStringLiteral("run_id")).toString() == QStringLiteral("native-run-7"));

    QVariantMap workspacePayload{{QStringLiteral("workspace"), QStringLiteral("/tmp/work/workspace-only")}};
    QVariantMap workspaceEnvelope{{QStringLiteral("executed"), true},
                                  {QStringLiteral("result"), workspacePayload}};
    const auto workspaceRecords = RepairRunEvidenceService::controlledRunPayloads(workspaceEnvelope);
    assert(workspaceRecords.size() == 1);
    assert(workspaceRecords.first().value(QStringLiteral("run_id")).toString()
           == QStringLiteral("workspace-only"));

    QVariantMap eighthHop{{QStringLiteral("workspace"), QStringLiteral("/tmp/work/too-deep")}};
    for (int index = 0; index < 8; ++index)
        eighthHop = {{QStringLiteral("result"), eighthHop}};
    const QVariantMap boundedEnvelope{{QStringLiteral("executed"), true},
                                      {QStringLiteral("result"), eighthHop}};
    assert(RepairRunEvidenceService::controlledRunPayloads(boundedEnvelope).isEmpty());

    const QVariantMap ignored{{QStringLiteral("executed"), QStringLiteral("true")},
                              {QStringLiteral("run_id"), QStringLiteral("not-a-bool")}};
    assert(RepairRunEvidenceService::controlledRunPayloads(ignored).isEmpty());

    const QVariantMap fan{
        {QStringLiteral("executed"), true},
        {QStringLiteral("backend"), QStringLiteral("fan_atpg")},
        {QStringLiteral("cross_validation"), QVariantMap{}},
        {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("candidates"), QVariantList{QStringLiteral("candidate-a")}},
            {QStringLiteral("goal"), QVariantMap{{QStringLiteral("coverage"), 99.0}}},
        }},
    };
    const auto fanRecords = RepairRunEvidenceService::controlledRunPayloads(fan);
    assert(fanRecords.size() == 1);
    assert(fanRecords.first().value(QStringLiteral("run_id")).toString()
           == QStringLiteral("fan-2ca702bf0aa04740"));

    return 0;
}
