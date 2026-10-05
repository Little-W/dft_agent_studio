#include "../src/configureddftflowservice.h"
#include "../src/edajobservice.h"
#include "../src/sourcecompatibilityservice.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>

namespace {

bool require(bool condition, const char *message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

void diagnoseStage(const char *label, const QVariantMap &stage)
{
    std::fprintf(stderr, "DIAG %s: %s\n", label,
                 QJsonDocument::fromVariant(stage).toJson(QJsonDocument::Compact).constData());
}

bool writeFile(const QString &path, const QByteArray &content, bool executable = false)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
        return false;
    file.close();
    if (executable)
        return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return true;
}

QVariantMap sampleProject(const QString &root, const QString &library, const QString &workspace)
{
    return {
        {QStringLiteral("id"), QStringLiteral("native_demo")},
        {QStringLiteral("name"), QStringLiteral("Native Demo")},
        {QStringLiteral("root"), root},
        {QStringLiteral("rtl_root"), QDir(root).filePath(QStringLiteral("rtl"))},
        {QStringLiteral("top"), QStringLiteral("top" )},
        {QStringLiteral("library_dir"), QFileInfo(library).absolutePath()},
        {QStringLiteral("library_file"), QFileInfo(library).fileName()},
        {QStringLiteral("metadata"), QVariantMap{
            {QStringLiteral("dft_execution"), QVariantMap{
                {QStringLiteral("source_files"), QVariantList{QStringLiteral("top.sv")}},
                {QStringLiteral("clock"), QStringLiteral("clk")},
                {QStringLiteral("reset"), QStringLiteral("rst_n")},
                {QStringLiteral("scan_chain_count"), 1},
                {QStringLiteral("max_chain_length"), 64},
                {QStringLiteral("workspace_path"), workspace},
                {QStringLiteral("timeout_seconds"), 30},
                {QStringLiteral("clock_period_ns"), 10.0}
            }},
            {QStringLiteral("flow_modules"), QVariantMap{
                {QStringLiteral("synthesis"), true}, {QStringLiteral("dft"), true},
                {QStringLiteral("scan"), true}, {QStringLiteral("atpg"), false},
                {QStringLiteral("mbist"), false}, {QStringLiteral("lbist"), false}
            }}
        }}
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    bool ok = require(temp.isValid(), "temporary workspace is available");
    if (!temp.isValid())
        return 1;
    const QString root = temp.filePath(QStringLiteral("project"));
    const QString workspace = temp.filePath(QStringLiteral("isolated"));
    const QString library = temp.filePath(QStringLiteral("lib/typical.db"));
    const QString cellModel = temp.filePath(QStringLiteral("lib/cells.v"));
    const QString tessentCellLibrary = temp.filePath(QStringLiteral("lib/cells.atpg"));
    const QString macroLibrary = temp.filePath(QStringLiteral("lib/sram_macro.db"));
    const QString ctlTestModel = temp.filePath(QStringLiteral("project/models/sram.ctl"));
    const QString fakeDc = temp.filePath(QStringLiteral("bin/dc_shell"));
    const QString fakeDcRetryInternalError = temp.filePath(QStringLiteral("bin/dc_shell_retry_internal_error"));
    const QString dcInternalErrorMarker = temp.filePath(QStringLiteral("dc_internal_error_marker"));
    const QString fakeDcTransientDcsH1 = temp.filePath(QStringLiteral("bin/dc_shell_transient_dcsh1"));
    const QString dcRetryMarker = temp.filePath(QStringLiteral("dc_retry_marker"));
    const QString fakeDcNoReports = temp.filePath(QStringLiteral("bin/dc_shell_no_reports"));
    const QString fakeDcPersistentDcsH1 = temp.filePath(QStringLiteral("bin/dc_shell_persistent_dcsh1"));
    const QString fakeDcRetryAfterTimeout = temp.filePath(QStringLiteral("bin/dc_shell_retry_after_timeout"));
    const QString dcTimeoutMarker = temp.filePath(QStringLiteral("dc_timeout_marker"));
    const QString fakeDcRetryAfterDrc = temp.filePath(QStringLiteral("bin/dc_shell_retry_after_drc"));
    const QString dcDrcMarker = temp.filePath(QStringLiteral("dc_drc_marker"));
    const QString fakeDcRetryAfterPreprocessError = temp.filePath(QStringLiteral("bin/dc_shell_retry_after_preprocess_error"));
    const QString dcPreprocessErrorMarker = temp.filePath(QStringLiteral("dc_preprocess_error_marker"));
    const QString fakeDcRetryAfterUnsupportedSource = temp.filePath(QStringLiteral("bin/dc_shell_retry_after_unsupported_source"));
    const QString dcUnsupportedSourceMarker = temp.filePath(QStringLiteral("dc_unsupported_source_marker"));
    const QString fakeTestmax = temp.filePath(QStringLiteral("bin/testmax"));
    const QString fakeTestmaxNoLog = temp.filePath(QStringLiteral("bin/testmax_no_log"));
    const QString fakeTestmaxMarker = temp.filePath(QStringLiteral("bin/testmax_marker"));
    const QString atpgLaunchMarker = temp.filePath(QStringLiteral("atpg_was_launched"));
    const QString fakeTessent = temp.filePath(QStringLiteral("bin/tessent"));
    const QString slowDc = temp.filePath(QStringLiteral("bin/slow_dc_shell"));
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/top.sv")),
                            "module top(input clk, input rst_n); endmodule\n"),
                  "create project RTL");
    ok &= require(writeFile(library, "fake liberty database\n"), "create library fixture");
    ok &= require(writeFile(macroLibrary, "fake SRAM macro library\n"), "create SRAM macro library fixture");
    ok &= require(writeFile(ctlTestModel, "STIL 1.0 { scan_structures {} }\n"), "create CTL test model fixture");
    ok &= require(writeFile(cellModel, "module DFF(input D, output Q); assign Q = D; endmodule\n"),
                  "create gate-level Verilog cell model");
    ok &= require(writeFile(tessentCellLibrary, "library_format_version = 9;\n"),
                  "create Tessent ATPG cell library");
    ok &= require(writeFile(fakeDc,
                            "#!/bin/sh\n[ \"$1\" = '-no_gui' ] && [ \"$2\" = '-f' ] || exit 98\n"
                            "mkdir -p reports mapped_scan synthesis\n"
                            "printf '%s\\n' 'HDL analysis completed' > reports/analyze.log\n"
                            "printf '%s\\n' 'Design top' > reports/read_link.rpt\n"
                            "printf '%s\\n' 'Design top' > reports/synthesis_qor.rpt\n"
                            "printf '%s\\n' 'Total cell area: 1' > reports/synthesis_area.rpt\n"
                            "printf '%s\\n' 'slack 0.1' > reports/synthesis_timing.rpt\n"
                            "printf '%s\\n' 'DRC precheck' > reports/pre_dft_drc.rpt\n"
                            "printf '%s\\n' 'Number of chains: 1' > reports/preview_dft.rpt\n"
                            "printf '%s\\n' 'Total violations: 0' > reports/post_dft_drc.rpt\n"
                            "printf '%s\\n' 'Scan_path chain_0' > reports/scan_path.rpt\n"
                            "printf '%s\\n' 'ScanEnable port' > reports/dft_signal.rpt\n"
                            "printf '%s\\n' 'module top; endmodule' > synthesis/native_demo_synth.v\n"
                            "touch synthesis/native_demo_synth.ddc mapped_scan/native_demo_scan.ddc\n"
                            "printf '%s\\n' 'module scan_top; endmodule' > mapped_scan/native_demo_scan.v\n"
                            "cat > mapped_scan/native_demo_scan.spf <<'DFT_AGENT_SPF'\n"
                            "STIL 1.0 { Design 2005; }\n"
                            "ScanStructures {\n"
                            "  ScanChain \"chain_1\" {\n"
                            "    ScanLength 7;\n"
                            "    ScanIn \"test_si\";\n"
                            "    ScanOut \"test_so\";\n"
                            "    ScanEnable \"test_se\";\n"
                            "    ScanMasterClock \"clk\";\n"
                            "  }\n"
                            "}\n"
                            "DFT_AGENT_SPF\n"
                  "printf 'fake dc_shell completed\\n'\nexit 0\n", true),
                  "create executable fake dc_shell");
    const QByteArray retryAfterDrcContents = QStringLiteral(
        "#!/bin/sh\n"
        "first=0\n"
        "if [ ! -f '%1' ]; then touch '%1'; first=1; fi\n"
        "'%2' \"$@\" || exit $?\n"
        "if [ \"$first\" = 1 ]; then printf 'Total violations: 1\\n' > reports/post_dft_drc.rpt; fi\n"
        "exit 0\n").arg(dcDrcMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcRetryAfterDrc, retryAfterDrcContents, true),
                  "create dc_shell fixture which emits one fresh DRC violation before a clean retry");
    const QByteArray internalErrorDcContents = QStringLiteral(
        "#!/bin/sh\n[ \"$1\" = '-no_gui' ] && [ \"$2\" = '-f' ] || exit 98\n"
        "if [ ! -f '%1' ]; then mkdir -p reports; printf 'HDL analysis completed\\n' > reports/analyze.log; printf 'Design top\\n' > reports/read_link.rpt; "
        "printf 'Fatal: Internal system error, cannot recover.\\n' >&2; touch '%1'; exit 1; fi\n"
        "grep -F 'compile -no_map' \"$2\" >/dev/null || exit 97\n"
        "exec '%2' \"$@\"\n").arg(dcInternalErrorMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcRetryInternalError, internalErrorDcContents, true),
                  "create dc_shell fixture which retries an internal fatal only after clean read/link evidence");
    const QByteArray transientDcContents = QStringLiteral(
        "#!/bin/sh\n"
        "[ \"$1\" = '-no_gui' ] && [ \"$2\" = '-f' ] || exit 98\n"
        "if [ ! -f '%1' ]; then printf 'first' > '%1'; printf 'Fatal: Design Compiler is not enabled. (DCSH-1)\\n'; exit 1; fi\n"
        "printf 'retry' >> '%1'\n"
        "exec '%2' \"$@\"\n").arg(dcRetryMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcTransientDcsH1, transientDcContents, true),
                  "create dc_shell fixture with a one-time DCSH-1 startup failure");
    ok &= require(writeFile(fakeDcPersistentDcsH1,
                            "#!/bin/sh\nprintf 'Fatal: Design Compiler is not enabled. (DCSH-1)\\n'\nexit 1\n", true),
                  "create dc_shell fixture with a persistent DCSH-1 startup failure");
    ok &= require(writeFile(fakeDcNoReports, "#!/bin/sh\nprintf 'finished without evidence\\n'\nexit 0\n", true),
                  "create successful dc_shell fixture that emits no reports or outputs");
    const QByteArray fakeAtpgContents = QStringLiteral(
        "#!/bin/sh\nif ! grep -F 'read_netlist {%1}' \"$1\" >/dev/null; then exit 9; fi\n"
        "mkdir -p logs\nprintf '%s\\n' 'Design rules checking was successful' 'test coverage 99.5%%' > logs/agent_atpg.log\n"
        "if grep -F 'report_faults -class AU -summary' \"$1\" >/dev/null; then printf '%s\\n' 'Detected faults 120' 'Possibly detected faults 3' 'Undetectable faults 2' 'ATPG untestable faults 1' 'Not detected faults 4' >> logs/agent_atpg.log; fi\n"
        "printf 'fake TestMAX completed\\n'\nexit 0\n").arg(cellModel).toUtf8();
    ok &= require(writeFile(fakeTestmax, fakeAtpgContents, true), "create executable fake TestMAX");
    ok &= require(writeFile(fakeTestmaxNoLog, "#!/bin/sh\nexit 0\n", true),
                  "create TestMAX fixture which exits without a report");
    ok &= require(writeFile(fakeTestmaxMarker,
                            QStringLiteral("#!/bin/sh\nprintf launched > '%1'\nexit 0\n")
                                .arg(atpgLaunchMarker).toUtf8(), true),
                  "create a TestMAX fixture that records whether ATPG was launched");
    ok &= require(writeFile(fakeTessent,
                            "#!/bin/sh\n"
                            "[ \"$1\" = '-shell' ] && [ \"$2\" = '-dofile' ] || exit 21\n"
                            "grep -F 'set_context patterns -scan' \"$3\" >/dev/null || exit 22\n"
                            "grep -F 'create_patterns -coverage_effort high' \"$3\" >/dev/null || exit 23\n"
                            "[ -f agent_tessent_scan.testproc ] || exit 24\n"
                            "mkdir -p logs reports mapped_scan\n"
                            "printf '%s\\n' 'Chain = chain_1 successfully traced with scan_cells = 7.' '#test_patterns 17' 'FU (full) 200' 'test_coverage 94.0%' 'fault_coverage 98.0%' 'atpg_effectiveness 99.0%' > logs/agent_atpg.log\n"
                            "printf '%s\\n' 'chain = chain_1 scan_cells = 7' > reports/tessent_scan_chains.rpt\n"
                            "printf '%s\\n' 'fault_coverage 98.0%' > reports/tessent_atpg_statistics.rpt\n"
                            "printf 'STIL patterns\\n' > mapped_scan/native_demo_scan_tessent_stuck_at.stil\n"
                            "exit 0\n", true),
                  "create Tessent fixture which verifies and emits scan/ATPG evidence");
    ok &= require(writeFile(slowDc, "#!/bin/sh\nsleep 30\n", true),
                  "create cancellable long-running dc_shell fixture");
    const QByteArray retryAfterTimeoutContents = QStringLiteral(
        "#!/bin/sh\n"
        "if [ ! -f '%1' ]; then : > '%1'; sleep 3; exit 124; fi\n"
        "exec '%2' \"$@\"\n").arg(dcTimeoutMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcRetryAfterTimeout, retryAfterTimeoutContents, true),
                  "create dc_shell fixture which times out once and succeeds on retry");
    const QByteArray retryAfterPreprocessErrorContents = QStringLiteral(
        "#!/bin/sh\n[ \"$1\" = '-no_gui' ] && [ \"$2\" = '-f' ] || exit 98\n"
        "if [ ! -f '%1' ]; then printf 'first' > '%1'; printf \"Syntax error at token '#'\\n\" >&2; exit 1; fi\n"
        "exec '%2' \"$@\"\n").arg(dcPreprocessErrorMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcRetryAfterPreprocessError, retryAfterPreprocessErrorContents, true),
                  "create dc_shell fixture that reports an RTL preprocessing error once");
    const QByteArray retryAfterUnsupportedSourceContents = QStringLiteral(
        "#!/bin/sh\n[ \"$1\" = '-no_gui' ] && [ \"$2\" = '-f' ] || exit 98\n"
        "if [ ! -f '%1' ]; then touch '%1'; printf 'rtl/orphan_switch.v:1: switches are not supported: tranif1\\n' >&2; exit 1; fi\n"
        "if grep -F 'orphan_switch.v' input/rtl.f >/dev/null; then exit 92; fi\n"
        "exec '%2' \"$@\"\n").arg(dcUnsupportedSourceMarker, fakeDc).toUtf8();
    ok &= require(writeFile(fakeDcRetryAfterUnsupportedSource, retryAfterUnsupportedSourceContents, true),
                  "create dc_shell fixture which succeeds only after a proven compatibility exclusion");
    const QVariantMap project = sampleProject(root, library, workspace);
    const QVariantMap runArguments{{QStringLiteral("dc_shell"), fakeDc}, {QStringLiteral("testmax"), fakeTestmax}};
    const QString agentRoot = temp.path();

    ok &= require(ConfiguredDftFlowService::supports(QStringLiteral("check_dft_readiness")),
                  "public readiness action alias is supported");
    ok &= require(ConfiguredDftFlowService::supports(QStringLiteral("run_dft_iteration")),
                  "public iteration action alias is supported");
    const QVariantMap readiness = ConfiguredDftFlowService::readiness(project, runArguments, agentRoot);
    ok &= require(readiness.value(QStringLiteral("ok")).toBool(), "readiness envelope succeeds");
    const QVariantMap ready = readiness.value(QStringLiteral("result")).toMap();
    ok &= require(ready.value(QStringLiteral("ready")).toBool(), "valid real project configuration is ready");
    ok &= require(ready.value(QStringLiteral("source_file_count")).toInt() == 1,
                  "declared RTL source is resolved");
    ok &= require(ready.value(QStringLiteral("synthesis_configuration")).toMap()
                          .value(QStringLiteral("analyze_format")).toString() == QStringLiteral("sverilog"),
                  "readiness exposes the effective HDL analyze format");
    const QVariantMap readinessSynthesis = ready.value(QStringLiteral("synthesis_configuration")).toMap();
    ok &= require(readinessSynthesis.value(QStringLiteral("analyze_format_is_derived")).toBool()
                      && readinessSynthesis.value(QStringLiteral("analyze_format_control_path")).toString()
                          == QStringLiteral("metadata.dft_execution.language"),
                  "readiness points agents from the derived HDL format to its editable project setting");
    ok &= require(!ready.contains(QStringLiteral("source_is_read_only")),
                  "readiness does not impose a read-only policy on project RTL");

    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("filelist.txt")),
                            "rtl/top.sv\nrtl/orphan_switch.v\n"),
                  "create project-root filelist fixture");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/orphan_switch.v")),
                            "module orphan_switch; wire a, b; tranif1 u1(a, b, 1'b1); endmodule\n"),
                  "create an unreferenced unsupported switch-primitive module");
    QVariantMap compatibilityProject = project;
    compatibilityProject.insert(QStringLiteral("session_id"), QStringLiteral("native-source-compatibility-session"));
    QVariantMap compatibilityMetadata = compatibilityProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap compatibilityExecution = compatibilityMetadata.value(QStringLiteral("dft_execution")).toMap();
    compatibilityExecution.insert(QStringLiteral("filelist"), QStringLiteral("filelist.txt"));
    compatibilityMetadata.insert(QStringLiteral("dft_execution"), compatibilityExecution);
    compatibilityProject.insert(QStringLiteral("metadata"), compatibilityMetadata);
    const QVariantMap compatibilityStage = ConfiguredDftFlowService::stage(compatibilityProject,
        temp.filePath(QStringLiteral("compatibility-stage")),
        QVariantMap{{QStringLiteral("source_exclude_files"), QVariantList{QStringLiteral("orphan_switch.v")}}},
        agentRoot);
    const QVariantMap compatibilityStageRecord = compatibilityStage.value(QStringLiteral("result")).toMap();
    const QVariantMap compatibilitySource = compatibilityStageRecord.value(QStringLiteral("source")).toMap();
    const QVariantMap compatibilitySynthesis = compatibilitySource
        .value(QStringLiteral("synthesis_configuration")).toMap();
    const QVariantMap exclusionEvidence = compatibilityStageRecord
        .value(QStringLiteral("source_compatibility_exclusions")).toList().value(0).toMap();
    const QString stagedInputList = QDir(compatibilityStageRecord.value(QStringLiteral("flow_directory")).toString())
        .filePath(QStringLiteral("input/rtl.f"));
    QFile stagedInput(stagedInputList);
    const bool stagedInputReadable = stagedInput.open(QIODevice::ReadOnly | QIODevice::Text);
    const QString stagedInputText = stagedInputReadable ? QString::fromUtf8(stagedInput.readAll()) : QString{};
    const bool compatibilityStagePassed = compatibilityStage.value(QStringLiteral("ok")).toBool()
                      && compatibilitySource.value(QStringLiteral("rtl_compile_files")).toStringList()
                             == QStringList{QStringLiteral("top.sv")}
                      && compatibilitySynthesis.value(QStringLiteral("analyze_format")).toString()
                             == QStringLiteral("sverilog")
                      && compatibilitySynthesis.value(QStringLiteral("analyze_format_is_derived")).toBool()
                      && compatibilitySynthesis.value(QStringLiteral("analyze_format_control_path")).toString()
                             == QStringLiteral("metadata.dft_execution.language")
                      && stagedInputText.contains(QStringLiteral("top.sv"))
                      && !stagedInputText.contains(QStringLiteral("orphan_switch.v"))
                      && exclusionEvidence.value(QStringLiteral("safe_exclusion")).toBool()
                      && exclusionEvidence.value(QStringLiteral("unsupported_constructs")).toList()
                             .contains(QStringLiteral("tranif"))
                      && !exclusionEvidence.value(QStringLiteral("sha256")).toString().isEmpty();
    if (!compatibilityStagePassed)
        diagnoseStage("source exclusion", compatibilityStage);
    ok &= require(compatibilityStagePassed,
                  "iteration exclusion stages only a proven unreferenced unsupported module and records its evidence");

    QVariantMap verilogFormatProject = project;
    QVariantMap verilogFormatMetadata = verilogFormatProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap verilogFormatExecution = verilogFormatMetadata.value(QStringLiteral("dft_execution")).toMap();
    verilogFormatExecution.insert(QStringLiteral("language"), QStringLiteral("verilog"));
    verilogFormatMetadata.insert(QStringLiteral("dft_execution"), verilogFormatExecution);
    verilogFormatProject.insert(QStringLiteral("metadata"), verilogFormatMetadata);
    const QVariantMap verilogFormatStage = ConfiguredDftFlowService::stage(verilogFormatProject,
        temp.filePath(QStringLiteral("verilog-format-stage")), {}, agentRoot);
    const QVariantMap verilogFormatSource = verilogFormatStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("source")).toMap();
    const QString verilogFormatFlow = verilogFormatStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    QFile verilogFormatDriver(QDir(verilogFormatFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool verilogFormatDriverReadable = verilogFormatDriver.open(QIODevice::ReadOnly | QIODevice::Text);
    const QString verilogFormatDriverText = verilogFormatDriverReadable
        ? QString::fromUtf8(verilogFormatDriver.readAll()) : QString{};
    const bool verilogFormatStagePassed = verilogFormatStage.value(QStringLiteral("ok")).toBool()
                      && verilogFormatSource.value(QStringLiteral("synthesis_configuration")).toMap()
                             .value(QStringLiteral("analyze_format")).toString() == QStringLiteral("verilog")
                      && verilogFormatSource.value(QStringLiteral("synthesis_configuration")).toMap()
                             .value(QStringLiteral("analyze_format_control_path")).toString()
                             == QStringLiteral("metadata.dft_execution.language")
                      && verilogFormatDriverText.contains(QStringLiteral("-format {verilog}"))
                      && verilogFormatDriverText.contains(QStringLiteral("reports/analyze.log"))
                      && verilogFormatDriverText.contains(QStringLiteral("HDL analysis failed"));
    if (!verilogFormatStagePassed)
        diagnoseStage("configured HDL format", verilogFormatStage);
    ok &= require(verilogFormatStagePassed,
                  "stage evidence records the exact configured HDL format used by generated Tcl");

    const QString annotationRoot = temp.filePath(QStringLiteral("annotation-project"));
    const QString annotationSourcePath = QDir(annotationRoot).filePath(QStringLiteral("rtl/top.sv"));
    const QByteArray annotationSource = "module top(input clk, input rst_n);\n"
        "  reg [3:0] state; // synopsys enum_state\n"
        "endmodule\n";
    ok &= require(writeFile(annotationSourcePath, annotationSource), "create RTL with a legacy state-encoding hint");
    const QVariantMap annotationProject = sampleProject(annotationRoot, library,
        temp.filePath(QStringLiteral("annotation-workspaces")));
    const QVariantMap annotationStage = ConfiguredDftFlowService::stage(annotationProject,
        temp.filePath(QStringLiteral("annotation-stage")),
        QVariantMap{{QStringLiteral("source_annotation_mode"), QStringLiteral("strip_unsupported_state_encoding_hints")}},
        agentRoot);
    const QVariantMap annotationRecord = annotationStage.value(QStringLiteral("result")).toMap();
    const QString annotationFlow = annotationRecord.value(QStringLiteral("flow_directory")).toString();
    const QString stagedAnnotationPath = QDir(annotationFlow).filePath(QStringLiteral("rtl/top.sv"));
    QFile stagedAnnotation(stagedAnnotationPath);
    const bool stagedAnnotationReadable = stagedAnnotation.open(QIODevice::ReadOnly);
    const QByteArray stagedAnnotationText = stagedAnnotationReadable ? stagedAnnotation.readAll() : QByteArray{};
    const QVariantMap annotationSourceRecord = annotationRecord.value(QStringLiteral("source")).toMap();
    const QVariantList annotationTransforms = annotationSourceRecord.value(QStringLiteral("source_annotation_transforms")).toList();
    const QVariantMap annotationTransform = annotationTransforms.value(0).toMap();
    QFile unchangedAnnotationSource(annotationSourcePath);
    const bool unchangedAnnotationReadable = unchangedAnnotationSource.open(QIODevice::ReadOnly);
    const bool annotationStagePassed = annotationStage.value(QStringLiteral("ok")).toBool()
                      && stagedAnnotationReadable && unchangedAnnotationReadable
                      && stagedAnnotationText.contains("reg [3:0] state;")
                      && !stagedAnnotationText.contains("synopsys enum_state")
                      && unchangedAnnotationSource.readAll() == annotationSource
                      && annotationTransforms.size() == 1
                      && annotationTransform.value(QStringLiteral("transformed_lines")).toList() == QVariantList{2}
                      && annotationTransform.value(QStringLiteral("original_sha256")).toString().size() == 64
                      && annotationTransform.value(QStringLiteral("staged_sha256")).toString().size() == 64;
    if (!annotationStagePassed)
        diagnoseStage("source annotation adapter", annotationStage);
    ok &= require(annotationStagePassed,
                  "explicit parser-compatibility mode normalizes only isolated staged RTL and records provenance");

    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/support/helper.v")),
                            "module helper; wire ready; endmodule\n"),
                  "create a project-local supplemental HDL module");
    const QVariantMap supplementalStage = ConfiguredDftFlowService::stage(project,
        temp.filePath(QStringLiteral("supplemental-stage")),
        QVariantMap{{QStringLiteral("source_extra_files"), QVariantList{QStringLiteral("support/*.v")}}},
        agentRoot);
    const QVariantMap supplementalStageRecord = supplementalStage.value(QStringLiteral("result")).toMap();
    const QVariantMap supplementalSource = supplementalStageRecord.value(QStringLiteral("source")).toMap();
    const QVariantMap supplementalEvidence = supplementalStageRecord.value(QStringLiteral("source_additions"))
        .toList().value(0).toMap();
    const QString supplementalFlow = supplementalStageRecord.value(QStringLiteral("flow_directory")).toString();
    QFile supplementalFilelist(QDir(supplementalFlow).filePath(QStringLiteral("input/rtl.f")));
    const bool supplementalFilelistReadable = supplementalFilelist.open(QIODevice::ReadOnly | QIODevice::Text);
    const QString supplementalFilelistText = supplementalFilelistReadable
        ? QString::fromUtf8(supplementalFilelist.readAll()) : QString{};
    const bool supplementalStagePassed = supplementalStage.value(QStringLiteral("ok")).toBool()
                      && supplementalSource.value(QStringLiteral("rtl_compile_files")).toStringList()
                             .contains(QStringLiteral("support/helper.v"))
                      && supplementalFilelistText.contains(QStringLiteral("./rtl/support/helper.v"))
                      && supplementalEvidence.value(QStringLiteral("module_names")).toStringList()
                             .contains(QStringLiteral("helper"))
                      && !supplementalEvidence.value(QStringLiteral("sha256")).toString().isEmpty();
    if (!supplementalStagePassed)
        diagnoseStage("supplemental sources", supplementalStage);
    ok &= require(supplementalStagePassed,
                  "iteration adds bounded project-local HDL sources and records module/hash evidence");
    const QVariantMap unsafeSupplementalStage = ConfiguredDftFlowService::stage(project,
        temp.filePath(QStringLiteral("unsafe-supplemental-stage")),
        QVariantMap{{QStringLiteral("source_extra_files"), QVariantList{QStringLiteral("../outside.v")}}},
        agentRoot);
    ok &= require(!unsafeSupplementalStage.value(QStringLiteral("ok")).toBool(),
                  "iteration rejects supplemental source paths outside the project RTL root");
    QVariantMap referencedProject = compatibilityProject;
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/top.sv")),
                            "module top(input clk, input rst_n); orphan_switch u_orphan(); endmodule\n"),
                  "make unsupported module referenced by the top-level design");
    const QVariantMap referencedExclusion = ConfiguredDftFlowService::stage(referencedProject,
        temp.filePath(QStringLiteral("referenced-compatibility-stage")),
        QVariantMap{{QStringLiteral("source_exclude_files"), QVariantList{QStringLiteral("orphan_switch.v")}}},
        agentRoot);
    ok &= require(!referencedExclusion.value(QStringLiteral("ok")).toBool(),
                  "iteration refuses to exclude an unsupported module referenced by project RTL");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/top.sv")),
                            "module top(input clk, input rst_n); endmodule\n"),
                  "restore top-level fixture after source-exclusion check");
    const QVariantMap compatibilityOptimizationArguments{{QStringLiteral("dc_shell"), fakeDcRetryAfterUnsupportedSource},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2}};
    const QVariantMap compatibilityReadiness = ConfiguredDftFlowService::readiness(
        compatibilityProject, compatibilityOptimizationArguments, agentRoot);
    if (!compatibilityReadiness.value(QStringLiteral("result")).toMap().value(QStringLiteral("ready")).toBool())
        std::fprintf(stderr, "compatibility readiness: %s\n",
            QJsonDocument::fromVariant(compatibilityReadiness).toJson(QJsonDocument::Compact).constData());
    QVariantMap compatibilityRoundReadinessArguments = compatibilityOptimizationArguments;
    compatibilityRoundReadinessArguments.remove(QStringLiteral("maximum_rounds"));
    compatibilityRoundReadinessArguments.insert(QStringLiteral("atpg_abort_limit"), 10);
    compatibilityRoundReadinessArguments.insert(QStringLiteral("drc_repair_mode"), QStringLiteral("off"));
    compatibilityRoundReadinessArguments.insert(QStringLiteral("flow_timeout_multiplier"), 1);
    compatibilityRoundReadinessArguments.insert(QStringLiteral("atpg_timeout_multiplier"), 1);
    compatibilityRoundReadinessArguments.insert(QStringLiteral("mbist_timeout_multiplier"), 1);
    compatibilityRoundReadinessArguments.insert(QStringLiteral("mbist_include_mode"), QStringLiteral("declared"));
    compatibilityRoundReadinessArguments.insert(QStringLiteral("mbist_diagnostic_mode"), QStringLiteral("off"));
    compatibilityRoundReadinessArguments.insert(QStringLiteral("compile_strategy"), QStringLiteral("single_pass"));
    compatibilityRoundReadinessArguments.insert(QStringLiteral("source_preprocess_mode"), QStringLiteral("off"));
    const QVariantMap compatibilityRoundReadiness = ConfiguredDftFlowService::readiness(
        compatibilityProject, compatibilityRoundReadinessArguments, agentRoot);
    if (!compatibilityRoundReadiness.value(QStringLiteral("result")).toMap().value(QStringLiteral("ready")).toBool())
        std::fprintf(stderr, "compatibility round readiness: %s\n",
            QJsonDocument::fromVariant(compatibilityRoundReadiness).toJson(QJsonDocument::Compact).constData());
    const QVariantMap compatibilityOptimizationStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), compatibilityProject,
        compatibilityOptimizationArguments, agentRoot);
    const QString compatibilityJobId = compatibilityOptimizationStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap compatibilityOptimizationWait = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        compatibilityProject,
        QVariantMap{{QStringLiteral("job_id"), compatibilityJobId}, {QStringLiteral("wait_seconds"), 30}},
        agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap compatibilityOptimizationPayload = compatibilityOptimizationWait
        .value(QStringLiteral("result")).toMap().value(QStringLiteral("result")).toMap();
    const QVariantMap compatibilityOptimization = compatibilityOptimizationPayload
        .value(QStringLiteral("optimization")).toMap();
    const QVariantList compatibilityProfiles = compatibilityOptimization.value(QStringLiteral("profiles")).toList();
    const QVariantMap compatibilityRetryRound = compatibilityProfiles.size() > 1
        ? compatibilityProfiles.at(1).toMap() : QVariantMap{};
    const QVariantMap compatibilityRetryDiagnosis = compatibilityProfiles.isEmpty()
        ? QVariantMap{} : compatibilityProfiles.at(0).toMap().value(QStringLiteral("source_compatibility")).toMap();
    const QString compatibilityRetryWorkspace = compatibilityRetryRound.value(QStringLiteral("workspace")).toString();
    QFile compatibilityRetryStageFile(QDir(compatibilityRetryWorkspace).filePath(QStringLiteral("stage.json")));
    QVariantMap compatibilityRetryStage;
    if (compatibilityRetryStageFile.open(QIODevice::ReadOnly))
        compatibilityRetryStage = QJsonDocument::fromJson(compatibilityRetryStageFile.readAll()).object().toVariantMap();
    ok &= require(compatibilityOptimizationStarted.value(QStringLiteral("ok")).toBool()
                      && compatibilityOptimizationWait.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && compatibilityProfiles.size() == 2
                      && compatibilityRetryDiagnosis.value(QStringLiteral("safe_exclusion")).toBool()
                      && compatibilityRetryRound.value(QStringLiteral("source_exclude_files")).toStringList()
                             .contains(QStringLiteral("orphan_switch.v"))
                      && compatibilityRetryStage.value(QStringLiteral("source_compatibility_exclusions")).toList().size() == 1,
                  "native optimization diagnoses compiler evidence, excludes only a safe orphan, and records retry evidence");
    if (compatibilityProfiles.size() != 2 || !compatibilityRetryDiagnosis.value(QStringLiteral("safe_exclusion")).toBool()) {
        std::fprintf(stderr, "compatibility optimization start: %s\n",
            QJsonDocument::fromVariant(compatibilityOptimizationStarted).toJson(QJsonDocument::Compact).constData());
        std::fprintf(stderr, "compatibility optimization wait: %s\n",
            QJsonDocument::fromVariant(compatibilityOptimizationWait).toJson(QJsonDocument::Compact).constData());
    }
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("filelist.txt")), "rtl/top.sv\n"),
                  "restore basic filelist fixture for subsequent tests");
    QVariantMap duplicatedManifestProject = project;
    QVariantMap duplicatedManifestMetadata = duplicatedManifestProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap duplicatedManifestExecution = duplicatedManifestMetadata.value(QStringLiteral("dft_execution")).toMap();
    duplicatedManifestExecution.insert(QStringLiteral("filelist"), QStringLiteral("filelist.txt"));
    duplicatedManifestExecution.insert(QStringLiteral("source_files"), QVariantList{QStringLiteral("filelist.txt")});
    duplicatedManifestMetadata.insert(QStringLiteral("dft_execution"), duplicatedManifestExecution);
    duplicatedManifestProject.insert(QStringLiteral("metadata"), duplicatedManifestMetadata);
    const QVariantMap duplicatedManifestReadiness = ConfiguredDftFlowService::readiness(
        duplicatedManifestProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(duplicatedManifestReadiness.value(QStringLiteral("ready")).toBool()
                      && duplicatedManifestReadiness.value(QStringLiteral("source_files")).toStringList()
                          == QStringList{QStringLiteral("top.sv")}
                      && duplicatedManifestReadiness.value(QStringLiteral("missing_source_files")).toStringList().isEmpty(),
                  "manifest repeated in source_files is parsed once rather than reported missing");

    QVariantMap modelsProject = project;
    QVariantMap modelsMetadata = modelsProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap modelsExecution = modelsMetadata.value(QStringLiteral("dft_execution")).toMap();
    modelsExecution.insert(QStringLiteral("macro_library_files"), QVariantList{QStringLiteral("sram_macro.db")});
    modelsExecution.insert(QStringLiteral("macro_library_limitations"), QStringLiteral("abstract SRAM model"));
    modelsExecution.insert(QStringLiteral("test_models"), QVariantList{QVariantMap{
        {QStringLiteral("format"), QStringLiteral("CTL")},
        {QStringLiteral("design"), QStringLiteral("sram_model")},
        {QStringLiteral("file"), QStringLiteral("models/sram.ctl")}}});
    modelsMetadata.insert(QStringLiteral("dft_execution"), modelsExecution);
    modelsProject.insert(QStringLiteral("metadata"), modelsMetadata);
    const QVariantMap modelsReadiness = ConfiguredDftFlowService::readiness(
        modelsProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap modelsStage = ConfiguredDftFlowService::stage(
        modelsProject, temp.filePath(QStringLiteral("models-isolated")), runArguments, agentRoot);
    const QVariantMap modelsStageResult = modelsStage.value(QStringLiteral("result")).toMap();
    const QString modelsFlow = modelsStageResult.value(QStringLiteral("flow_directory")).toString();
    QFile modelsDriver(QDir(modelsFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool modelsDriverReadable = modelsDriver.open(QIODevice::ReadOnly);
    const QByteArray modelsScript = modelsDriverReadable ? modelsDriver.readAll() : QByteArray{};
    const QVariantMap modelsSource = modelsStageResult.value(QStringLiteral("source")).toMap();
    const qsizetype linkIndex = modelsScript.indexOf("link\n");
    const qsizetype testModelIndex = modelsScript.indexOf("read_test_model -format ctl -design sram_model");
    const qsizetype readLinkIndex = modelsScript.indexOf("redirect -tee reports/read_link.rpt");
    ok &= require(modelsReadiness.value(QStringLiteral("ready")).toBool()
                      && modelsStage.value(QStringLiteral("ok")).toBool() && modelsDriverReadable
                      && modelsScript.contains(QByteArray("set link_library [concat * $target_library {")
                                                   + QFile::encodeName(macroLibrary) + "} dw_foundation.sldb]")
                      && modelsScript.contains(QByteArray("read_test_model -format ctl -design sram_model {")
                                                   + QFile::encodeName(ctlTestModel) + "}")
                      && linkIndex >= 0 && testModelIndex > linkIndex && readLinkIndex > testModelIndex
                      && modelsScript.contains("Error/Fatal diagnostics; stopping before constraints and compile")
                      && modelsSource.value(QStringLiteral("test_models")).toList().size() == 1
                      && modelsSource.value(QStringLiteral("macro_library_files")).toStringList()
                          == QStringList{macroLibrary}
                      && modelsSource.value(QStringLiteral("macro_library_limitations")).toString()
                          == QStringLiteral("abstract SRAM model"),
                  "native flow stages macro/test-model configuration into link/read Tcl and evidence");
    QVariantMap invalidDdcProject = modelsProject;
    QVariantMap invalidDdcMetadata = invalidDdcProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap invalidDdcExecution = invalidDdcMetadata.value(QStringLiteral("dft_execution")).toMap();
    invalidDdcExecution.insert(QStringLiteral("test_models"), QVariantList{QVariantMap{
        {QStringLiteral("format"), QStringLiteral("ddc")},
        {QStringLiteral("design"), QStringLiteral("unexpected_design")},
        {QStringLiteral("file"), QStringLiteral("models/sram.ctl")}}});
    invalidDdcMetadata.insert(QStringLiteral("dft_execution"), invalidDdcExecution);
    invalidDdcProject.insert(QStringLiteral("metadata"), invalidDdcMetadata);
    const QVariantMap invalidDdcReadiness = ConfiguredDftFlowService::readiness(
        invalidDdcProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(!invalidDdcReadiness.value(QStringLiteral("ready")).toBool(),
                  "native readiness rejects DDC models with a design field");

    const QString nestedFilelist = QDir(root).filePath(QStringLiteral("rtl/lists/top.f"));
    ok &= require(writeFile(nestedFilelist, "-f rtl/lists/rtl_files.f\n"),
                  "create root filelist which nests another list relative to the project");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/rtl_files.f")),
                            "-F sources.f\n+incdir+../include\n+define+NATIVE_TEST=1\n-DNATIVE_FLAG=2\n-I../include\n"),
                  "create nested filelist with manifest-relative child, include dirs, and macro forms");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/sources.f")),
                            "../top.sv\n"),
                  "create leaf filelist with source relative to its own location");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/include/defs.vh")),
                            "`define NATIVE_TEST 1\n"),
                  "create nested filelist include directory");
    QVariantMap nestedProject = project;
    QVariantMap nestedMetadata = nestedProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap nestedExecution = nestedMetadata.value(QStringLiteral("dft_execution")).toMap();
    nestedExecution.remove(QStringLiteral("source_files"));
    nestedExecution.insert(QStringLiteral("filelist"), QStringLiteral("rtl/lists/top.f"));
    nestedMetadata.insert(QStringLiteral("dft_execution"), nestedExecution);
    nestedProject.insert(QStringLiteral("metadata"), nestedMetadata);
    const QVariantMap nestedReadinessEnvelope = ConfiguredDftFlowService::readiness(
        nestedProject, runArguments, agentRoot);
    const QVariantMap nestedReadiness = nestedReadinessEnvelope.value(QStringLiteral("result")).toMap();
    ok &= require(nestedReadinessEnvelope.value(QStringLiteral("ok")).toBool()
                      && nestedReadiness.value(QStringLiteral("ready")).toBool()
                      && nestedReadiness.value(QStringLiteral("source_files")).toStringList()
                          == QStringList{QStringLiteral("top.sv")}
                      && nestedReadiness.value(QStringLiteral("source_include_dirs")).toStringList()
                          == QStringList{QStringLiteral("include")},
                  "native readiness resolves nested -f/-F filelists and keeps their relative source/include paths");
    const QString nestedWorkspace = temp.filePath(QStringLiteral("nested-isolated"));
    const QVariantMap nestedStageEnvelope = ConfiguredDftFlowService::stage(
        nestedProject, nestedWorkspace, runArguments, agentRoot);
    const QVariantMap nestedStage = nestedStageEnvelope.value(QStringLiteral("result")).toMap();
    QFile nestedStagedList(QDir(nestedStage.value(QStringLiteral("flow_directory")).toString())
                               .filePath(QStringLiteral("input/rtl.f")));
    const bool nestedListOpened = nestedStagedList.open(QIODevice::ReadOnly);
    const QByteArray nestedStagedContents = nestedListOpened ? nestedStagedList.readAll() : QByteArray{};
    ok &= require(nestedStageEnvelope.value(QStringLiteral("ok")).toBool()
                      && nestedStagedContents.contains("+incdir+./rtl/include")
                      && nestedStagedContents.contains("+define+NATIVE_TEST=1+NATIVE_FLAG=2")
                      && QFileInfo(QDir(nestedStage.value(QStringLiteral("flow_directory")).toString())
                                       .filePath(QStringLiteral("rtl/include/defs.vh"))).isFile(),
                  "nested filelist include directories are staged into an isolated normalized list");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/sources.f")),
                            "-F rtl_files.f\n"),
                  "replace leaf list with a cycle for validation");
    const QVariantMap cyclicReadiness = ConfiguredDftFlowService::readiness(
        nestedProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList cyclicErrors = cyclicReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(!cyclicReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(cyclicErrors.cbegin(), cyclicErrors.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("cycle detected")); }),
                  "native filelist recursion rejects cycles without hanging or duplicating inputs");

    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/bender.yml")),
                            "sources:\n"
                            "  include_dirs: [include]\n"
                            "  files:\n"
                            "    - ../top.sv\n"
                            "    - target: any(synthesis,rtl)\n"
                            "      files: [bender_rtl.sv]\n"
                            "    - target: not(simulation)\n"
                            "      files: [bender_synth.sv]\n"
                            "export_include_dirs: [export_include]\n"
                            "dependencies:\n"
                            "  acme:local:unused: '>=1.0'\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/bender_rtl.sv")),
                                   "module bender_rtl; endmodule\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/bender_synth.sv")),
                                   "module bender_synth; endmodule\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/include/defs.svh")),
                                   "`define BENDER_INCLUDE 1\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/lists/export_include/export.vh")),
                                   "`define BENDER_EXPORT 1\n"),
                  "create a Bender manifest with target conditions and include directories");
    QVariantMap benderProject = project;
    QVariantMap benderMetadata = benderProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap benderExecution = benderMetadata.value(QStringLiteral("dft_execution")).toMap();
    benderExecution.remove(QStringLiteral("source_files"));
    benderExecution.insert(QStringLiteral("filelist"), QStringLiteral("rtl/lists/bender.yml"));
    benderExecution.insert(QStringLiteral("manifest_target"), QStringLiteral("synthesis,rtl"));
    benderMetadata.insert(QStringLiteral("dft_execution"), benderExecution);
    benderProject.insert(QStringLiteral("metadata"), benderMetadata);
    const QVariantMap benderEnvelope = ConfiguredDftFlowService::readiness(
        benderProject, runArguments, agentRoot);
    const QVariantMap benderReadiness = benderEnvelope.value(QStringLiteral("result")).toMap();
    const QStringList benderSources = benderReadiness.value(QStringLiteral("source_files")).toStringList();
    const QStringList benderIncludes = benderReadiness.value(QStringLiteral("source_include_dirs")).toStringList();
    ok &= require(benderEnvelope.value(QStringLiteral("ok")).toBool()
                      && benderReadiness.value(QStringLiteral("ready")).toBool()
                      && benderSources == QStringList{QStringLiteral("top.sv"),
                                                       QStringLiteral("lists/bender_rtl.sv"),
                                                       QStringLiteral("lists/bender_synth.sv")}
                      && benderIncludes.contains(QStringLiteral("lists/include"))
                      && benderIncludes.contains(QStringLiteral("lists/export_include")),
                  "native Bender resolution applies target predicates and stages declared/exported includes");

    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/top.core")),
                            "CAPI=2:\n"
                            "name: acme:demo:top:1.0\n"
                            "filesets:\n"
                            "  rtl:\n"
                            "    depend: [acme:lib:cells:1.0]\n"
                            "    files: [top.sv, {include/defs.vh: {is_include_file: true}}]\n"
                            "targets:\n"
                            "  default:\n"
                            "    filesets: [rtl]\n"
                            "    toplevel: top\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/deps/cells.core")),
                                   "CAPI=2:\n"
                                   "name: acme:lib:cells:1.0\n"
                                   "filesets:\n"
                                   "  rtl:\n"
                                   "    files: [cell.sv]\n"
                                   "targets:\n"
                                   "  default:\n"
                                   "    filesets: [rtl]\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/top.sv")),
                                   "module top; endmodule\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/deps/cell.sv")),
                                   "module cell; endmodule\n")
                      && writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/include/defs.vh")),
                                   "`define FUSESOC_INCLUDE 1\n"),
                  "create FuseSoC cores with a local dependency and an include file");
    QVariantMap fuseSoCProject = project;
    QVariantMap fuseSoCMetadata = fuseSoCProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap fuseSoCExecution = fuseSoCMetadata.value(QStringLiteral("dft_execution")).toMap();
    fuseSoCExecution.remove(QStringLiteral("source_files"));
    fuseSoCExecution.insert(QStringLiteral("filelist"), QStringLiteral("rtl/fusesoc/top.core"));
    fuseSoCMetadata.insert(QStringLiteral("dft_execution"), fuseSoCExecution);
    fuseSoCProject.insert(QStringLiteral("metadata"), fuseSoCMetadata);
    const QVariantMap fuseSoCEnvelope = ConfiguredDftFlowService::readiness(
        fuseSoCProject, runArguments, agentRoot);
    const QVariantMap fuseSoCReadiness = fuseSoCEnvelope.value(QStringLiteral("result")).toMap();
    const QStringList fuseSoCSources = fuseSoCReadiness.value(QStringLiteral("source_files")).toStringList();
    ok &= require(fuseSoCEnvelope.value(QStringLiteral("ok")).toBool()
                      && fuseSoCReadiness.value(QStringLiteral("ready")).toBool()
                      && fuseSoCSources == QStringList{QStringLiteral("fusesoc/deps/cell.sv"),
                                                       QStringLiteral("fusesoc/top.sv")}
                      && fuseSoCReadiness.value(QStringLiteral("source_support_files")).toStringList()
                          == QStringList{QStringLiteral("fusesoc/include/defs.vh")}
                      && fuseSoCReadiness.value(QStringLiteral("source_include_dirs")).toStringList()
                          == QStringList{QStringLiteral("fusesoc/include")},
                  "native FuseSoC resolver follows local core dependencies and records include headers");
    QVariantMap excludedFuseSoCProject = fuseSoCProject;
    QVariantMap excludedFuseSoCMetadata = excludedFuseSoCProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap excludedFuseSoCExecution = excludedFuseSoCMetadata.value(QStringLiteral("dft_execution")).toMap();
    excludedFuseSoCExecution.insert(QStringLiteral("manifest_exclude_files"),
                                    QVariantList{QStringLiteral("fusesoc/deps/cell.sv")});
    excludedFuseSoCMetadata.insert(QStringLiteral("dft_execution"), excludedFuseSoCExecution);
    excludedFuseSoCProject.insert(QStringLiteral("metadata"), excludedFuseSoCMetadata);
    const QVariantMap excludedFuseSoCReadiness = ConfiguredDftFlowService::readiness(
        excludedFuseSoCProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(excludedFuseSoCReadiness.value(QStringLiteral("ready")).toBool()
                      && excludedFuseSoCReadiness.value(QStringLiteral("source_files")).toStringList()
                          == QStringList{QStringLiteral("fusesoc/top.sv")},
                  "legacy manifest_exclude_files removes only a resolved FuseSoC compile source");
    QVariantMap unmatchedFuseSoCProject = fuseSoCProject;
    QVariantMap unmatchedFuseSoCMetadata = unmatchedFuseSoCProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap unmatchedFuseSoCExecution = unmatchedFuseSoCMetadata.value(QStringLiteral("dft_execution")).toMap();
    unmatchedFuseSoCExecution.insert(QStringLiteral("manifest_exclude_files"),
                                     QVariantList{QStringLiteral("fusesoc/not_in_manifest.sv")});
    unmatchedFuseSoCMetadata.insert(QStringLiteral("dft_execution"), unmatchedFuseSoCExecution);
    unmatchedFuseSoCProject.insert(QStringLiteral("metadata"), unmatchedFuseSoCMetadata);
    const QVariantMap unmatchedFuseSoCReadiness = ConfiguredDftFlowService::readiness(
        unmatchedFuseSoCProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList unmatchedFuseSoCErrors = unmatchedFuseSoCReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(!unmatchedFuseSoCReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(unmatchedFuseSoCErrors.cbegin(), unmatchedFuseSoCErrors.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("not present in the filelist")); }),
                  "legacy manifest exclusions reject entries absent from the selected source manifest");
    const QString fuseSoCWorkspace = temp.filePath(QStringLiteral("fusesoc-isolated"));
    const QVariantMap fuseSoCStageEnvelope = ConfiguredDftFlowService::stage(
        fuseSoCProject, fuseSoCWorkspace, runArguments, agentRoot);
    const QVariantMap fuseSoCStage = fuseSoCStageEnvelope.value(QStringLiteral("result")).toMap();
    ok &= require(fuseSoCStageEnvelope.value(QStringLiteral("ok")).toBool()
                      && QFileInfo(QDir(fuseSoCStage.value(QStringLiteral("flow_directory")).toString())
                                       .filePath(QStringLiteral("rtl/fusesoc/deps/cell.sv"))).isFile()
                      && QFileInfo(QDir(fuseSoCStage.value(QStringLiteral("flow_directory")).toString())
                                       .filePath(QStringLiteral("rtl/fusesoc/include/defs.vh"))).isFile(),
                  "native FuseSoC sources and headers are copied into isolated staging");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/cycle.core")),
                            "CAPI=2:\n"
                            "name: acme:cycle:top:1.0\n"
                            "filesets:\n"
                            "  rtl:\n"
                            "    depend: [acme:cycle:top:1.0]\n"
                            "    files: [top.sv]\n"
                            "targets:\n"
                            "  default:\n"
                            "    filesets: [rtl]\n"),
                  "create a FuseSoC dependency cycle fixture");
    QVariantMap cycleProject = fuseSoCProject;
    QVariantMap cycleMetadata = cycleProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap cycleExecution = cycleMetadata.value(QStringLiteral("dft_execution")).toMap();
    cycleExecution.insert(QStringLiteral("filelist"), QStringLiteral("rtl/fusesoc/cycle.core"));
    cycleMetadata.insert(QStringLiteral("dft_execution"), cycleExecution);
    cycleProject.insert(QStringLiteral("metadata"), cycleMetadata);
    const QVariantMap cycleReadiness = ConfiguredDftFlowService::readiness(
        cycleProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList cycleUnsupported = cycleReadiness.value(QStringLiteral("unsupported_modules")).toStringList();
    ok &= require(!cycleReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(cycleUnsupported.cbegin(), cycleUnsupported.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("dependency_cycle")); }),
                  "native FuseSoC resolution rejects local dependency cycles");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/fusesoc/escape.core")),
                            "CAPI=2:\n"
                            "name: acme:escape:top:1.0\n"
                            "filesets:\n"
                            "  rtl:\n"
                            "    files: [../../../outside.sv]\n"
                            "targets:\n"
                            "  default:\n"
                            "    filesets: [rtl]\n"),
                  "create a FuseSoC path escape fixture");
    QVariantMap escapeProject = fuseSoCProject;
    QVariantMap escapeMetadata = escapeProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap escapeExecution = escapeMetadata.value(QStringLiteral("dft_execution")).toMap();
    escapeExecution.insert(QStringLiteral("filelist"), QStringLiteral("rtl/fusesoc/escape.core"));
    escapeMetadata.insert(QStringLiteral("dft_execution"), escapeExecution);
    escapeProject.insert(QStringLiteral("metadata"), escapeMetadata);
    const QVariantMap escapeReadiness = ConfiguredDftFlowService::readiness(
        escapeProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(!escapeReadiness.value(QStringLiteral("ready")).toBool()
                      && escapeReadiness.value(QStringLiteral("missing_source_files")).toStringList()
                          .contains(QStringLiteral("../../../outside.sv")),
                  "native FuseSoC resolver refuses source paths outside approved project roots");

    const QString externalRtlRoot = temp.filePath(QStringLiteral("shared/rtl-tree"));
    const QString externalFilelist = QDir(externalRtlRoot).filePath(QStringLiteral("design.f"));
    const QString externalConstraint = QDir(externalRtlRoot).filePath(QStringLiteral("timing.sdc"));
    ok &= require(writeFile(QDir(externalRtlRoot).filePath(QStringLiteral("top.sv")),
                            "module top(input clk, input rst_n); endmodule\n")
                      && writeFile(externalFilelist, "top.sv\n")
                      && writeFile(externalConstraint, "create_clock -period 10 [get_ports clk]\n"),
                  "create external RTL, manifest, and constraint fixtures");
    QVariantMap externalProject = project;
    externalProject.insert(QStringLiteral("rtl_root"), externalRtlRoot);
    QVariantMap externalMetadata = externalProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap externalExecution = externalMetadata.value(QStringLiteral("dft_execution")).toMap();
    externalExecution.remove(QStringLiteral("source_files"));
    externalExecution.insert(QStringLiteral("filelist"), externalFilelist);
    externalExecution.insert(QStringLiteral("constraint_file"), externalConstraint);
    externalExecution.insert(QStringLiteral("use_constraint_file"), true);
    externalMetadata.insert(QStringLiteral("dft_execution"), externalExecution);
    externalProject.insert(QStringLiteral("metadata"), externalMetadata);
    const QVariantMap externalReadiness = ConfiguredDftFlowService::readiness(
        externalProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(externalReadiness.value(QStringLiteral("ready")).toBool()
                      && externalReadiness.value(QStringLiteral("source_rtl_root")).toString() == externalRtlRoot
                      && externalReadiness.value(QStringLiteral("source_files")).toStringList()
                          == QStringList{QStringLiteral("top.sv")},
                  "native readiness supports an explicitly configured external RTL tree and filelist");
    const QString externalWorkspace = temp.filePath(QStringLiteral("external-isolated"));
    const QVariantMap externalStageEnvelope = ConfiguredDftFlowService::stage(
        externalProject, externalWorkspace, runArguments, agentRoot);
    const QString externalFlow = externalStageEnvelope.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    ok &= require(externalStageEnvelope.value(QStringLiteral("ok")).toBool()
                      && QFileInfo(QDir(externalFlow).filePath(QStringLiteral("rtl/top.sv"))).isFile()
                      && QFileInfo(QDir(externalFlow).filePath(QStringLiteral("input/timing.sdc"))).isFile(),
                  "native staging copies explicitly configured external RTL and constraints into isolation");

    const QString cppSource = QDir(root).filePath(QStringLiteral("rtl/cpp_top.sv"));
    const QString cppInclude = QDir(root).filePath(QStringLiteral("cpp_includes/feature.h"));
    const QString cppMacroFile = QDir(root).filePath(QStringLiteral("cpp_macros.h"));
    ok &= require(writeFile(cppSource,
                            "#if ENABLE_EXTRA\n// preserve Verilog comment\n"
                            "module top #(parameter [7:0] MASK = 8'hA5)(input clk, input rst_n);\n"
                            "  localparam WIDTH = PARAM_WIDTH;\nendmodule\n#else\n#error missing feature macro\n#endif\n")
                      && writeFile(cppInclude, "#define ENABLE_EXTRA 1\n")
                      && writeFile(cppMacroFile, "#include \"feature.h\"\n#define PARAM_WIDTH 8\n"),
                  "create RTL and macro/include fixtures for staged C preprocessing");
    QVariantMap preprocessorProject = project;
    QVariantMap preprocessorMetadata = preprocessorProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap preprocessorExecution = preprocessorMetadata.value(QStringLiteral("dft_execution")).toMap();
    preprocessorExecution.insert(QStringLiteral("source_files"), QVariantList{QStringLiteral("cpp_top.sv")});
    preprocessorExecution.insert(QStringLiteral("source_preprocessor"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("cpp")},
        {QStringLiteral("source_files"), QVariantList{QStringLiteral("cpp_top.sv")}},
        {QStringLiteral("macro_file"), QStringLiteral("cpp_macros.h")},
        {QStringLiteral("definitions"), QVariantList{QStringLiteral("EXTRA_FLAG=1")}},
        {QStringLiteral("include_dirs"), QVariantList{QStringLiteral("cpp_includes")}},
        {QStringLiteral("timeout_seconds"), 30},
    });
    preprocessorMetadata.insert(QStringLiteral("dft_execution"), preprocessorExecution);
    preprocessorProject.insert(QStringLiteral("metadata"), preprocessorMetadata);
    const QVariantMap preprocessorReadinessEnvelope = ConfiguredDftFlowService::readiness(
        preprocessorProject, runArguments, agentRoot);
    const QVariantMap preprocessorReadiness = preprocessorReadinessEnvelope.value(QStringLiteral("result")).toMap();
    ok &= require(preprocessorReadinessEnvelope.value(QStringLiteral("ok")).toBool()
                      && preprocessorReadiness.value(QStringLiteral("ready")).toBool(),
                  "native readiness accepts a valid staged C-preprocessor configuration");
    const QString preprocessorWorkspace = temp.filePath(QStringLiteral("preprocessor-isolated"));
    const QVariantMap preprocessorStage = ConfiguredDftFlowService::stage(
        preprocessorProject, preprocessorWorkspace, runArguments, agentRoot);
    const QString preprocessorFlow = preprocessorStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    QFile preprocessedFile(QDir(preprocessorFlow).filePath(QStringLiteral("rtl/cpp_top.sv")));
    QFile originalPreprocessorSource(cppSource);
    const bool preprocessedReadable = preprocessedFile.open(QIODevice::ReadOnly);
    const QByteArray preprocessedText = preprocessedReadable ? preprocessedFile.readAll() : QByteArray{};
    const bool originalReadable = originalPreprocessorSource.open(QIODevice::ReadOnly);
    const QByteArray originalPreprocessorText = originalReadable ? originalPreprocessorSource.readAll() : QByteArray{};
    const QVariantMap preprocessorSource = preprocessorStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("source")).toMap().value(QStringLiteral("source_preprocessor")).toMap();
    ok &= require(preprocessorStage.value(QStringLiteral("ok")).toBool() && preprocessedReadable
                      && preprocessedText.contains("module top") && preprocessedText.contains("8'hA5")
                      && preprocessedText.contains("localparam WIDTH = 8")
                      && preprocessedText.contains("// preserve Verilog comment")
                      && !preprocessedText.contains("#if ENABLE_EXTRA")
                      && originalReadable && originalPreprocessorText.contains("#if ENABLE_EXTRA")
                      && preprocessorSource.value(QStringLiteral("original_project_modified")).toBool() == false
                      && preprocessorSource.value(QStringLiteral("files")).toList().size() == 1,
                  "native preprocessing expands configured macros while preserving Verilog syntax and source immutability");

    QVariantMap preprocessOffArguments = runArguments;
    preprocessOffArguments.insert(QStringLiteral("source_preprocess_mode"), QStringLiteral("off"));
    const QVariantMap preprocessOffStage = ConfiguredDftFlowService::stage(
        preprocessorProject, temp.filePath(QStringLiteral("preprocessor-forced-off")),
        preprocessOffArguments, agentRoot);
    QFile preprocessOffFile(QDir(preprocessOffStage.value(QStringLiteral("result")).toMap()
                                     .value(QStringLiteral("flow_directory")).toString())
                                .filePath(QStringLiteral("rtl/cpp_top.sv")));
    const bool preprocessOffReadable = preprocessOffFile.open(QIODevice::ReadOnly);
    const QByteArray preprocessOffText = preprocessOffReadable ? preprocessOffFile.readAll() : QByteArray{};
    ok &= require(preprocessOffStage.value(QStringLiteral("ok")).toBool() && preprocessOffReadable
                      && preprocessOffText.contains("#if ENABLE_EXTRA")
                      && preprocessOffText.contains("#error missing feature macro"),
                  "iteration source_preprocess_mode=off overrides configured preprocessing in the staged copy");

    QVariantMap configuredPreprocessorOff = preprocessorProject;
    QVariantMap configuredOffMetadata = configuredPreprocessorOff.value(QStringLiteral("metadata")).toMap();
    QVariantMap configuredOffExecution = configuredOffMetadata.value(QStringLiteral("dft_execution")).toMap();
    QVariantMap configuredOffPreprocessor = configuredOffExecution.value(QStringLiteral("source_preprocessor")).toMap();
    configuredOffPreprocessor.insert(QStringLiteral("mode"), QStringLiteral("off"));
    configuredOffExecution.insert(QStringLiteral("source_preprocessor"), configuredOffPreprocessor);
    configuredOffMetadata.insert(QStringLiteral("dft_execution"), configuredOffExecution);
    configuredPreprocessorOff.insert(QStringLiteral("metadata"), configuredOffMetadata);
    QVariantMap preprocessCppArguments = runArguments;
    preprocessCppArguments.insert(QStringLiteral("source_preprocess_mode"), QStringLiteral("cpp"));
    const QVariantMap preprocessCppStage = ConfiguredDftFlowService::stage(
        configuredPreprocessorOff, temp.filePath(QStringLiteral("preprocessor-forced-cpp")),
        preprocessCppArguments, agentRoot);
    QFile preprocessCppFile(QDir(preprocessCppStage.value(QStringLiteral("result")).toMap()
                                     .value(QStringLiteral("flow_directory")).toString())
                                .filePath(QStringLiteral("rtl/cpp_top.sv")));
    const bool preprocessCppReadable = preprocessCppFile.open(QIODevice::ReadOnly);
    const QByteArray preprocessCppText = preprocessCppReadable ? preprocessCppFile.readAll() : QByteArray{};
    ok &= require(preprocessCppStage.value(QStringLiteral("ok")).toBool() && preprocessCppReadable
                      && preprocessCppText.contains("module top") && !preprocessCppText.contains("#if ENABLE_EXTRA"),
                  "iteration source_preprocess_mode=cpp enables declared preprocessing on the staged copy");

    QVariantMap implicitPreprocessorProject = preprocessorProject;
    QVariantMap implicitMetadata = implicitPreprocessorProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap implicitExecution = implicitMetadata.value(QStringLiteral("dft_execution")).toMap();
    QVariantMap implicitPreprocessor = implicitExecution.value(QStringLiteral("source_preprocessor")).toMap();
    implicitPreprocessor.remove(QStringLiteral("source_files"));
    implicitPreprocessor.insert(QStringLiteral("mode"), QStringLiteral("off"));
    implicitExecution.insert(QStringLiteral("source_preprocessor"), implicitPreprocessor);
    implicitMetadata.insert(QStringLiteral("dft_execution"), implicitExecution);
    implicitPreprocessorProject.insert(QStringLiteral("metadata"), implicitMetadata);
    const QVariantMap implicitPreprocessStage = ConfiguredDftFlowService::stage(
        implicitPreprocessorProject, temp.filePath(QStringLiteral("preprocessor-implicit-sources")),
        preprocessCppArguments, agentRoot);
    const QVariantMap implicitSourcePreprocessor = implicitPreprocessStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("source")).toMap().value(QStringLiteral("source_preprocessor")).toMap();
    ok &= require(implicitPreprocessStage.value(QStringLiteral("ok")).toBool()
                      && implicitSourcePreprocessor.value(QStringLiteral("files")).toList().size() == 1,
                  "cpp mode defaults to declared RTL inputs when no narrower source list is configured");

    QVariantMap timingProject = project;
    QVariantMap timingMetadata = timingProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap timingExecution = timingMetadata.value(QStringLiteral("dft_execution")).toMap();
    timingExecution.insert(QStringLiteral("map_effort"), QStringLiteral("low"));
    timingExecution.insert(QStringLiteral("area_effort"), QStringLiteral("low"));
    timingExecution.insert(QStringLiteral("power_effort"), QStringLiteral("none"));
    timingExecution.insert(QStringLiteral("synthesis_settings"), QVariantMap{
        {QStringLiteral("clocks"), QVariantList{QVariantMap{
            {QStringLiteral("name"), QStringLiteral("core_clk")},
            {QStringLiteral("source"), QStringLiteral("clk")},
            {QStringLiteral("period"), 10.0}, {QStringLiteral("rise"), 1.0}, {QStringLiteral("fall"), 6.0},
            {QStringLiteral("setup_uncertainty"), 0.2}, {QStringLiteral("hold_uncertainty"), 0.1},
            {QStringLiteral("transition"), 0.03}, {QStringLiteral("source_latency"), 0.4},
            {QStringLiteral("network_latency"), 0.2},
        }}},
        {QStringLiteral("generated_clocks"), QVariantList{QVariantMap{
            {QStringLiteral("name"), QStringLiteral("div_clk")},
            {QStringLiteral("source"), QStringLiteral("u_div/clk")},
            {QStringLiteral("target"), QStringLiteral("u_div/clk_out")},
            {QStringLiteral("master"), QStringLiteral("core_clk")},
            {QStringLiteral("divide_by"), 2}, {QStringLiteral("invert"), true},
        }}},
        {QStringLiteral("io_delays"), QVariantList{QVariantMap{
            {QStringLiteral("direction"), QStringLiteral("input")},
            {QStringLiteral("ports"), QStringLiteral("data_in")},
            {QStringLiteral("clock"), QStringLiteral("core_clk")},
            {QStringLiteral("max"), 2.0}, {QStringLiteral("min"), 0.1},
        }}},
        {QStringLiteral("timing_exceptions"), QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("false_path")},
                        {QStringLiteral("from"), QStringLiteral("[get_ports {async_reset}]")}},
            QVariantMap{{QStringLiteral("type"), QStringLiteral("multicycle")},
                        {QStringLiteral("value"), 2}, {QStringLiteral("check"), QStringLiteral("setup")},
                        {QStringLiteral("from"), QStringLiteral("[get_clocks {core_clk}]" )},
                        {QStringLiteral("to"), QStringLiteral("[get_pins {u_reg/D}]")}},
        }},
        {QStringLiteral("clock_groups_tcl"), QStringLiteral(
            "set_clock_groups -asynchronous -group [get_clocks {core_clk}] -group [get_clocks {aux_clk}]")},
        {QStringLiteral("reports"), QVariantList{
            QStringLiteral("power"), QStringLiteral("constraints"), QStringLiteral("resources"), QStringLiteral("unknown")}},
        {QStringLiteral("operating_condition"), QStringLiteral("WCCOM")},
        {QStringLiteral("min_library"), QStringLiteral("slow.db")},
        {QStringLiteral("max_transition"), 0.2},
        {QStringLiteral("max_fanout"), 16},
        {QStringLiteral("max_capacitance"), 0.5},
        {QStringLiteral("driving_cell"), QStringLiteral("INV_X1")},
        {QStringLiteral("output_load"), 0.01},
        {QStringLiteral("additional_tcl_commands"), QVariantList{
            QStringLiteral("set_max_area 100000"),
            QStringLiteral("set_case_analysis 0 [get_ports test_mode]")}},
    });
    timingMetadata.insert(QStringLiteral("dft_execution"), timingExecution);
    timingProject.insert(QStringLiteral("metadata"), timingMetadata);
    const QVariantMap timingReadiness = ConfiguredDftFlowService::readiness(
        timingProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QString timingWorkspace = temp.filePath(QStringLiteral("timing-isolated"));
    const QVariantMap timingStage = ConfiguredDftFlowService::stage(
        timingProject, timingWorkspace, runArguments, agentRoot);
    QFile timingDriver(QDir(timingStage.value(QStringLiteral("result")).toMap()
                                .value(QStringLiteral("flow_directory")).toString())
                           .filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool timingDriverReadable = timingDriver.open(QIODevice::ReadOnly);
    const QByteArray timingScript = timingDriverReadable ? timingDriver.readAll() : QByteArray{};
    ok &= require(timingReadiness.value(QStringLiteral("ready")).toBool()
                      && timingStage.value(QStringLiteral("ok")).toBool() && timingDriverReadable
                      && timingScript.contains("create_clock -name {core_clk} -period 10")
                      && timingScript.contains("set_clock_uncertainty -setup 0.2 [get_clocks {core_clk}]")
                      && timingScript.contains("set_clock_latency -source 0.4 [get_clocks {core_clk}]")
                      && timingScript.contains("create_generated_clock -name {div_clk} -source [get_pins {u_div/clk}] -master_clock {core_clk} -divide_by 2 -invert [get_pins {u_div/clk_out}]")
                      && timingScript.contains("set_input_delay 2 -max -clock [get_clocks {core_clk}] [get_ports {data_in}]")
                      && timingScript.contains("set_false_path -from [get_ports {async_reset}]")
                      && timingScript.contains("set_multicycle_path 2 -setup -from [get_clocks {core_clk}] -to [get_pins {u_reg/D}]")
                      && timingScript.contains("set_clock_groups -asynchronous")
                      && timingScript.contains("{report_power}")
                      && timingScript.contains("{report_constraint -all_violators}")
                      && timingScript.contains("{report_resources}")
                      && timingScript.contains("set_operating_conditions {WCCOM}")
                      && timingScript.contains("set_min_library {typical.db} -min_version {slow.db}")
                      && timingScript.contains("set_max_transition 0.2 [current_design]")
                      && timingScript.contains("set_max_fanout 16 [current_design]")
                      && timingScript.contains("set_max_capacitance 0.5 [current_design]")
                      && timingScript.contains("set_driving_cell -lib_cell {INV_X1} [all_inputs]")
                      && timingScript.contains("set_load 0.01 [all_outputs]")
                      && !timingScript.contains("reports/synthesis_qor.rpt")
                      && !timingScript.contains("reports/synthesis_area.rpt")
                      && !timingScript.contains("reports/synthesis_timing.rpt")
                      && timingScript.contains("set_max_area 100000")
                      && timingScript.contains("set_case_analysis 0 [get_ports test_mode]")
                      && timingScript.contains("compile -map_effort low -area_effort low -boundary_optimization -scan\n")
                      && timingScript.indexOf("set_case_analysis 0 [get_ports test_mode]")
                          < timingScript.indexOf("compile -map_effort low -area_effort low -boundary_optimization -scan\n"),
                  "native timing and allowlisted synthesis settings generate Tcl before compile");
    QVariantMap twoStageProject = project;
    QVariantMap twoStageMetadata = twoStageProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap twoStageExecution = twoStageMetadata.value(QStringLiteral("dft_execution")).toMap();
    twoStageExecution.insert(QStringLiteral("compile_strategy"), QStringLiteral("two_stage_mapping"));
    twoStageMetadata.insert(QStringLiteral("dft_execution"), twoStageExecution);
    twoStageProject.insert(QStringLiteral("metadata"), twoStageMetadata);
    const QVariantMap twoStage = ConfiguredDftFlowService::stage(
        twoStageProject, temp.filePath(QStringLiteral("two-stage-isolated")), runArguments, agentRoot);
    const QString twoStageFlow = twoStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    QFile twoStageDriver(QDir(twoStageFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool twoStageDriverReadable = twoStageDriver.open(QIODevice::ReadOnly);
    const QByteArray twoStageScript = twoStageDriverReadable ? twoStageDriver.readAll() : QByteArray{};
    const qsizetype unmappedCompile = twoStageScript.indexOf("compile -no_map\n");
    const qsizetype mappedCompile = twoStageScript.indexOf("\ncompile ", unmappedCompile + 1);
    ok &= require(twoStage.value(QStringLiteral("ok")).toBool() && unmappedCompile >= 0
                      && mappedCompile > unmappedCompile,
                  "two-stage mapping emits compile -no_map before the configured mapped compile");
    QVariantMap singlePassOverride{{QStringLiteral("compile_strategy"), QStringLiteral("single_pass")}};
    for (auto it = runArguments.cbegin(); it != runArguments.cend(); ++it)
        singlePassOverride.insert(it.key(), it.value());
    singlePassOverride.insert(QStringLiteral("compile_strategy"), QStringLiteral("single_pass"));
    const QVariantMap singlePassStage = ConfiguredDftFlowService::stage(
        twoStageProject, temp.filePath(QStringLiteral("single-pass-override")), singlePassOverride, agentRoot);
    QFile singlePassDriver(QDir(singlePassStage.value(QStringLiteral("result")).toMap()
                                    .value(QStringLiteral("flow_directory")).toString())
                               .filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool singlePassDriverReadable = singlePassDriver.open(QIODevice::ReadOnly);
    const QByteArray singlePassScript = singlePassDriverReadable ? singlePassDriver.readAll() : QByteArray{};
    ok &= require(singlePassStage.value(QStringLiteral("ok")).toBool()
                      && !singlePassScript.contains("compile -no_map\n"),
                  "per-run compile_strategy overrides the saved project strategy");
    QVariantMap optimizedProject = project;
    QVariantMap optimizedMetadata = optimizedProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap optimizedExecution = optimizedMetadata.value(QStringLiteral("dft_execution")).toMap();
    optimizedExecution.insert(QStringLiteral("synthesis_settings"), QVariantMap{
        {QStringLiteral("compile_command"), QStringLiteral("compile_ultra")},
        {QStringLiteral("incremental"), true}, {QStringLiteral("retime"), true},
        {QStringLiteral("gate_clock"), true}, {QStringLiteral("scan_ready"), true},
        {QStringLiteral("boundary_optimization"), false},
        {QStringLiteral("auto_ungroup"), QStringLiteral("none")}});
    optimizedMetadata.insert(QStringLiteral("dft_execution"), optimizedExecution);
    optimizedProject.insert(QStringLiteral("metadata"), optimizedMetadata);
    const QVariantMap optimizedReadiness = ConfiguredDftFlowService::readiness(
        optimizedProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap optimizedStage = ConfiguredDftFlowService::stage(
        optimizedProject, temp.filePath(QStringLiteral("optimized-isolated")), runArguments, agentRoot);
    QFile optimizedDriver(QDir(optimizedStage.value(QStringLiteral("result")).toMap()
                                   .value(QStringLiteral("flow_directory")).toString())
                              .filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool optimizedDriverReadable = optimizedDriver.open(QIODevice::ReadOnly);
    const QByteArray optimizedScript = optimizedDriverReadable ? optimizedDriver.readAll() : QByteArray{};
    ok &= require(optimizedReadiness.value(QStringLiteral("ready")).toBool()
                      && optimizedStage.value(QStringLiteral("ok")).toBool() && optimizedDriverReadable
                      && optimizedScript.contains("compile_ultra -incremental -retime -no_boundary_optimization -no_autoungroup -gate_clock -scan\n"),
                  "native synthesis preserves compile_ultra optimization option semantics");
    QVariantMap unsafeAdditionalProject = timingProject;
    QVariantMap unsafeAdditionalMetadata = unsafeAdditionalProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap unsafeAdditionalExecution = unsafeAdditionalMetadata.value(QStringLiteral("dft_execution")).toMap();
    QVariantMap unsafeAdditionalSynthesis = unsafeAdditionalExecution.value(QStringLiteral("synthesis_settings")).toMap();
    unsafeAdditionalSynthesis.insert(QStringLiteral("additional_tcl_commands"),
                                     QVariantList{QStringLiteral("set_max_area 1; exec touch /tmp/unsafe")});
    unsafeAdditionalExecution.insert(QStringLiteral("synthesis_settings"), unsafeAdditionalSynthesis);
    unsafeAdditionalMetadata.insert(QStringLiteral("dft_execution"), unsafeAdditionalExecution);
    unsafeAdditionalProject.insert(QStringLiteral("metadata"), unsafeAdditionalMetadata);
    const QVariantMap unsafeAdditionalReadiness = ConfiguredDftFlowService::readiness(
        unsafeAdditionalProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList unsafeAdditionalErrors = unsafeAdditionalReadiness
        .value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(!unsafeAdditionalReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(unsafeAdditionalErrors.cbegin(), unsafeAdditionalErrors.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("single Tcl command")); }),
                  "native readiness rejects Tcl command chaining before execution");
    QVariantMap unsafeTimingProject = timingProject;
    QVariantMap unsafeTimingMetadata = unsafeTimingProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap unsafeTimingExecution = unsafeTimingMetadata.value(QStringLiteral("dft_execution")).toMap();
    QVariantMap unsafeSynthesis = unsafeTimingExecution.value(QStringLiteral("synthesis_settings")).toMap();
    QVariantList unsafeExceptions = unsafeSynthesis.value(QStringLiteral("timing_exceptions")).toList();
    QVariantMap unsafeException = unsafeExceptions.first().toMap();
    unsafeException.insert(QStringLiteral("from"), QStringLiteral("[get_ports *]; quit"));
    unsafeExceptions[0] = unsafeException;
    unsafeSynthesis.insert(QStringLiteral("timing_exceptions"), unsafeExceptions);
    unsafeTimingExecution.insert(QStringLiteral("synthesis_settings"), unsafeSynthesis);
    unsafeTimingMetadata.insert(QStringLiteral("dft_execution"), unsafeTimingExecution);
    unsafeTimingProject.insert(QStringLiteral("metadata"), unsafeTimingMetadata);
    const QVariantMap unsafeTimingReadiness = ConfiguredDftFlowService::readiness(
        unsafeTimingProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap unsafeTimingStage = ConfiguredDftFlowService::stage(
        unsafeTimingProject, temp.filePath(QStringLiteral("unsafe-timing")), runArguments, agentRoot);
    ok &= require(unsafeTimingReadiness.value(QStringLiteral("ready")).toBool()
                      && !unsafeTimingStage.value(QStringLiteral("ok")).toBool()
                      && unsafeTimingStage.value(QStringLiteral("message")).toString().contains(QStringLiteral("supported Tcl collection")),
                  "native timing generator rejects injected Tcl in timing selectors");

    QVariantMap scanConfigProject = project;
    QVariantMap scanConfigMetadata = scanConfigProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap scanConfigExecution = scanConfigMetadata.value(QStringLiteral("dft_execution")).toMap();
    scanConfigExecution.insert(QStringLiteral("reset_kind"), QStringLiteral("synchronous"));
    scanConfigExecution.insert(QStringLiteral("drc_autofix"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("clock_reset_set")}});
    scanConfigExecution.insert(QStringLiteral("additional_scan_clocks"), QVariantList{QStringLiteral("clk_aux")});
    scanConfigExecution.insert(QStringLiteral("additional_resets"), QVariantList{QVariantMap{
        {QStringLiteral("port"), QStringLiteral("rst_aux_n")}, {QStringLiteral("active_state"), 1}}});
    scanConfigMetadata.insert(QStringLiteral("dft_execution"), scanConfigExecution);
    scanConfigProject.insert(QStringLiteral("metadata"), scanConfigMetadata);
    const QVariantMap scanConfigReady = ConfiguredDftFlowService::readiness(
        scanConfigProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap resolvedScanConfiguration = scanConfigReady.value(QStringLiteral("scan_configuration")).toMap();
    const QVariantList resolvedResets = resolvedScanConfiguration.value(QStringLiteral("resets")).toList();
    const QVariantMap scanConfigStage = ConfiguredDftFlowService::stage(
        scanConfigProject, temp.filePath(QStringLiteral("scan-config-isolated")), runArguments, agentRoot);
    QFile scanConfigDriver(QDir(scanConfigStage.value(QStringLiteral("result")).toMap()
                                    .value(QStringLiteral("flow_directory")).toString())
                               .filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool scanConfigDriverReadable = scanConfigDriver.open(QIODevice::ReadOnly);
    const QByteArray scanConfigScript = scanConfigDriverReadable ? scanConfigDriver.readAll() : QByteArray{};
    ok &= require(scanConfigReady.value(QStringLiteral("ready")).toBool()
                      && resolvedScanConfiguration.value(QStringLiteral("clocks")).toStringList()
                          == QStringList{QStringLiteral("clk"), QStringLiteral("clk_aux")}
                      && resolvedResets.size() == 2
                      && resolvedResets.at(1).toMap().value(QStringLiteral("port")).toString()
                          == QStringLiteral("rst_aux_n")
                      && scanConfigStage.value(QStringLiteral("ok")).toBool() && scanConfigDriverReadable
                      && scanConfigScript.contains("create_clock -name DFT_AUX_CLK_1 -period 10 [get_ports {clk_aux}]")
                      && scanConfigScript.contains("set_false_path -from [get_ports {rst_aux_n}]")
                      && scanConfigScript.contains("set_dft_signal -view existing_dft -type ScanClock -timing {45 55} -port {clk_aux}"),
                  "native flow stages extra scan clock and reset setup");
    ok &= require(scanConfigScript.contains(
                      "set_dft_signal -view existing_dft -type Reset -active_state 1 -port {rst_aux_n}"),
                  "native flow declares asynchronous additional reset");
    ok &= require(!scanConfigScript.contains(
                      "set_dft_signal -view existing_dft -type Reset -active_state 0 -port {rst_n}"),
                  "native flow does not classify synchronous primary reset as asynchronous DFT Reset");
    ok &= require(scanConfigScript.contains("set_dft_signal -view spec -type TestData -port {rst_n}"),
                  "native flow keeps synchronous primary reset available as TestData");

    const QString replacementAdapter = temp.filePath(QStringLiteral("reviewed/top_adapter.sv"));
    const QByteArray originalTop = "module top(input clk, input rst_n); endmodule\n";
    const QByteArray adaptedTop = "module top(input clk, input rst_n); wire reviewed = clk; endmodule\n";
    ok &= require(writeFile(replacementAdapter, adaptedTop), "create reviewed RTL replacement");
    const QString replacementHash = QString::fromLatin1(QCryptographicHash::hash(
        adaptedTop, QCryptographicHash::Sha256).toHex());
    QVariantMap replacementProject = project;
    QVariantMap replacementMetadata = replacementProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap replacementExecution = replacementMetadata.value(QStringLiteral("dft_execution")).toMap();
    replacementExecution.insert(QStringLiteral("source_replacements"), QVariantList{QVariantMap{
        {QStringLiteral("target"), QStringLiteral("top.sv")},
        {QStringLiteral("file"), replacementAdapter},
        {QStringLiteral("sha256"), replacementHash},
        {QStringLiteral("reason"), QStringLiteral("Reviewed compatibility adapter")}}});
    replacementMetadata.insert(QStringLiteral("dft_execution"), replacementExecution);
    replacementProject.insert(QStringLiteral("metadata"), replacementMetadata);
    const QVariantMap replacementReadiness = ConfiguredDftFlowService::readiness(
        replacementProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap replacementStage = ConfiguredDftFlowService::stage(
        replacementProject, temp.filePath(QStringLiteral("replacement-isolated")), runArguments, agentRoot);
    const QVariantMap replacementStageResult = replacementStage.value(QStringLiteral("result")).toMap();
    const QVariantMap replacementSource = replacementStageResult.value(QStringLiteral("source")).toMap();
    const QVariantList replacementEvidence = replacementSource.value(QStringLiteral("source_replacements")).toList();
    const QString replacementFlow = replacementStageResult.value(QStringLiteral("flow_directory")).toString();
    QFile stagedReplacement(QDir(replacementFlow).filePath(QStringLiteral("rtl/top.sv")));
    const bool stagedReplacementReadable = stagedReplacement.open(QIODevice::ReadOnly);
    const QByteArray stagedReplacementBytes = stagedReplacementReadable ? stagedReplacement.readAll() : QByteArray{};
    QFile originalProjectTop(QDir(root).filePath(QStringLiteral("rtl/top.sv")));
    const bool originalTopReadable = originalProjectTop.open(QIODevice::ReadOnly);
    ok &= require(replacementReadiness.value(QStringLiteral("ready")).toBool()
                      && replacementStage.value(QStringLiteral("ok")).toBool()
                      && stagedReplacementBytes == adaptedTop && originalTopReadable
                      && originalProjectTop.readAll() == originalTop && replacementEvidence.size() == 1
                      && replacementEvidence.first().toMap().value(QStringLiteral("target")).toString()
                          == QStringLiteral("top.sv")
                      && replacementEvidence.first().toMap().value(QStringLiteral("original")).toMap()
                             .value(QStringLiteral("sha256")).toString()
                          == QString::fromLatin1(QCryptographicHash::hash(originalTop, QCryptographicHash::Sha256).toHex())
                      && replacementEvidence.first().toMap().value(QStringLiteral("replacement")).toMap()
                             .value(QStringLiteral("sha256")).toString() == replacementHash,
                  "native staging applies a hash-pinned RTL replacement only in isolation and records provenance");
    QVariantMap invalidReplacementProject = replacementProject;
    QVariantMap invalidReplacementMetadata = invalidReplacementProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap invalidReplacementExecution = invalidReplacementMetadata.value(QStringLiteral("dft_execution")).toMap();
    QVariantList invalidReplacementList = invalidReplacementExecution.value(QStringLiteral("source_replacements")).toList();
    QVariantMap invalidReplacement = invalidReplacementList.first().toMap();
    invalidReplacement.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));
    invalidReplacementList[0] = invalidReplacement;
    invalidReplacementExecution.insert(QStringLiteral("source_replacements"), invalidReplacementList);
    invalidReplacementMetadata.insert(QStringLiteral("dft_execution"), invalidReplacementExecution);
    invalidReplacementProject.insert(QStringLiteral("metadata"), invalidReplacementMetadata);
    const QVariantMap invalidReplacementReadiness = ConfiguredDftFlowService::readiness(
        invalidReplacementProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList invalidReplacementErrors = invalidReplacementReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(!invalidReplacementReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(invalidReplacementErrors.cbegin(), invalidReplacementErrors.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("sha256")); }),
                  "native readiness rejects source replacement digest mismatches");

    const QList<QPair<QString, QByteArray>> autofixCases{
        {QStringLiteral("clock_only"),
         QByteArray("set_dft_configuration -fix_clock enable -fix_reset disable -fix_set disable\n")
         + "set_dft_signal -view spec -type TestMode -active_state 1 -port {scan_mode}\n"
         + "set_dft_signal -view spec -type TestData -port {clk}\n"
         + "set_dft_signal -view spec -type TestData -port {rst_n}\n"
         + "set_autofix_configuration -type clock -method mux -control_signal scan_mode -test_data clk -fix_data enable"},
        {QStringLiteral("clock_reset_set"),
         QByteArray("set_dft_configuration -fix_clock enable -fix_reset enable -fix_set enable\n")
         + "set_dft_signal -view existing_dft -type TestMode -active_state 1 -port {scan_mode}\n"
         + "set_dft_signal -view spec -type TestMode -active_state 1 -port {scan_mode}\n"
         + "set_dft_signal -view spec -type TestData -port {clk}\n"
         + "set_dft_signal -view spec -type TestData -port {rst_n}\n"
         + "set_autofix_configuration -type clock -method mux -control_signal scan_mode -test_data clk -fix_data enable\n"
         + "set_autofix_configuration -type reset -method mux -control_signal scan_mode -test_data rst_n -fix_data enable -fix_latch enable\n"
         + "set_autofix_configuration -type set -method mux -control_signal scan_mode -test_data rst_n -fix_data enable -fix_latch enable"},
        {QStringLiteral("reset_set"),
         QByteArray("set_dft_configuration -fix_clock disable -fix_reset enable -fix_set enable\n")
         + "set_dft_signal -view existing_dft -type TestMode -active_state 1 -port {scan_mode}\n"
         + "set_dft_signal -view spec -type TestData -port {rst_n}\n"
         + "set_autofix_configuration -type reset -method mux -test_data rst_n -fix_data enable -fix_latch enable\n"
         + "set_autofix_configuration -type set -method mux -test_data rst_n -fix_data enable -fix_latch enable"},
    };
    for (const auto &test : autofixCases) {
        QVariantMap autofixProject = project;
        QVariantMap autofixMetadata = autofixProject.value(QStringLiteral("metadata")).toMap();
        QVariantMap autofixExecution = autofixMetadata.value(QStringLiteral("dft_execution")).toMap();
        autofixExecution.insert(QStringLiteral("drc_autofix"), QVariantMap{
            {QStringLiteral("mode"), test.first}, {QStringLiteral("test_mode_port"), QStringLiteral("scan_mode")}});
        autofixMetadata.insert(QStringLiteral("dft_execution"), autofixExecution);
        autofixProject.insert(QStringLiteral("metadata"), autofixMetadata);
        const QVariantMap autofixReadiness = ConfiguredDftFlowService::readiness(
            autofixProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
        const QVariantMap autofixStage = ConfiguredDftFlowService::stage(
            autofixProject, temp.filePath(QStringLiteral("autofix-") + test.first), runArguments, agentRoot);
        const QString autofixFlow = autofixStage.value(QStringLiteral("result")).toMap()
                                        .value(QStringLiteral("flow_directory")).toString();
        QFile autofixDriver(QDir(autofixFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
        const bool autofixDriverReadable = autofixDriver.open(QIODevice::ReadOnly);
        const QByteArray autofixScript = autofixDriverReadable ? autofixDriver.readAll() : QByteArray{};
        ok &= require(autofixReadiness.value(QStringLiteral("ready")).toBool()
                          && autofixStage.value(QStringLiteral("ok")).toBool() && autofixDriverReadable
                          && autofixScript.contains(test.second),
                      "native AutoFix mode emits the legacy TestMode and AutoFix Tcl sequence");
    }
    QVariantMap synchronousAutofixProject = project;
    QVariantMap synchronousAutofixMetadata = synchronousAutofixProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap synchronousAutofixExecution = synchronousAutofixMetadata.value(QStringLiteral("dft_execution")).toMap();
    synchronousAutofixExecution.insert(QStringLiteral("reset_kind"), QStringLiteral("synchronous"));
    synchronousAutofixExecution.insert(QStringLiteral("drc_autofix"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("clock_reset_set")},
        {QStringLiteral("test_mode_port"), QStringLiteral("scan_mode")}});
    synchronousAutofixMetadata.insert(QStringLiteral("dft_execution"), synchronousAutofixExecution);
    synchronousAutofixProject.insert(QStringLiteral("metadata"), synchronousAutofixMetadata);
    const QVariantMap synchronousAutofixReady = ConfiguredDftFlowService::readiness(
        synchronousAutofixProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap effectiveAutofix = synchronousAutofixReady.value(QStringLiteral("scan_configuration")).toMap()
        .value(QStringLiteral("drc_autofix")).toMap();
    const QVariantMap synchronousAutofixStage = ConfiguredDftFlowService::stage(
        synchronousAutofixProject, temp.filePath(QStringLiteral("autofix-synchronous")), runArguments, agentRoot);
    const QString synchronousAutofixFlow = synchronousAutofixStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    QFile synchronousAutofixDriver(QDir(synchronousAutofixFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool synchronousAutofixDriverReadable = synchronousAutofixDriver.open(QIODevice::ReadOnly);
    const QByteArray synchronousAutofixScript = synchronousAutofixDriverReadable
        ? synchronousAutofixDriver.readAll() : QByteArray{};
    synchronousAutofixExecution.insert(QStringLiteral("drc_autofix"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("reset_set")}});
    synchronousAutofixMetadata.insert(QStringLiteral("dft_execution"), synchronousAutofixExecution);
    synchronousAutofixProject.insert(QStringLiteral("metadata"), synchronousAutofixMetadata);
    const QVariantMap syncResetOnlyReadiness = ConfiguredDftFlowService::readiness(
        synchronousAutofixProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList syncResetOnlyErrors = syncResetOnlyReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(synchronousAutofixReady.value(QStringLiteral("ready")).toBool()
                      && effectiveAutofix.value(QStringLiteral("requested_mode")).toString()
                          == QStringLiteral("clock_reset_set")
                      && effectiveAutofix.value(QStringLiteral("effective_mode")).toString()
                          == QStringLiteral("clock_only")
                      && !effectiveAutofix.value(QStringLiteral("adjustment")).toString().isEmpty()
                      && synchronousAutofixStage.value(QStringLiteral("ok")).toBool()
                      && synchronousAutofixDriverReadable
                      && synchronousAutofixScript.contains("set_autofix_configuration -type clock")
                      && !synchronousAutofixScript.contains("set_autofix_configuration -type reset")
                      && !synchronousAutofixScript.contains("set_autofix_configuration -type set")
                      && !syncResetOnlyReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(syncResetOnlyErrors.cbegin(), syncResetOnlyErrors.cend(),
                          [](const QString &error) { return error.contains(QStringLiteral("asynchronous reset")); }),
                  "native AutoFix adapts to synchronous reset semantics instead of issuing invalid Reset AutoFix Tcl");
    QVariantMap invalidAutofixProject = project;
    QVariantMap invalidAutofixMetadata = invalidAutofixProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap invalidAutofixExecution = invalidAutofixMetadata.value(QStringLiteral("dft_execution")).toMap();
    invalidAutofixExecution.insert(QStringLiteral("drc_autofix"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("clock_only")},
        {QStringLiteral("test_mode_port"), QStringLiteral("bad;quit")}});
    invalidAutofixMetadata.insert(QStringLiteral("dft_execution"), invalidAutofixExecution);
    invalidAutofixProject.insert(QStringLiteral("metadata"), invalidAutofixMetadata);
    const QVariantMap invalidAutofixReadiness = ConfiguredDftFlowService::readiness(
        invalidAutofixProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QStringList invalidAutofixErrors = invalidAutofixReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    ok &= require(!invalidAutofixReadiness.value(QStringLiteral("ready")).toBool()
                      && std::any_of(invalidAutofixErrors.cbegin(), invalidAutofixErrors.cend(),
                                     [](const QString &error) { return error.contains(QStringLiteral("test_mode_port")); }),
                  "native AutoFix rejects an injected TestMode port before Tcl generation");

    QVariantMap duplicateScanConfig = scanConfigProject;
    QVariantMap duplicateMetadata = duplicateScanConfig.value(QStringLiteral("metadata")).toMap();
    QVariantMap duplicateExecution = duplicateMetadata.value(QStringLiteral("dft_execution")).toMap();
    duplicateExecution.insert(QStringLiteral("additional_scan_clocks"),
                              QVariantList{QStringLiteral("clk_aux"), QStringLiteral("clk_aux")});
    duplicateMetadata.insert(QStringLiteral("dft_execution"), duplicateExecution);
    duplicateScanConfig.insert(QStringLiteral("metadata"), duplicateMetadata);
    const QVariantMap duplicateReadiness = ConfiguredDftFlowService::readiness(
        duplicateScanConfig, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(!duplicateReadiness.value(QStringLiteral("ready")).toBool(),
                  "native readiness rejects duplicate additional scan clocks");

    const QString preScript = QDir(root).filePath(QStringLiteral("scripts/pre.tcl"));
    const QString postScript = QDir(root).filePath(QStringLiteral("scripts/post.tcl"));
    const QString extraConstraint = QDir(root).filePath(QStringLiteral("constraints/extra.sdc"));
    ok &= require(writeFile(preScript, "set PRE_SCRIPT_RAN 1\n")
                      && writeFile(postScript, "set POST_SCRIPT_RAN 1\n")
                      && writeFile(extraConstraint, "set EXTRA_CONSTRAINT_RAN 1\n"),
                  "create configured pre/post synthesis scripts and extra SDC");
    QVariantMap scriptsProject = project;
    QVariantMap scriptsMetadata = scriptsProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap scriptsExecution = scriptsMetadata.value(QStringLiteral("dft_execution")).toMap();
    scriptsExecution.insert(QStringLiteral("synthesis_settings"), QVariantMap{
        {QStringLiteral("pre_scripts"), QVariantList{QStringLiteral("scripts/pre.tcl")}},
        {QStringLiteral("constraint_files"), QVariantList{QStringLiteral("constraints/extra.sdc")}},
        {QStringLiteral("post_scripts"), QVariantList{QStringLiteral("scripts/post.tcl")}},
    });
    scriptsMetadata.insert(QStringLiteral("dft_execution"), scriptsExecution);
    scriptsProject.insert(QStringLiteral("metadata"), scriptsMetadata);
    const QVariantMap scriptsReadiness = ConfiguredDftFlowService::readiness(
        scriptsProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QString scriptsWorkspace = temp.filePath(QStringLiteral("scripts-isolated"));
    const QVariantMap scriptsStage = ConfiguredDftFlowService::stage(
        scriptsProject, scriptsWorkspace, runArguments, agentRoot);
    const QVariantMap scriptsStageResult = scriptsStage.value(QStringLiteral("result")).toMap();
    const QString scriptsFlow = scriptsStageResult.value(QStringLiteral("flow_directory")).toString();
    QFile scriptsDriver(QDir(scriptsFlow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool scriptsDriverReadable = scriptsDriver.open(QIODevice::ReadOnly);
    const QByteArray scriptsText = scriptsDriverReadable ? scriptsDriver.readAll() : QByteArray{};
    const QByteArray preSource = "source {scripts/project/scripts/pre.tcl}";
    const QByteArray constraintSource = "source {scripts/project/constraints/extra.sdc}";
    const QByteArray postSource = "source {scripts/project/scripts/post.tcl}";
    const qsizetype prePosition = scriptsText.indexOf(preSource);
    const qsizetype constraintPosition = scriptsText.indexOf(constraintSource);
    const qsizetype compilePosition = scriptsText.indexOf("compile -boundary_optimization -scan\n");
    const qsizetype postPosition = scriptsText.indexOf(postSource);
    const QVariantList scriptRecords = scriptsStageResult.value(QStringLiteral("source")).toMap()
        .value(QStringLiteral("synthesis_scripts")).toList();
    ok &= require(scriptsReadiness.value(QStringLiteral("ready")).toBool()
                      && scriptsStage.value(QStringLiteral("ok")).toBool()
                      && scriptsDriverReadable && prePosition >= 0 && constraintPosition > prePosition
                      && compilePosition > constraintPosition && postPosition > compilePosition
                      && QFileInfo(QDir(scriptsFlow).filePath(QStringLiteral("scripts/project/scripts/pre.tcl"))).isFile()
                      && QFileInfo(QDir(scriptsFlow).filePath(QStringLiteral("scripts/project/constraints/extra.sdc"))).isFile()
                      && QFileInfo(QDir(scriptsFlow).filePath(QStringLiteral("scripts/project/scripts/post.tcl"))).isFile()
                      && scriptRecords.size() == 3,
                  "native staging isolates and records project Tcl/SDC, preserving pre/constraint/post ordering");

    const QString sourcePath = QDir(root).filePath(QStringLiteral("rtl/top.sv"));
    QFile sourceBefore(sourcePath);
    ok &= require(sourceBefore.open(QIODevice::ReadOnly), "read source before staging");
    const QByteArray original = sourceBefore.readAll();
    const QVariantMap stagedEnvelope = ConfiguredDftFlowService::stage(project, workspace, runArguments, agentRoot);
    ok &= require(stagedEnvelope.value(QStringLiteral("ok")).toBool(), "stage configured project into isolated workspace");
    const QVariantMap staged = stagedEnvelope.value(QStringLiteral("result")).toMap();
    const QString flow = staged.value(QStringLiteral("flow_directory")).toString();
    QFile stagedDriver(QDir(flow).filePath(QStringLiteral("agent_synthesis_dft.tcl")));
    const bool stagedDriverReadable = stagedDriver.open(QIODevice::ReadOnly);
    const QByteArray stagedDriverText = stagedDriverReadable ? stagedDriver.readAll() : QByteArray{};
    ok &= require(QFileInfo(QDir(flow).filePath(QStringLiteral("rtl/top.sv"))).isFile(),
                  "source RTL is copied into staged flow tree");
    ok &= require(stagedDriverReadable
                      && stagedDriverText.contains("reports/synthesis_qor.rpt")
                      && stagedDriverText.contains("reports/synthesis_area.rpt")
                      && stagedDriverText.contains("reports/synthesis_timing.rpt"),
                  "empty configured synthesis report list retains legacy default reports");
    QFile stagedList(QDir(flow).filePath(QStringLiteral("input/rtl.f")));
    ok &= require(stagedList.open(QIODevice::ReadOnly)
                      && stagedList.readAll().contains("./rtl/top.sv"),
                  "staged filelist references only staged RTL");
    QFile sourceAfter(sourcePath);
    ok &= require(sourceAfter.open(QIODevice::ReadOnly), "read source after staging");
    ok &= require(sourceAfter.readAll() == original, "project RTL remains unchanged after staging");

    const QString fixedWorkspaceRoot = temp.filePath(QStringLiteral("fixed-workspaces"));
    QVariantMap fixedProject = project;
    QVariantMap fixedMetadata = fixedProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap fixedExecution = fixedMetadata.value(QStringLiteral("dft_execution")).toMap();
    fixedExecution.insert(QStringLiteral("workspace_path"), fixedWorkspaceRoot);
    fixedExecution.insert(QStringLiteral("workspace_suffix_enabled"), false);
    fixedMetadata.insert(QStringLiteral("dft_execution"), fixedExecution);
    fixedProject.insert(QStringLiteral("metadata"), fixedMetadata);
    const QVariantMap fixedReadiness = ConfiguredDftFlowService::readiness(
        fixedProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QString fixedWorkspace = QDir(fixedWorkspaceRoot).filePath(QStringLiteral("native_demo"));
    ok &= require(fixedReadiness.value(QStringLiteral("ready")).toBool()
                      && !fixedReadiness.value(QStringLiteral("workspace_suffix_enabled")).toBool()
                      && fixedReadiness.value(QStringLiteral("effective_workspace_path")).toString() == fixedWorkspace,
                  "native readiness accepts and reports a project-owned fixed workspace");
    QVariantMap missingFixedPathProject = fixedProject;
    QVariantMap missingFixedPathMetadata = missingFixedPathProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap missingFixedPathExecution = missingFixedPathMetadata.value(QStringLiteral("dft_execution")).toMap();
    missingFixedPathExecution.remove(QStringLiteral("workspace_path"));
    missingFixedPathMetadata.insert(QStringLiteral("dft_execution"), missingFixedPathExecution);
    missingFixedPathProject.insert(QStringLiteral("metadata"), missingFixedPathMetadata);
    const QVariantMap missingFixedPathReadiness = ConfiguredDftFlowService::readiness(
        missingFixedPathProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(!missingFixedPathReadiness.value(QStringLiteral("ready")).toBool()
                      && missingFixedPathReadiness.value(QStringLiteral("configuration_errors")).toStringList()
                          .join(QLatin1Char('\n')).contains(QStringLiteral("必须显式设置")),
                  "native readiness requires an explicit workspace path when suffixing is disabled");
    const QVariantMap fixedFirst = ConfiguredDftFlowService::stage(
        fixedProject, {}, runArguments, agentRoot);
    const QString fixedFlow = fixedFirst.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    const QString retainedNote = QDir(fixedWorkspace).filePath(QStringLiteral("operator_notes.tcl"));
    const QString staleEvidence = QDir(fixedWorkspace).filePath(QStringLiteral("agent_summary.json"));
    ok &= require(fixedFirst.value(QStringLiteral("ok")).toBool()
                      && fixedFirst.value(QStringLiteral("result")).toMap().value(QStringLiteral("workspace")).toString()
                          == fixedWorkspace
                      && writeFile(retainedNote, "set operator_note keep\n")
                      && writeFile(staleEvidence, "stale evidence\n")
                      && writeFile(QDir(fixedFlow).filePath(QStringLiteral("reports/old.rpt")), "stale report\n"),
                  "native fixed workspace stages under the project-owned path");
    const QVariantMap fixedSecond = ConfiguredDftFlowService::stage(fixedProject, {}, runArguments, agentRoot);
    QFile retainedNoteFile(retainedNote);
    QFile fixedMarker(QDir(fixedWorkspace).filePath(QStringLiteral(".dft_agent_workspace.json")));
    const QJsonDocument fixedMarkerDocument = fixedMarker.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(fixedMarker.readAll()) : QJsonDocument{};
    ok &= require(fixedSecond.value(QStringLiteral("ok")).toBool()
                      && fixedSecond.value(QStringLiteral("result")).toMap().value(QStringLiteral("workspace")).toString()
                          == fixedWorkspace
                      && retainedNoteFile.open(QIODevice::ReadOnly)
                      && retainedNoteFile.readAll() == QByteArray("set operator_note keep\n")
                      && !QFileInfo::exists(staleEvidence)
                      && !QFileInfo::exists(QDir(fixedFlow).filePath(QStringLiteral("reports/old.rpt")))
                      && fixedMarkerDocument.object().value(QStringLiteral("project_id")).toString()
                          == QStringLiteral("native_demo"),
                  "native fixed workspace reuses ownership, preserves operator notes, and removes stale generated evidence");

    const QString unownedRoot = temp.filePath(QStringLiteral("unowned-workspaces"));
    const QString unownedWorkspace = QDir(unownedRoot).filePath(QStringLiteral("native_demo"));
    ok &= require(writeFile(QDir(unownedWorkspace).filePath(QStringLiteral("keep.txt")), "unowned\n"),
                  "create unrelated directory at fixed workspace path");
    QVariantMap unownedProject = fixedProject;
    QVariantMap unownedMetadata = unownedProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap unownedExecution = unownedMetadata.value(QStringLiteral("dft_execution")).toMap();
    unownedExecution.insert(QStringLiteral("workspace_path"), unownedRoot);
    unownedMetadata.insert(QStringLiteral("dft_execution"), unownedExecution);
    unownedProject.insert(QStringLiteral("metadata"), unownedMetadata);
    const QVariantMap unownedReadiness = ConfiguredDftFlowService::readiness(
        unownedProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap unownedStage = ConfiguredDftFlowService::stage(unownedProject, {}, runArguments, agentRoot);
    ok &= require(!unownedReadiness.value(QStringLiteral("ready")).toBool()
                      && !unownedStage.value(QStringLiteral("ok")).toBool()
                      && QFileInfo::exists(QDir(unownedWorkspace).filePath(QStringLiteral("keep.txt"))),
                  "native fixed workspace refuses an unowned directory without modifying it");

    const QString adoptedRoot = temp.filePath(QStringLiteral("adopted-workspaces"));
    const QString oldRun = QDir(QDir(adoptedRoot).filePath(QStringLiteral("native_demo")))
        .filePath(QStringLiteral("20260901T120000Z_native_demo_deadbeef"));
    ok &= require(writeFile(QDir(oldRun).filePath(QStringLiteral("stage.json")),
                            "{\"source\":{\"project\":\"native_demo\"}}\n"),
                  "create legacy owned run directory for fixed workspace migration");
    QVariantMap adoptedProject = fixedProject;
    QVariantMap adoptedMetadata = adoptedProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap adoptedExecution = adoptedMetadata.value(QStringLiteral("dft_execution")).toMap();
    adoptedExecution.insert(QStringLiteral("workspace_path"), adoptedRoot);
    adoptedMetadata.insert(QStringLiteral("dft_execution"), adoptedExecution);
    adoptedProject.insert(QStringLiteral("metadata"), adoptedMetadata);
    const QVariantMap adoptedStage = ConfiguredDftFlowService::stage(adoptedProject, {}, runArguments, agentRoot);
    ok &= require(adoptedStage.value(QStringLiteral("ok")).toBool()
                      && QFileInfo::exists(QDir(oldRun).filePath(QStringLiteral("stage.json"))),
                  "native fixed workspace adopts a container containing only owned legacy run directories");

    const QVariantMap run = ConfiguredDftFlowService::run(project, workspace, runArguments, agentRoot);
    ok &= require(run.value(QStringLiteral("ok")).toBool(), "configured native flow starts and collects process result");
    const QVariantMap payload = run.value(QStringLiteral("result")).toMap();
    ok &= require(!payload.contains(QStringLiteral("python_fallback_required"))
                      && payload.value(QStringLiteral("status")).toString() == QStringLiteral("verified"),
                  "fresh synthesis/scan evidence passes native acceptance without a legacy Python fallback signal");
    ok &= require(payload.value(QStringLiteral("acceptance")).toList().size() >= 14,
                  "native run records synthesis, DRC, scan-report, and output-artifact acceptance checks");
    ok &= require(QFileInfo(payload.value(QStringLiteral("evidence_file")).toString()).isFile(),
                  "run evidence is atomically persisted");
    const QVariantMap execution = payload.value(QStringLiteral("execution")).toMap();
    ok &= require(execution.value(QStringLiteral("returncode")).toInt() == 0,
                  "NativeProcessOutputService captures the actual dc_shell process exit");
    ok &= require(execution.value(QStringLiteral("stdout")).toString().contains(QStringLiteral("fake dc_shell completed")),
                  "NativeProcessOutputService captures live process output");
    const QVariantMap transientDcArguments{{QStringLiteral("dc_shell"), fakeDcTransientDcsH1},
                                           {QStringLiteral("testmax"), fakeTestmax}};
    const QString transientDcWorkspace = temp.filePath(QStringLiteral("transient_dcsh1_workspace"));
    const QVariantMap transientDcRun = ConfiguredDftFlowService::run(
        project, transientDcWorkspace, transientDcArguments, agentRoot);
    const QVariantMap transientDcPayload = transientDcRun.value(QStringLiteral("result")).toMap();
    const QVariantMap transientDcExecution = transientDcPayload.value(QStringLiteral("execution")).toMap();
    const QString firstAttemptLog = transientDcExecution.value(QStringLiteral("first_attempt_log")).toString();
    QFile firstAttemptLogFile(firstAttemptLog);
    const bool firstAttemptLogHasDcsH1 = firstAttemptLogFile.open(QIODevice::ReadOnly)
        && firstAttemptLogFile.readAll().contains("DCSH-1");
    QFile retryMarkerFile(dcRetryMarker);
    const bool retryMarkerShowsExactlyTwoCalls = retryMarkerFile.open(QIODevice::ReadOnly)
        && retryMarkerFile.readAll() == QByteArray("firstretry");
    ok &= require(transientDcRun.value(QStringLiteral("ok")).toBool()
                      && transientDcPayload.value(QStringLiteral("status")).toString() == QStringLiteral("verified")
                      && transientDcExecution.value(QStringLiteral("attempt_count")).toInt() == 2
                      && transientDcExecution.value(QStringLiteral("retry_reason")).toString() == QStringLiteral("DCSH-1")
                      && firstAttemptLogHasDcsH1 && retryMarkerShowsExactlyTwoCalls,
                  "DCSH-1 is retried exactly once with the same arguments and the first failure log is preserved");

    const QVariantMap persistentDcArguments{{QStringLiteral("dc_shell"), fakeDcPersistentDcsH1},
                                            {QStringLiteral("testmax"), fakeTestmax}};
    const QVariantMap persistentDcRun = ConfiguredDftFlowService::run(
        project, temp.filePath(QStringLiteral("persistent_dcsh1_workspace")), persistentDcArguments, agentRoot);
    const QVariantMap persistentDcExecution = persistentDcRun.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("execution")).toMap();
    const QStringList persistentDcErrors = persistentDcExecution.value(QStringLiteral("errors")).toStringList();
    ok &= require(persistentDcExecution.value(QStringLiteral("attempt_count")).toInt() == 2
                      && persistentDcExecution.value(QStringLiteral("retry_reason")).toString() == QStringLiteral("DCSH-1")
                      && persistentDcExecution.value(QStringLiteral("failure_category")).toString()
                          == QStringLiteral("dc_shell_startup_failure")
                      && persistentDcExecution.value(QStringLiteral("next_action")).toString().contains(
                          QStringLiteral("no HDL diagnosis"))
                      && persistentDcExecution.value(QStringLiteral("stdout")).toString().contains(QStringLiteral("DCSH-1"))
                      && !persistentDcErrors.join(QLatin1Char('\n')).contains(QStringLiteral("HDL analysis failed")),
                  "persistent DCSH-1 is reported as an EDA startup failure, not a missing HDL diagnostic");

    const QString mbistSource = QDir(root).filePath(QStringLiteral("mbist/mbist_tb.sv"));
    const QString mbistHeader = QDir(root).filePath(QStringLiteral("mbist/mbist_markers.svh"));
    const QString mbistConstraint = QDir(root).filePath(QStringLiteral("mbist/timing.sdc"));
    ok &= require(writeFile(mbistHeader, "`define MBIST_RESULT \"MBIST_PASS\"\n")
                      && writeFile(mbistConstraint, "create_clock -period 10 [get_ports clk]\n")
                      && writeFile(mbistSource,
                          "`include \"mbist_markers.svh\"\n"
                          "module mbist_tb; initial begin $display(`MBIST_RESULT); $finish; end endmodule\n"),
                  "create real Icarus MBIST simulation and compile-parent include fixtures");
    QVariantMap mbistProject = project;
    mbistProject.insert(QStringLiteral("session_id"), QStringLiteral("root-session-mbist"));
    QVariantMap mbistMetadata = mbistProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap mbistModules = mbistMetadata.value(QStringLiteral("flow_modules")).toMap();
    mbistModules.insert(QStringLiteral("scan"), false);
    mbistModules.insert(QStringLiteral("mbist"), true);
    mbistMetadata.insert(QStringLiteral("flow_modules"), mbistModules);
    QVariantMap mbistExecution = mbistMetadata.value(QStringLiteral("dft_execution")).toMap();
    mbistExecution.insert(QStringLiteral("constraint_file"), mbistConstraint);
    mbistExecution.insert(QStringLiteral("use_constraint_file"), true);
    mbistExecution.insert(QStringLiteral("mbist_simulation"), QVariantMap{
        {QStringLiteral("compile_files"), QVariantList{QStringLiteral("mbist/mbist_tb.sv")}},
        {QStringLiteral("include_dirs"), QVariantList{}},
        {QStringLiteral("expected_output"), QVariantList{QStringLiteral("MBIST_PASS")}},
        {QStringLiteral("forbidden_output"), QVariantList{QStringLiteral("MBIST_FAIL")}},
        {QStringLiteral("test_top"), QStringLiteral("mbist_tb")},
        {QStringLiteral("timeout_seconds"), 30}});
    mbistMetadata.insert(QStringLiteral("dft_execution"), mbistExecution);
    mbistProject.insert(QStringLiteral("metadata"), mbistMetadata);
    const QVariantMap mbistReadinessEnvelope = ConfiguredDftFlowService::readiness(
        mbistProject, runArguments, agentRoot);
    const QVariantMap mbistReadiness = mbistReadinessEnvelope.value(QStringLiteral("result")).toMap();
    QVariantMap mbistArguments = runArguments;
    mbistArguments.insert(QStringLiteral("mbist_include_mode"), QStringLiteral("compile_parents"));
    mbistArguments.insert(QStringLiteral("mbist_diagnostic_mode"), QStringLiteral("verbose"));
    mbistArguments.insert(QStringLiteral("mbist_timeout_multiplier"), 2);
    const QString mbistWorkspaceRoot = temp.filePath(QStringLiteral("mbist-workspaces"));
    const QVariantMap mbistStage = ConfiguredDftFlowService::stage(
        mbistProject, mbistWorkspaceRoot, mbistArguments, agentRoot);
    const QVariantMap mbistStageResult = mbistStage.value(QStringLiteral("result")).toMap();
    const QString mbistFlow = mbistStageResult.value(QStringLiteral("flow_directory")).toString();
    const QVariantMap stagedMbist = mbistStageResult.value(QStringLiteral("source")).toMap()
        .value(QStringLiteral("mbist_simulation")).toMap();
    const QVariantMap mbistRun = ConfiguredDftFlowService::run(
        mbistProject, mbistWorkspaceRoot, mbistArguments, agentRoot);
    const QVariantMap mbistPayload = mbistRun.value(QStringLiteral("result")).toMap();
    const QVariantMap mbistResult = mbistPayload.value(QStringLiteral("mbist")).toMap();
    const QVariantMap mbistSummary = mbistResult.value(QStringLiteral("summary")).toMap();
    const QStringList mbistCompileCommand = mbistResult.value(QStringLiteral("compile_command")).toStringList();
    const bool mbistStagingPassed = mbistReadinessEnvelope.value(QStringLiteral("ok")).toBool()
                      && mbistReadiness.value(QStringLiteral("ready")).toBool()
                      && mbistStage.value(QStringLiteral("ok")).toBool()
                      && QFileInfo::exists(QDir(mbistFlow).filePath(QStringLiteral("mbist_sim/mbist/mbist_markers.svh")))
                      && stagedMbist.value(QStringLiteral("include_mode")).toString() == QStringLiteral("compile_parents");
    if (!mbistStagingPassed) {
        diagnoseStage("MBIST readiness", mbistReadinessEnvelope);
        diagnoseStage("MBIST staging", mbistStage);
    }
    ok &= require(mbistStagingPassed,
                  "native MBIST readiness and staging validate project inputs and preserve declared compile-parent headers");
    const bool mbistRunPassed = mbistRun.value(QStringLiteral("ok")).toBool()
                      && mbistPayload.value(QStringLiteral("status")).toString() == QStringLiteral("verified")
                      && mbistResult.value(QStringLiteral("completed_cleanly")).toBool()
                      && mbistSummary.value(QStringLiteral("declared_case_coverage_percent")).toDouble() == 100.0
                      && mbistSummary.value(QStringLiteral("observed_pass_markers")).toStringList()
                          == QStringList{QStringLiteral("MBIST_PASS")}
                      && mbistCompileCommand.contains(QStringLiteral("-Wall"))
                      && mbistResult.value(QStringLiteral("run_command")).toStringList().contains(QStringLiteral("-v"))
                      && QFileInfo::exists(mbistResult.value(QStringLiteral("summary_file")).toString());
    if (!mbistRunPassed)
        diagnoseStage("MBIST run", mbistRun);
    ok &= require(mbistRunPassed,
                  "native MBIST compiles and runs with Icarus, enforces pass markers, records diagnostics, and verifies evidence");
    QVariantMap noEvidenceArguments = runArguments;
    noEvidenceArguments.insert(QStringLiteral("dc_shell"), fakeDcNoReports);
    const QVariantMap noEvidenceRun = ConfiguredDftFlowService::run(
        project, temp.filePath(QStringLiteral("no_evidence_workspace")), noEvidenceArguments, agentRoot);
    const QVariantMap noEvidencePayload = noEvidenceRun.value(QStringLiteral("result")).toMap();
    ok &= require(noEvidenceRun.value(QStringLiteral("ok")).toBool()
                      && noEvidencePayload.value(QStringLiteral("status")).toString() == QStringLiteral("execution_incomplete")
                      && noEvidencePayload.value(QStringLiteral("execution")).toMap()
                             .value(QStringLiteral("attempt_count")).toInt() == 1
                      && !noEvidencePayload.contains(QStringLiteral("python_fallback_required")),
                  "successful tool exit without fresh acceptance reports or outputs remains incomplete and is not retried");
    QVariantMap noEvidenceAtpgProject = project;
    QVariantMap noEvidenceAtpgMetadata = noEvidenceAtpgProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap noEvidenceAtpgSettings = noEvidenceAtpgMetadata.value(QStringLiteral("dft_execution")).toMap();
    noEvidenceAtpgSettings.insert(QStringLiteral("atpg_cell_model_files"), QVariantList{cellModel});
    noEvidenceAtpgMetadata.insert(QStringLiteral("dft_execution"), noEvidenceAtpgSettings);
    QVariantMap noEvidenceAtpgModules = noEvidenceAtpgMetadata.value(QStringLiteral("flow_modules")).toMap();
    noEvidenceAtpgModules.insert(QStringLiteral("atpg"), true);
    noEvidenceAtpgMetadata.insert(QStringLiteral("flow_modules"), noEvidenceAtpgModules);
    noEvidenceAtpgProject.insert(QStringLiteral("metadata"), noEvidenceAtpgMetadata);
    QVariantMap noEvidenceAtpgArguments = noEvidenceArguments;
    noEvidenceAtpgArguments.insert(QStringLiteral("testmax"), fakeTestmaxMarker);
    const QVariantMap noEvidenceAtpgRun = ConfiguredDftFlowService::run(noEvidenceAtpgProject,
        temp.filePath(QStringLiteral("no_evidence_atpg_workspace")), noEvidenceAtpgArguments, agentRoot);
    const QVariantMap noEvidenceAtpgPayload = noEvidenceAtpgRun.value(QStringLiteral("result")).toMap();
    const QVariantMap noEvidenceAtpgExecution = noEvidenceAtpgPayload.value(QStringLiteral("execution")).toMap();
    const QVariantMap noEvidenceAtpgResult = noEvidenceAtpgPayload.value(QStringLiteral("atpg")).toMap();
    ok &= require(noEvidenceAtpgRun.value(QStringLiteral("ok")).toBool()
                      && noEvidenceAtpgResult.value(QStringLiteral("status")).toString() == QStringLiteral("blocked")
                      && !noEvidenceAtpgExecution.value(QStringLiteral("completed_cleanly")).toBool()
                      && !QFileInfo::exists(atpgLaunchMarker)
                      && noEvidenceAtpgExecution.value(QStringLiteral("errors")).toStringList()
                          .join(QLatin1Char(' ')).contains(QStringLiteral("ATPG was not launched")),
                  "TestMAX is not launched when Design Compiler exits zero without valid synthesis/read-link evidence");

    QVariantMap sessionProject = project;
    sessionProject.insert(QStringLiteral("session_id"), QStringLiteral("root-session-a"));
    const QVariantMap startedEnvelope = EdaJobService::dispatch(QStringLiteral("run_dft_flow"), sessionProject,
                                                                 runArguments, agentRoot);
    const QVariantMap started = startedEnvelope.value(QStringLiteral("result")).toMap();
    const QString jobId = started.value(QStringLiteral("job_id")).toString();
    bool shortJobIdNumeric = false;
    const qulonglong shortJobNumber = jobId.mid(QStringLiteral("eda-").size()).toULongLong(&shortJobIdNumeric);
    ok &= require(startedEnvelope.value(QStringLiteral("ok")).toBool()
                      && (started.value(QStringLiteral("state")).toString() == QStringLiteral("running")
                          || started.value(QStringLiteral("state")).toString() == QStringLiteral("completed"))
                      && shortJobIdNumeric && shortJobNumber == 1,
                  "native DFT run starts as a session-owned background job with a concise handle");
    const QVariantMap statusEnvelope = EdaJobService::dispatch(QStringLiteral("status_dft_job"), sessionProject,
        QVariantMap{{QStringLiteral("job_id"), jobId}}, agentRoot);
    ok &= require(statusEnvelope.value(QStringLiteral("ok")).toBool()
                      && (statusEnvelope.value(QStringLiteral("result")).toMap()
                              .value(QStringLiteral("state")).toString() == QStringLiteral("running")
                          || statusEnvelope.value(QStringLiteral("result")).toMap()
                              .value(QStringLiteral("state")).toString() == QStringLiteral("completed")),
                  "status reports a valid state without waiting for the job");
    const QVariantMap wrongSession = EdaJobService::dispatch(QStringLiteral("status_dft_job"),
        QVariantMap{{QStringLiteral("session_id"), QStringLiteral("root-session-b")}},
        QVariantMap{{QStringLiteral("job_id"), jobId}}, agentRoot);
    ok &= require(!wrongSession.value(QStringLiteral("ok")).toBool(),
                  "jobs are isolated between root sessions");
    const QVariantMap waitedEnvelope = EdaJobService::dispatch(QStringLiteral("wait_dft_job"), sessionProject,
        QVariantMap{{QStringLiteral("job_id"), jobId}, {QStringLiteral("wait_seconds"), 20}}, agentRoot);
    const QVariantMap waited = waitedEnvelope.value(QStringLiteral("result")).toMap();
    ok &= require(waitedEnvelope.value(QStringLiteral("ok")).toBool()
                      && waited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && waited.value(QStringLiteral("result")).toMap().value(QStringLiteral("ok")).toBool(),
                  "wait returns the underlying native flow result");

    const QVariantMap iterationStartedEnvelope = EdaJobService::dispatch(
        QStringLiteral("run_dft_iteration"), sessionProject, runArguments, agentRoot);
    const QVariantMap iterationStarted = iterationStartedEnvelope.value(QStringLiteral("result")).toMap();
    const QString iterationJobId = iterationStarted.value(QStringLiteral("job_id")).toString();
    const QVariantMap iterationWaitedEnvelope = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        sessionProject, QVariantMap{{QStringLiteral("job_id"), iterationJobId},
                                    {QStringLiteral("wait_seconds"), 20}}, agentRoot);
    const QVariantMap iterationWaited = iterationWaitedEnvelope.value(QStringLiteral("result")).toMap();
    ok &= require(iterationStartedEnvelope.value(QStringLiteral("ok")).toBool()
                      && iterationStarted.value(QStringLiteral("operation")).toString()
                          == QStringLiteral("run_dft_iteration")
                      && !iterationJobId.isEmpty()
                      && iterationWaitedEnvelope.value(QStringLiteral("ok")).toBool()
                      && iterationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && iterationWaited.value(QStringLiteral("result")).toMap()
                          .value(QStringLiteral("ok")).toBool(),
                  "native DFT iterations use the same session-scoped background job and wait lifecycle as baselines");

    QVariantMap optimizationArguments = runArguments;
    optimizationArguments.insert(QStringLiteral("maximum_rounds"), 2);
    const QVariantMap optimizationStartedEnvelope = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), sessionProject, optimizationArguments, agentRoot);
    const QVariantMap optimizationStarted = optimizationStartedEnvelope.value(QStringLiteral("result")).toMap();
    const QString optimizationJobId = optimizationStarted.value(QStringLiteral("job_id")).toString();
    const QVariantMap optimizationWaitedEnvelope = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        sessionProject, QVariantMap{{QStringLiteral("job_id"), optimizationJobId},
                                    {QStringLiteral("wait_seconds"), 20}}, agentRoot);
    const QVariantMap optimizationWaited = optimizationWaitedEnvelope.value(QStringLiteral("result")).toMap();
    const QVariantMap optimizedPayload = optimizationWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap optimization = optimizedPayload.value(QStringLiteral("optimization")).toMap();
    ok &= require(optimizationStartedEnvelope.value(QStringLiteral("ok")).toBool()
                      && optimizationStarted.value(QStringLiteral("operation")).toString()
                          == QStringLiteral("run_dft_optimization")
                      && !optimizationJobId.isEmpty()
                      && optimizationWaitedEnvelope.value(QStringLiteral("ok")).toBool()
                      && optimizationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && optimization.value(QStringLiteral("attempted_rounds")).toInt() == 1
                      && QFileInfo::exists(optimization.value(QStringLiteral("report_file")).toString()),
                  "native bounded DFT optimization records measured rounds and a durable report in a background job");

    QVariantMap synchronousResetProject = sessionProject;
    QVariantMap synchronousMetadata = synchronousResetProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap synchronousExecution = synchronousMetadata.value(QStringLiteral("dft_execution")).toMap();
    synchronousExecution.insert(QStringLiteral("reset_kind"), QStringLiteral("synchronous"));
    synchronousMetadata.insert(QStringLiteral("dft_execution"), synchronousExecution);
    synchronousResetProject.insert(QStringLiteral("metadata"), synchronousMetadata);
    QVariantMap synchronousDrcArguments{{QStringLiteral("dc_shell"), fakeDcRetryAfterDrc},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2}};
    const QVariantMap synchronousDrcStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), synchronousResetProject, synchronousDrcArguments, agentRoot);
    const QString synchronousDrcJobId = synchronousDrcStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap synchronousDrcWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        synchronousResetProject, QVariantMap{{QStringLiteral("job_id"), synchronousDrcJobId},
            {QStringLiteral("wait_seconds"), 30}}, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap synchronousDrcPayload = synchronousDrcWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap synchronousDrcOptimization = synchronousDrcPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList synchronousDrcProfiles = synchronousDrcOptimization.value(QStringLiteral("profiles")).toList();
    const QVariantMap synchronousRetry = synchronousDrcProfiles.size() > 1
        ? synchronousDrcProfiles.at(1).toMap() : QVariantMap{};
    QFile synchronousRetryScript(QDir(synchronousRetry.value(QStringLiteral("workspace")).toString())
        .filePath(QStringLiteral("flow/agent_synthesis_dft.tcl")));
    QString synchronousRetryScriptText;
    if (synchronousRetryScript.open(QIODevice::ReadOnly | QIODevice::Text))
        synchronousRetryScriptText = QString::fromUtf8(synchronousRetryScript.readAll());
    ok &= require(synchronousDrcStarted.value(QStringLiteral("ok")).toBool()
                      && synchronousDrcWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && synchronousDrcProfiles.size() == 2
                      && synchronousDrcProfiles.at(0).toMap().value(QStringLiteral("post_dft_drc")).toInt() == 1
                      && synchronousRetry.value(QStringLiteral("drc_repair_mode")).toString()
                          == QStringLiteral("clock_only")
                      && synchronousRetryScriptText.contains(
                          QStringLiteral("set_dft_configuration -fix_clock enable -fix_reset disable -fix_set disable")),
                  "native DRC optimization uses clock-only autofix for a configured synchronous primary reset");

    QVariantMap timeoutProject = sessionProject;
    QVariantMap timeoutMetadata = timeoutProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap timeoutExecution = timeoutMetadata.value(QStringLiteral("dft_execution")).toMap();
    timeoutExecution.insert(QStringLiteral("timeout_seconds"), 1);
    timeoutMetadata.insert(QStringLiteral("dft_execution"), timeoutExecution);
    timeoutProject.insert(QStringLiteral("metadata"), timeoutMetadata);
    QVariantMap timeoutOptimizationArguments{{QStringLiteral("dc_shell"), fakeDcRetryAfterTimeout},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2}};
    const QVariantMap timeoutOptimizationStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), timeoutProject, timeoutOptimizationArguments, agentRoot);
    const QString timeoutOptimizationJobId = timeoutOptimizationStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap timeoutOptimizationWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        timeoutProject, QVariantMap{{QStringLiteral("job_id"), timeoutOptimizationJobId},
                                    {QStringLiteral("wait_seconds"), 20}}, agentRoot)
        .value(QStringLiteral("result")).toMap();
    const QVariantMap timeoutOptimizationPayload = timeoutOptimizationWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap timeoutOptimization = timeoutOptimizationPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList timeoutProfiles = timeoutOptimization.value(QStringLiteral("profiles")).toList();
    ok &= require(timeoutOptimizationStarted.value(QStringLiteral("ok")).toBool()
                      && timeoutOptimizationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && timeoutProfiles.size() == 2
                      && timeoutProfiles.at(0).toMap().value(QStringLiteral("timed_out")).toBool()
                      && timeoutProfiles.at(0).toMap().value(QStringLiteral("flow_timeout_multiplier")).toInt() == 1
                      && timeoutProfiles.at(1).toMap().value(QStringLiteral("flow_timeout_multiplier")).toInt() == 2
                      && timeoutProfiles.at(1).toMap().value(QStringLiteral("executed")).toBool(),
                  "native optimization retries a timed-out flow once with a larger configured timeout and records both rounds");

    QVariantMap preprocessProject = sessionProject;
    QVariantMap preprocessMetadata = preprocessProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap preprocessExecution = preprocessMetadata.value(QStringLiteral("dft_execution")).toMap();
    preprocessExecution.insert(QStringLiteral("source_preprocessor"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("off")},
        {QStringLiteral("source_files"), QVariantList{QStringLiteral("top.sv")}},
        {QStringLiteral("definitions"), QVariantList{QStringLiteral("SYNTHESIS")}}
    });
    preprocessMetadata.insert(QStringLiteral("dft_execution"), preprocessExecution);
    preprocessProject.insert(QStringLiteral("metadata"), preprocessMetadata);
    QVariantMap preprocessOptimizationArguments{{QStringLiteral("dc_shell"), fakeDcRetryAfterPreprocessError},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2}};
    const QVariantMap preprocessOptimizationStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), preprocessProject, preprocessOptimizationArguments, agentRoot);
    const QString preprocessOptimizationJobId = preprocessOptimizationStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap preprocessOptimizationWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        preprocessProject, QVariantMap{{QStringLiteral("job_id"), preprocessOptimizationJobId},
            {QStringLiteral("wait_seconds"), 30}}, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap preprocessOptimizationPayload = preprocessOptimizationWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap preprocessOptimization = preprocessOptimizationPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList preprocessProfiles = preprocessOptimization.value(QStringLiteral("profiles")).toList();
    const QVariantMap preprocessedRound = preprocessProfiles.size() > 1 ? preprocessProfiles.at(1).toMap() : QVariantMap{};
    QFile preprocessStageFile(QDir(preprocessedRound.value(QStringLiteral("workspace")).toString())
                                  .filePath(QStringLiteral("stage.json")));
    QVariantMap preprocessStage;
    if (preprocessStageFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(preprocessStageFile.readAll());
        preprocessStage = document.object().toVariantMap();
    }
    ok &= require(preprocessOptimizationStarted.value(QStringLiteral("ok")).toBool()
                      && preprocessOptimizationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && preprocessProfiles.size() == 2
                      && preprocessProfiles.at(0).toMap().value(QStringLiteral("source_preprocess_mode")).toString()
                          == QStringLiteral("off")
                      && preprocessedRound.value(QStringLiteral("profile")).toString()
                          == QStringLiteral("source_cpp_preprocessed")
                      && preprocessedRound.value(QStringLiteral("source_preprocess_mode")).toString()
                          == QStringLiteral("cpp")
                      && preprocessStage.value(QStringLiteral("source")).toMap()
                          .value(QStringLiteral("source_preprocessor")).toMap()
                          .value(QStringLiteral("mode")).toString() == QStringLiteral("cpp")
                      && QFileInfo::exists(preprocessOptimization.value(QStringLiteral("report_file")).toString()),
                  "native optimization retries a clear C-preprocessor compile error using declared source macros and records staged evidence");

    QVariantMap internalErrorOptimizationArguments{{QStringLiteral("dc_shell"), fakeDcRetryInternalError},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2}};
    const QVariantMap internalErrorOptimizationStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), sessionProject, internalErrorOptimizationArguments, agentRoot);
    const QString internalErrorOptimizationJobId = internalErrorOptimizationStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap internalErrorOptimizationWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        sessionProject, QVariantMap{{QStringLiteral("job_id"), internalErrorOptimizationJobId},
                                    {QStringLiteral("wait_seconds"), 20}}, agentRoot)
        .value(QStringLiteral("result")).toMap();
    const QVariantMap internalErrorOptimizationPayload = internalErrorOptimizationWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap internalErrorOptimization = internalErrorOptimizationPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList internalErrorProfiles = internalErrorOptimization.value(QStringLiteral("profiles")).toList();
    ok &= require(internalErrorOptimizationStarted.value(QStringLiteral("ok")).toBool()
                      && internalErrorOptimizationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && internalErrorProfiles.size() == 2
                      && QFileInfo::exists(dcInternalErrorMarker)
                      && internalErrorProfiles.at(0).toMap().value(QStringLiteral("compile_strategy")).toString()
                          == QStringLiteral("single_pass")
                      && internalErrorProfiles.at(1).toMap().value(QStringLiteral("compile_strategy")).toString()
                          == QStringLiteral("two_stage_mapping")
                      && internalErrorProfiles.at(1).toMap().value(QStringLiteral("executed")).toBool()
                      && QFileInfo::exists(internalErrorOptimization.value(QStringLiteral("report_file")).toString()),
                  "native optimization retries an internal DC fatal only after fresh read/link evidence, using two-stage mapping");

    QVariantMap mbistOptimizationArguments{{QStringLiteral("dc_shell"), fakeDc},
        {QStringLiteral("testmax"), fakeTestmax}, {QStringLiteral("maximum_rounds"), 2},
        {QStringLiteral("mbist_include_mode"), QStringLiteral("declared")}};
    const QVariantMap mbistOptimizationStarted = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), mbistProject, mbistOptimizationArguments, agentRoot);
    const QString mbistOptimizationJobId = mbistOptimizationStarted.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap mbistOptimizationWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        mbistProject, QVariantMap{{QStringLiteral("job_id"), mbistOptimizationJobId},
                                  {QStringLiteral("wait_seconds"), 30}}, agentRoot)
        .value(QStringLiteral("result")).toMap();
    const QVariantMap mbistOptimizationPayload = mbistOptimizationWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap mbistOptimization = mbistOptimizationPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList mbistOptimizationProfiles = mbistOptimization.value(QStringLiteral("profiles")).toList();
    ok &= require(mbistOptimizationStarted.value(QStringLiteral("ok")).toBool()
                      && mbistOptimizationWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && mbistOptimizationProfiles.size() == 2
                      && mbistOptimizationProfiles.at(0).toMap().value(QStringLiteral("mbist_compiled")).toBool() == false
                      && mbistOptimizationProfiles.at(0).toMap().value(QStringLiteral("mbist_include_mode")).toString()
                          == QStringLiteral("declared")
                      && mbistOptimizationProfiles.at(1).toMap().value(QStringLiteral("profile")).toString()
                          == QStringLiteral("mbist_compile_parent_includes")
                      && mbistOptimizationProfiles.at(1).toMap().value(QStringLiteral("mbist_include_mode")).toString()
                          == QStringLiteral("compile_parents")
                      && mbistOptimizationProfiles.at(1).toMap().value(QStringLiteral("mbist_compiled")).toBool(),
                  "native optimization retries a failed MBIST compile with compile-parent include paths and records both rounds");

    QVariantMap slowArguments = runArguments;
    slowArguments.insert(QStringLiteral("dc_shell"), slowDc);
    const QVariantMap slowStartedEnvelope = EdaJobService::dispatch(QStringLiteral("run_dft_flow"), sessionProject,
                                                                    slowArguments, agentRoot);
    const QVariantMap slowStarted = slowStartedEnvelope.value(QStringLiteral("result")).toMap();
    const QString slowJobId = slowStarted.value(QStringLiteral("job_id")).toString();
    const QVariantMap duplicateSlowStart = EdaJobService::dispatch(QStringLiteral("run_dft_flow"), sessionProject,
                                                                    slowArguments, agentRoot);
    const QVariantMap duplicateSlow = duplicateSlowStart.value(QStringLiteral("result")).toMap();
    ok &= require(duplicateSlowStart.value(QStringLiteral("ok")).toBool()
                      && duplicateSlow.value(QStringLiteral("job_id")).toString() == slowJobId
                      && duplicateSlow.value(QStringLiteral("reused_active_job")).toBool(),
                  "duplicate active DFT flow calls reuse one job instead of launching a second EDA process");
    const QVariantMap interruptedEnvelope = EdaJobService::dispatch(QStringLiteral("interrupt_dft_job"), sessionProject,
        QVariantMap{{QStringLiteral("job_id"), slowJobId}, {QStringLiteral("wait_seconds"), 10}}, agentRoot);
    ok &= require(interruptedEnvelope.value(QStringLiteral("ok")).toBool()
                      && interruptedEnvelope.value(QStringLiteral("result")).toMap()
                             .value(QStringLiteral("state")).toString() == QStringLiteral("interrupted"),
                  "interrupt cancels the active EDA process and resolves the session job");

    QVariantMap atpgProject = project;
    QVariantMap metadata = atpgProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap executionConfig = metadata.value(QStringLiteral("dft_execution")).toMap();
    executionConfig.insert(QStringLiteral("atpg_cell_model_files"), QVariantList{QStringLiteral("cells.v")});
    executionConfig.insert(QStringLiteral("atpg_minimum_coverage"), 99.0);
    metadata.insert(QStringLiteral("dft_execution"), executionConfig);
    QVariantMap modules = metadata.value(QStringLiteral("flow_modules")).toMap();
    modules.insert(QStringLiteral("atpg"), true);
    metadata.insert(QStringLiteral("flow_modules"), modules);
    atpgProject.insert(QStringLiteral("metadata"), metadata);
    const QVariantMap atpgReady = ConfiguredDftFlowService::readiness(atpgProject, runArguments, agentRoot)
                                      .value(QStringLiteral("result")).toMap();
    ok &= require(atpgReady.value(QStringLiteral("ready")).toBool(),
                  "configured TestMAX and gate-level model pass native readiness");
    const QVariantMap unavailableTestmax = ConfiguredDftFlowService::readiness(
        atpgProject, QVariantMap{{QStringLiteral("dc_shell"), fakeDc},
                                 {QStringLiteral("testmax"), temp.filePath(QStringLiteral("bin/missing_testmax"))}}, agentRoot)
                                              .value(QStringLiteral("result")).toMap();
    ok &= require(!unavailableTestmax.value(QStringLiteral("ready")).toBool()
                      && !unavailableTestmax.value(QStringLiteral("configuration_errors")).toList().isEmpty(),
                  "ATPG readiness blocks when the TestMAX executable is unavailable");
    const QString atpgWorkspace = temp.filePath(QStringLiteral("atpg_workspace"));
    QVariantMap optimizedAtpgArguments = runArguments;
    optimizedAtpgArguments.insert(QStringLiteral("atpg_abort_limit"), 50);
    optimizedAtpgArguments.insert(QStringLiteral("atpg_timeout_multiplier"), 2);
    const QVariantMap atpgRun = ConfiguredDftFlowService::run(
        atpgProject, atpgWorkspace, optimizedAtpgArguments, agentRoot);
    ok &= require(atpgRun.value(QStringLiteral("ok")).toBool(), "configured TestMAX ATPG run completes");
    const QVariantMap atpgPayload = atpgRun.value(QStringLiteral("result")).toMap();
    ok &= require(atpgPayload.value(QStringLiteral("status")).toString() == QStringLiteral("verified"),
                  "fresh ATPG and post-DFT DRC reports pass two-round evidence review");
    const QVariantMap atpg = atpgPayload.value(QStringLiteral("atpg")).toMap();
    ok &= require(atpg.value(QStringLiteral("status")).toString() == QStringLiteral("verified")
                      && atpg.value(QStringLiteral("summary")).toMap().value(QStringLiteral("coverage_percent")).toDouble() == 99.5,
                  "ATPG status and coverage are based on the generated raw report");
    const QString atpgDriver = QDir(atpgPayload.value(QStringLiteral("execution")).toMap()
                                        .value(QStringLiteral("flow_directory")).toString())
                                   .filePath(QStringLiteral("agent_atpg.tcl"));
    QFile generatedAtpgDriver(atpgDriver);
    ok &= require(generatedAtpgDriver.open(QIODevice::ReadOnly), "generated TestMAX Tcl is available");
    const QByteArray atpgTcl = generatedAtpgDriver.readAll();
    ok &= require(atpgTcl.contains(QByteArray("read_netlist {") + QFile::encodeName(cellModel) + "}")
                      && atpgTcl.contains("run_drc mapped_scan/native_demo_scan.spf")
                      && atpgTcl.contains("run_atpg -auto")
                      && atpgTcl.contains("set_atpg -abort_limit 50")
                      && !atpgTcl.contains("report_faults -class AU -summary"),
                  "default ATPG omits optional fault-class reports while iteration abort-limit overrides reach generated Tcl");
    QVariantMap faultClassArguments = optimizedAtpgArguments;
    faultClassArguments.insert(QStringLiteral("atpg_diagnostic_mode"), QStringLiteral("full"));
    const QVariantMap faultClassRun = ConfiguredDftFlowService::run(
        atpgProject, temp.filePath(QStringLiteral("atpg_fault_class_workspace")), faultClassArguments, agentRoot);
    const QVariantMap faultClassPayload = faultClassRun.value(QStringLiteral("result")).toMap();
    const QVariantMap faultClassSummary = faultClassPayload.value(QStringLiteral("atpg")).toMap()
        .value(QStringLiteral("summary")).toMap();
    const QString faultClassFlow = faultClassPayload.value(QStringLiteral("execution")).toMap()
        .value(QStringLiteral("flow_directory")).toString();
    QFile faultClassDriver(QDir(faultClassFlow).filePath(QStringLiteral("agent_atpg.tcl")));
    ok &= require(faultClassRun.value(QStringLiteral("ok")).toBool()
                      && faultClassSummary.value(QStringLiteral("diagnostic_mode")).toString()
                          == QStringLiteral("fault_classes")
                      && faultClassSummary.value(QStringLiteral("fault_classes")).toMap()
                          .value(QStringLiteral("detected")).toInt() == 120
                      && faultClassDriver.open(QIODevice::ReadOnly)
                      && faultClassDriver.readAll().contains("report_faults -class AU -summary"),
                  "the common full-diagnostic alias maps to fault-class reports and persists their parsed summary");
    QVariantMap plateauAtpgProject = atpgProject;
    plateauAtpgProject.insert(QStringLiteral("session_id"), QStringLiteral("root-session-atpg-plateau"));
    QVariantMap plateauMetadata = plateauAtpgProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap plateauExecution = plateauMetadata.value(QStringLiteral("dft_execution")).toMap();
    plateauExecution.insert(QStringLiteral("atpg_minimum_coverage"), 99.9);
    plateauMetadata.insert(QStringLiteral("dft_execution"), plateauExecution);
    plateauAtpgProject.insert(QStringLiteral("metadata"), plateauMetadata);
    QVariantMap plateauArguments = runArguments;
    plateauArguments.insert(QStringLiteral("maximum_rounds"), 6);
    const QVariantMap plateauStartedEnvelope = EdaJobService::dispatch(
        QStringLiteral("run_dft_optimization"), plateauAtpgProject, plateauArguments, agentRoot);
    const QString plateauJobId = plateauStartedEnvelope.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    const QVariantMap plateauWaited = EdaJobService::dispatch(QStringLiteral("wait_dft_job"),
        plateauAtpgProject, QVariantMap{{QStringLiteral("job_id"), plateauJobId},
                                        {QStringLiteral("wait_seconds"), 90}}, agentRoot)
        .value(QStringLiteral("result")).toMap();
    const QVariantMap plateauPayload = plateauWaited.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    const QVariantMap plateauOptimization = plateauPayload.value(QStringLiteral("optimization")).toMap();
    const QVariantList plateauProfiles = plateauOptimization.value(QStringLiteral("profiles")).toList();
    const QVariantMap diagnosticRound = plateauProfiles.size() >= 5 ? plateauProfiles.at(4).toMap() : QVariantMap{};
    ok &= require(plateauStartedEnvelope.value(QStringLiteral("ok")).toBool()
                      && plateauWaited.value(QStringLiteral("state")).toString() == QStringLiteral("completed")
                      && plateauOptimization.value(QStringLiteral("attempted_rounds")).toInt() == 5
                      && diagnosticRound.value(QStringLiteral("atpg_abort_limit")).toInt() == 200
                      && diagnosticRound.value(QStringLiteral("atpg_diagnostic_mode")).toString()
                          == QStringLiteral("fault_classes")
                      && diagnosticRound.value(QStringLiteral("atpg_fault_classes")).toMap()
                          .value(QStringLiteral("detected")).toInt() == 120,
                  "ATPG plateau restores the previous search limit, collects fault classes once, and records the evidence");
    QVariantMap autofixIterationArguments = runArguments;
    autofixIterationArguments.insert(QStringLiteral("drc_repair_mode"), QStringLiteral("clock_reset_set"));
    const QVariantMap autofixStage = ConfiguredDftFlowService::stage(atpgProject,
        temp.filePath(QStringLiteral("autofix_iteration_workspace")), autofixIterationArguments, agentRoot);
    const QString autofixDriverPath = autofixStage.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("driver")).toString();
    QFile autofixDriver(autofixDriverPath);
    const bool autofixDriverHasIterationMode = autofixDriver.open(QIODevice::ReadOnly)
        && autofixDriver.readAll().contains("set_dft_configuration -fix_clock enable -fix_reset enable -fix_set enable");
    ok &= require(autofixStage.value(QStringLiteral("ok")).toBool() && autofixDriverHasIterationMode,
                  "iteration DRC autofix mode reaches the staged synthesis Tcl");
    ok &= require(QFileInfo(QDir(atpgPayload.value(QStringLiteral("execution")).toMap()
                                     .value(QStringLiteral("flow_directory")).toString())
                                .filePath(QStringLiteral("reports/atpg_summary.json"))).isFile(),
                  "parsed ATPG summary is generated in this run");

    QVariantMap missingConfigProject = atpgProject;
    QVariantMap missingMetadata = missingConfigProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap missingExecution = missingMetadata.value(QStringLiteral("dft_execution")).toMap();
    missingExecution.insert(QStringLiteral("atpg_cell_model_files"), QVariantList{});
    missingMetadata.insert(QStringLiteral("dft_execution"), missingExecution);
    missingConfigProject.insert(QStringLiteral("metadata"), missingMetadata);
    const QVariantMap blockedReadiness = ConfiguredDftFlowService::readiness(
        missingConfigProject, runArguments, agentRoot).value(QStringLiteral("result")).toMap();
    ok &= require(!blockedReadiness.value(QStringLiteral("ready")).toBool()
                      && !blockedReadiness.value(QStringLiteral("configuration_errors")).toList().isEmpty(),
                  "ATPG with no cell model is blocked before tool launch");

    const QVariantMap noReportArguments{{QStringLiteral("dc_shell"), fakeDc},
                                        {QStringLiteral("testmax"), fakeTestmaxNoLog}};
    const QVariantMap noReportRun = ConfiguredDftFlowService::run(
        atpgProject, temp.filePath(QStringLiteral("no_report_workspace")), noReportArguments, agentRoot);
    const QVariantMap noReportPayload = noReportRun.value(QStringLiteral("result")).toMap();
    ok &= require(noReportRun.value(QStringLiteral("ok")).toBool()
                      && noReportPayload.value(QStringLiteral("status")).toString() != QStringLiteral("verified")
                      && noReportPayload.value(QStringLiteral("atpg")).toMap()
                             .value(QStringLiteral("report_fresh")).toBool() == false,
                  "successful TestMAX exit without fresh report evidence is never ATPG PASS");

    QVariantMap tessentProject = atpgProject;
    QVariantMap tessentMetadata = tessentProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap tessentExecution = tessentMetadata.value(QStringLiteral("dft_execution")).toMap();
    tessentExecution.insert(QStringLiteral("dft_tool"), QStringLiteral("tessent"));
    tessentExecution.insert(QStringLiteral("atpg_cell_model_files"), QVariantList{});
    tessentExecution.insert(QStringLiteral("tessent_cell_library_files"), QVariantList{QStringLiteral("cells.atpg")});
    tessentExecution.insert(QStringLiteral("atpg_minimum_coverage"), 97.0);
    tessentMetadata.insert(QStringLiteral("dft_execution"), tessentExecution);
    tessentProject.insert(QStringLiteral("metadata"), tessentMetadata);
    const QVariantMap tessentArguments{{QStringLiteral("dc_shell"), fakeDc},
        {QStringLiteral("testmax"), temp.filePath(QStringLiteral("bin/missing_testmax"))},
        {QStringLiteral("tessent"), fakeTessent}};
    const QVariantMap tessentReadiness = ConfiguredDftFlowService::readiness(
        tessentProject, tessentArguments, agentRoot).value(QStringLiteral("result")).toMap();
    const QVariantMap tessentRun = ConfiguredDftFlowService::run(
        tessentProject, temp.filePath(QStringLiteral("tessent_workspace")), tessentArguments, agentRoot);
    const QVariantMap tessentPayload = tessentRun.value(QStringLiteral("result")).toMap();
    const QVariantMap tessentAtpg = tessentPayload.value(QStringLiteral("atpg")).toMap();
    const QVariantMap tessentSummary = tessentAtpg.value(QStringLiteral("summary")).toMap();
    const QString tessentFlow = tessentPayload.value(QStringLiteral("execution")).toMap()
                                    .value(QStringLiteral("flow_directory")).toString();
    QFile generatedTessentDriver(QDir(tessentFlow).filePath(QStringLiteral("agent_atpg.tcl")));
    const bool generatedTessentDriverReadable = generatedTessentDriver.open(QIODevice::ReadOnly);
    const QByteArray generatedTessentTcl = generatedTessentDriverReadable ? generatedTessentDriver.readAll() : QByteArray{};
    QFile generatedTessentProcedure(QDir(tessentFlow).filePath(QStringLiteral("agent_tessent_scan.testproc")));
    const bool generatedTessentProcedureReadable = generatedTessentProcedure.open(QIODevice::ReadOnly);
    const QByteArray generatedTessentProcedureText = generatedTessentProcedureReadable
        ? generatedTessentProcedure.readAll() : QByteArray{};
    const bool tessentPassed = tessentReadiness.value(QStringLiteral("ready")).toBool()
                      && tessentRun.value(QStringLiteral("ok")).toBool()
                      && tessentPayload.value(QStringLiteral("status")).toString() == QStringLiteral("verified")
                      && tessentAtpg.value(QStringLiteral("status")).toString() == QStringLiteral("verified")
                      && tessentSummary.value(QStringLiteral("tool")).toString() == QStringLiteral("tessent")
                      && tessentSummary.value(QStringLiteral("coverage_percent")).toDouble() == 98.0
                      && tessentSummary.value(QStringLiteral("test_coverage_percent")).toDouble() == 94.0
                      && generatedTessentDriverReadable
                      && generatedTessentTcl.contains("read_cell_library {")
                      && generatedTessentTcl.contains("add_scan_chains {chain_chain_1} dft_agent_scan_group {test_si} {test_so}")
                      && generatedTessentTcl.contains("write_patterns mapped_scan/native_demo_scan_tessent_stuck_at.stil")
                      && generatedTessentProcedureReadable
                      && generatedTessentProcedureText.contains("scan_group dft_agent_scan_group");
    ok &= require(tessentPassed,
                  "native Tessent backend generates chain procedure, executes ATPG, and accepts fresh fault-coverage evidence");

    const QVariantMap unsafe = ConfiguredDftFlowService::stage(project, root, runArguments, agentRoot);
    ok &= require(!unsafe.value(QStringLiteral("ok")).toBool(),
                  "stage rejects workspace overlap with source project");
    if (!ok)
        return 1;
    std::puts("ConfiguredDftFlowService checks passed");
    return 0;
}
