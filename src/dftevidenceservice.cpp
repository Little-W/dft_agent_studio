#include "dftevidenceservice.h"
#include "dftreportevidenceservice.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject objectAt(const QJsonObject &object, const QString &key) {
    return object.value(key).toObject();
}

bool truthy(const QJsonValue &value) {
    if (value.isUndefined() || value.isNull())
        return false;
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0.0;
    if (value.isString())
        return !value.toString().isEmpty();
    if (value.isArray())
        return !value.toArray().isEmpty();
    if (value.isObject())
        return !value.toObject().isEmpty();
    return false;
}

QString pythonString(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

QVariant boundedValues(const QJsonValue &value, qsizetype maximum) {
    if (!value.isArray())
        return QVariantList{};
    QJsonArray bounded;
    const QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < qMin(maximum, array.size()); ++index)
        bounded.append(array.at(index));
    return bounded.toVariantList();
}

QVariant boundedStrings(const QJsonValue &value, qsizetype maximum, qsizetype maximumChars = -1) {
    if (!value.isArray())
        return QStringList{};
    QStringList bounded;
    const QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < qMin(maximum, array.size()); ++index) {
        QString text = pythonString(array.at(index));
        if (maximumChars >= 0)
            text = text.left(maximumChars);
        bounded.append(text);
    }
    return bounded;
}

bool integerNumber(const QJsonValue &value) {
    return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
}

QVariant firstNumeric(const QJsonObject &object, const QStringList &keys, bool integerOnly = false) {
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble() && (!integerOnly || integerNumber(value)))
            return value.toVariant();
    }
    return {};
}

QVariant firstValue(const QJsonObject &object, const QStringList &keys) {
    for (const QString &key : keys) {
        if (object.contains(key))
            return object.value(key).toVariant();
    }
    return {};
}

QVariant numericValue(const QVariant &value, bool integerOnly = false) {
    if (!value.isValid() || value.metaType().id() == QMetaType::Bool)
        return {};
    if (value.metaType().id() == QMetaType::QString) {
        bool valid = false;
        const double number = value.toString().toDouble(&valid);
        if (valid && (!integerOnly || std::floor(number) == number))
            return integerOnly ? QVariant::fromValue<qlonglong>(static_cast<qlonglong>(number))
                               : QVariant(number);
        return {};
    }
    bool valid = false;
    const double number = value.toDouble(&valid);
    if (!valid || (integerOnly && std::floor(number) != number))
        return {};
    return integerOnly ? QVariant::fromValue<qlonglong>(static_cast<qlonglong>(number)) : QVariant(number);
}

QVariant numericValue(const QJsonValue &value, bool integerOnly = false) {
    return numericValue(value.toVariant(), integerOnly);
}

QStringList boundedUniqueText(const QStringList &values, qsizetype maximum) {
    QStringList result;
    QSet<QString> seen;
    for (const QString &value : values) {
        if (value.isEmpty() || seen.contains(value))
            continue;
        seen.insert(value);
        result.append(value);
        if (result.size() >= maximum)
            break;
    }
    return result;
}

void appendText(const QJsonValue &value, QStringList *target, qsizetype maximum) {
    if (target->size() >= maximum || !truthy(value))
        return;
    const QString text = pythonString(value).left(800);
    if (!text.isEmpty())
        target->append(text);
}

void appendTextList(const QJsonValue &value, QStringList *target, qsizetype maximum) {
    if (!value.isArray())
        return;
    for (const QJsonValue &item : value.toArray()) {
        if (target->size() >= maximum)
            return;
        const QString text = pythonString(item).left(800);
        if (!text.trimmed().isEmpty())
            target->append(text);
    }
}

QVariantMap diagnosticResult(const QVariantMap &base, const QString &category,
                             bool retryAllowed, const QString &reason,
                             const QStringList &nextTools) {
    QVariantMap result = base;
    result.insert(QStringLiteral("category"), category);
    result.insert(QStringLiteral("retry_allowed"), retryAllowed);
    result.insert(QStringLiteral("reason"), reason);
    result.insert(QStringLiteral("next_tools"), nextTools);
    return result;
}
}

QVariantMap DftEvidenceService::analyze(const QVariantMap &arguments) {
    const QJsonObject result = QJsonObject::fromVariantMap(arguments.value(QStringLiteral("result")).toMap());
    if (result.isEmpty())
        return {{QStringLiteral("evidence_available"), false},
            {QStringLiteral("status"), QStringLiteral("no_evidence")},
            {QStringLiteral("reason"), QStringLiteral("No completed DFT job result was supplied for analysis.")}};
    QString scope = arguments.value(QStringLiteral("scope"), QStringLiteral("all")).toString().trimmed();
    if (scope != QStringLiteral("all") && scope != QStringLiteral("drc")
        && scope != QStringLiteral("scan") && scope != QStringLiteral("atpg")
        && scope != QStringLiteral("patterns"))
        scope = QStringLiteral("all");

    QList<QJsonObject> records{result};
    const QJsonObject nested = objectAt(result, QStringLiteral("result"));
    if (!nested.isEmpty())
        records.append(nested);
    const QList<QJsonObject> initialRecords = records;
    for (const QJsonObject &candidate : initialRecords) {
        const QJsonObject execution = objectAt(candidate, QStringLiteral("execution"));
        if (!execution.isEmpty())
            records.append(execution);
    }

    QList<QJsonObject> executionRecords;
    for (const QJsonObject &item : records) {
        if (item.value(QStringLiteral("execution")).isObject())
            executionRecords.append(item);
    }
    QJsonObject execution;
    if (!executionRecords.isEmpty()) {
        execution = objectAt(executionRecords.first(), QStringLiteral("execution"));
    } else {
        for (const QJsonObject &item : records) {
            if (item.contains(QStringLiteral("returncode")) || item.contains(QStringLiteral("workspace"))) {
                execution = item;
                break;
            }
        }
    }

    QJsonObject source;
    QJsonObject readiness;
    bool hasSource = false;
    bool hasReadiness = false;
    for (const QJsonObject &item : records) {
        if (!hasSource && item.value(QStringLiteral("source")).isObject()) {
            source = objectAt(item, QStringLiteral("source"));
            hasSource = true;
        }
        if (!hasReadiness && item.value(QStringLiteral("readiness")).isObject()) {
            readiness = objectAt(item, QStringLiteral("readiness"));
            hasReadiness = true;
        }
    }

    QJsonArray acceptance;
    for (const QJsonObject &item : records) {
        const QJsonArray values = item.value(QStringLiteral("acceptance")).toArray();
        for (const QJsonValue &value : values) {
            if (value.isObject())
                acceptance.append(value);
        }
    }
    QJsonArray drcItems;
    for (const QJsonValue &value : acceptance) {
        const QJsonObject item = value.toObject();
        if (item.contains(QStringLiteral("observed_dft_drc_violations"))
            || item.contains(QStringLiteral("maximum_dft_drc_violations")))
            drcItems.append(item);
    }
    QVariant drcObserved;
    QVariant drcLimit;
    for (qsizetype index = drcItems.size(); index > 0; --index) {
        const QJsonObject item = drcItems.at(index - 1).toObject();
        if (!drcObserved.isValid() && integerNumber(item.value(QStringLiteral("observed_dft_drc_violations"))))
            drcObserved = item.value(QStringLiteral("observed_dft_drc_violations")).toVariant();
        if (!drcLimit.isValid() && integerNumber(item.value(QStringLiteral("maximum_dft_drc_violations"))))
            drcLimit = item.value(QStringLiteral("maximum_dft_drc_violations")).toVariant();
    }

    QJsonArray drcBreakdown;
    for (const QJsonObject &item : records) {
        QJsonValue candidate = item.value(QStringLiteral("post_dft_drc_breakdown"));
        if (!candidate.isArray())
            candidate = item.value(QStringLiteral("drc_breakdown"));
        if (!candidate.isArray())
            continue;
        const QJsonArray values = candidate.toArray();
        for (qsizetype index = 0; index < values.size() && drcBreakdown.size() < 32; ++index) {
            if (values.at(index).isObject())
                drcBreakdown.append(values.at(index));
        }
        if (!drcBreakdown.isEmpty())
            break;
    }

    QJsonObject scanConfig = objectAt(source, QStringLiteral("scan_configuration"));
    if (scanConfig.isEmpty())
        scanConfig = objectAt(readiness, QStringLiteral("scan_configuration"));
    QJsonObject atpg;
    for (const QJsonObject &item : records) {
        const QJsonObject candidate = objectAt(item, QStringLiteral("atpg"));
        if (candidate.isEmpty())
            continue;
        const QJsonObject summary = objectAt(candidate, QStringLiteral("summary"));
        for (auto it = summary.constBegin(); it != summary.constEnd(); ++it)
            atpg.insert(it.key(), it.value());
        for (auto it = candidate.constBegin(); it != candidate.constEnd(); ++it) {
            if (it.key() != QStringLiteral("summary"))
                atpg.insert(it.key(), it.value());
        }
    }
    if (atpg.isEmpty()) {
        for (const QJsonObject &item : records) {
            for (const QString &key : {QStringLiteral("coverage_percent"), QStringLiteral("fault_coverage_percent"),
                     QStringLiteral("test_coverage_percent"), QStringLiteral("test_patterns")}) {
                if (item.contains(key))
                    atpg.insert(key, item.value(key));
            }
        }
    }
    QString drcReportPath = pythonString(atpg.value(QStringLiteral("post_dft_drc_report")));
    if (drcReportPath.isEmpty()) {
        for (const QJsonValue &value : acceptance) {
            const QString candidate = pythonString(value.toObject().value(QStringLiteral("path")));
            if (candidate.contains(QStringLiteral("post_dft_drc"))) {
                drcReportPath = candidate;
                break;
            }
        }
    }
    if (!drcObserved.isValid() && !drcReportPath.isEmpty()) {
        const QString workspace = QFileInfo(pythonString(execution.value(QStringLiteral("workspace"))))
            .canonicalFilePath();
        QString candidate = QFileInfo(drcReportPath).isAbsolute()
            ? drcReportPath : QDir(workspace).filePath(drcReportPath);
        const QFileInfo reportInfo(candidate);
        const QString canonicalReport = reportInfo.canonicalFilePath();
        if (!workspace.isEmpty() && !canonicalReport.isEmpty() && !reportInfo.isSymLink()
            && canonicalReport.startsWith(workspace + QDir::separator())) {
            const QVariantMap parsed = DftReportEvidenceService::parseDrcReport(canonicalReport);
            if (parsed.value(QStringLiteral("ok")).toBool()) {
                const QVariantMap report = parsed.value(QStringLiteral("result")).toMap();
                const QVariant total = report.value(QStringLiteral("total_violations"));
                bool countOk = false;
                const int count = total.toInt(&countOk);
                if (countOk && count >= 0) {
                    drcObserved = count;
                    drcReportPath = canonicalReport;
                }
            }
        }
    }
    const QVariant coverage = firstNumeric(atpg, {QStringLiteral("fault_coverage_percent"),
        QStringLiteral("coverage_percent"), QStringLiteral("test_coverage_percent")});
    const QVariant patterns = firstNumeric(atpg, {QStringLiteral("test_patterns"),
        QStringLiteral("internal_patterns"), QStringLiteral("patterns"), QStringLiteral("pattern_count")}, true);

    QJsonObject artifacts = objectAt(execution, QStringLiteral("output_exports"));
    QString backend;
    QString status;
    QString evidenceFile;
    for (const QJsonObject &item : records) {
        if (backend.isEmpty() && truthy(item.value(QStringLiteral("backend"))))
            backend = pythonString(item.value(QStringLiteral("backend")));
        if (status.isEmpty() && truthy(item.value(QStringLiteral("status"))))
            status = pythonString(item.value(QStringLiteral("status")));
        const QJsonObject crossValidation = objectAt(item, QStringLiteral("cross_validation"));
        if (status.isEmpty() && truthy(crossValidation.value(QStringLiteral("status"))))
            status = pythonString(crossValidation.value(QStringLiteral("status")));
        if (evidenceFile.isEmpty() && truthy(item.value(QStringLiteral("evidence_file"))))
            evidenceFile = pythonString(item.value(QStringLiteral("evidence_file")));
    }
    if (evidenceFile.isEmpty() && truthy(execution.value(QStringLiteral("workspace"))))
        evidenceFile = QDir(pythonString(execution.value(QStringLiteral("workspace"))))
            .filePath(QStringLiteral("skill_result.json"));

    QVariantList failedChecks;
    const QJsonArray failedAcceptance = objectAt(result, QStringLiteral("acceptance")).value(
        QStringLiteral("checks")).toArray();
    QJsonArray acceptanceChecks = failedAcceptance;
    if (acceptanceChecks.isEmpty()) {
        const QJsonArray directChecks = result.value(QStringLiteral("acceptance")).toArray();
        acceptanceChecks = directChecks;
    }
    for (const QJsonValue &value : acceptanceChecks) {
        if (!value.isObject())
            continue;
        const QJsonObject check = value.toObject();
        const QJsonArray issues = check.value(QStringLiteral("issues")).toArray();
        const bool passed = check.contains(QStringLiteral("passed"))
            ? check.value(QStringLiteral("passed")).toBool()
            : (check.value(QStringLiteral("exists")).toBool(true) && issues.isEmpty());
        if (passed && issues.isEmpty())
            continue;
        QVariantList boundedIssues;
        for (qsizetype index = 0; index < qMin<qsizetype>(6, issues.size()); ++index)
            boundedIssues.append(pythonString(issues.at(index)).left(500));
        failedChecks.append(QVariantMap{
            {QStringLiteral("path"), pythonString(check.value(QStringLiteral("path")))},
            {QStringLiteral("issues"), boundedIssues},
            {QStringLiteral("contains"), pythonString(check.value(QStringLiteral("contains")))},
        });
        if (failedChecks.size() >= 16)
            break;
    }
    if (status.isEmpty()) {
        status = !failedChecks.isEmpty() || !truthy(execution.value(QStringLiteral("completed_cleanly")))
            ? QStringLiteral("blocked") : QStringLiteral("evidence_ready");
    } else if (status == QLatin1String("completed") && !failedChecks.isEmpty()) {
        status = QStringLiteral("review_blocked");
    }

    QVariantMap drc{{QStringLiteral("observed_violations"), drcObserved},
        {QStringLiteral("maximum_allowed"), drcLimit}, {QStringLiteral("breakdown"), drcBreakdown.toVariantList()},
        {QStringLiteral("report"), drcReportPath}};
    if (drcObserved.isValid() && drcLimit.isValid())
        drc.insert(QStringLiteral("passed"), drcObserved.toLongLong() <= drcLimit.toLongLong());
    else
        drc.insert(QStringLiteral("passed"), QVariant{});

    const QJsonValue chainCount = scanConfig.value(QStringLiteral("chain_count"));
    const QJsonValue clocks = scanConfig.value(QStringLiteral("clocks"));
    QVariantMap scan{
        {QStringLiteral("enabled"), truthy(chainCount) || truthy(clocks)},
        {QStringLiteral("chain_count"), chainCount.toVariant()},
        {QStringLiteral("max_chain_length"), scanConfig.value(QStringLiteral("max_chain_length")).toVariant()},
        {QStringLiteral("clocks"), boundedValues(clocks, 16)},
        {QStringLiteral("resets"), boundedValues(scanConfig.value(QStringLiteral("resets")), 16)},
    };

    QVariant target;
    for (const QJsonValue &value : acceptance) {
        const QJsonValue candidate = value.toObject().value(QStringLiteral("minimum_percentage"));
        if (candidate.isDouble()) {
            target = candidate.toVariant();
            break;
        }
    }
    QVariantMap atpgResult{
        {QStringLiteral("enabled"), atpg.contains(QStringLiteral("enabled"))
             ? atpg.value(QStringLiteral("enabled")).toVariant() : QVariant{}},
        {QStringLiteral("tool"), atpg.contains(QStringLiteral("tool"))
             ? atpg.value(QStringLiteral("tool")).toVariant() : QVariant{}},
        {QStringLiteral("coverage_percent"), coverage}, {QStringLiteral("target_percent"), target},
        {QStringLiteral("patterns"), patterns},
        {QStringLiteral("total_faults"), atpg.contains(QStringLiteral("total_faults"))
             ? atpg.value(QStringLiteral("total_faults")).toVariant() : QVariant{}},
        {QStringLiteral("fault_classes"), atpg.value(QStringLiteral("fault_classes")).isUndefined()
             ? QVariantMap{} : atpg.value(QStringLiteral("fault_classes")).toVariant()},
        {QStringLiteral("drc_breakdown"), atpg.value(QStringLiteral("drc_breakdown")).isUndefined()
             ? QVariantList{} : atpg.value(QStringLiteral("drc_breakdown")).toVariant()},
        {QStringLiteral("drc_completed"), atpg.contains(QStringLiteral("drc_completed"))
             ? atpg.value(QStringLiteral("drc_completed")).toVariant() : QVariant{}},
        {QStringLiteral("blocking_errors"), boundedStrings(atpg.value(QStringLiteral("blocking_errors")), 16)},
        {QStringLiteral("scan_chain_blockers"), boundedStrings(atpg.value(QStringLiteral("scan_chain_blockers")), 24)},
    };
    const QJsonArray errors = execution.value(QStringLiteral("errors")).toArray();
    QVariantList boundedErrors;
    for (qsizetype index = 0; index < qMin<qsizetype>(12, errors.size()); ++index)
        boundedErrors.append(pythonString(errors.at(index)).left(800));
    for (const QJsonObject &item : records) {
        for (const QString &key : {QStringLiteral("error"), QStringLiteral("message")}) {
            if (!truthy(item.value(key)) || boundedErrors.size() >= 12)
                continue;
            const QString text = pythonString(item.value(key)).left(800);
            if (!text.isEmpty() && !boundedErrors.contains(text))
                boundedErrors.append(text);
        }
    }
    QVariantMap executionResult{
        {QStringLiteral("workspace"), pythonString(execution.value(QStringLiteral("workspace")))},
        {QStringLiteral("flow_directory"), pythonString(execution.value(QStringLiteral("flow_directory")))},
        {QStringLiteral("log"), pythonString(execution.value(QStringLiteral("log")))},
        {QStringLiteral("returncode"), execution.value(QStringLiteral("returncode")).toVariant()},
        {QStringLiteral("timed_out"), truthy(execution.value(QStringLiteral("timed_out")))},
        {QStringLiteral("completed_cleanly"), truthy(execution.value(QStringLiteral("completed_cleanly")))},
        {QStringLiteral("errors"), boundedErrors},
    };
    QVariantMap artifactResult;
    for (auto it = artifacts.constBegin(); it != artifacts.constEnd(); ++it) {
        if (truthy(it.value()))
            artifactResult.insert(it.key(), pythonString(it.value()));
    }

    QVariantMap analyzed{
        {QStringLiteral("evidence_available"), true}, {QStringLiteral("backend"), backend},
        {QStringLiteral("status"), status}, {QStringLiteral("drc"), drc},
        {QStringLiteral("scan"), scan}, {QStringLiteral("atpg"), atpgResult},
        {QStringLiteral("patterns"), QVariantMap{
            {QStringLiteral("count"), patterns},
            {QStringLiteral("compression_enabled"), atpg.value(QStringLiteral("compression_enabled")).toVariant()},
            {QStringLiteral("pattern_file"), atpg.value(QStringLiteral("pattern_file")).isUndefined()
                 ? QVariant(QString{}) : atpg.value(QStringLiteral("pattern_file")).toVariant()},
        }},
        {QStringLiteral("execution"), executionResult}, {QStringLiteral("artifacts"), artifactResult},
        {QStringLiteral("evidence_file"), evidenceFile}, {QStringLiteral("failed_checks"), failedChecks},
        {QStringLiteral("errors"), boundedErrors},
    };
    if (scope == QStringLiteral("all"))
        return analyzed;
    return {{QStringLiteral("evidence_available"), true}, {QStringLiteral("scope"), scope},
        {scope, analyzed.value(scope)}, {QStringLiteral("status"), status},
        {QStringLiteral("backend"), backend}, {QStringLiteral("evidence_file"), evidenceFile}};
}

QVariantMap DftEvidenceService::diagnose(const QVariantMap &arguments) {
    const QJsonObject latest = QJsonObject::fromVariantMap(arguments.value(QStringLiteral("result")).toMap());
    QList<QJsonObject> records{latest};
    if (latest.value(QStringLiteral("result")).isObject())
        records.append(latest.value(QStringLiteral("result")).toObject());

    QStringList errors;
    QStringList blockers;
    QStringList backends;
    QList<QJsonObject> goals;
    QList<QJsonObject> candidates;
    QVariant returnCode;
    bool hasReturnCode = false;
    QString status = QStringLiteral("unknown");
    QVariant objectiveMet;
    for (const QJsonObject &record : records) {
        QList<QJsonObject> sources{record};
        if (record.value(QStringLiteral("execution")).isObject())
            sources.append(record.value(QStringLiteral("execution")).toObject());
        for (const QJsonObject &candidate : sources) {
            appendTextList(candidate.value(QStringLiteral("errors")), &errors, 12);
            if (candidate.value(QStringLiteral("error")).isString())
                appendText(candidate.value(QStringLiteral("error")), &errors, 12);
            const QJsonValue code = candidate.value(QStringLiteral("returncode"));
            if (!hasReturnCode && code.isDouble() && integerNumber(code)) {
                returnCode = code.toVariant();
                hasReturnCode = true;
            }
            const QJsonObject validation = objectAt(candidate, QStringLiteral("cross_validation"));
            if (truthy(validation.value(QStringLiteral("status"))))
                status = pythonString(validation.value(QStringLiteral("status")));
            appendTextList(validation.value(QStringLiteral("blockers")), &blockers, 12);
            const QJsonValue backend = candidate.value(QStringLiteral("backend"));
            if (backend.isString() && !backend.toString().trimmed().isEmpty())
                backends.append(backend.toString());
            const QJsonValue goal = candidate.value(QStringLiteral("goal"));
            if (goal.isObject())
                goals.append(goal.toObject());
            if (candidate.value(QStringLiteral("objective_met")).isBool())
                objectiveMet = candidate.value(QStringLiteral("objective_met")).toVariant();
            for (const QJsonValue &value : candidate.value(QStringLiteral("candidates")).toArray()) {
                if (value.isObject())
                    candidates.append(value.toObject());
            }
        }
    }
    errors = boundedUniqueText(errors, 12);
    blockers = boundedUniqueText(blockers, 12);
    backends = boundedUniqueText(backends, backends.size());

    QVariant targetCoverage;
    QVariant targetPatterns;
    for (const QJsonObject &goal : goals) {
        const QVariant coverage = numericValue(firstValue(goal, {QStringLiteral("min_coverage"),
            QStringLiteral("minimum_coverage")}));
        if (coverage.isValid() && (!targetCoverage.isValid() || coverage.toDouble() > targetCoverage.toDouble()))
            targetCoverage = coverage;
        const QVariant patternLimit = numericValue(firstValue(goal, {QStringLiteral("max_patterns"),
            QStringLiteral("maximum_patterns")}), true);
        if (patternLimit.isValid() && (!targetPatterns.isValid() || patternLimit.toLongLong() < targetPatterns.toLongLong()))
            targetPatterns = patternLimit;
    }

    QList<double> coverages;
    QList<qlonglong> patternCounts;
    for (const QJsonObject &candidate : candidates) {
        if (candidate.value(QStringLiteral("success")).isBool()
            && !candidate.value(QStringLiteral("success")).toBool())
            continue;
        const QJsonObject metrics = objectAt(candidate, QStringLiteral("metrics"));
        const QVariant coverage = numericValue(firstValue(metrics, {QStringLiteral("fault_coverage"),
            QStringLiteral("test_coverage"), QStringLiteral("coverage")}));
        if (coverage.isValid())
            coverages.append(coverage.toDouble());
        const QVariant count = numericValue(firstValue(metrics, {QStringLiteral("patterns"),
            QStringLiteral("pattern_count")}), true);
        if (count.isValid())
            patternCounts.append(count.toLongLong());
    }

    const QVariantMap structured = analyze({{QStringLiteral("result"), latest}});
    const QVariantMap atpg = structured.value(QStringLiteral("atpg")).toMap();
    const QVariant structuredCoverage = numericValue(atpg.value(QStringLiteral("coverage_percent")));
    if (structuredCoverage.isValid())
        coverages.append(structuredCoverage.toDouble());
    const QVariant structuredPatterns = numericValue(atpg.value(QStringLiteral("patterns")), true);
    if (structuredPatterns.isValid())
        patternCounts.append(structuredPatterns.toLongLong());
    if (!targetCoverage.isValid())
        targetCoverage = numericValue(atpg.value(QStringLiteral("target_percent")));

    QVariant bestCoverage;
    if (!coverages.isEmpty())
        bestCoverage = *std::max_element(coverages.cbegin(), coverages.cend());
    QVariant lowestPatterns;
    if (!patternCounts.isEmpty())
        lowestPatterns = *std::min_element(patternCounts.cbegin(), patternCounts.cend());
    const QString backend = backends.isEmpty() ? QString{} : backends.first();
    QString joined;
    for (const QString &item : errors + blockers)
        joined += item.toCaseFolded() + QLatin1Char('\n');
    const QVariantMap base{
        {QStringLiteral("evidence_available"), true},
        {QStringLiteral("returncode"), hasReturnCode ? returnCode : QVariant{}},
        {QStringLiteral("cross_validation_status"), status},
        {QStringLiteral("errors"), errors}, {QStringLiteral("blockers"), blockers},
        {QStringLiteral("source"), QStringLiteral("current_turn_controlled_execution")},
    };

    for (const QString &error : errors) {
        if (error.contains(QStringLiteral("ver-294"), Qt::CaseInsensitive)
            || (error.contains(QStringLiteral("syntax error"), Qt::CaseInsensitive)
                && error.contains(QLatin1Char('#')))) {
            QVariantMap result = diagnosticResult(base, QStringLiteral("source_preprocessing_required"), true,
                QStringLiteral("综合在生成 RTL 的预处理语法处失败；当前项目已配置 cpp 宏文件，应在隔离工作区启用预处理后重试。"),
                {QStringLiteral("run_dft_iteration"), QStringLiteral("analyze_dft_results")});
            result.insert(QStringLiteral("suggested_iteration"), QVariantMap{
                {QStringLiteral("source_dependency_mode"), QStringLiteral("off")},
                {QStringLiteral("source_preprocess_mode"), QStringLiteral("cpp")},
                {QStringLiteral("source_extra_files"), QStringList{
                    QStringLiteral("vmod/vlibs/*.v"), QStringLiteral("vmod/rams/synth/*.v"),
                    QStringLiteral("vmod/rams/model/*.v")}},
            });
            return result;
        }
    }
    if (status == QStringLiteral("verified"))
        return diagnosticResult(base, QStringLiteral("verified"), false,
            QStringLiteral("该受控流程已经取得交叉验证证据，不应为了诊断而再次运行或启动终端。"),
            {QStringLiteral("read_file"), QStringLiteral("exec_command")});

    const bool coverageMissed = targetCoverage.isValid()
        && (!bestCoverage.isValid() || bestCoverage.toDouble() < targetCoverage.toDouble());
    const bool patternsMissed = targetPatterns.isValid()
        && (!lowestPatterns.isValid() || lowestPatterns.toLongLong() > targetPatterns.toLongLong());
    if (objectiveMet.isValid() && !objectiveMet.toBool() && (coverageMissed || patternsMissed)) {
        QVariantMap result = base;
        if (targetCoverage.isValid()) result.insert(QStringLiteral("target_fault_coverage"), targetCoverage);
        if (bestCoverage.isValid()) result.insert(QStringLiteral("observed_best_fault_coverage"), bestCoverage);
        if (targetPatterns.isValid()) result.insert(QStringLiteral("target_max_patterns"), targetPatterns);
        if (lowestPatterns.isValid()) result.insert(QStringLiteral("observed_lowest_patterns"), lowestPatterns);
        QStringList reasons;
        if (coverageMissed) {
            const QString observed = bestCoverage.isValid()
                ? QStringLiteral("最高实测覆盖率 %1% ").arg(bestCoverage.toDouble(), 0, 'g', 12).trimmed()
                : QStringLiteral("没有成功候选的覆盖率度量");
            reasons.append(QStringLiteral("%1 未达到目标 %2%").arg(observed)
                .arg(targetCoverage.toDouble(), 0, 'g', 12));
        }
        if (patternsMissed) {
            const QString observed = lowestPatterns.isValid()
                ? QStringLiteral("最少实测向量数 %1").arg(lowestPatterns.toLongLong())
                : QStringLiteral("没有成功候选的向量数度量");
            reasons.append(QStringLiteral("%1 未满足上限 %2").arg(observed).arg(targetPatterns.toLongLong()));
        }
        const bool fanExhausted = backend == QStringLiteral("fan_atpg");
        result.insert(QStringLiteral("category"), coverageMissed
            ? QStringLiteral("coverage_shortfall") : QStringLiteral("pattern_budget_exceeded"));
        result.insert(QStringLiteral("retry_allowed"), !fanExhausted);
        result.insert(QStringLiteral("reason"), reasons.join(QStringLiteral("；"))
            + QStringLiteral("。该结论来自本回合受控流程返回的结构化目标和候选实测值。"));
        result.insert(QStringLiteral("next_tools"), fanExhausted
            ? QStringList{QStringLiteral("read_file"), QStringLiteral("exec_command")}
            : QStringList{QStringLiteral("read_file"), QStringLiteral("run_dft_iteration")});
        result.insert(QStringLiteral("operator_action"), fanExhausted
            ? QStringLiteral("FAN 已遍历当前项目批准的压缩配置；请降低目标，或由工程师增加并批准新的配置后重新运行。")
            : QStringLiteral("先核对受控报告；仅在项目允许的参数范围内进行下一轮迭代。"));
        return result;
    }
    if (joined.contains(QStringLiteral("dcsh-1")) || joined.contains(QStringLiteral("design compiler is not enabled"))) {
        QVariantMap result = diagnosticResult(base, QStringLiteral("dc_license_or_feature_unavailable"), false,
            QStringLiteral("Design Compiler 明确报告未启用；这不是 RTL、Tcl 或 ATPG 参数问题。"),
            {QStringLiteral("read_file"), QStringLiteral("exec_command")});
        result.insert(QStringLiteral("operator_action"), QStringLiteral("确认当前主机的 Design Compiler 许可证、功能特性和环境配置；恢复后从受控基线重新运行。"));
        return result;
    }
    if (joined.contains(QStringLiteral("启动器不可用")) || joined.contains(QStringLiteral("not found"))
        || joined.contains(QStringLiteral("no such file"))) {
        QVariantMap result = diagnosticResult(base, QStringLiteral("eda_launcher_or_input_unavailable"), false,
            QStringLiteral("执行证据表明 EDA 启动器或必需输入不可用，不能通过 DFT 参数迭代修复。"),
            {QStringLiteral("check_dft_readiness"), QStringLiteral("inspect_project")});
        result.insert(QStringLiteral("operator_action"), QStringLiteral("修正项目保存的工具路径、PATH 或输入文件配置后再运行基线。"));
        return result;
    }
    if (joined.contains(QStringLiteral("文件清单")) || joined.contains(QStringLiteral("source_manifest"))
        || joined.contains(QStringLiteral("未解析")) || joined.contains(QStringLiteral("缺少源文件"))) {
        QVariantMap result = diagnosticResult(base, QStringLiteral("source_manifest_failure"), false,
            QStringLiteral("证据明确指向 RTL/filelist 输入不完整或未解析。"),
            {QStringLiteral("inspect_project"), QStringLiteral("search_project_text"),
             QStringLiteral("read_project_excerpt"), QStringLiteral("propose_patch")});
        result.insert(QStringLiteral("operator_action"), QStringLiteral("先定位并最小化修复 filelist 或受控源依赖，再请求人工批准补丁验证。"));
        return result;
    }
    if (joined.contains(QStringLiteral("coverage")) || joined.contains(QStringLiteral("fault coverage")))
        return diagnosticResult(base, QStringLiteral("coverage_shortfall"), true,
            QStringLiteral("执行证据包含覆盖率不足信息；先读取对应受控报告，再只改变一组已允许的 ATPG 参数。"),
            {QStringLiteral("read_file"), QStringLiteral("run_dft_iteration")});
    if (joined.contains(QStringLiteral("drc")) || joined.contains(QStringLiteral("violation")))
        return diagnosticResult(base, QStringLiteral("dft_drc_failure"), true,
            QStringLiteral("执行证据包含 DFT DRC 失败信息；先定位具体时钟、复位或 Scan 结构，再提出最小补丁或受限修复参数。"),
            {QStringLiteral("terminal_execute"), QStringLiteral("propose_patch"), QStringLiteral("run_dft_iteration")});
    return diagnosticResult(base, QStringLiteral("unclassified_execution_failure"), false,
        QStringLiteral("现有结构化证据不足以安全选择 DFT 参数；应先做有界的只读诊断，而不是盲目重试。"),
        {QStringLiteral("exec_command"), QStringLiteral("read_file")});
}
