#include "../src/repairruntimestateservice.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>

namespace {
QVariantMap snapshot(const QString &base = QStringLiteral("base-1"),
                     const QString &key = QStringLiteral("run-key-1")) {
    return {{QStringLiteral("base"), base}, {QStringLiteral("key"), key},
            {QStringLiteral("complete"), true}, {QStringLiteral("options"), QVariantMap{}}};
}

QString sha256(const QByteArray &contents) {
    return QString::fromLatin1(QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());

    RepairRuntimeStateService defaults(RepairRuntimeStateService::defaultState(),
                                       temporary.filePath(QStringLiteral("default-budget")));
    const QVariantMap defaultBudget = defaults.beginTurn().value(QStringLiteral("budget")).toMap();
    assert(defaultBudget.value(QStringLiteral("maximum_calls")).toInt() == 16'384);
    assert(defaultBudget.value(QStringLiteral("maximum_investigation")).toInt() == 16'384);
    assert(defaultBudget.value(QStringLiteral("maximum_eda_runs")).toInt() == 500);

    const QString artifacts = temporary.filePath(QStringLiteral("artifacts"));

    // Editing is not coupled to a specific read tool; the patch backend validates context.
    const QString projectRoot = temporary.filePath(QStringLiteral("project"));
    assert(QDir().mkpath(projectRoot));
    const QString sourcePath = QDir(projectRoot).filePath(QStringLiteral("rtl/core.v"));
    assert(QDir().mkpath(QFileInfo(sourcePath).absolutePath()));
    const QByteArray original = "module core;\nwire a;\nendmodule\n";
    {
        QFile source(sourcePath);
        assert(source.open(QIODevice::WriteOnly));
        assert(source.write(original) == original.size());
    }
    RepairRuntimeStateService readGate(RepairRuntimeStateService::defaultState(),
                                       temporary.filePath(QStringLiteral("read-gate")));
    QVariantMap gateSnapshot = snapshot();
    gateSnapshot.insert(QStringLiteral("project_root"), projectRoot);
    const QVariantMap gatePatch{{QStringLiteral("files"), QStringList{QStringLiteral("rtl/core.v")}},
        {QStringLiteral("patch"), QStringLiteral("--- a/rtl/core.v\n+++ b/rtl/core.v\n@@ -2,1 +2,1 @@\n-wire a;\n+wire b;\n")}};
    QVariantMap gate = readGate.beforeAction(QStringLiteral("unread-patch"), QStringLiteral("apply_patch"),
                                             gatePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    gate = readGate.beforeAction(QStringLiteral("unread-patch-retry"), QStringLiteral("apply_patch"),
                                 gatePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    const QVariantMap readResult{{QStringLiteral("path"), sourcePath}, {QStringLiteral("sha256"), sha256(original)},
                                 {QStringLiteral("start_line"), 1}, {QStringLiteral("end_line"), 3},
                                 {QStringLiteral("text"), QStringLiteral("1: module core;\n2: wire a;\n3: endmodule")}};
    readGate.beforeAction(QStringLiteral("read-source"), QStringLiteral("read_file"),
                          {{QStringLiteral("path"), sourcePath}}, gateSnapshot);
    readGate.afterAction(QStringLiteral("read-source"), readResult, gateSnapshot);
    gate = readGate.beforeAction(QStringLiteral("read-patch"), QStringLiteral("apply_patch"),
                                 gatePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    {
        QFile source(sourcePath);
        assert(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        assert(source.write("module core;\nwire changed;\nendmodule\n") > 0);
    }
    gate = readGate.beforeAction(QStringLiteral("stale-patch"), QStringLiteral("apply_patch"),
                                 gatePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    readGate.beforeAction(QStringLiteral("refresh-stale-source"), QStringLiteral("read_file"),
                          {{QStringLiteral("path"), sourcePath}}, gateSnapshot);
    readGate.afterAction(QStringLiteral("refresh-stale-source"),
        {{QStringLiteral("path"), sourcePath},
         {QStringLiteral("sha256"), sha256("module core;\nwire changed;\nendmodule\n")},
         {QStringLiteral("start_line"), 1}, {QStringLiteral("end_line"), 3},
         {QStringLiteral("text"), QStringLiteral("1: module core;\n2: wire changed;\n3: endmodule")}}, gateSnapshot);
    gate = readGate.beforeAction(QStringLiteral("stale-patch-after-read"), QStringLiteral("apply_patch"),
                                 gatePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    const QVariantMap addFilePatch{{QStringLiteral("files"), QStringList{QStringLiteral("rtl/new_core.v")}},
        {QStringLiteral("patch"), QStringLiteral("--- /dev/null\n+++ b/rtl/new_core.v\n@@ -0,0 +1,2 @@\n+module new_core;\n+endmodule\n")}};
    gate = readGate.beforeAction(QStringLiteral("add-file"), QStringLiteral("apply_patch"),
                                 addFilePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());

    // Models sometimes serialize POSIX path underscores as `\\_`. The source
    // read and patch guard must resolve that spelling to the same file.
    const QString underscorePath = QDir(projectRoot).filePath(QStringLiteral("rtl/core_module.v"));
    const QByteArray underscoreSource("module core_module;\nwire a;\nendmodule\n");
    {
        QFile source(underscorePath);
        assert(source.open(QIODevice::WriteOnly));
        assert(source.write(underscoreSource) == underscoreSource.size());
    }
    readGate.beforeAction(QStringLiteral("read-underscore-source"), QStringLiteral("read_file"),
                          {{QStringLiteral("path"), underscorePath}}, gateSnapshot);
    readGate.afterAction(QStringLiteral("read-underscore-source"),
        {{QStringLiteral("path"), underscorePath}, {QStringLiteral("sha256"), sha256(underscoreSource)},
         {QStringLiteral("start_line"), 1}, {QStringLiteral("end_line"), 3},
         {QStringLiteral("text"), QStringLiteral("1: module core_module;\n2: wire a;\n3: endmodule")}}, gateSnapshot);
    QString escapedUnderscorePath = underscorePath;
    escapedUnderscorePath.replace(QStringLiteral("_"), QStringLiteral("\\_"));
    const QVariantMap escapedUnderscorePatch{
        {QStringLiteral("files"), QStringList{escapedUnderscorePath}},
        {QStringLiteral("patch"), QStringLiteral("--- %1\n+++ %1\n@@ -2,1 +2,1 @@\n-wire a;\n+wire b;\n")
             .arg(escapedUnderscorePath)}};
    gate = readGate.beforeAction(QStringLiteral("escaped-underscore-patch"), QStringLiteral("apply_patch"),
                                 escapedUnderscorePatch, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    const QVariantMap mismatchedDiffHeader{
        {QStringLiteral("files"), QStringList{underscorePath}},
        {QStringLiteral("patch"), QStringLiteral("--- a/rtl/core_module.v_typo\n+++ b/rtl/core_module.v\n"
            "@@ -2,1 +2,1 @@\n-wire a;\n+wire b;\n")}};
    gate = readGate.beforeAction(QStringLiteral("normalized-diff-header"), QStringLiteral("apply_patch"),
                                 mismatchedDiffHeader, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());
    const QVariantMap malformedCodexPath{
        {QStringLiteral("files"), QStringList{QStringLiteral("rtl/core_module.v")}},
        {QStringLiteral("patch"), QStringLiteral(
            "*** Begin Patch\n*** Update File: rtl/core_module\\. v\n"
            "@@ -2,1 +2,1 @@\n-wire a;\n+wire b;\n*** End Patch")}};
    gate = readGate.beforeAction(QStringLiteral("malformed-codex-path"), QStringLiteral("apply_patch"),
                                 malformedCodexPath, gateSnapshot);
    assert(gate.value(QStringLiteral("allowed")).toBool());

    RepairRuntimeStateService::Limits limits;
    limits.toolCalls = 16;
    limits.stalledCalls = 3;
    limits.investigationCalls = 8;
    limits.edaRuns = 4;
    RepairRuntimeStateService service(RepairRuntimeStateService::defaultState(), artifacts, limits);

    auto allowed = service.beforeAction(QStringLiteral("read-1"), QStringLiteral("inspect_project"), {}, snapshot());
    assert(allowed.value(QStringLiteral("allowed")).toBool());
    auto observed = service.afterAction(QStringLiteral("read-1"),
                                       {{QStringLiteral("project_id"), QStringLiteral("demo")},
                                        {QStringLiteral("top"), QStringLiteral("core")}}, snapshot());
    const QString evidenceId = observed.value(QStringLiteral("evidence_id")).toString();
    assert(evidenceId.startsWith(QStringLiteral("E-")));
    assert(observed.value(QStringLiteral("repair_progress")).toMap()
               .value(QStringLiteral("new_evidence")).toBool());

    allowed = service.beforeAction(QStringLiteral("repeat-1"), QStringLiteral("inspect_project"), {}, snapshot());
    assert(allowed.value(QStringLiteral("status")).toString() == QStringLiteral("repeated_action_blocked"));
    assert(allowed.value(QStringLiteral("same_evidence_id")).toString() == evidenceId);
    const QString repeatGuidance = allowed.value(QStringLiteral("agent_guidance")).toString();
    assert(!repeatGuidance.contains(QStringLiteral("run_dft_flow")));
    assert(!repeatGuidance.contains(QStringLiteral("read_file")));
    assert(repeatGuidance.contains(QStringLiteral("新证据")));
    service.beforeAction(QStringLiteral("repeat-2"), QStringLiteral("inspect_project"), {}, snapshot());
    const QVariantMap stalledRepeat = service.beforeAction(QStringLiteral("repeat-3"), QStringLiteral("inspect_project"), {}, snapshot());
    assert(stalledRepeat.value(QStringLiteral("status")).toString() == QStringLiteral("repeated_action_stalled"));
    assert(service.state().value(QStringLiteral("stop_reason")).toString()
           == QStringLiteral("repeated_action_without_progress"));
    assert(QFile::exists(service.checkpointPath()));

    QString loadError;
    QVariantMap reopened = RepairRuntimeStateService::loadCheckpoint(service.checkpointPath(), &loadError);
    assert(loadError.isEmpty());
    assert(reopened.value(QStringLiteral("evidence")).toMap().contains(evidenceId));
    assert(reopened.value(QStringLiteral("stop_reason")).toString()
           == QStringLiteral("repeated_action_without_progress"));
    RepairRuntimeStateService resumed(reopened, temporary.filePath(QStringLiteral("resumed")), limits);
    assert(resumed.state().value(QStringLiteral("stop_reason")).toString().isEmpty());
    allowed = resumed.beforeAction(QStringLiteral("read-after-resume"), QStringLiteral("inspect_project"), {}, snapshot());
    assert(allowed.value(QStringLiteral("allowed")).toBool());

    // A failed patch signature is persisted against the exact input base.
    const QVariantMap patchArgs{{QStringLiteral("files"), QStringList{QStringLiteral("rtl/core.v")}},
                                {QStringLiteral("patch"), QStringLiteral("*** Begin Patch\n*** Update File: rtl/core.v\n@@\n-a\n+b\n*** End Patch")}};
    allowed = resumed.beforeAction(QStringLiteral("patch-1"), QStringLiteral("apply_patch"), patchArgs, snapshot());
    assert(allowed.value(QStringLiteral("allowed")).toBool());
    resumed.afterAction(QStringLiteral("patch-1"),
                        {{QStringLiteral("edited"), false}, {QStringLiteral("status"), QStringLiteral("blocked")},
                         {QStringLiteral("reason"), QStringLiteral("fixture rejection")}}, snapshot());
    const QVariantMap failedRetry = resumed.beforeAction(QStringLiteral("patch-2"), QStringLiteral("apply_patch"), patchArgs, snapshot());
    assert(failedRetry.value(QStringLiteral("status")).toString() == QStringLiteral("repeated_action_blocked"));
    assert(failedRetry.value(QStringLiteral("previous_error")).toString() == QStringLiteral("fixture rejection"));

    const QVariantMap prerequisitePatchArgs{{QStringLiteral("files"), QStringList{QStringLiteral("rtl/core.v")} },
        {QStringLiteral("patch"), QStringLiteral("--- a/rtl/core.v\n+++ b/rtl/core.v\n@@ -1 +1 @@\n-a\n+b\n")}};
    auto prerequisiteAllowed = resumed.beforeAction(QStringLiteral("patch-prerequisite-1"),
        QStringLiteral("apply_patch"), prerequisitePatchArgs, snapshot());
    assert(prerequisiteAllowed.value(QStringLiteral("allowed")).toBool());
    const int stalledBeforeSkillLoad = resumed.state().value(QStringLiteral("budget")).toMap()
        .value(QStringLiteral("stalled")).toInt();
    resumed.afterAction(QStringLiteral("patch-prerequisite-1"),
        {{QStringLiteral("status"), QStringLiteral("skill_required")},
         {QStringLiteral("skill_id"), QStringLiteral("dft-rtl-editing")}}, snapshot());
    assert(resumed.state().value(QStringLiteral("budget")).toMap()
               .value(QStringLiteral("stalled")).toInt() == stalledBeforeSkillLoad);
    prerequisiteAllowed = resumed.beforeAction(QStringLiteral("patch-prerequisite-2"),
        QStringLiteral("apply_patch"), prerequisitePatchArgs, snapshot());
    assert(prerequisiteAllowed.value(QStringLiteral("allowed")).toBool());
    resumed.afterAction(QStringLiteral("patch-prerequisite-2"),
        {{QStringLiteral("status"), QStringLiteral("evidence_required")},
         {QStringLiteral("path"), QStringLiteral("/tmp/stage.json")}}, snapshot());
    assert(resumed.state().value(QStringLiteral("budget")).toMap()
               .value(QStringLiteral("stalled")).toInt() == stalledBeforeSkillLoad);
    assert(resumed.beforeAction(QStringLiteral("report-prerequisite"), QStringLiteral("save_run_report"),
                                {}, snapshot()).value(QStringLiteral("allowed")).toBool());
    resumed.afterAction(QStringLiteral("report-prerequisite"),
        {{QStringLiteral("status"), QStringLiteral("skill_required")},
         {QStringLiteral("skill_id"), QStringLiteral("scientific-report-writing")}}, snapshot());
    assert(resumed.state().value(QStringLiteral("budget")).toMap()
               .value(QStringLiteral("stalled")).toInt() == stalledBeforeSkillLoad);
    prerequisiteAllowed = resumed.beforeAction(QStringLiteral("patch-prerequisite-3"),
        QStringLiteral("apply_patch"), prerequisitePatchArgs, snapshot());
    assert(prerequisiteAllowed.value(QStringLiteral("allowed")).toBool());

    // Generic failed calls get one same-input retry, then explain why an exact replay is blocked.
    RepairRuntimeStateService failedTool(RepairRuntimeStateService::defaultState(),
                                         temporary.filePath(QStringLiteral("failed-tool")));
    const QVariantMap shellArgs{{QStringLiteral("command"), QStringLiteral("cd /missing && pwd")}};
    auto shellGate = failedTool.beforeAction(QStringLiteral("shell-fail-1"), QStringLiteral("shell"),
                                             shellArgs, snapshot());
    assert(shellGate.value(QStringLiteral("allowed")).toBool());
    failedTool.afterAction(QStringLiteral("shell-fail-1"),
        {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("working directory not found")}},
        snapshot());
    shellGate = failedTool.beforeAction(QStringLiteral("shell-fail-2"), QStringLiteral("shell"),
                                        shellArgs, snapshot());
    assert(shellGate.value(QStringLiteral("allowed")).toBool());
    failedTool.afterAction(QStringLiteral("shell-fail-2"),
        {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("working directory not found")}},
        snapshot());
    shellGate = failedTool.beforeAction(QStringLiteral("shell-fail-3"), QStringLiteral("shell"),
                                        shellArgs, snapshot());
    assert(shellGate.value(QStringLiteral("status")).toString() == QStringLiteral("repeated_action_blocked"));
    assert(shellGate.value(QStringLiteral("previous_error")).toString()
        == QStringLiteral("working directory not found"));
    const QString failureGuidance = shellGate.value(QStringLiteral("agent_guidance")).toString();
    assert(failureGuidance.contains(QStringLiteral("previous_error")));
    assert(!shellGate.contains(QStringLiteral("next_action")));
    shellGate = failedTool.beforeAction(QStringLiteral("shell-revised"), QStringLiteral("shell"),
        {{QStringLiteral("command"), QStringLiteral("pwd")}}, snapshot());
    assert(shellGate.value(QStringLiteral("allowed")).toBool());

    // Same-input DFT work is reserved before execution, then deduplicated.
    resumed.beginTurn();
    auto runAllowed = resumed.beforeAction(QStringLiteral("run-1"), QStringLiteral("run_dft_flow"), {}, snapshot());
    assert(runAllowed.value(QStringLiteral("allowed")).toBool());
    const QVariantMap duplicateRun = resumed.beforeAction(QStringLiteral("run-2"), QStringLiteral("run_dft_flow"), {}, snapshot());
    assert(duplicateRun.value(QStringLiteral("status")).toString() == QStringLiteral("unchanged_inputs"));

    const QVariantMap runResult{{QStringLiteral("executed"), true},
                                {QStringLiteral("run_id"), QStringLiteral("run-1")},
                                {QStringLiteral("workspace"), QStringLiteral("/tmp/work/run-1")},
                                {QStringLiteral("cross_validation"), QVariantMap{{QStringLiteral("status"), QStringLiteral("verified")}}}};
    const QVariantMap completed = resumed.afterAction(QStringLiteral("run-1"), runResult, snapshot());
    assert(completed.value(QStringLiteral("repair_progress")).toMap()
               .value(QStringLiteral("new_controlled_run")).toBool());
    assert(resumed.executionState(snapshot()) == QStringLiteral("evidence_verified"));

    // A background job's original input snapshot survives a process restart.
    const QVariantMap jobSnapshot = snapshot(QStringLiteral("job-base"), QStringLiteral("job-key"));
    assert(resumed.beforeAction(QStringLiteral("job-start"), QStringLiteral("run_dft_flow"), {}, jobSnapshot)
               .value(QStringLiteral("allowed")).toBool());
    resumed.afterAction(QStringLiteral("job-start"),
                        {{QStringLiteral("job_id"), QStringLiteral("job-7")},
                         {QStringLiteral("state"), QStringLiteral("running")}}, jobSnapshot);
    reopened = RepairRuntimeStateService::loadCheckpoint(resumed.checkpointPath(), &loadError);
    RepairRuntimeStateService resumedJob(reopened, temporary.filePath(QStringLiteral("resumed-job")), limits);
    assert(resumedJob.beforeAction(QStringLiteral("wait-1"), QStringLiteral("wait_dft_job"),
                                   {{QStringLiteral("job_id"), QStringLiteral("job-7")}}, jobSnapshot)
               .value(QStringLiteral("allowed")).toBool());
    const QVariantMap jobDone = resumedJob.afterAction(QStringLiteral("wait-1"),
        {{QStringLiteral("executed"), true}, {QStringLiteral("run_id"), QStringLiteral("job-run-7")},
         {QStringLiteral("workspace"), QStringLiteral("/tmp/work/job-7")},
         {QStringLiteral("state"), QStringLiteral("completed")},
         {QStringLiteral("cross_validation"), QVariantMap{{QStringLiteral("status"), QStringLiteral("verified")}}}}, jobSnapshot);
    assert(jobDone.value(QStringLiteral("repair_progress")).toMap()
               .value(QStringLiteral("new_controlled_run")).toBool());
    assert(resumedJob.executionState(jobSnapshot) == QStringLiteral("evidence_verified"));

    const QVariantMap nativeJobSnapshot = snapshot(QStringLiteral("native-job-base"), QStringLiteral("native-job-key"));
    RepairRuntimeStateService nativeJob(RepairRuntimeStateService::defaultState(),
                                        temporary.filePath(QStringLiteral("native-job")), limits);
    assert(nativeJob.beforeAction(QStringLiteral("native-job-start"), QStringLiteral("run_dft_flow"), {}, nativeJobSnapshot)
               .value(QStringLiteral("allowed")).toBool());
    nativeJob.afterAction(QStringLiteral("native-job-start"),
        {{QStringLiteral("job_id"), QStringLiteral("native-job-8")},
         {QStringLiteral("state"), QStringLiteral("running")}}, nativeJobSnapshot);
    assert(nativeJob.beforeAction(QStringLiteral("native-wait"), QStringLiteral("wait_dft_job"),
                                  {{QStringLiteral("job_id"), QStringLiteral("native-job-8")}}, nativeJobSnapshot)
               .value(QStringLiteral("allowed")).toBool());
    const QVariantMap nativeJobDone = nativeJob.afterAction(QStringLiteral("native-wait"),
        {{QStringLiteral("job_id"), QStringLiteral("native-job-8")},
         {QStringLiteral("state"), QStringLiteral("completed")},
         {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
             {QStringLiteral("result"), QVariantMap{
                 {QStringLiteral("status"), QStringLiteral("verified")},
                 {QStringLiteral("execution"), QVariantMap{
                     {QStringLiteral("completed_cleanly"), true},
                     {QStringLiteral("returncode"), 0},
                     {QStringLiteral("workspace"), QStringLiteral("/tmp/work/native-job-8")},
                 }},
                 {QStringLiteral("verification"), QVariantMap{
                     {QStringLiteral("status"), QStringLiteral("verified")},
                 }},
             }}}}}, nativeJobSnapshot);
    const QVariantMap nativeProgress = nativeJobDone.value(QStringLiteral("repair_progress")).toMap();
    assert(nativeProgress.value(QStringLiteral("new_controlled_run")).toBool());
    assert(nativeProgress.value(QStringLiteral("stalled_calls")).toInt() == 0);
    assert(nativeJob.state().value(QStringLiteral("last_run")).toMap()
               .value(QStringLiteral("verified")).toBool());

    // A genuinely changed input advances the revision and marks prior evidence stale.
    resumed.beforeAction(QStringLiteral("edit-1"), QStringLiteral("apply_patch"),
                         {{QStringLiteral("patch"), QStringLiteral("different")}}, snapshot());
    resumed.afterAction(QStringLiteral("edit-1"), {{QStringLiteral("edited"), true}}, snapshot(QStringLiteral("base-2")));
    assert(resumed.state().value(QStringLiteral("revision")).toInt() == 1);
    assert(resumed.executionState(snapshot(QStringLiteral("base-2"))) == QStringLiteral("verification_required"));

    // Non-informative repeated actions consume stall budget and persist pause.
    RepairRuntimeStateService stalled(RepairRuntimeStateService::defaultState(),
                                      temporary.filePath(QStringLiteral("stalled")), limits);
    for (int index = 0; index < 3; ++index) {
        const QString id = QStringLiteral("noop-%1").arg(index);
        assert(stalled.beforeAction(id, QStringLiteral("update_studio_project"), {}, snapshot())
                   .value(QStringLiteral("allowed")).toBool());
        const QVariantMap output = stalled.afterAction(id, {{QStringLiteral("updated"), false}}, snapshot());
        if (index == 2)
            assert(output.value(QStringLiteral("stop_reason")).toString() == QStringLiteral("no_progress"));
    }
    assert(stalled.state().value(QStringLiteral("stop_reason")).toString() == QStringLiteral("no_progress"));
    return 0;
}
