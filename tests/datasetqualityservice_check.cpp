#include "datasetqualityservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QJsonObject row(const QString &caseId, const QString &project, int number,
                const QString &fault = QStringLiteral("C26")) {
    return {
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), QStringLiteral("fixed prompt")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QStringLiteral("{\"project\":\"%1\",\"report\":\"/tmp/%1/drc_%2.rpt\"}").arg(project).arg(number)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QStringLiteral("{\"fault\":\"%1\",\"count\":%2,\"status\":\"not_verified\"}").arg(fault).arg(number)}}
        }},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("case"), caseId}}}
    };
}

bool writeJsonl(const QString &path, const QJsonArray &rows) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    for (const QJsonValue &value : rows)
        file.write(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact) + '\n');
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString normalized = DatasetQualityService::normalizeTrainingText(
        QStringLiteral("IEEE 1149.1 C26 /tmp/run_17/report_3.rpt 92.7%"));
    require(normalized.contains(QStringLiteral("ieee_1149.1"))
                && normalized.contains(QStringLiteral("c26"))
                && !normalized.contains(QStringLiteral("92.7"))
                && !normalized.contains(QStringLiteral("/tmp/run_17")),
            "normalization preserves engineering standards and removes run-specific values");

    const QJsonArray variants{row(QStringLiteral("a"), QStringLiteral("alpha_1"), 4),
                              row(QStringLiteral("b"), QStringLiteral("beta_2"), 19)};
    const QJsonObject variantReport = DatasetQualityService::auditRows(variants);
    require(variantReport.value(QStringLiteral("exact_messages")).toObject().value(QStringLiteral("duplicate_rows")).toInt() == 0,
            "exact message duplicate count is preserved");
    require(variantReport.value(QStringLiteral("normalized_conversation")).toObject().value(QStringLiteral("duplicate_rows")).toInt() == 1
                && !variantReport.value(QStringLiteral("strict_unique")).toBool(),
            "project/path/number-only variants are detected as duplicates");

    QJsonObject ruleA = row(QStringLiteral("rule-a"), QStringLiteral("alpha"), 4);
    QJsonObject ruleB = row(QStringLiteral("rule-b"), QStringLiteral("beta"), 19, QStringLiteral("C4"));
    QJsonArray ruleMessagesA = ruleA.value(QStringLiteral("messages")).toArray();
    QJsonArray ruleMessagesB = ruleB.value(QStringLiteral("messages")).toArray();
    ruleMessagesA[1] = QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QStringLiteral("inspect C26")}};
    ruleMessagesB[1] = QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QStringLiteral("inspect C4")}};
    ruleA.insert(QStringLiteral("messages"), ruleMessagesA);
    ruleB.insert(QStringLiteral("messages"), ruleMessagesB);
    const QJsonObject ruleReport = DatasetQualityService::auditRows(QJsonArray{ruleA, ruleB});
    require(ruleReport.value(QStringLiteral("normalized_conversation")).toObject().value(QStringLiteral("duplicate_rows")).toInt() == 0
                && ruleReport.value(QStringLiteral("strict_unique")).toBool(),
            "different DFT rule identifiers remain distinct");

    QJsonObject repeated = row(QStringLiteral("repeat"), QStringLiteral("alpha"), 1);
    QJsonArray repeatedMessages = repeated.value(QStringLiteral("messages")).toArray();
    const QJsonObject repeatedAssistant{{QStringLiteral("role"), QStringLiteral("assistant")},
                                       {QStringLiteral("content"), QStringLiteral("read the short report")}};
    repeatedMessages.insert(2, repeatedAssistant);
    repeatedMessages.insert(3, repeatedAssistant);
    repeated.insert(QStringLiteral("messages"), repeatedMessages);
    const QJsonObject repeatedReport = DatasetQualityService::auditRows(QJsonArray{repeated});
    require(repeatedReport.value(QStringLiteral("rows_with_repeated_assistant_turns")).toInt() == 1
                && repeatedReport.value(QStringLiteral("repeated_assistant_turns")).toInt() == 1
                && !repeatedReport.value(QStringLiteral("strict_unique")).toBool(),
            "repeated assistant turns inside a sample are rejected");

    QJsonObject toolRow = row(QStringLiteral("tool"), QStringLiteral("alpha"), 4);
    QJsonArray toolMessages{toolRow.value(QStringLiteral("messages")).toArray().at(0),
                            toolRow.value(QStringLiteral("messages")).toArray().at(1),
        QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QStringLiteral("Read source")},
            {QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
                {QStringLiteral("id"), QStringLiteral("call_a")},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("read_excerpt")},
                    {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("start"), 1}}}}}}}}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
            {QStringLiteral("tool_call_id"), QStringLiteral("call_a")},
            {QStringLiteral("name"), QStringLiteral("read_excerpt")},
            {QStringLiteral("content"), QStringLiteral("source excerpt")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), QStringLiteral("Done")}}
    };
    toolRow.insert(QStringLiteral("messages"), toolMessages);
    toolRow.insert(QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("read_excerpt")}}}}});
    const QJsonObject structure = DatasetQualityService::auditStructure(QJsonArray{toolRow});
    require(structure.value(QStringLiteral("valid")).toBool()
                && structure.value(QStringLiteral("tool_calls")).toInt() == 1
                && structure.value(QStringLiteral("tool_results")).toInt() == 1,
            "native function call and tool result pair pass structural validation");

    const QJsonArray selectionRows{row(QStringLiteral("a"), QStringLiteral("alpha_1"), 4),
                                  row(QStringLiteral("b"), QStringLiteral("beta_2"), 19),
                                  row(QStringLiteral("c"), QStringLiteral("gamma_3"), 7, QStringLiteral("C4"))};
    const QVariantMap selection = DatasetQualityService::selectUniqueRows(selectionRows);
    require(selection.value(QStringLiteral("rows")).toList().size() == 2
                && selection.value(QStringLiteral("rejected")).toInt() == 1,
            "unique selection deduplicates normalized conversations");

    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary test directory available");
    const QString datasetPath = temporary.filePath(QStringLiteral("samples.jsonl"));
    require(writeJsonl(datasetPath, QJsonArray{row(QStringLiteral("one"), QStringLiteral("alpha"), 1)}),
            "write JSONL fixture");
    const QVariantMap audit = DatasetQualityService::auditJsonl(datasetPath);
    require(audit.value(QStringLiteral("ok")).toBool()
                && audit.value(QStringLiteral("report")).toMap().value(QStringLiteral("rows")).toInt() == 1,
            "JSONL audit reads native dataset files");
    QFile malformed(temporary.filePath(QStringLiteral("malformed.jsonl")));
    require(malformed.open(QIODevice::WriteOnly), "open malformed JSONL fixture");
    malformed.write("{bad json}\n");
    malformed.close();
    const QVariantMap badAudit = DatasetQualityService::auditJsonl(malformed.fileName());
    require(!badAudit.value(QStringLiteral("ok")).toBool()
                && badAudit.value(QStringLiteral("message")).toString().contains(QStringLiteral(":1")),
            "malformed JSONL reports the source line number");

    std::cout << "Native dataset quality checks passed\n";
    return 0;
}
