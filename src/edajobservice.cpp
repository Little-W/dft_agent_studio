#include "edajobservice.h"

#include "configureddftflowservice.h"
#include "dftreportevidenceservice.h"
#include "fanatpgservice.h"
#include "sourcecompatibilityservice.h"
#include "studiopaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

struct Job {
    QString id;
    QString operation;
    QString state = QStringLiteral("queued");
    QString startedAt;
    QString completedAt;
    QString error;
    QVariantMap result;
    std::atomic_bool cancelRequested{false};
    mutable std::mutex mutex;
    std::condition_variable finished;
};

struct SessionJobs {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs;
    std::string activeFlowJobId;
    quint64 nextJobNumber = 1;
};

std::mutex registryMutex;
std::unordered_map<std::string, std::shared_ptr<SessionJobs>> registry;

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QVariantMap effectiveIterationArguments(QVariantMap arguments)
{
    for (const QString &key : {QStringLiteral("source_dependency_mode"),
             QStringLiteral("source_preprocess_mode"), QStringLiteral("source_annotation_mode"),
             QStringLiteral("drc_repair_mode"), QStringLiteral("mbist_include_mode"),
             QStringLiteral("mbist_diagnostic_mode"), QStringLiteral("atpg_diagnostic_mode"),
             QStringLiteral("compile_strategy")}) {
        if (arguments.value(key).metaType().id() == QMetaType::QString
            && arguments.value(key).toString().trimmed().isEmpty())
            arguments.remove(key);
    }
    if (arguments.value(QStringLiteral("mbist_diagnostic_mode")).toString().trimmed().compare(
            QStringLiteral("none"), Qt::CaseInsensitive) == 0)
        arguments.insert(QStringLiteral("mbist_diagnostic_mode"), QStringLiteral("off"));
    return arguments;
}

std::shared_ptr<SessionJobs> jobsForSession(const QString &sessionId)
{
    const std::string key = sessionId.trimmed().toStdString();
    std::lock_guard<std::mutex> lock(registryMutex);
    auto &entry = registry[key];
    if (!entry)
        entry = std::make_shared<SessionJobs>();
    return entry;
}

std::shared_ptr<Job> findJob(const QString &sessionId, const QString &jobId)
{
    const auto session = jobsForSession(sessionId);
    std::lock_guard<std::mutex> lock(session->mutex);
    const auto found = session->jobs.find(jobId.trimmed().toStdString());
    return found == session->jobs.end() ? std::shared_ptr<Job>{} : found->second;
}

QVariantMap summary(const std::shared_ptr<Job> &job, bool withResult)
{
    std::lock_guard<std::mutex> lock(job->mutex);
    QVariantMap value{
        {QStringLiteral("job_id"), job->id},
        {QStringLiteral("operation"), job->operation},
        {QStringLiteral("state"), job->state},
        {QStringLiteral("started_at"), job->startedAt},
        {QStringLiteral("completed_at"), job->completedAt},
        {QStringLiteral("cancellable"), job->state == QLatin1String("queued")
                                           || job->state == QLatin1String("running")}
    };
    if (!job->error.isEmpty())
        value.insert(QStringLiteral("error"), job->error);
    if (withResult && (job->state == QLatin1String("completed")
                       || job->state == QLatin1String("failed")
                       || job->state == QLatin1String("interrupted")))
        value.insert(QStringLiteral("result"), job->result);
    return value;
}

int boundedInteger(const QVariant &value, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? std::clamp(parsed, minimum, maximum) : fallback;
}

int nextTimeoutMultiplier(int current)
{
    if (current == 1)
        return 2;
    if (current == 2)
        return 4;
    return 0;
}

bool hasInternalDesignCompilerError(const QVariantMap &execution)
{
    QStringList evidence{execution.value(QStringLiteral("stdout")).toString(),
                         execution.value(QStringLiteral("stderr")).toString()};
    for (const QVariant &item : execution.value(QStringLiteral("errors")).toList())
        evidence.append(item.toString());
    return evidence.join(QLatin1Char('\n')).contains(
        QStringLiteral("Fatal: Internal system error, cannot recover."), Qt::CaseInsensitive);
}

bool hasCPreprocessingError(const QVariantMap &execution)
{
    QStringList evidence{execution.value(QStringLiteral("stdout")).toString(),
                         execution.value(QStringLiteral("stderr")).toString()};
    for (const QVariant &item : execution.value(QStringLiteral("errors")).toList())
        evidence.append(item.toString());
    const QString text = evidence.join(QLatin1Char('\n')).toLower();
    const QStringList markers{QStringLiteral("token '#'"), QStringLiteral("token \"#\""),
                               QStringLiteral("#ifdef"), QStringLiteral("#ifndef"),
                               QStringLiteral("#if "), QStringLiteral("c preprocessor"),
                               QStringLiteral("c-preprocessor")};
    return std::any_of(markers.cbegin(), markers.cend(), [&text](const QString &marker) {
        return text.contains(marker);
    });
}

bool readLinkReportPassed(const QString &workspace, const QString &top)
{
    QFile report(QDir(workspace).filePath(QStringLiteral("flow/reports/read_link.rpt")));
    if (!report.open(QIODevice::ReadOnly) || report.size() > 2 * 1024 * 1024)
        return false;
    const QString contents = QString::fromUtf8(report.readAll());
    const QString lowered = contents.toLower();
    return !top.trimmed().isEmpty() && lowered.contains(top.toLower())
        && !lowered.contains(QStringLiteral("unresolved references"))
        && !lowered.contains(QStringLiteral("black box (unknown) components"));
}

QVariantMap runOptimization(const QVariantMap &project, const QVariantMap &arguments,
                            const QString &agentRoot, const std::shared_ptr<std::atomic_bool> &cancelToken)
{
    if (FanAtpgService::selected(project))
        return FanAtpgService::run(project, arguments, agentRoot, cancelToken);

    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    const int roundsLimit = boundedInteger(arguments.value(QStringLiteral("maximum_rounds"),
        execution.value(QStringLiteral("iteration_limit"), 3)), 3, 1, 8);
    int abortLimit = boundedInteger(arguments.value(QStringLiteral("atpg_abort_limit"),
        execution.value(QStringLiteral("atpg_abort_limit"))), 10, 1, 1000);
    int flowTimeoutMultiplier = boundedInteger(arguments.value(QStringLiteral("flow_timeout_multiplier")), 1, 1, 4);
    int atpgTimeoutMultiplier = boundedInteger(arguments.value(QStringLiteral("atpg_timeout_multiplier")), 1, 1, 4);
    int mbistTimeoutMultiplier = boundedInteger(arguments.value(QStringLiteral("mbist_timeout_multiplier")), 1, 1, 4);
    QString mbistIncludeMode = arguments.value(QStringLiteral("mbist_include_mode"),
        QStringLiteral("declared")).toString().trimmed().toLower();
    QString mbistDiagnosticMode = arguments.value(QStringLiteral("mbist_diagnostic_mode"),
        QStringLiteral("off")).toString().trimmed().toLower();
    QString atpgDiagnosticMode = arguments.value(QStringLiteral("atpg_diagnostic_mode"),
        QStringLiteral("off")).toString().trimmed().toLower();
    QString compileStrategy = arguments.value(QStringLiteral("compile_strategy"),
        execution.value(QStringLiteral("compile_strategy"), QStringLiteral("single_pass"))).toString().trimmed().toLower();
    const QVariantMap sourcePreprocessor = execution.value(QStringLiteral("source_preprocessor")).toMap();
    QString sourcePreprocessMode = arguments.value(QStringLiteral("source_preprocess_mode"),
        sourcePreprocessor.value(QStringLiteral("mode"), QStringLiteral("off"))).toString().trimmed().toLower();
    QStringList sourceExcludeFiles = arguments.value(QStringLiteral("source_exclude_files")).toStringList();
    if (sourceExcludeFiles.isEmpty()) {
        for (const QVariant &value : arguments.value(QStringLiteral("source_exclude_files")).toList())
            sourceExcludeFiles.append(value.toString());
    }
    QString repairMode = QStringLiteral("off");
    QString nextProfile = QStringLiteral("atpg_abort_%1").arg(abortLimit);
    QVariantMap selectedPayload;
    QVariantMap selectedRound;
    QVariantList profiles;
    QVariant previousCoverage;
    int previousAbortLimit = abortLimit;
    bool autoTriggeredFaultDiagnostics = false;
    QString stopReason = QStringLiteral("Maximum optimization rounds reached.");
    const QVariantMap ready = ConfiguredDftFlowService::readiness(project, arguments, agentRoot)
                                  .value(QStringLiteral("result")).toMap();
    bool targetValid = false;
    const double target = ready.value(QStringLiteral("atpg_configuration")).toMap()
                              .value(QStringLiteral("minimum_coverage")).toDouble(&targetValid);
    const double coverageTarget = targetValid ? target : -1.0;
    const int maximumDrc = project.value(QStringLiteral("maximum_dft_drc_violations")).toInt();
    const QList<int> abortLimits{10, 50, 200, 500, 1000};

    for (int round = 1; round <= roundsLimit; ++round) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            stopReason = QStringLiteral("Optimization was cancelled.");
            break;
        }
        QVariantMap roundArguments = arguments;
        roundArguments.remove(QStringLiteral("maximum_rounds"));
        roundArguments.insert(QStringLiteral("atpg_abort_limit"), abortLimit);
        roundArguments.insert(QStringLiteral("drc_repair_mode"), repairMode);
        roundArguments.insert(QStringLiteral("flow_timeout_multiplier"), flowTimeoutMultiplier);
        roundArguments.insert(QStringLiteral("atpg_timeout_multiplier"), atpgTimeoutMultiplier);
        roundArguments.insert(QStringLiteral("mbist_timeout_multiplier"), mbistTimeoutMultiplier);
        roundArguments.insert(QStringLiteral("mbist_include_mode"), mbistIncludeMode);
        roundArguments.insert(QStringLiteral("mbist_diagnostic_mode"), mbistDiagnosticMode);
        roundArguments.insert(QStringLiteral("atpg_diagnostic_mode"), atpgDiagnosticMode);
        roundArguments.insert(QStringLiteral("compile_strategy"), compileStrategy);
        roundArguments.insert(QStringLiteral("source_preprocess_mode"), sourcePreprocessMode);
        if (arguments.contains(QStringLiteral("source_annotation_mode")))
            roundArguments.insert(QStringLiteral("source_annotation_mode"), arguments.value(QStringLiteral("source_annotation_mode")));
        if (!sourceExcludeFiles.isEmpty())
            roundArguments.insert(QStringLiteral("source_exclude_files"), sourceExcludeFiles);
        if (arguments.contains(QStringLiteral("max_cores")))
            roundArguments.insert(QStringLiteral("max_cores"), arguments.value(QStringLiteral("max_cores")));
        QVariantMap response = ConfiguredDftFlowService::run(project,
            roundArguments.value(QStringLiteral("workspace_root")).toString(),
            roundArguments, agentRoot, cancelToken);
        QVariantMap payload = response.value(QStringLiteral("result")).toMap();
        const QVariantMap executionResult = payload.value(QStringLiteral("execution")).toMap();
        const QVariantMap atpg = payload.value(QStringLiteral("atpg")).toMap();
        const QVariantMap atpgSummary = atpg.value(QStringLiteral("summary")).toMap();
        const QVariantMap mbist = payload.value(QStringLiteral("mbist")).toMap();
        const QVariant coverageValue = atpgSummary.value(QStringLiteral("coverage_percent"));
        bool coverageOk = false;
        const double coverage = coverageValue.toDouble(&coverageOk);
        const bool coverageValid = coverageValue.isValid() && !coverageValue.isNull() && coverageOk;
        const QString workspace = executionResult.value(QStringLiteral("workspace")).toString();
        const QString drcPath = QDir(workspace).filePath(QStringLiteral("flow/reports/post_dft_drc.rpt"));
        QVariantMap drcResult;
        if (QFileInfo(drcPath).isFile())
            drcResult = DftReportEvidenceService::parseDrcReport(drcPath).value(QStringLiteral("result")).toMap();
        bool drcCountOk = false;
        const int drcCount = drcResult.value(QStringLiteral("total_violations")).toInt(&drcCountOk);
        const QString verification = payload.value(QStringLiteral("verification")).toMap()
                                         .value(QStringLiteral("status")).toString();
        const bool drcPassed = drcCountOk && drcCount <= maximumDrc;
        const bool verified = verification == QLatin1String("verified");
        const bool flowTimedOut = executionResult.value(QStringLiteral("timed_out")).toBool();
        const bool atpgTimedOut = atpg.value(QStringLiteral("timed_out")).toBool();
        const bool mbistTimedOut = mbist.value(QStringLiteral("timed_out")).toBool();
        QStringList executionErrors = executionResult.value(QStringLiteral("errors")).toStringList();
        if (executionErrors.isEmpty()) {
            for (const QVariant &item : executionResult.value(QStringLiteral("errors")).toList())
                if (!item.toString().trimmed().isEmpty())
                    executionErrors.append(item.toString());
        }
        QVariantList diagnosticErrors;
        for (const QString &error : executionErrors)
            diagnosticErrors.append(error);
        for (const QString &line : executionResult.value(QStringLiteral("stdout")).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            diagnosticErrors.append(line);
        for (const QString &line : executionResult.value(QStringLiteral("stderr")).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            diagnosticErrors.append(line);
        const QVariantMap sourceCompatibilityDiagnosis = executionErrors.isEmpty()
            ? QVariantMap{} : SourceCompatibilityService::diagnoseUnsupportedCompileSource(workspace, diagnosticErrors);
        QVariantMap record{
            {QStringLiteral("round"), round},
            {QStringLiteral("profile"), nextProfile},
            {QStringLiteral("atpg_abort_limit"), abortLimit},
            {QStringLiteral("compile_strategy"), compileStrategy},
            {QStringLiteral("source_preprocess_mode"), sourcePreprocessMode},
            {QStringLiteral("source_exclude_files"), sourceExcludeFiles},
            {QStringLiteral("source_compatibility"), sourceCompatibilityDiagnosis},
            {QStringLiteral("flow_timeout_multiplier"), flowTimeoutMultiplier},
            {QStringLiteral("atpg_timeout_multiplier"), atpgTimeoutMultiplier},
            {QStringLiteral("mbist_timeout_multiplier"), mbistTimeoutMultiplier},
            {QStringLiteral("mbist_include_mode"), mbistIncludeMode},
            {QStringLiteral("mbist_diagnostic_mode"), mbistDiagnosticMode},
            {QStringLiteral("atpg_diagnostic_mode"), atpgDiagnosticMode},
            {QStringLiteral("atpg_fault_classes"), atpgSummary.value(QStringLiteral("fault_classes"))},
            {QStringLiteral("mbist_compiled"), mbist.value(QStringLiteral("compiled"))},
            {QStringLiteral("mbist_timed_out"), mbistTimedOut},
            {QStringLiteral("drc_repair_mode"), repairMode},
            {QStringLiteral("workspace"), workspace},
            {QStringLiteral("evidence_file"), payload.value(QStringLiteral("evidence_file"))},
            {QStringLiteral("verification_status"), verification},
            {QStringLiteral("coverage_percent"), coverageValid ? QVariant(coverage) : QVariant{}},
            {QStringLiteral("coverage_target_percent"), coverageTarget >= 0.0 ? QVariant(coverageTarget) : QVariant{}},
            {QStringLiteral("coverage_met"), coverageTarget >= 0.0 && coverageValid && coverage >= coverageTarget},
            {QStringLiteral("post_dft_drc"), drcCountOk ? QVariant(drcCount) : QVariant{}},
            {QStringLiteral("post_dft_drc_limit"), maximumDrc},
            {QStringLiteral("drc_passed"), drcPassed},
            {QStringLiteral("errors"), executionResult.value(QStringLiteral("errors"))},
            {QStringLiteral("timed_out"), executionResult.value(QStringLiteral("timed_out"))},
            {QStringLiteral("atpg_timed_out"), atpg.value(QStringLiteral("timed_out"))},
            {QStringLiteral("executed"), response.value(QStringLiteral("ok")).toBool()}
        };
        profiles.append(record);
        const auto better = [&] {
            if (selectedRound.isEmpty())
                return true;
            const auto metric = [](const QVariantMap &value, const QString &key, double fallback) {
                bool ok = false;
                const double number = value.value(key).toDouble(&ok);
                return ok ? number : fallback;
            };
            const QList<double> candidateKey{
                drcPassed ? 1.0 : 0.0, verified ? 1.0 : 0.0,
                -double(qMax(drcCount - maximumDrc, 0)), coverageValid ? coverage : -1.0,
                double(round)};
            const QList<double> selectedKey{
                selectedRound.value(QStringLiteral("drc_passed")).toBool() ? 1.0 : 0.0,
                selectedRound.value(QStringLiteral("verification_status")).toString() == QLatin1String("verified") ? 1.0 : 0.0,
                -qMax(metric(selectedRound, QStringLiteral("post_dft_drc"), maximumDrc) - maximumDrc, 0.0),
                metric(selectedRound, QStringLiteral("coverage_percent"), -1.0),
                metric(selectedRound, QStringLiteral("round"), 0.0)};
            return std::lexicographical_compare(selectedKey.cbegin(), selectedKey.cend(),
                                                candidateKey.cbegin(), candidateKey.cend());
        };
        if (better()) {
            selectedRound = record;
            selectedPayload = payload;
        }
        const bool previousCoverageValid = previousCoverage.isValid() && !previousCoverage.isNull();
        const double previousCoverageValue = previousCoverage.toDouble();
        const int priorAbortLimit = previousAbortLimit;
        const bool abortLimitIncreased = abortLimit > priorAbortLimit;
        const bool coverageRegressed = previousCoverageValid && coverageValid && abortLimitIncreased
            && coverage < previousCoverageValue - 0.01;
        const bool coveragePlateau = previousCoverageValid && coverageValid
            && qAbs(previousCoverageValue - coverage) < 0.01 && abortLimit >= 500;
        previousCoverage = coverageValid ? QVariant(coverage) : QVariant{};
        previousAbortLimit = abortLimit;
        if (verified && drcPassed && coverageTarget >= 0.0 && coverageValid && coverage >= coverageTarget) {
            stopReason = QStringLiteral("Coverage, DRC, and fresh evidence verification meet the configured objective.");
            break;
        }
        if (atpgDiagnosticMode == QLatin1String("fault_classes") && autoTriggeredFaultDiagnostics) {
            stopReason = QStringLiteral("Fault-class diagnostics were collected after restoring the previous ATPG limit; another unchanged search would add no evidence.");
            break;
        }
        if (!executionErrors.isEmpty() && round < roundsLimit
            && compileStrategy == QLatin1String("single_pass")
            && !flowTimedOut && !atpgTimedOut
            && hasInternalDesignCompilerError(executionResult)
            && readLinkReportPassed(workspace, project.value(QStringLiteral("top")).toString())) {
            compileStrategy = QStringLiteral("two_stage_mapping");
            repairMode = QStringLiteral("off");
            nextProfile = QStringLiteral("compile_two_stage_mapping");
            stopReason = QStringLiteral("DC reported an internal fatal error after a clean read/link report; retrying with two-stage mapping.");
            continue;
        }
        if (!executionErrors.isEmpty() && round < roundsLimit
            && !sourcePreprocessor.isEmpty()
            && sourcePreprocessMode == QLatin1String("off")
            && hasCPreprocessingError(executionResult)) {
            sourcePreprocessMode = QStringLiteral("cpp");
            nextProfile = QStringLiteral("source_cpp_preprocessed");
            stopReason = QStringLiteral("The compiler error points to C-style RTL preprocessing; retrying with the project's declared preprocessing configuration.");
            continue;
        }
        if (!executionErrors.isEmpty() && round < roundsLimit) {
            const QString candidate = sourceCompatibilityDiagnosis.value(QStringLiteral("file")).toString();
            if (sourceCompatibilityDiagnosis.value(QStringLiteral("safe_exclusion")).toBool()
                && !candidate.isEmpty() && !sourceExcludeFiles.contains(candidate)) {
                sourceExcludeFiles.append(candidate);
                nextProfile = QStringLiteral("source_compatibility_filter_%1").arg(sourceExcludeFiles.size());
                stopReason = QStringLiteral("A fresh compiler diagnostic identified a synthesis-unsupported module that is unreferenced by all other compile inputs; retrying with an audited isolated filelist exclusion.");
                continue;
            }
        }
        if (flowTimedOut) {
            const int next = nextTimeoutMultiplier(flowTimeoutMultiplier);
            if (next > 0) {
                flowTimeoutMultiplier = next;
                nextProfile = QStringLiteral("flow_timeout_x%1").arg(flowTimeoutMultiplier);
                stopReason = QStringLiteral("The flow timed out; retrying once with a larger configured flow timeout.");
                continue;
            }
            stopReason = QStringLiteral("The flow timeout has reached its configured maximum multiplier.");
            break;
        }
        if (atpgTimedOut) {
            const int next = nextTimeoutMultiplier(atpgTimeoutMultiplier);
            if (next > 0) {
                atpgTimeoutMultiplier = next;
                nextProfile = QStringLiteral("atpg_timeout_x%1").arg(atpgTimeoutMultiplier);
                stopReason = QStringLiteral("ATPG timed out; retrying once with a larger configured ATPG timeout.");
                continue;
            }
            stopReason = QStringLiteral("The ATPG timeout has reached its configured maximum multiplier.");
            break;
        }
        if (mbistTimedOut) {
            const int next = nextTimeoutMultiplier(mbistTimeoutMultiplier);
            if (next > 0) {
                mbistTimeoutMultiplier = next;
                nextProfile = QStringLiteral("mbist_timeout_x%1").arg(mbistTimeoutMultiplier);
                stopReason = QStringLiteral("MBIST timed out; retrying with a larger configured simulation timeout.");
                continue;
            }
            stopReason = QStringLiteral("The MBIST timeout has reached its configured maximum multiplier.");
            break;
        }
        if (!executionErrors.isEmpty() && !mbist.isEmpty() && round < roundsLimit
            && !mbist.value(QStringLiteral("compiled")).toBool()
            && mbistIncludeMode == QLatin1String("declared")) {
            mbistIncludeMode = QStringLiteral("compile_parents");
            nextProfile = QStringLiteral("mbist_compile_parent_includes");
            stopReason = QStringLiteral("MBIST compilation failed with declared includes; retrying with source-parent include paths.");
            continue;
        }
        if (!executionErrors.isEmpty() && !mbist.isEmpty() && round < roundsLimit
            && mbist.value(QStringLiteral("compiled")).toBool()
            && mbistDiagnosticMode == QLatin1String("off")) {
            mbistDiagnosticMode = QStringLiteral("verbose");
            nextProfile = QStringLiteral("mbist_verbose_diagnostics");
            stopReason = QStringLiteral("MBIST compiled but failed its run checks; retrying with verbose simulator diagnostics.");
            continue;
        }
        const bool coverageOnlyMiss = !response.value(QStringLiteral("ok")).toBool()
            && coverageValid && coverageTarget >= 0.0 && coverage < coverageTarget
            && executionErrors.isEmpty() && !flowTimedOut && !atpgTimedOut && !mbistTimedOut;
        if (round == roundsLimit || (!response.value(QStringLiteral("ok")).toBool() && !coverageOnlyMiss)) {
            stopReason = response.value(QStringLiteral("message"),
                QStringLiteral("No further evidence-selected optimization round is available.")).toString();
            break;
        }
        if (!executionErrors.isEmpty()) {
            stopReason = QStringLiteral("The current run has execution errors without a safe parameter-only retry.");
            break;
        }
        if (drcCountOk && drcCount > maximumDrc && repairMode == QLatin1String("off")) {
            const QString resetKind = execution.value(QStringLiteral("reset_kind"),
                QStringLiteral("asynchronous")).toString().trimmed().toLower();
            repairMode = resetKind == QLatin1String("synchronous")
                ? QStringLiteral("clock_only") : QStringLiteral("clock_reset_set");
            nextProfile = QStringLiteral("drc_autofix_%1").arg(repairMode);
            stopReason = resetKind == QLatin1String("synchronous")
                ? QStringLiteral("Post-DFT DRC is above the configured limit; the configured primary reset is synchronous, so retrying with clock-only autofix.")
                : QStringLiteral("Post-DFT DRC is above the configured limit; trying clock/reset/set autofix for the configured asynchronous reset.");
            continue;
        }
        if (coverageValid && coverageTarget >= 0.0 && coverage < coverageTarget) {
            if ((coveragePlateau || coverageRegressed)
                && atpgDiagnosticMode == QLatin1String("off")) {
                abortLimit = priorAbortLimit;
                atpgDiagnosticMode = QStringLiteral("fault_classes");
                autoTriggeredFaultDiagnostics = true;
                repairMode = QStringLiteral("off");
                nextProfile = QStringLiteral("atpg_restored_%1_fault_classes").arg(abortLimit);
                stopReason = coverageRegressed
                    ? QStringLiteral("ATPG coverage regressed after a higher abort limit; restoring the previous limit and collecting fault-class diagnostics.")
                    : QStringLiteral("ATPG coverage plateaued at a higher abort limit; restoring the previous limit and collecting fault-class diagnostics.");
                continue;
            }
            const auto next = std::find_if(abortLimits.cbegin(), abortLimits.cend(),
                [abortLimit](int value) { return value > abortLimit; });
            if (next != abortLimits.cend()) {
                abortLimit = *next;
                repairMode = QStringLiteral("off");
                nextProfile = QStringLiteral("atpg_abort_%1").arg(abortLimit);
                stopReason = QStringLiteral("Coverage is below target; increasing ATPG search depth for the next fresh run.");
                continue;
            }
        }
        stopReason = QStringLiteral("The reports do not support another controlled optimization action.");
        break;
    }

    if (selectedPayload.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), stopReason}};
    const QString dataRoot = studioDataRoot(agentRoot);
    const QString reportDirectory = QDir(dataRoot).filePath(QStringLiteral("agent_runtime/iteration_reports/%1")
        .arg(project.value(QStringLiteral("id")).toString()));
    if (!QDir().mkpath(reportDirectory))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"),
            QStringLiteral("Unable to create native optimization report directory.")}};
    const QString reportPath = QDir(reportDirectory).filePath(
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
        + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::Id128).left(8) + QStringLiteral(".json"));
    const QVariantMap optimization{
        {QStringLiteral("project"), project.value(QStringLiteral("id"))},
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("maximum_rounds"), roundsLimit},
        {QStringLiteral("attempted_rounds"), profiles.size()},
        {QStringLiteral("profiles"), profiles},
        {QStringLiteral("selected_round"), selectedRound.value(QStringLiteral("round"))},
        {QStringLiteral("objective_met"), selectedRound.value(QStringLiteral("coverage_met")).toBool()
             && selectedRound.value(QStringLiteral("drc_passed")).toBool()
             && selectedRound.value(QStringLiteral("verification_status")).toString() == QLatin1String("verified")},
        {QStringLiteral("stop_reason"), stopReason},
        {QStringLiteral("report_file"), reportPath}
    };
    QSaveFile report(reportPath);
    const QByteArray reportBytes = QJsonDocument(QJsonObject::fromVariantMap(optimization)).toJson(QJsonDocument::Indented);
    if (!report.open(QIODevice::WriteOnly) || report.write(reportBytes) != reportBytes.size() || !report.commit())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"),
            QStringLiteral("Unable to persist native optimization report: %1").arg(report.errorString())}};
    selectedPayload.insert(QStringLiteral("optimization"), optimization);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), selectedPayload}};
}

} // namespace

bool EdaJobService::supports(const QString &action)
{
    return action == QLatin1String("run_dft_flow")
        || action == QLatin1String("run_dft_iteration")
        || action == QLatin1String("run_dft_optimization")
        || action == QLatin1String("wait_dft_job")
        || action == QLatin1String("status_dft_job")
        || action == QLatin1String("interrupt_dft_job");
}

QVariantMap EdaJobService::dispatch(const QString &action, const QVariantMap &project,
                                    const QVariantMap &arguments, const QString &agentRoot)
{
    if (!supports(action))
        return failure(QStringLiteral("Unsupported EDA job action."));
    const QString sessionId = project.value(QStringLiteral("session_id"),
        project.value(QStringLiteral("sessionId"))).toString().trimmed();
    if (sessionId.isEmpty())
        return failure(QStringLiteral("EDA jobs require an active root session_id."));

    if (action == QLatin1String("run_dft_flow") || action == QLatin1String("run_dft_iteration")
        || action == QLatin1String("run_dft_optimization")) {
        const auto session = jobsForSession(sessionId);
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(session->mutex);
            if (!session->activeFlowJobId.empty()) {
                const auto active = session->jobs.find(session->activeFlowJobId);
                if (active != session->jobs.end()) {
                    if (active->second->operation != action)
                        return failure(QStringLiteral("A DFT job is already active for this root session."));
                    QVariantMap reused = summary(active->second, false);
                    reused.insert(QStringLiteral("reused_active_job"), true);
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), reused}};
                }
                session->activeFlowJobId.clear();
            }
            job = std::make_shared<Job>();
            job->id = QStringLiteral("eda-%1").arg(session->nextJobNumber++);
            job->operation = action;
            job->startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            job->state = QStringLiteral("running");
            session->jobs.emplace(job->id.toStdString(), job);
            session->activeFlowJobId = job->id.toStdString();
        }
        const QVariantMap projectCopy = project;
        const QVariantMap argumentsCopy = effectiveIterationArguments(arguments);
        const QString rootCopy = agentRoot;
        const QString operation = action;
        std::thread([job, projectCopy, argumentsCopy, rootCopy, operation] {
            QVariantMap output;
            QString error;
            try {
                const auto cancelToken = std::shared_ptr<std::atomic_bool>(job, &job->cancelRequested);
                if (operation == QLatin1String("run_dft_optimization")) {
                    output = runOptimization(projectCopy, argumentsCopy, rootCopy, cancelToken);
                } else if (operation == QLatin1String("run_dft_iteration") && FanAtpgService::selected(projectCopy)) {
                    output = FanAtpgService::runIteration(projectCopy, argumentsCopy);
                } else if (FanAtpgService::selected(projectCopy)) {
                    output = FanAtpgService::run(projectCopy, argumentsCopy, rootCopy, cancelToken);
                } else {
                    output = ConfiguredDftFlowService::run(projectCopy,
                        argumentsCopy.value(QStringLiteral("workspace_root")).toString(),
                        argumentsCopy, rootCopy, cancelToken);
                }
            } catch (const std::exception &exception) {
                error = QString::fromUtf8(exception.what());
            } catch (...) {
                error = QStringLiteral("Unknown native DFT worker failure.");
            }
            if (!error.isEmpty()) {
                output.insert(QStringLiteral("ok"), false);
                output.insert(QStringLiteral("error"), error);
                output.insert(QStringLiteral("operation"), operation);
            }
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->result = output;
                job->error = error;
                if (!error.isEmpty())
                    job->state = job->cancelRequested.load(std::memory_order_relaxed)
                        ? QStringLiteral("interrupted") : QStringLiteral("failed");
                else if (job->cancelRequested.load(std::memory_order_relaxed))
                    job->state = QStringLiteral("interrupted");
                else
                    job->state = output.value(QStringLiteral("ok")).toBool()
                        ? QStringLiteral("completed") : QStringLiteral("failed");
                job->completedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            }
            const auto completedSession = jobsForSession(projectCopy.value(QStringLiteral("session_id"),
                projectCopy.value(QStringLiteral("sessionId"))).toString());
            {
                std::lock_guard<std::mutex> lock(completedSession->mutex);
                if (completedSession->activeFlowJobId == job->id.toStdString())
                    completedSession->activeFlowJobId.clear();
            }
            job->finished.notify_all();
        }).detach();
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), summary(job, false)}};
    }

    const QString jobId = arguments.value(QStringLiteral("job_id")).toString().trimmed();
    if (jobId.isEmpty())
        return failure(QStringLiteral("job_id is required."));
    const auto job = findJob(sessionId, jobId);
    if (!job)
        return failure(QStringLiteral("Unknown EDA job for this session: %1").arg(jobId));

    if (action == QLatin1String("wait_dft_job")) {
        const int waitSeconds = boundedInteger(arguments.value(QStringLiteral("wait_seconds")), 900, 1, 86400);
        std::unique_lock<std::mutex> lock(job->mutex);
        const bool complete = job->finished.wait_for(lock, std::chrono::seconds(waitSeconds), [&] {
            return job->state != QLatin1String("queued") && job->state != QLatin1String("running");
        });
        lock.unlock();
        QVariantMap value = summary(job, complete);
        if (!complete) {
            value.insert(QStringLiteral("next_action"), QStringLiteral("wait_dft_job"));
            value.insert(QStringLiteral("wait_timed_out"), true);
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), value}};
    }
    if (action == QLatin1String("status_dft_job"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), summary(job, false)}};
    if (action == QLatin1String("interrupt_dft_job")) {
        job->cancelRequested.store(true, std::memory_order_relaxed);
        const int waitSeconds = boundedInteger(arguments.value(QStringLiteral("wait_seconds")), 5, 0, 30);
        std::unique_lock<std::mutex> lock(job->mutex);
        job->finished.wait_for(lock, std::chrono::seconds(waitSeconds), [&] {
            return job->state != QLatin1String("queued") && job->state != QLatin1String("running");
        });
        lock.unlock();
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), summary(job, true)}};
    }
    return failure(QStringLiteral("Unsupported EDA job action."));
}
