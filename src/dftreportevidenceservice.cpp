#include "dftreportevidenceservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 kMaximumEvidenceJsonBytes = 64LL * 1024 * 1024;
constexpr qint64 kMaximumReportLineBytes = 1024 * 1024;
const QStringList kActions{
    QStringLiteral("dft_report_parse_drc"),
    QStringLiteral("dft_report_parse_atpg"),
    QStringLiteral("dft_report_validate_acceptance"),
    QStringLiteral("dft_evidence_cross_validate")
};

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QVariantMap success(const QVariantMap &result)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

QString normalizedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool within(const QString &path, const QString &root)
{
    const QString target = QDir::cleanPath(path);
    const QString base = QDir::cleanPath(root);
    return target == base || target.startsWith(base + QDir::separator());
}

bool jsonObjectFile(const QString &path, QVariantMap *map, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.size() > kMaximumEvidenceJsonBytes) {
        if (error)
            *error = QStringLiteral("Execution evidence exceeds the 64 MiB native parsing limit.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid execution evidence JSON: %1").arg(parseError.errorString());
        return false;
    }
    *map = document.object().toVariantMap();
    return true;
}

struct ReportFacts {
    bool readable = false;
    qint64 bytes = 0;
    qint64 lines = 0;
    QString error;
    QMap<QString, QVariantMap> drcRules;
    int totalViolations = -1;
    QStringList diagnostics;
    QStringList linkDiagnostics;
    QVariant testCoverage;
    QVariant faultCoverage;
    QVariant tcCoverage;
    QVariant atpgEffectiveness;
    QVariant mbistCaseCoverage;
    QVariant testPatterns;
    QVariant totalFaults;
    int tracedScanChains = 0;
    QVariantMap faultClasses;
    QStringList allWarnings;
    bool testmaxDrcSuccess = false;
};

void updateNumber(QVariant *target, const QRegularExpressionMatch &match)
{
    bool valid = false;
    const double number = match.captured(1).toDouble(&valid);
    if (valid && std::isfinite(number))
        *target = number;
}

void addErrorDiagnostic(const QString &line, QStringList *errors, QStringList *linkErrors)
{
    QString normalized = line.trimmed();
    if (normalized.startsWith(QStringLiteral("//")))
        normalized = normalized.mid(2).trimmed();
    else if (normalized.startsWith(QLatin1Char('#')))
        normalized = normalized.mid(1).trimmed();
    if ((normalized.startsWith(QStringLiteral("Error:")) || normalized.startsWith(QStringLiteral("Fatal:")))
        && errors->size() < 100)
        errors->append(line.trimmed());
    if (normalized.startsWith(QStringLiteral("Warning:"))
        && normalized.contains(QStringLiteral("Unable to resolve reference '"))
        && normalized.contains(QStringLiteral("(LINK-5)"))
        && linkErrors->size() < 100)
        linkErrors->append(line.trimmed());
}

ReportFacts scanReport(const QString &path)
{
    ReportFacts facts;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        facts.error = file.errorString();
        return facts;
    }
    facts.readable = true;
    facts.bytes = file.size();
    static const QRegularExpression drcLine(
        QStringLiteral("^\\s*(\\d+)\\s+(.+?)\\s+violations?\\s+\\(([A-Z][A-Z0-9_-]+)\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression totalDrc(QStringLiteral("Total violations:\\s*([0-9]+)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression testCoverage(
        QStringLiteral("test[ _-]?coverage\\s*[:=]?\\s*([0-9]+(?:\\.[0-9]+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression faultCoverage(
        QStringLiteral("fault[ _-]?coverage\\s*[:=]?\\s*([0-9]+(?:\\.[0-9]+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tcCoverage(
        QStringLiteral("(?:%TC|TC%)\\s*[:=]?\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mbistCoverage(
        QStringLiteral("\"declared_case_coverage_percent\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tessentTestCoverage(
        QStringLiteral("test_coverage\\s+([0-9]+(?:\\.[0-9]+)?)%"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tessentFaultCoverage(
        QStringLiteral("fault_coverage\\s+([0-9]+(?:\\.[0-9]+)?)%"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tessentEffectiveness(
        QStringLiteral("atpg_effectiveness\\s+([0-9]+(?:\\.[0-9]+)?)%"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression testmaxPatterns(QStringLiteral("#internal patterns\\s+([0-9]+)"),
                                                     QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tessentPatterns(QStringLiteral("#test_patterns\\s+([0-9]+)"),
                                                     QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression testmaxFaults(QStringLiteral("total faults\\s+([0-9]+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tessentFull(QStringLiteral("FU\\s+\\(full\\)\\s+([0-9]+)"),
                                                 QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression traced(QStringLiteral("successfully traced with scan_cells"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QStringList testmaxLabels{
        QStringLiteral("Detected"), QStringLiteral("Possibly detected"),
        QStringLiteral("Undetectable"), QStringLiteral("ATPG untestable"), QStringLiteral("Not detected")
    };
    const QStringList testmaxKeys{
        QStringLiteral("detected"), QStringLiteral("possibly_detected"),
        QStringLiteral("undetectable"), QStringLiteral("atpg_untestable"), QStringLiteral("not_detected")
    };
    const QStringList tessentCodes{QStringLiteral("FU"), QStringLiteral("DS"), QStringLiteral("DI"),
                                    QStringLiteral("UU"), QStringLiteral("RE"), QStringLiteral("AU"), QStringLiteral("PC")};
    const QStringList tessentKeys{QStringLiteral("full"), QStringLiteral("detected_simulation"),
                                  QStringLiteral("detected_implication"), QStringLiteral("unused"),
                                  QStringLiteral("redundant"), QStringLiteral("atpg_untestable"),
                                  QStringLiteral("pin_constraints")};
    static const QList<QRegularExpression> testmaxClassExpressions = [] {
        QList<QRegularExpression> expressions;
        for (const QString &label : {QStringLiteral("Detected"), QStringLiteral("Possibly detected"),
                                     QStringLiteral("Undetectable"), QStringLiteral("ATPG untestable"),
                                     QStringLiteral("Not detected")}) {
            expressions.append(QRegularExpression(QStringLiteral("^\\s*%1\\s+\\S+\\s+([0-9]+)\\s*$")
                .arg(QRegularExpression::escape(label)), QRegularExpression::CaseInsensitiveOption));
        }
        return expressions;
    }();
    static const QList<QRegularExpression> tessentClassExpressions = [] {
        QList<QRegularExpression> expressions;
        for (const QString &code : {QStringLiteral("FU"), QStringLiteral("DS"), QStringLiteral("DI"),
                                    QStringLiteral("UU"), QStringLiteral("RE"), QStringLiteral("AU"),
                                    QStringLiteral("PC")}) {
            expressions.append(QRegularExpression(QStringLiteral("^\\s*%1\\s+\\([^)]+\\)\\s+([0-9]+)")
                .arg(code), QRegularExpression::CaseInsensitiveOption));
        }
        return expressions;
    }();
    int oversizedLineBytes = 0;
    while (!file.atEnd()) {
        QByteArray raw = file.readLine(kMaximumReportLineBytes + 1);
        const bool completeLine = raw.endsWith('\n') || file.atEnd();
        if (raw.size() > kMaximumReportLineBytes && !completeLine) {
            do {
                raw = file.readLine(kMaximumReportLineBytes + 1);
            } while (!raw.endsWith('\n') && !file.atEnd());
            ++oversizedLineBytes;
            ++facts.lines;
            continue;
        }
        ++facts.lines;
        const QString line = QString::fromUtf8(raw).trimmed();
        auto match = drcLine.match(line);
        if (match.hasMatch()) {
            bool countOk = false;
            const int count = match.captured(1).toInt(&countOk);
            if (countOk) {
                const QString rule = match.captured(3).toUpper();
                facts.drcRules.insert(rule, {{QStringLiteral("rule"), rule},
                                             {QStringLiteral("count"), count},
                                             {QStringLiteral("description"), match.captured(2).trimmed()}});
            }
        }
        match = totalDrc.match(line);
        if (match.hasMatch())
            facts.totalViolations = match.captured(1).toInt();
        match = testCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.testCoverage, match);
        match = faultCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.faultCoverage, match);
        match = tcCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.tcCoverage, match);
        match = mbistCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.mbistCaseCoverage, match);
        match = tessentTestCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.testCoverage, match);
        match = tessentFaultCoverage.match(line);
        if (match.hasMatch())
            updateNumber(&facts.faultCoverage, match);
        match = tessentEffectiveness.match(line);
        if (match.hasMatch())
            updateNumber(&facts.atpgEffectiveness, match);
        match = testmaxPatterns.match(line);
        if (match.hasMatch())
            facts.testPatterns = match.captured(1).toInt();
        match = tessentPatterns.match(line);
        if (match.hasMatch())
            facts.testPatterns = match.captured(1).toInt();
        match = testmaxFaults.match(line);
        if (match.hasMatch())
            facts.totalFaults = match.captured(1).toInt();
        match = tessentFull.match(line);
        if (match.hasMatch())
            facts.totalFaults = match.captured(1).toInt();
        if (traced.match(line).hasMatch())
            ++facts.tracedScanChains;
        if (line.contains(QStringLiteral("Design rules checking was successful")))
            facts.testmaxDrcSuccess = true;
        if (line.startsWith(QStringLiteral("Warning:")) && facts.allWarnings.size() < 1000)
            facts.allWarnings.append(line);
        addErrorDiagnostic(line, &facts.diagnostics, &facts.linkDiagnostics);

        for (qsizetype index = 0; index < testmaxLabels.size(); ++index) {
            if (facts.faultClasses.contains(testmaxKeys.at(index)))
                continue;
            const auto classMatch = testmaxClassExpressions.at(index).match(line);
            if (classMatch.hasMatch())
                facts.faultClasses.insert(testmaxKeys.at(index), classMatch.captured(1).toInt());
        }
        for (qsizetype index = 0; index < tessentCodes.size(); ++index) {
            if (facts.faultClasses.contains(tessentKeys.at(index)))
                continue;
            const auto classMatch = tessentClassExpressions.at(index).match(line);
            if (classMatch.hasMatch())
                facts.faultClasses.insert(tessentKeys.at(index), classMatch.captured(1).toInt());
        }
    }
    if (oversizedLineBytes)
        facts.error = QStringLiteral("%1 report line(s) exceeded the 1 MiB inspection bound and were skipped.").arg(oversizedLineBytes);
    return facts;
}

QVariantList sortedDrcRules(const QMap<QString, QVariantMap> &rules, int maximum)
{
    QVariantList result;
    QList<QVariantMap> values = rules.values();
    std::sort(values.begin(), values.end(), [](const QVariantMap &left, const QVariantMap &right) {
        const int leftCount = left.value(QStringLiteral("count")).toInt();
        const int rightCount = right.value(QStringLiteral("count")).toInt();
        if (leftCount != rightCount)
            return leftCount > rightCount;
        return left.value(QStringLiteral("rule")).toString() < right.value(QStringLiteral("rule")).toString();
    });
    for (qsizetype index = 0; index < std::min<qsizetype>(maximum, values.size()); ++index)
        result.append(values.at(index));
    return result;
}

QString sha256File(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error)
                *error = file.errorString();
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QVariantList asStringList(const QVariant &value, bool *valid)
{
    if (value.metaType().id() == QMetaType::QString) {
        *valid = true;
        return {value};
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        *valid = std::all_of(value.toList().cbegin(), value.toList().cend(), [](const QVariant &entry) {
            return entry.metaType().id() == QMetaType::QString;
        });
        return value.toList();
    }
    *valid = !value.isValid() || value.isNull();
    return {};
}

QVariantMap checkReport(const QString &root, const QVariantMap &expectation, QStringList *validPaths)
{
    QStringList issues;
    const QString relative = expectation.value(QStringLiteral("path")).toString();
    if (relative.isEmpty())
        return {{QStringLiteral("path"), relative}, {QStringLiteral("passed"), false},
                {QStringLiteral("blocking"), true}, {QStringLiteral("issues"), QStringList{QStringLiteral("An acceptance check has no report path.")}}};
    const QString path = normalizedPath(QDir(root).filePath(relative));
    if (!within(path, root)) {
        return {{QStringLiteral("path"), relative}, {QStringLiteral("passed"), false},
                {QStringLiteral("blocking"), true}, {QStringLiteral("issues"), QStringList{QStringLiteral("Acceptance report path is outside the staged workspace.")}}};
    }
    const bool exists = QFileInfo(path).isFile();
    if (exists)
        validPaths->append(path);
    ReportFacts facts;
    if (exists)
        facts = scanReport(path);
    const QVariant containsValue = expectation.value(QStringLiteral("contains"));
    const QString contains = containsValue.toString();
    const QVariantList forbidden = expectation.value(QStringLiteral("not_contains")).toList();
    bool notContainsPassed = true;
    const QStringList forbiddenStrings = [&] {
        QStringList items;
        if (expectation.value(QStringLiteral("not_contains")).metaType().id() == QMetaType::QString)
            items.append(expectation.value(QStringLiteral("not_contains")).toString());
        else
            for (const QVariant &value : forbidden)
                items.append(value.toString());
        return items;
    }();
    if (expectation.contains(QStringLiteral("not_contains"))) {
        bool valid = false;
        asStringList(expectation.value(QStringLiteral("not_contains")), &valid);
        if (!valid)
            issues.append(QStringLiteral("Acceptance not_contains must be a string or a string list."));
        for (const QString &needle : forbiddenStrings) {
            QFile file(path);
            if (exists && file.open(QIODevice::ReadOnly)) {
                bool found = false;
                while (!file.atEnd()) {
                    if (QString::fromUtf8(file.readLine(kMaximumReportLineBytes + 1)).contains(needle)) {
                        found = true;
                        break;
                    }
                }
                notContainsPassed &= !found;
            }
        }
    }
    bool containsPassed = contains.isEmpty();
    if (!contains.isEmpty() && exists) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            while (!file.atEnd()) {
                if (QString::fromUtf8(file.readLine(kMaximumReportLineBytes + 1)).contains(contains)) {
                    containsPassed = true;
                    break;
                }
            }
        }
    }
    const QVariant minimumPercentValue = expectation.value(QStringLiteral("minimum_percentage"));
    const bool hasMinimumPercent = expectation.contains(QStringLiteral("minimum_percentage"))
        && minimumPercentValue.isValid() && !minimumPercentValue.isNull();
    const QVariant observedPercent = facts.tcCoverage.isValid() ? facts.tcCoverage
        : facts.faultCoverage.isValid() ? facts.faultCoverage : facts.testCoverage;
    bool percentPassed = !hasMinimumPercent;
    if (hasMinimumPercent && observedPercent.isValid())
        percentPassed = observedPercent.toDouble() >= minimumPercentValue.toDouble();
    const QVariant minimumMbist = expectation.value(QStringLiteral("minimum_mbist_case_coverage"));
    const bool hasMinimumMbist = expectation.contains(QStringLiteral("minimum_mbist_case_coverage"))
        && minimumMbist.isValid() && !minimumMbist.isNull();
    bool mbistPassed = !hasMinimumMbist;
    if (hasMinimumMbist && facts.mbistCaseCoverage.isValid())
        mbistPassed = facts.mbistCaseCoverage.toDouble() >= minimumMbist.toDouble();
    const QVariant maxDrcValue = expectation.value(QStringLiteral("maximum_dft_drc_violations"));
    const bool hasMaxDrc = expectation.contains(QStringLiteral("maximum_dft_drc_violations"))
        && maxDrcValue.isValid() && !maxDrcValue.isNull();
    bool drcPassed = !hasMaxDrc;
    if (hasMaxDrc && facts.totalViolations >= 0)
        drcPassed = facts.totalViolations <= maxDrcValue.toInt();
    QStringList errors = facts.diagnostics;
    errors.append(facts.linkDiagnostics);
    const bool noErrors = expectation.value(QStringLiteral("no_errors")).toBool();
    const bool noErrorsPassed = !noErrors || errors.isEmpty();
    const bool passed = exists && containsPassed && notContainsPassed && percentPassed
        && mbistPassed && drcPassed && noErrorsPassed && issues.isEmpty();
    if (!exists)
        issues.append(QStringLiteral("Acceptance report is missing."));
    if (!containsPassed)
        issues.append(QStringLiteral("Required report text was not found."));
    if (!notContainsPassed)
        issues.append(QStringLiteral("Forbidden report text was found."));
    if (!percentPassed)
        issues.append(QStringLiteral("Report coverage is missing or below the configured minimum."));
    if (!mbistPassed)
        issues.append(QStringLiteral("Declared MBIST case coverage is missing or below the configured minimum."));
    if (!drcPassed)
        issues.append(QStringLiteral("Observed DFT DRC violations exceed the configured maximum or are absent."));
    if (!noErrorsPassed)
        issues.append(QStringLiteral("Report contains Error/Fatal or unresolved LINK-5 diagnostics."));
    QVariantMap result{
        {QStringLiteral("path"), path},
        {QStringLiteral("contains"), containsValue},
        {QStringLiteral("not_contains"), forbiddenStrings},
        {QStringLiteral("minimum_percentage"), minimumPercentValue},
        {QStringLiteral("minimum_mbist_case_coverage"), minimumMbist},
        {QStringLiteral("maximum_dft_drc_violations"), maxDrcValue},
        {QStringLiteral("observed_dft_drc_violations"), facts.totalViolations < 0 ? QVariant{} : QVariant(facts.totalViolations)},
        {QStringLiteral("no_errors"), noErrors},
        {QStringLiteral("blocking"), expectation.value(QStringLiteral("blocking")).toBool()},
        {QStringLiteral("observed_percentage"), observedPercent},
        {QStringLiteral("observed_mbist_case_coverage"), facts.mbistCaseCoverage},
        {QStringLiteral("exists"), exists},
        {QStringLiteral("passed"), passed},
        {QStringLiteral("issues"), issues},
        {QStringLiteral("report_bytes"), facts.bytes},
        {QStringLiteral("report_lines"), facts.lines}
    };
    return result;
}

QVariantMap validateExecution(const QVariantMap &payload, const QString &evidenceFile,
                              const QString &evidenceRoot, QStringList *associatedFiles)
{
    QStringList issues;
    bool blocking = false;
    const QVariantMap candidate = payload.value(QStringLiteral("execution")).toMap();
    QString workspace;
    QString flow;
    QString driver;
    QString log;
    if (candidate.isEmpty()) {
        issues.append(QStringLiteral("The project evidence has no execution record."));
        blocking = true;
    } else {
        const QString workspaceText = candidate.value(QStringLiteral("workspace")).toString();
        const QString flowText = candidate.value(QStringLiteral("flow_directory")).toString();
        const QString driverText = candidate.value(QStringLiteral("driver")).toString();
        const QString logText = candidate.value(QStringLiteral("log")).toString();
        if (workspaceText.isEmpty() || flowText.isEmpty() || driverText.isEmpty() || logText.isEmpty()) {
            issues.append(QStringLiteral("The execution record lacks workspace, flow directory, driver, or log paths."));
            blocking = true;
        }
        workspace = normalizedPath(workspaceText);
        flow = normalizedPath(flowText);
        driver = normalizedPath(driverText);
        log = normalizedPath(logText);
        if (workspace != evidenceRoot) {
            issues.append(QStringLiteral("The evidence file is not stored in the final execution workspace."));
            blocking = true;
        }
        for (const auto &record : {qMakePair(QStringLiteral("flow directory"), flow),
                                   qMakePair(QStringLiteral("generated driver"), driver),
                                   qMakePair(QStringLiteral("captured log"), log)}) {
            const QFileInfo info(record.second);
            const QString allowedRoot = record.first == QLatin1String("flow directory") ? workspace : flow;
            const bool valid = within(record.second, allowedRoot)
                && (record.first == QLatin1String("flow directory") ? info.isDir() : info.isFile());
            if (!valid) {
                issues.append(QStringLiteral("The %1 is missing or outside its staged workspace.").arg(record.first));
                blocking = true;
            }
        }
        bool returnCodeOk = false;
        const int returnCode = candidate.value(QStringLiteral("returncode")).toInt(&returnCodeOk);
        if (!returnCodeOk || returnCode != 0 || candidate.value(QStringLiteral("timed_out")).toBool()) {
            issues.append(QStringLiteral("The final execution did not finish with return code 0."));
            blocking = true;
        }
        if (!candidate.value(QStringLiteral("errors")).toList().isEmpty()) {
            issues.append(QStringLiteral("The final execution record contains parsed tool errors."));
            blocking = true;
        }
        if (!candidate.value(QStringLiteral("completed_cleanly")).toBool()) {
            issues.append(QStringLiteral("The final execution record is not marked completed_cleanly."));
            blocking = true;
        }
    }
    if (payload.value(QStringLiteral("status")).toString() != QLatin1String("review_ready")) {
        issues.append(QStringLiteral("The project evidence is not marked review_ready."));
        blocking = true;
    }
    if (QFileInfo(driver).isFile())
        associatedFiles->append(driver);
    if (QFileInfo(log).isFile())
        associatedFiles->append(log);
    return {{QStringLiteral("passed"), issues.isEmpty()},
            {QStringLiteral("blocking"), blocking},
            {QStringLiteral("issues"), issues},
            {QStringLiteral("returncode"), candidate.value(QStringLiteral("returncode"))}};
}

QVariantMap crossValidationRound(const QString &evidenceFile, const QString &evidenceRoot,
                                 QString *error)
{
    QVariantMap payload;
    if (!jsonObjectFile(evidenceFile, &payload, error))
        return {};
    QStringList associatedFiles{evidenceFile};
    QVariantMap execution = validateExecution(payload, evidenceFile, evidenceRoot, &associatedFiles);
    QVariantMap reportReview = DftReportEvidenceService::validateAcceptance(
        evidenceRoot, payload.value(QStringLiteral("acceptance")).toList());
    const QVariantList checks = reportReview.value(QStringLiteral("checks")).toList();
    for (const QVariant &check : checks) {
        const QString path = check.toMap().value(QStringLiteral("path")).toString();
        if (QFileInfo(path).isFile())
            associatedFiles.append(path);
    }
    const QVariantMap candidate = payload.value(QStringLiteral("execution")).toMap();
    const QString log = normalizedPath(candidate.value(QStringLiteral("log")).toString());
    QStringList skepticIssues;
    QStringList crashArtifacts;
    if (log.isEmpty() || !within(log, evidenceRoot) || !QFileInfo(log).isFile()) {
        skepticIssues.append(QStringLiteral("The captured tool log is unavailable for negative-evidence review."));
    } else {
        const ReportFacts facts = scanReport(log);
        for (const QString &line : facts.diagnostics)
            skepticIssues.append(QStringLiteral("Tool log diagnostic: %1").arg(line));
        for (const QString &line : facts.linkDiagnostics)
            skepticIssues.append(QStringLiteral("Tool log diagnostic: %1").arg(line));
    }
    for (const QVariant &check : checks) {
        const QString path = check.toMap().value(QStringLiteral("path")).toString();
        if (!QFileInfo(path).isFile())
            continue;
        const ReportFacts facts = scanReport(path);
        for (const QString &line : facts.diagnostics)
            skepticIssues.append(QStringLiteral("Acceptance report diagnostic in %1: %2")
                                     .arg(QFileInfo(path).fileName(), line));
    }
    const QString flow = normalizedPath(candidate.value(QStringLiteral("flow_directory")).toString());
    if (within(flow, evidenceRoot) && QFileInfo(flow).isDir()) {
        QDirIterator iterator(flow, {QStringLiteral("Synopsys_stack_trace_*.txt"), QStringLiteral("crte_*.txt")},
                              QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            crashArtifacts.append(path);
            associatedFiles.append(path);
        }
        if (!crashArtifacts.isEmpty())
            skepticIssues.append(QStringLiteral("Crash artifacts are present in the final staged flow directory."));
    }
    QVariantMap skeptic{
        {QStringLiteral("passed"), skepticIssues.isEmpty()},
        {QStringLiteral("blocking"), !skepticIssues.isEmpty()},
        {QStringLiteral("issues"), skepticIssues},
        {QStringLiteral("crash_artifacts"), crashArtifacts}
    };
    associatedFiles.removeDuplicates();
    std::sort(associatedFiles.begin(), associatedFiles.end());
    QVariantList digestRecords;
    for (const QString &path : std::as_const(associatedFiles)) {
        if (!QFileInfo(path).isFile() || !within(normalizedPath(path), evidenceRoot))
            continue;
        QString hashError;
        const QString digest = sha256File(path, &hashError);
        if (!hashError.isEmpty()) {
            if (error)
                *error = hashError;
            return {};
        }
        const QString relative = QDir(evidenceRoot).relativeFilePath(normalizedPath(path));
        digestRecords.append(QVariantMap{{QStringLiteral("path"), relative}, {QStringLiteral("sha256"), digest}});
    }
    QJsonArray digestArray;
    for (const QVariant &entry : digestRecords)
        digestArray.append(QJsonObject::fromVariantMap(entry.toMap()));
    const QString fingerprint = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(digestArray).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    QVariantMap roles{{QStringLiteral("execution_reviewer"), execution},
                      {QStringLiteral("report_reviewer"), reportReview},
                      {QStringLiteral("skeptic_reviewer"), skeptic}};
    return {{QStringLiteral("fingerprint"), fingerprint},
            {QStringLiteral("files"), digestRecords},
            {QStringLiteral("roles"), roles}};
}

} // namespace

bool DftReportEvidenceService::supports(const QString &action)
{
    return kActions.contains(action);
}

QVariantMap DftReportEvidenceService::parseDrcReport(const QString &path, int maximum)
{
    const ReportFacts facts = scanReport(path);
    if (!facts.readable)
        return failure(facts.error);
    return success({
        {QStringLiteral("path"), normalizedPath(path)},
        {QStringLiteral("total_violations"), facts.totalViolations < 0 ? QVariant{} : QVariant(facts.totalViolations)},
        {QStringLiteral("drc_breakdown"), sortedDrcRules(facts.drcRules, std::max(0, maximum))},
        {QStringLiteral("diagnostics"), facts.diagnostics},
        {QStringLiteral("bytes_scanned"), facts.bytes},
        {QStringLiteral("lines_scanned"), facts.lines},
        {QStringLiteral("warning"), facts.error}
    });
}

QVariantMap DftReportEvidenceService::parseAtpgReport(const QString &path, const QString &tool,
                                                      const QString &diagnosticMode)
{
    if (QFileInfo(path).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
        QVariantMap structured;
        QString jsonError;
        if (!jsonObjectFile(path, &structured, &jsonError))
            return failure(QStringLiteral("Unable to parse ATPG JSON summary: ") + jsonError);
        const auto valueOr = [&structured](const QString &key, const QVariant &fallback = {}) {
            const QVariant value = structured.value(key);
            return value.isValid() ? value : fallback;
        };
        const QString reportTool = structured.value(QStringLiteral("tool")).toString();
        const bool tessent = reportTool == QLatin1String("tessent") || tool == QLatin1String("tessent");
        structured.insert(QStringLiteral("tool"), tessent ? QStringLiteral("tessent") : QStringLiteral("testmax"));
        structured.insert(QStringLiteral("coverage_percent"), valueOr(QStringLiteral("coverage_percent"),
            tessent ? valueOr(QStringLiteral("fault_coverage_percent"), valueOr(QStringLiteral("test_coverage_percent")))
                    : valueOr(QStringLiteral("test_coverage_percent"))));
        structured.insert(QStringLiteral("diagnostic_mode"), valueOr(QStringLiteral("diagnostic_mode"), diagnosticMode));
        structured.insert(QStringLiteral("drc_breakdown"), valueOr(QStringLiteral("drc_breakdown"), QVariantList{}));
        structured.insert(QStringLiteral("diagnostics"), valueOr(QStringLiteral("diagnostics"), QStringList{}));
        structured.insert(QStringLiteral("warning"), valueOr(QStringLiteral("warning"), QString{}));
        structured.insert(QStringLiteral("bytes_scanned"), valueOr(QStringLiteral("bytes_scanned"), QFileInfo(path).size()));
        structured.insert(QStringLiteral("lines_scanned"), valueOr(QStringLiteral("lines_scanned")));
        return success(structured);
    }
    const ReportFacts facts = scanReport(path);
    if (!facts.readable)
        return failure(facts.error);
    const bool tessent = tool == QLatin1String("tessent");
    QVariant coverage = tessent && facts.faultCoverage.isValid() ? facts.faultCoverage
        : facts.tcCoverage.isValid() ? facts.tcCoverage
        : facts.faultCoverage.isValid() ? facts.faultCoverage : facts.testCoverage;
    const QVariantMap summary{
        {QStringLiteral("tool"), tessent ? QStringLiteral("tessent") : QStringLiteral("testmax")},
        {QStringLiteral("drc_completed"), tessent ? QVariant{} : QVariant(facts.testmaxDrcSuccess)},
        {QStringLiteral("drc_breakdown"), sortedDrcRules(facts.drcRules, 8)},
        {QStringLiteral("coverage_percent"), coverage},
        {QStringLiteral("test_coverage_percent"), tessent ? facts.testCoverage : QVariant{}},
        {QStringLiteral("fault_coverage_percent"), tessent ? facts.faultCoverage : QVariant{}},
        {QStringLiteral("atpg_effectiveness_percent"), tessent ? facts.atpgEffectiveness : QVariant{}},
        {QStringLiteral("test_patterns"), facts.testPatterns},
        {QStringLiteral("internal_patterns"), tessent ? QVariant{} : facts.testPatterns},
        {QStringLiteral("total_faults"), facts.totalFaults},
        {QStringLiteral("traced_scan_chains"), tessent ? QVariant(facts.tracedScanChains) : QVariant{}},
        {QStringLiteral("fault_classes"), facts.faultClasses},
        {QStringLiteral("diagnostic_mode"), diagnosticMode},
        {QStringLiteral("drc_warning_count"), facts.allWarnings.size()},
        {QStringLiteral("drc_warning_rules"), QVariantList{}},
        {QStringLiteral("diagnostics"), facts.diagnostics},
        {QStringLiteral("bytes_scanned"), facts.bytes},
        {QStringLiteral("lines_scanned"), facts.lines},
        {QStringLiteral("warning"), facts.error}
    };
    return success(summary);
}

QVariantMap DftReportEvidenceService::validateAcceptance(const QString &evidenceRoot,
                                                         const QVariantList &acceptance)
{
    const QString root = normalizedPath(evidenceRoot);
    QVariantList checks;
    QStringList issues;
    QStringList validPaths;
    bool blocking = false;
    if (acceptance.isEmpty()) {
        return {{QStringLiteral("passed"), false}, {QStringLiteral("blocking"), true},
                {QStringLiteral("issues"), QStringList{QStringLiteral("The project evidence contains no acceptance checks.")}},
                {QStringLiteral("checks"), QVariantList{}}, {QStringLiteral("report_paths"), QVariantList{}}};
    }
    for (const QVariant &entry : acceptance) {
        const QVariantMap check = checkReport(root, entry.toMap(), &validPaths);
        checks.append(check);
        if (!check.value(QStringLiteral("passed")).toBool()) {
            const QString label = check.value(QStringLiteral("path")).toString();
            issues.append(QStringLiteral("Acceptance report check failed: %1 did not satisfy its declared rule.").arg(label));
            blocking |= check.value(QStringLiteral("blocking")).toBool();
        }
        const QString path = check.value(QStringLiteral("path")).toString();
        if (QFileInfo(path).isFile())
            validPaths.append(path);
    }
    validPaths.removeDuplicates();
    QVariantList paths;
    for (const QString &path : std::as_const(validPaths))
        paths.append(path);
    return {{QStringLiteral("passed"), issues.isEmpty()},
            {QStringLiteral("blocking"), blocking},
            {QStringLiteral("issues"), issues},
            {QStringLiteral("checks"), checks},
            {QStringLiteral("report_paths"), paths}};
}

QVariantMap DftReportEvidenceService::crossValidateEvidence(const QString &evidenceFile,
                                                            const QString &workspaceRoot)
{
    const QString evidence = normalizedPath(evidenceFile);
    const QString root = normalizedPath(workspaceRoot);
    if (!QFileInfo(evidence).isFile())
        return failure(QStringLiteral("Execution evidence is unavailable: %1").arg(evidence));
    if (!within(evidence, root))
        return failure(QStringLiteral("Execution evidence must reside in the configured workspace root."));
    QString error;
    const QVariantMap first = crossValidationRound(evidence, QFileInfo(evidence).absolutePath(), &error);
    if (!error.isEmpty())
        return failure(error);
    const QVariantMap second = crossValidationRound(evidence, QFileInfo(evidence).absolutePath(), &error);
    if (!error.isEmpty())
        return failure(error);
    bool rolesPassed = true;
    bool blocking = false;
    for (const QVariantMap &round : {first, second}) {
        const QVariantMap roles = round.value(QStringLiteral("roles")).toMap();
        for (const QString &roleName : {QStringLiteral("execution_reviewer"), QStringLiteral("report_reviewer"),
                                        QStringLiteral("skeptic_reviewer")}) {
            const QVariantMap role = roles.value(roleName).toMap();
            rolesPassed &= role.value(QStringLiteral("passed")).toBool();
            blocking |= role.value(QStringLiteral("blocking")).toBool();
        }
    }
    const bool stable = first.value(QStringLiteral("fingerprint")) == second.value(QStringLiteral("fingerprint"));
    const QString status = rolesPassed && stable ? QStringLiteral("verified")
        : blocking ? QStringLiteral("blocked") : QStringLiteral("needs_review");
    const QString verificationFile = QDir(QFileInfo(evidence).absolutePath()).filePath(QStringLiteral("cross_validation.json"));
    QVariantMap verification{
        {QStringLiteral("status"), status},
        {QStringLiteral("evidence_file"), evidence},
        {QStringLiteral("verification_roles"), QStringList{QStringLiteral("execution_reviewer"),
             QStringLiteral("report_reviewer"), QStringLiteral("skeptic_reviewer")}},
        {QStringLiteral("confirmation"), QVariantMap{
            {QStringLiteral("rounds"), 2}, {QStringLiteral("stable_snapshot"), stable},
            {QStringLiteral("review_kind"), QStringLiteral("same_execution_evidence_review")},
            {QStringLiteral("execution_count"), 1},
            {QStringLiteral("independent_execution_count"), 0},
            {QStringLiteral("same_evidence_file"), true},
            {QStringLiteral("policy"), QStringLiteral("These are two evidence-review passes over the same single execution and evidence files, not two independent EDA runs. Both passes require every reviewer check to pass; model text is not execution evidence.")}
        }},
        {QStringLiteral("rounds"), QVariantList{first, second}},
        {QStringLiteral("approval"), QStringLiteral("Verified structural DFT evidence still requires DFT engineering review before signoff.")}
    };
    QSaveFile output(verificationFile);
    if (!output.open(QIODevice::WriteOnly))
        return failure(output.errorString());
    output.write(QJsonDocument(QJsonObject::fromVariantMap(verification)).toJson(QJsonDocument::Indented));
    output.write("\n");
    if (!output.commit())
        return failure(output.errorString());
    verification.insert(QStringLiteral("verification_file"), verificationFile);
    return success(verification);
}

QVariantMap DftReportEvidenceService::dispatch(const QString &action, const QVariantMap &arguments)
{
    if (!supports(action))
        return failure(QStringLiteral("Unsupported DFT report evidence action."));
    if (action == QLatin1String("dft_report_parse_drc"))
        return parseDrcReport(arguments.value(QStringLiteral("path")).toString(),
                              arguments.value(QStringLiteral("maximum"), 8).toInt());
    if (action == QLatin1String("dft_report_parse_atpg"))
        return parseAtpgReport(arguments.value(QStringLiteral("path")).toString(),
                               arguments.value(QStringLiteral("tool"), QStringLiteral("testmax")).toString(),
                               arguments.value(QStringLiteral("diagnostic_mode")).toString());
    if (action == QLatin1String("dft_report_validate_acceptance"))
        return success(validateAcceptance(arguments.value(QStringLiteral("evidence_root")).toString(),
                                          arguments.value(QStringLiteral("acceptance")).toList()));
    return crossValidateEvidence(arguments.value(QStringLiteral("evidence_file")).toString(),
                                 arguments.value(QStringLiteral("workspace_root")).toString());
}
