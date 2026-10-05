#include "repairruntimestateservice.h"

#include "repairactionfingerprintservice.h"
#include "repairissueservice.h"
#include "repairrunevidenceservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace {
const QSet<QString> kEdaTools{
    QStringLiteral("run_dft_flow"), QStringLiteral("run_dft_iteration"),
    QStringLiteral("run_dft_optimization"), QStringLiteral("run_approved_patch")};
const QSet<QString> kRepeatGuardTools{
    QStringLiteral("search_project_text"), QStringLiteral("inspect_project"),
    QStringLiteral("inspect_studio_project"), QStringLiteral("analyze_dft_results"),
    QStringLiteral("diagnose_dft_failure"), QStringLiteral("check_dft_readiness")};
const QSet<QString> kReadTools{
    QStringLiteral("read_file"), QStringLiteral("read_project_excerpt"),
    QStringLiteral("search_project_text"), QStringLiteral("inspect_project"),
    QStringLiteral("inspect_studio_project"), QStringLiteral("analyze_dft_results"),
    QStringLiteral("diagnose_dft_failure")};
const QSet<QString> kEditTools{
    QStringLiteral("apply_patch"), QStringLiteral("create_file"),
    QStringLiteral("rollback_file_edit"), QStringLiteral("run_approved_patch"),
    QStringLiteral("update_studio_project")};
const QSet<QString> kVolatileKeys{
    QStringLiteral("request_id"), QStringLiteral("timestamp"), QStringLiteral("duration_ms"),
    QStringLiteral("duration_seconds"), QStringLiteral("elapsed_seconds"),
    QStringLiteral("session_id"), QStringLiteral("started_at"),
    QStringLiteral("completed_at"), QStringLiteral("created_at"),
    QStringLiteral("pid"), QStringLiteral("process_id")};

QVariant stableValue(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            if (!kVolatileKeys.contains(it.key()))
                result.insert(it.key(), stableValue(it.value()));
        }
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        for (const QVariant &item : value.toList())
            result.append(stableValue(item));
        return result;
    }
    return value;
}

bool hasBody(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (const QString &key : {QStringLiteral("content"), QStringLiteral("text"),
                                   QStringLiteral("stdout"), QStringLiteral("excerpt"),
                                   QStringLiteral("matches"), QStringLiteral("entries"),
                                   QStringLiteral("lines")}) {
            const QVariant body = map.value(key);
            if (body.isValid() && !body.isNull()
                && (!(body.metaType().id() == QMetaType::QString) || !body.toString().isEmpty())
                && (!(body.metaType().id() == QMetaType::QVariantList) || !body.toList().isEmpty())
                && (!(body.metaType().id() == QMetaType::QVariantMap) || !body.toMap().isEmpty()))
                return true;
        }
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (hasBody(it.value()))
                return true;
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &item : value.toList()) {
            if (hasBody(item))
                return true;
        }
    }
    return false;
}

bool actionablePrerequisiteStatus(const QString &status) {
    return QSet<QString>{QStringLiteral("skill_required"), QStringLiteral("evidence_required"),
                         QStringLiteral("approval_required"), QStringLiteral("approval_rejected"),
                         QStringLiteral("needs_source_read"), QStringLiteral("input_manifest_incomplete")}
        .contains(status);
}

bool failedToolResult(const QVariantMap &result) {
    if (result.contains(QStringLiteral("ok")) && !result.value(QStringLiteral("ok")).toBool())
        return true;
    const QString status = result.value(QStringLiteral("status")).toString();
    if (actionablePrerequisiteStatus(status))
        return false;
    if (QSet<QString>{QStringLiteral("failed"), QStringLiteral("blocked"),
                      QStringLiteral("rejected"), QStringLiteral("error"),
                      QStringLiteral("repeated_action_blocked"),
                      QStringLiteral("repeated_action_stalled"),
                      QStringLiteral("no_completed_result"), QStringLiteral("unchanged_inputs"),
                      QStringLiteral("tool_compatibility_edit_blocked"),
                      QStringLiteral("cross_check_blocked")}.contains(status))
        return true;
    return !result.value(QStringLiteral("error")).toString().trimmed().isEmpty();
}

QList<QVariantMap> nestedMaps(const QVariant &value) {
    QList<QVariantMap> result;
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        result.append(map);
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            result.append(nestedMaps(it.value()));
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &item : value.toList())
            result.append(nestedMaps(item));
    }
    return result;
}

void appendBounded(QVariantList &list, const QVariant &value, int limit) {
    list.append(value);
    while (list.size() > limit)
        list.removeFirst();
}

QVariantMap mapValue(const QVariantMap &map, const QString &key) {
    return map.value(key).toMap();
}

} // namespace

RepairRuntimeStateService::RepairRuntimeStateService(QVariantMap state, QString artifactRoot)
    : RepairRuntimeStateService(std::move(state), std::move(artifactRoot), Limits{}) {}

RepairRuntimeStateService::RepairRuntimeStateService(QVariantMap state, QString artifactRoot,
                                                     Limits limits)
    : m_state(std::move(state)), m_artifactRoot(QDir::cleanPath(std::move(artifactRoot))),
      m_limits(limits) {
    if (m_limits.toolCalls < 4) m_limits.toolCalls = 4;
    if (m_limits.stalledCalls < 3) m_limits.stalledCalls = 3;
    if (m_limits.investigationCalls < 8) m_limits.investigationCalls = 8;
    if (m_limits.edaRuns < 1) m_limits.edaRuns = 1;
    const QVariantMap defaults = defaultState();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        if (!m_state.contains(it.key()))
            m_state.insert(it.key(), it.value());
    }
    m_state.insert(QStringLiteral("stop_reason"), QString{});
}

QVariantMap RepairRuntimeStateService::defaultState() {
    return {{QStringLiteral("version"), 1}, {QStringLiteral("issues"), QVariantMap{}},
            {QStringLiteral("evidence"), QVariantMap{}},
            {QStringLiteral("runs"), QVariantMap{}}, {QStringLiteral("changes"), QVariantList{}},
            {QStringLiteral("revision"), 0}, {QStringLiteral("last_run"), QVariantMap{}},
            {QStringLiteral("jobs"), QVariantMap{}}, {QStringLiteral("failed_actions"), QVariantMap{}},
            {QStringLiteral("tool_failures"), QVariantMap{}},
            {QStringLiteral("stop_reason"), QString{}}};
}

QVariantMap RepairRuntimeStateService::loadCheckpoint(const QString &path, QString *error) {
    QFile file(path);
    if (!file.exists())
        return defaultState();
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.error != QJsonParseError::NoError
            ? parseError.errorString() : QStringLiteral("Repair checkpoint root must be a JSON object.");
        return {};
    }
    QVariantMap state = document.object().toVariantMap();
    const QVariantMap defaults = defaultState();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        if (!state.contains(it.key()))
            state.insert(it.key(), it.value());
    }
    return state;
}

QVariantMap RepairRuntimeStateService::beginTurn() {
    QMutexLocker locker(&m_mutex);
    m_calls = m_stalled = m_investigationCalls = m_edaRuns = m_repeatBlocks = 0;
    m_progressSerial = 0;
    m_inFlight.clear();
    m_lastReadSignature.clear();
    m_lastReadBase.clear();
    m_lastReadEvidence.clear();
    m_state.insert(QStringLiteral("stop_reason"), QString{});
    setBudgetState();
    persist();
    return m_state;
}

QVariantMap RepairRuntimeStateService::beforeAction(const QString &requestId, const QString &tool,
                                                    const QVariantMap &arguments,
                                                    const QVariantMap &inputSnapshot) {
    QMutexLocker locker(&m_mutex);
    const QString stopped = m_state.value(QStringLiteral("stop_reason")).toString();
    if (!stopped.isEmpty())
        return pause(stopped);
    if (++m_calls > m_limits.toolCalls)
        return pause(QStringLiteral("tool_budget_exhausted"));

    const QString base = inputSnapshot.value(QStringLiteral("base")).toString();
    const QString signature = RepairActionFingerprintService::actionSignature(tool, arguments);
    const QVariantMap failedActions = mapValue(m_state, QStringLiteral("failed_actions"));
    if (tool == QStringLiteral("apply_patch") || tool == QStringLiteral("create_file")) {
        QVariantMap failed = mapValue(failedActions, signature);
        if (!failed.isEmpty() && failed.value(QStringLiteral("input_base")).toString() == base) {
            QVariantMap response{{QStringLiteral("status"), QStringLiteral("repeated_action_blocked")},
                                 {QStringLiteral("allowed"), false}, {QStringLiteral("tool"), tool},
                                 {QStringLiteral("previous_error"), failed.value(QStringLiteral("error"))},
                                 {QStringLiteral("path"), failed.value(QStringLiteral("path"))},
                                 {QStringLiteral("project_root"), failed.value(QStringLiteral("project_root"))},
                                 {QStringLiteral("reason"), QStringLiteral("相同文件操作在当前输入状态下已失败；本次未执行。")}};
            persist();
            return response;
        }
    }

    const QVariantMap previousFailure = mapValue(mapValue(m_state, QStringLiteral("tool_failures")), signature);
    if (!previousFailure.isEmpty() && previousFailure.value(QStringLiteral("input_base")).toString() == base
        && previousFailure.value(QStringLiteral("attempts")).toInt() >= 2) {
        const QString previousError = previousFailure.value(QStringLiteral("error")).toString();
        QVariantMap response{{QStringLiteral("status"), QStringLiteral("repeated_action_blocked")},
                             {QStringLiteral("allowed"), false}, {QStringLiteral("executed"), false},
                             {QStringLiteral("tool"), tool}, {QStringLiteral("previous_error"), previousError},
                             {QStringLiteral("attempts"), previousFailure.value(QStringLiteral("attempts"))},
                             {QStringLiteral("agent_guidance"), QStringLiteral(
                                 "本次没有执行，因为相同输入已连续失败。结合 previous_error 判断失败来自请求、路径、权限还是环境；"
                                 "只有请求或环境状态发生有意义变化时才重试。")},
                             {QStringLiteral("reason"), QStringLiteral(
                                 "相同工具调用在同一输入基线下已连续失败；本次未执行。")}};
        persist();
        return response;
    }

    if (repeatGuardTool(tool) && m_lastReadSignature == signature && m_lastReadBase == base
        && !m_lastReadEvidence.isEmpty()) {
        ++m_repeatBlocks;
        QVariantMap response{{QStringLiteral("status"), QStringLiteral("repeated_action_blocked")},
                             {QStringLiteral("allowed"), false}, {QStringLiteral("tool"), tool},
                             {QStringLiteral("same_evidence_id"), m_lastReadEvidence},
                             {QStringLiteral("repeat_count"), m_repeatBlocks},
                             {QStringLiteral("agent_guidance"), repeatedActionGuidance(tool)},
                             {QStringLiteral("reason"), QStringLiteral("相同输入和证据已在当前回合观察过；本次未重复执行。")}};
        if (m_repeatBlocks >= 3) {
            response.insert(QStringLiteral("status"), QStringLiteral("repeated_action_stalled"));
            response.insert(QStringLiteral("stop_reason"), QStringLiteral("repeated_action_without_progress"));
            response.insert(QStringLiteral("agent_guidance"), response.value(QStringLiteral("agent_guidance")).toString()
                            + QStringLiteral(" 当前回合因重复且无新证据而暂停，检查点已保留；基于已有证据说明卡点和仍未知的事实。"));
            m_state.insert(QStringLiteral("stop_reason"), QStringLiteral("repeated_action_without_progress"));
        }
        persist();
        return response;
    }
    if (!repeatGuardTool(tool) || signature != m_lastReadSignature)
        m_repeatBlocks = 0;

    QVariantMap snapshot = inputSnapshot;
    snapshot.insert(QStringLiteral("action_signature"), signature);
    snapshot.insert(QStringLiteral("tool"), tool);
    snapshot.insert(QStringLiteral("arguments"), arguments);
    m_inFlight.insert(requestId, snapshot);

    if (edaTool(tool)) {
        if (m_edaRuns >= m_limits.edaRuns) {
            m_inFlight.remove(requestId);
            return pause(QStringLiteral("eda_budget_exhausted"));
        }
        if (!inputSnapshot.value(QStringLiteral("complete"), true).toBool()) {
            m_inFlight.remove(requestId);
            return {{QStringLiteral("status"), QStringLiteral("input_manifest_incomplete")},
                    {QStringLiteral("allowed"), false},
                    {QStringLiteral("missing"), inputSnapshot.value(QStringLiteral("missing"))}};
        }
        const QString key = inputSnapshot.value(QStringLiteral("key")).toString();
        QVariantMap runs = mapValue(m_state, QStringLiteral("runs"));
        QVariantMap previous = mapValue(runs, key);
        if (!previous.isEmpty()) {
            const bool forced = arguments.value(QStringLiteral("force_rerun")).toBool();
            const bool forcedUsed = previous.value(QStringLiteral("forced_rerun_used")).toBool();
            const bool retryReason = !arguments.value(QStringLiteral("verification_reason")).toString().trimmed().isEmpty();
            const bool retryEligible = retryReason
                && (previous.value(QStringLiteral("verified")).toBool()
                    || previous.value(QStringLiteral("failure_class")).toString() == QStringLiteral("environment"))
                && previous.value(QStringLiteral("retries")).toInt() < 1;
            if (forced && forcedUsed) {
                m_inFlight.remove(requestId);
                return {{QStringLiteral("status"), QStringLiteral("unchanged_inputs")},
                        {QStringLiteral("allowed"), false}, {QStringLiteral("previous_run"), previous},
                        {QStringLiteral("input_fingerprint"), key},
                        {QStringLiteral("reason"), QStringLiteral("相同输入的强制新鲜运行已经执行过。")}};
            }
            if (!forced && !retryEligible) {
                m_inFlight.remove(requestId);
                return {{QStringLiteral("status"), QStringLiteral("unchanged_inputs")},
                        {QStringLiteral("allowed"), false}, {QStringLiteral("previous_run"), previous},
                        {QStringLiteral("input_fingerprint"), key},
                        {QStringLiteral("reason"), QStringLiteral("相同有效输入已有运行记录；先分析已有证据或修改真实输入。")}};
            }
            if (forced)
                previous.insert(QStringLiteral("forced_rerun_used"), true);
            else
                previous.insert(QStringLiteral("retries"), previous.value(QStringLiteral("retries")).toInt() + 1);
        }
        previous.insert(QStringLiteral("state"), QStringLiteral("pending"));
        previous.insert(QStringLiteral("input_base"), base);
        runs.insert(key, previous);
        m_state.insert(QStringLiteral("runs"), runs);
        setBudgetState();
        persist();
    }
    return {{QStringLiteral("status"), QStringLiteral("ready")}, {QStringLiteral("allowed"), true}};
}

QVariantMap RepairRuntimeStateService::afterAction(const QString &requestId, const QVariantMap &rawResult,
                                                   const QVariantMap &afterSnapshot,
                                                   QVariantMap observation) {
    QMutexLocker locker(&m_mutex);
    QVariantMap result = rawResult;
    if (!m_inFlight.contains(requestId))
        return result;
    const QVariantMap before = m_inFlight.take(requestId);
    const QString tool = before.value(QStringLiteral("tool")).toString();
    const QString signature = before.value(QStringLiteral("action_signature")).toString();
    const QString beforeBase = before.value(QStringLiteral("base")).toString();
    const QString afterBase = afterSnapshot.value(QStringLiteral("base")).toString();
    const bool changed = !beforeBase.isEmpty() && beforeBase != afterBase;
    bool controlled = false;

    if (changed) {
        const int revision = m_state.value(QStringLiteral("revision")).toInt() + 1;
        m_state.insert(QStringLiteral("revision"), revision);
        QVariantList changes = m_state.value(QStringLiteral("changes")).toList();
        appendBounded(changes, QVariantMap{{QStringLiteral("tool"), tool},
                                           {QStringLiteral("request_id"), requestId},
                                           {QStringLiteral("before"), beforeBase},
                                           {QStringLiteral("after"), afterBase},
                                           {QStringLiteral("edit_id"), result.value(QStringLiteral("edit_id"))},
                                           {QStringLiteral("revision"), revision}}, 64);
        m_state.insert(QStringLiteral("changes"), changes);
        QVariantMap lastRun = mapValue(m_state, QStringLiteral("last_run"));
        lastRun.insert(QStringLiteral("stale"), true);
        m_state.insert(QStringLiteral("last_run"), lastRun);
        QVariantMap issues = mapValue(m_state, QStringLiteral("issues"));
        const QString activeId = m_state.value(QStringLiteral("active_issue")).toString();
        QVariantMap active = mapValue(issues, activeId);
        if (!active.isEmpty()) {
            active.insert(QStringLiteral("applied_revision"), revision);
            active.insert(QStringLiteral("status"), QStringLiteral("awaiting_verification"));
            issues.insert(activeId, active);
            m_state.insert(QStringLiteral("issues"), issues);
        }
        result.insert(QStringLiteral("effective_input_change"), QVariantMap{
            {QStringLiteral("tool"), tool}, {QStringLiteral("request_id"), requestId},
            {QStringLiteral("before"), beforeBase}, {QStringLiteral("after"), afterBase},
            {QStringLiteral("edit_id"), result.value(QStringLiteral("edit_id"))},
            {QStringLiteral("revision"), revision}});
    } else if (editTool(tool)) {
        result.insert(QStringLiteral("effective_input_change"), false);
    }

    if (tool == QStringLiteral("apply_patch") || tool == QStringLiteral("create_file")) {
        const QString status = result.value(QStringLiteral("status")).toString();
        const bool prerequisiteOnly = QSet<QString>{QStringLiteral("skill_required"),
            QStringLiteral("evidence_required"), QStringLiteral("tool_compatibility_edit_blocked"),
            QStringLiteral("approval_required"), QStringLiteral("approval_rejected"),
            QStringLiteral("needs_source_read"), QStringLiteral("repeated_action_blocked")}
            .contains(status);
        const bool failed = (tool == QStringLiteral("apply_patch") && result.value(QStringLiteral("edited")).metaType().id() == QMetaType::Bool
                             && !result.value(QStringLiteral("edited")).toBool())
            || (tool == QStringLiteral("create_file") && result.value(QStringLiteral("created")).metaType().id() == QMetaType::Bool
                && !result.value(QStringLiteral("created")).toBool() && !result.value(QStringLiteral("already_exists")).toBool()
                && !result.value(QStringLiteral("updated")).toBool())
            || QSet<QString>{QStringLiteral("blocked"), QStringLiteral("failed"), QStringLiteral("cross_check_blocked"),
                             QStringLiteral("rejected"), QStringLiteral("needs_source_read")}.contains(status);
        if (failed && !prerequisiteOnly) {
            QVariantMap failedActions = mapValue(m_state, QStringLiteral("failed_actions"));
            QVariantMap failure{{QStringLiteral("input_base"), beforeBase},
                                {QStringLiteral("status"), status},
                                {QStringLiteral("error"), result.value(QStringLiteral("reason"), result.value(QStringLiteral("error"), status))},
                                {QStringLiteral("path"), result.value(QStringLiteral("path"))},
                                {QStringLiteral("project_root"), before.value(QStringLiteral("project_root"))}};
            failedActions.insert(signature, failure);
            while (failedActions.size() > 64)
                failedActions.erase(failedActions.begin());
            m_state.insert(QStringLiteral("failed_actions"), failedActions);
        } else if (changed) {
            m_state.insert(QStringLiteral("failed_actions"), QVariantMap{});
        }
    }

    if (failedToolResult(result) && !actionablePrerequisiteStatus(result.value(QStringLiteral("status")).toString())) {
        QVariantMap failures = mapValue(m_state, QStringLiteral("tool_failures"));
        QVariantMap previous = mapValue(failures, signature);
        const int attempts = previous.value(QStringLiteral("input_base")).toString() == beforeBase
            ? previous.value(QStringLiteral("attempts")).toInt() + 1 : 1;
        const QString error = result.value(QStringLiteral("error"),
            result.value(QStringLiteral("message"), result.value(QStringLiteral("reason"),
                         result.value(QStringLiteral("status")))).toString()).toString();
        failures.insert(signature, QVariantMap{{QStringLiteral("tool"), tool},
            {QStringLiteral("input_base"), beforeBase}, {QStringLiteral("attempts"), attempts},
            {QStringLiteral("error"), error}, {QStringLiteral("last_result"), stableValue(result)}});
        while (failures.size() > 64)
            failures.erase(failures.begin());
        m_state.insert(QStringLiteral("tool_failures"), failures);
    } else if (!actionablePrerequisiteStatus(result.value(QStringLiteral("status")).toString())) {
        QVariantMap failures = mapValue(m_state, QStringLiteral("tool_failures"));
        if (failures.remove(signature) > 0)
            m_state.insert(QStringLiteral("tool_failures"), failures);
    }

    if (observation.isEmpty())
        observation = makeObservation(tool, before.value(QStringLiteral("arguments")).toMap(), result);
    const QString evidenceId = observation.value(QStringLiteral("evidence_id")).toString();
    const bool freshEvidence = !evidenceId.isEmpty() && !mapValue(m_state, QStringLiteral("evidence")).contains(evidenceId);
    const bool informative = informativeTool(tool) && observation.value(QStringLiteral("has_body")).toBool();
    if (informative) {
        QVariantMap evidence = mapValue(m_state, QStringLiteral("evidence"));
        evidence.insert(evidenceId, observation);
        m_state.insert(QStringLiteral("evidence"), evidence);
        result.insert(QStringLiteral("evidence_id"), evidenceId);
    }

    QVariantMap runSnapshot;
    if (edaTool(tool))
        runSnapshot = before;
    if (tool == QStringLiteral("wait_dft_job"))
        runSnapshot = mapValue(m_state, QStringLiteral("jobs")).value(
            before.value(QStringLiteral("arguments")).toMap().value(QStringLiteral("job_id")).toString()).toMap();
    if (!runSnapshot.isEmpty()) {
        const auto payloads = RepairRunEvidenceService::controlledRunPayloads(result);
        if (!payloads.isEmpty()) {
            const QVariantMap run = payloads.last();
            const QString key = runSnapshot.value(QStringLiteral("key")).toString();
            QVariantList validations;
            for (const QVariantMap &node : nestedMaps(run)) {
                QVariantMap validation = node.value(QStringLiteral("cross_validation")).toMap();
                if (validation.isEmpty())
                    validation = node.value(QStringLiteral("verification")).toMap();
                if (!validation.isEmpty() && validation.value(QStringLiteral("kind")).toString() != QStringLiteral("patch_cross_check"))
                    validations.append(validation);
            }
            bool verified = !validations.isEmpty()
                && validations.last().toMap().value(QStringLiteral("status")).toString() == QStringLiteral("verified");
            if (!verified) {
                for (const QVariantMap &node : nestedMaps(run)) {
                    if (node.value(QStringLiteral("status")).toString() == QStringLiteral("verified")) {
                        verified = true;
                        break;
                    }
                }
            }
            QVariantMap record{{QStringLiteral("input_base"), runSnapshot.value(QStringLiteral("base"))},
                               {QStringLiteral("input_fingerprint"), key},
                               {QStringLiteral("iteration_options"), runSnapshot.value(QStringLiteral("options"))},
                               {QStringLiteral("run_id"), run.value(QStringLiteral("run_id"))},
                               {QStringLiteral("workspace"), run.value(QStringLiteral("workspace"))},
                               {QStringLiteral("state"), QStringLiteral("completed")},
                               {QStringLiteral("verified"), verified},
                               {QStringLiteral("stale"), afterBase != runSnapshot.value(QStringLiteral("base")).toString()},
                               {QStringLiteral("failure_class"), run.value(QStringLiteral("failure_class"))}};
            QVariantMap runs = mapValue(m_state, QStringLiteral("runs"));
            const QVariantMap previous = mapValue(runs, key);
            record.insert(QStringLiteral("retries"), previous.value(QStringLiteral("retries"), 0));
            record.insert(QStringLiteral("forced_rerun_used"), previous.value(QStringLiteral("forced_rerun_used"), false));
            QVariantMap reportPaths;
            QVariantMap drc;
            for (const QVariantMap &node : nestedMaps(run)) {
                if (reportPaths.isEmpty()) reportPaths = node.value(QStringLiteral("report_paths")).toMap();
                if (drc.isEmpty()) drc = node.value(QStringLiteral("drc")).toMap();
            }
            record.insert(QStringLiteral("report_paths"), reportPaths);
            record.insert(QStringLiteral("drc"), drc);
            controlled = previous.value(QStringLiteral("state")).toString() != QStringLiteral("completed")
                || previous.value(QStringLiteral("run_id")).toString() != record.value(QStringLiteral("run_id")).toString();
            runs.insert(key, record);
            m_state.insert(QStringLiteral("runs"), runs);
            m_state.insert(QStringLiteral("last_run"), record);
            QVariantMap issues = mapValue(m_state, QStringLiteral("issues"));
            const QString activeId = m_state.value(QStringLiteral("active_issue")).toString();
            QVariantMap active = mapValue(issues, activeId);
            if (!active.isEmpty() && active.value(QStringLiteral("applied_revision")).toInt() == m_state.value(QStringLiteral("revision")).toInt()) {
                active.insert(QStringLiteral("last_validation"), record);
                active.insert(QStringLiteral("status"), verified && !record.value(QStringLiteral("stale")).toBool()
                             ? QStringLiteral("verified_by_run") : QStringLiteral("hypothesis_not_confirmed"));
                issues.insert(activeId, active);
                m_state.insert(QStringLiteral("issues"), issues);
            }
            result.insert(QStringLiteral("input_fingerprint"), key);
            result.insert(QStringLiteral("evidence_stale"), record.value(QStringLiteral("stale")));
        } else if (edaTool(tool) && !result.contains(QStringLiteral("job_id"))
                   && result.value(QStringLiteral("status")).toString() != QStringLiteral("unchanged_inputs")
                   && result.value(QStringLiteral("status")).toString() != QStringLiteral("input_manifest_incomplete")) {
            QVariantMap runs = mapValue(m_state, QStringLiteral("runs"));
            runs.remove(runSnapshot.value(QStringLiteral("key")).toString());
            m_state.insert(QStringLiteral("runs"), runs);
        }
    }

    if (edaTool(tool) && result.contains(QStringLiteral("job_id"))
        && result.value(QStringLiteral("state")).toString() != QStringLiteral("completed")
        && result.value(QStringLiteral("state")).toString() != QStringLiteral("failed")) {
        ++m_edaRuns;
        QVariantMap jobs = mapValue(m_state, QStringLiteral("jobs"));
        jobs.insert(result.value(QStringLiteral("job_id")).toString(), before);
        m_state.insert(QStringLiteral("jobs"), jobs);
    }
    if (tool == QStringLiteral("run_dft_flow") || tool == QStringLiteral("run_dft_iteration")
        || tool == QStringLiteral("run_dft_optimization") || tool == QStringLiteral("run_approved_patch")) {
        if (!RepairRunEvidenceService::controlledRunPayloads(result).isEmpty())
            ++m_edaRuns;
    }

    const bool progress = changed || controlled || (informative && freshEvidence);
    const bool waiting = tool == QStringLiteral("wait_dft_job") && !runSnapshot.isEmpty()
        && (result.value(QStringLiteral("state")).toString() == QStringLiteral("queued")
            || result.value(QStringLiteral("state")).toString() == QStringLiteral("running"));
    const QString resultStatus = result.value(QStringLiteral("status")).toString();
    const bool actionablePrerequisite = QSet<QString>{
        QStringLiteral("skill_required"), QStringLiteral("evidence_required"),
        QStringLiteral("approval_required"), QStringLiteral("approval_rejected")}.contains(resultStatus)
        || ((tool == QStringLiteral("apply_patch") || tool == QStringLiteral("create_file"))
            && QSet<QString>{QStringLiteral("tool_compatibility_edit_blocked"),
                             QStringLiteral("needs_source_read")}.contains(resultStatus));
    if (!waiting && !actionablePrerequisite) {
        m_stalled = progress ? 0 : m_stalled + 1;
        m_investigationCalls = (changed || controlled) ? 0 : m_investigationCalls + 1;
    }
    if (progress)
        ++m_progressSerial;
    if (m_stalled >= m_limits.stalledCalls)
        m_state.insert(QStringLiteral("stop_reason"), QStringLiteral("no_progress"));
    else if (m_investigationCalls >= m_limits.investigationCalls)
        m_state.insert(QStringLiteral("stop_reason"), QStringLiteral("investigation_budget_exhausted"));

    result.insert(QStringLiteral("repair_progress"), QVariantMap{
        {QStringLiteral("new_evidence"), informative && freshEvidence},
        {QStringLiteral("input_changed"), changed}, {QStringLiteral("new_controlled_run"), controlled},
        {QStringLiteral("stalled_calls"), m_stalled}});
    if (repeatGuardTool(tool) && !evidenceId.isEmpty()) {
        m_lastReadSignature = signature;
        m_lastReadBase = afterBase;
        m_lastReadEvidence = evidenceId;
        if (!freshEvidence)
            result.insert(QStringLiteral("agent_guidance"), repeatedActionGuidance(tool));
    } else if (progress || changed || controlled || editTool(tool)) {
        m_lastReadSignature.clear();
        m_lastReadBase.clear();
        m_lastReadEvidence.clear();
    }
    const QString stopReason = m_state.value(QStringLiteral("stop_reason")).toString();
    if (!stopReason.isEmpty()) {
        result.insert(QStringLiteral("stop_reason"), stopReason);
        result.insert(QStringLiteral("checkpoint"), checkpointPath());
    }
    setBudgetState();
    persist();
    return result;
}

QVariantMap RepairRuntimeStateService::recordIssue(const QVariantMap &arguments) {
    QMutexLocker locker(&m_mutex);
    QVariantMap response = RepairIssueService::updateIssue(arguments, m_state);
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    result.remove(QStringLiteral("state"));
    persist();
    return result;
}

QString RepairRuntimeStateService::executionState(const QVariantMap &currentSnapshot) const {
    QMutexLocker locker(&m_mutex);
    const QVariantMap run = mapValue(m_state, QStringLiteral("last_run"));
    if (run.isEmpty())
        return m_calls ? QStringLiteral("analysis_with_tools") : QStringLiteral("planning_only");
    if (run.value(QStringLiteral("stale")).toBool()
        || !currentSnapshot.value(QStringLiteral("complete"), true).toBool()
        || run.value(QStringLiteral("input_base")).toString() != currentSnapshot.value(QStringLiteral("base")).toString())
        return QStringLiteral("verification_required");
    return run.value(QStringLiteral("verified")).toBool()
        ? QStringLiteral("evidence_verified") : QStringLiteral("execution_blocked");
}

QVariantMap RepairRuntimeStateService::state() const {
    QMutexLocker locker(&m_mutex);
    return m_state;
}

QString RepairRuntimeStateService::checkpointPath() const {
    return QDir(m_artifactRoot).filePath(QStringLiteral("repair-state.json"));
}

bool RepairRuntimeStateService::persist(QString *error) const {
    QDir directory;
    if (!directory.mkpath(m_artifactRoot)) {
        if (error) *error = QStringLiteral("Could not create repair artifact directory.");
        return false;
    }
    QSaveFile file(checkpointPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(m_state))
                                 .toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QVariantMap RepairRuntimeStateService::pause(const QString &reason) {
    m_state.insert(QStringLiteral("stop_reason"), reason);
    setBudgetState();
    persist();
    return {{QStringLiteral("status"), QStringLiteral("paused")},
            {QStringLiteral("allowed"), false}, {QStringLiteral("executed"), false},
            {QStringLiteral("stop_reason"), reason}, {QStringLiteral("checkpoint"), checkpointPath()},
            {QStringLiteral("next_action"), QStringLiteral("停止自动续跑；汇报已读证据、当前假设、失败尝试和具体缺口。")}};
}

QVariantMap RepairRuntimeStateService::makeObservation(const QString &tool, const QVariantMap &arguments,
                                                       const QVariantMap &result) const {
    const QString id = QStringLiteral("E-") + RepairActionFingerprintService::digest(stableValue(result)).left(16);
    const QString path = result.value(QStringLiteral("path"), result.value(QStringLiteral("resolved_path"),
                                       arguments.value(QStringLiteral("path")))).toString();
    bool body = hasBody(result);
    if (tool == QStringLiteral("inspect_project") || tool == QStringLiteral("inspect_studio_project")
        || tool == QStringLiteral("analyze_dft_results") || tool == QStringLiteral("diagnose_dft_failure"))
        body = !result.isEmpty() && !result.contains(QStringLiteral("error"));
    const QString evidenceFile = QDir(m_artifactRoot).filePath(QStringLiteral("observations/%1.json").arg(id));
    if (body) {
        QDir().mkpath(QFileInfo(evidenceFile).absolutePath());
        QSaveFile file(evidenceFile);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(QJsonObject{{QStringLiteral("tool"), tool},
                                                {QStringLiteral("arguments"), QJsonObject::fromVariantMap(arguments)},
                                                {QStringLiteral("result"), QJsonObject::fromVariantMap(result)}})
                           .toJson(QJsonDocument::Indented));
            file.commit();
        }
    }
    return {{QStringLiteral("evidence_id"), id}, {QStringLiteral("tool"), tool},
            {QStringLiteral("path"), path},
            {QStringLiteral("sha256"), result.value(QStringLiteral("sha256"))},
            {QStringLiteral("start_line"), result.value(QStringLiteral("start_line"), arguments.value(QStringLiteral("start_line")))},
            {QStringLiteral("end_line"), result.value(QStringLiteral("end_line"))},
            {QStringLiteral("evidence_file"), evidenceFile}, {QStringLiteral("has_body"), body},
            {QStringLiteral("summary"), result.value(QStringLiteral("status"), QStringLiteral("observed"))}};
}

QString RepairRuntimeStateService::repeatedActionGuidance(const QString &tool) const {
    Q_UNUSED(tool);
    return QStringLiteral(
        "本次调用与刚才相同，且没有带来新证据，因此未重复执行。结合已有结果判断当前未知点；"
        "只有能改变输入、补充信息或检验不同假设的操作才会增加进展。可选择任何适合的工具或检查对象；"
        "若证据已足够，直接分析、修改并验证，若暂时无法推进则明确说明缺口。 ");
}

bool RepairRuntimeStateService::informativeTool(const QString &tool) const {
    return kReadTools.contains(tool) || tool == QStringLiteral("exec_command")
        || tool == QStringLiteral("shell")
        || tool == QStringLiteral("write_stdin");
}
bool RepairRuntimeStateService::repeatGuardTool(const QString &tool) const { return kRepeatGuardTools.contains(tool); }
bool RepairRuntimeStateService::edaTool(const QString &tool) const { return kEdaTools.contains(tool); }
bool RepairRuntimeStateService::editTool(const QString &tool) const { return kEditTools.contains(tool); }

void RepairRuntimeStateService::setBudgetState() {
    m_state.insert(QStringLiteral("budget"), QVariantMap{
        {QStringLiteral("calls"), m_calls}, {QStringLiteral("stalled"), m_stalled},
        {QStringLiteral("investigation_calls"), m_investigationCalls},
        {QStringLiteral("maximum_calls"), m_limits.toolCalls},
        {QStringLiteral("maximum_stalled"), m_limits.stalledCalls},
        {QStringLiteral("maximum_investigation"), m_limits.investigationCalls},
        {QStringLiteral("eda_runs"), m_edaRuns}, {QStringLiteral("maximum_eda_runs"), m_limits.edaRuns}});
}
