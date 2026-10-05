#include "../src/dftreportevidenceservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QVariantMap readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory is available");
    const QString root = temporary.path();
    const QString reportPath = QDir(root).filePath(QStringLiteral("reports/atpg.log"));

    QByteArray longReport;
    longReport.reserve(2 * 1024 * 1024);
    for (int i = 0; i < 44000; ++i)
        longReport += "informational output line for streaming scan\n";
    longReport += "1 rule description violations (RULE_A)\n";
    longReport += "3 rule description violations (RULE_B)\n";
    longReport += "5 updated description violations (RULE_A)\n";
    longReport += "Total violations: 0\n";
    longReport += "test coverage: 95.0%\n";
    longReport += "fault coverage: 96.0%\n";
    longReport += "%TC: 97.0\n";
    longReport += "#internal patterns 42\n";
    longReport += "Total faults 100\n";
    longReport += "Detected faults 70\n";
    longReport += "Design rules checking was successful\n";
    require(writeFile(reportPath, longReport), "long ATPG report written");

    const QVariantMap atpg = DftReportEvidenceService::parseAtpgReport(reportPath);
    require(atpg.value(QStringLiteral("ok")).toBool(), "ATPG report parsed");
    const QVariantMap atpgResult = atpg.value(QStringLiteral("result")).toMap();
    require(atpgResult.value(QStringLiteral("coverage_percent")).toDouble() == 97.0,
            "TC coverage takes precedence over fault and test coverage");
    require(atpgResult.value(QStringLiteral("test_patterns")).toInt() == 42, "TestMAX pattern count extracted");
    require(atpgResult.value(QStringLiteral("total_faults")).toInt() == 100, "TestMAX total faults extracted");
    require(atpgResult.value(QStringLiteral("fault_classes")).toMap().value(QStringLiteral("detected")).toInt() == 70,
            "TestMAX fault class extracted");
    require(atpgResult.value(QStringLiteral("bytes_scanned")).toLongLong() > 1024 * 1024,
            "large report was scanned without truncating the whole file");

    const QString tessentPath = QDir(root).filePath(QStringLiteral("reports/tessent.log"));
    require(writeFile(tessentPath,
                      "test_coverage 94.0%\nfault_coverage 98.0%\natpg_effectiveness 99.0%\n"
                      "#test_patterns 17\nFU (full) 200\nDS (detected simulation) 180\n"
                      "chain 1 successfully traced with scan_cells\n"), "Tessent fixture written");
    const QVariantMap tessent = DftReportEvidenceService::parseAtpgReport(tessentPath, QStringLiteral("tessent"));
    const QVariantMap tessentResult = tessent.value(QStringLiteral("result")).toMap();
    require(tessentResult.value(QStringLiteral("coverage_percent")).toDouble() == 98.0
                && tessentResult.value(QStringLiteral("test_coverage_percent")).toDouble() == 94.0
                && tessentResult.value(QStringLiteral("atpg_effectiveness_percent")).toDouble() == 99.0,
            "Tessent coverage and effectiveness follow the native summary contract");
    require(tessentResult.value(QStringLiteral("test_patterns")).toInt() == 17
                && tessentResult.value(QStringLiteral("total_faults")).toInt() == 200
                && tessentResult.value(QStringLiteral("traced_scan_chains")).toInt() == 1,
            "Tessent patterns, fault totals, and traced chains are extracted");

    const QString summaryPath = QDir(root).filePath(QStringLiteral("reports/atpg_summary.json"));
    require(writeFile(summaryPath, R"({
        "tool":"testmax", "coverage_percent":100, "minimum_coverage":99,
        "drc_completed":true, "post_dft_drc_violations":0, "test_patterns":125,
        "internal_patterns":125, "total_faults":73740,
        "fault_classes":{"detected":73372,"not_detected":1,"undetectable":367},
        "report_fresh":true, "diagnostics":[], "warning":""
    })"), "structured ATPG summary written");
    const QVariantMap jsonAtpg = DftReportEvidenceService::parseAtpgReport(summaryPath);
    const QVariantMap jsonResult = jsonAtpg.value(QStringLiteral("result")).toMap();
    require(jsonAtpg.value(QStringLiteral("ok")).toBool()
                && jsonResult.value(QStringLiteral("coverage_percent")).toDouble() == 100.0
                && jsonResult.value(QStringLiteral("minimum_coverage")).toDouble() == 99.0
                && jsonResult.value(QStringLiteral("post_dft_drc_violations")).toInt() == 0
                && jsonResult.value(QStringLiteral("total_faults")).toInt() == 73740
                && jsonResult.value(QStringLiteral("report_fresh")).toBool(),
            "JSON ATPG summary preserves measured coverage and acceptance metadata");

    const QVariantMap drc = DftReportEvidenceService::parseDrcReport(reportPath);
    const QVariantList rules = drc.value(QStringLiteral("result")).toMap().value(QStringLiteral("drc_breakdown")).toList();
    require(rules.size() == 2 && rules.first().toMap().value(QStringLiteral("rule")).toString() == QLatin1String("RULE_A")
                && rules.first().toMap().value(QStringLiteral("count")).toInt() == 5,
            "DRC rules use last count and sort by descending violations");

    const QString evidence = QDir(root).filePath(QStringLiteral("run_evidence.json"));
    const QString flow = QDir(root).filePath(QStringLiteral("flow"));
    const QString driver = QDir(flow).filePath(QStringLiteral("run.tcl"));
    const QString log = QDir(flow).filePath(QStringLiteral("tool.log"));
    const QString acceptedReport = QDir(root).filePath(QStringLiteral("reports/acceptance.txt"));
    require(writeFile(driver, "run\n"), "driver fixture written");
    require(writeFile(log, "tool completed successfully\n"), "log fixture written");
    require(writeFile(acceptedReport,
                      "Post-DFT DRC passed\nTotal violations: 0\nfault coverage: 99.5%\nNo Error or Fatal diagnostics\n"),
            "acceptance fixture written");

    QVariantMap execution{
        {QStringLiteral("workspace"), root}, {QStringLiteral("flow_directory"), flow},
        {QStringLiteral("driver"), driver}, {QStringLiteral("log"), log},
        {QStringLiteral("returncode"), 0}, {QStringLiteral("timed_out"), false},
        {QStringLiteral("errors"), QVariantList{}}, {QStringLiteral("completed_cleanly"), true}
    };
    QVariantMap payload{
        {QStringLiteral("status"), QStringLiteral("review_ready")},
        {QStringLiteral("execution"), execution},
        {QStringLiteral("acceptance"), QVariantList{QVariantMap{
             {QStringLiteral("path"), QStringLiteral("reports/acceptance.txt")},
             {QStringLiteral("contains"), QStringLiteral("Post-DFT DRC passed")},
             {QStringLiteral("not_contains"), QVariantList{QStringLiteral("Fatal:")}},
             {QStringLiteral("minimum_percentage"), 99.0},
             {QStringLiteral("maximum_dft_drc_violations"), 0},
             {QStringLiteral("no_errors"), true}, {QStringLiteral("blocking"), true}
        }}}
    };
    require(writeFile(evidence, QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson()),
            "execution evidence written");

    const QVariantMap acceptance = DftReportEvidenceService::validateAcceptance(
        root, payload.value(QStringLiteral("acceptance")).toList());
    require(acceptance.value(QStringLiteral("passed")).toBool(), "acceptance criteria pass on raw report evidence");
    const QVariantMap escaped = DftReportEvidenceService::validateAcceptance(
        root, QVariantList{QVariantMap{{QStringLiteral("path"), QStringLiteral("../outside.txt")}}});
    require(!escaped.value(QStringLiteral("passed")).toBool() && escaped.value(QStringLiteral("blocking")).toBool(),
            "acceptance path traversal is rejected");

    const QVariantMap cross = DftReportEvidenceService::crossValidateEvidence(evidence, root);
    require(cross.value(QStringLiteral("ok")).toBool(), "cross validation completed");
    const QVariantMap verification = cross.value(QStringLiteral("result")).toMap();
    require(verification.value(QStringLiteral("status")).toString() == QLatin1String("verified"),
            "two stable raw-evidence rounds verify");
    require(verification.value(QStringLiteral("confirmation")).toMap()
                .value(QStringLiteral("stable_snapshot")).toBool(), "evidence fingerprints are stable");
    const QVariantMap confirmation = verification.value(QStringLiteral("confirmation")).toMap();
    require(confirmation.value(QStringLiteral("review_kind")).toString()
                == QLatin1String("same_execution_evidence_review")
                && confirmation.value(QStringLiteral("execution_count")).toInt() == 1
                && confirmation.value(QStringLiteral("independent_execution_count")).toInt() == 0
                && confirmation.value(QStringLiteral("same_evidence_file")).toBool()
                && confirmation.value(QStringLiteral("policy")).toString().contains(QStringLiteral("not two independent EDA runs")),
            "evidence review rounds are not mislabeled as separate EDA executions");
    const QVariantMap persisted = readJson(QDir(root).filePath(QStringLiteral("cross_validation.json")));
    require(persisted.value(QStringLiteral("status")).toString() == QLatin1String("verified"),
            "cross-validation result is atomically persisted");

    payload.insert(QStringLiteral("status"), QStringLiteral("needs_review"));
    require(writeFile(evidence, QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson()),
            "stale evidence fixture rewritten");
    const QVariantMap stale = DftReportEvidenceService::crossValidateEvidence(evidence, root);
    require(stale.value(QStringLiteral("ok")).toBool()
                && stale.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
                    == QLatin1String("blocked"), "non-review-ready evidence cannot verify");

    std::cout << "DFT report evidence checks passed\n";
    return 0;
}
