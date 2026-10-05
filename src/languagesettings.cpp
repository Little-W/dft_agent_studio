#include "languagesettings.h"
#include "studiopaths.h"

#include <QDir>

#include <QHash>

namespace {

using Catalog = QHash<QString, QString>;

const Catalog &chineseCatalog() {
    static const Catalog catalog{
        {"workspace", "工作区"}, {"overview", "概览"}, {"projects", "项目"},
        {"flowMonitor", "流程监控"}, {"agentMonitor", "Agent 会话"},
        {"skillsTools", "技能与工具"}, {"modelSettings", "模型与推理"}, {"manualDebug", "人工调试"},
        {"settingsGroup", "设置"}, {"studioSettings", "全部设置"}, {"appearanceSettings", "外观与编辑器"},
        {"skillsSettings", "Skills"}, {"agentToolsSettings", "Agent 工具"}, {"modelsContextSettings", "模型与上下文"},
        {"settingsHomeDescription", "配置 Studio 外观、模型与可用工具。"},
        {"about", "关于"}, {"aboutDescription", "配置项目参数、执行 DFT 流程，并集中审查运行证据。"},
        {"applicationVersion", "版本"}, {"qtVersion", "Qt 版本"},
        {"applicationLicense", "应用程序开源协议"}, {"licenseNotDeclared", "MIT License"},
        {"licenseNotDeclaredDetail", "版权所有者 Little-W。软件按 MIT License 授权。"},
        {"applicationAuthor", "作者"}, {"applicationAuthorName", "Little-W"},
        {"thirdPartyNotices", "第三方声明"},
        {"appearanceSettingsDescription", "设置编辑器、终端文字和界面缩放。"},
        {"skillsSettingsDescription", "管理项目 Skills 在 Chat 中的共享选择，以及 Agent 能力开关。"},
        {"agentToolsSettingsDescription", "管理 DFT Agent 可使用的工具能力。"},
        {"modelsContextSettingsDescription", "管理模型、API 上下文上限、采样和压缩。"},
        {"evidence", "运行报告"}, {"models", "模型"}, {"fileEditor", "文件编辑器"}, {"workspaceGroup", "工作区"},
        {"projectsGroup", "项目"}, {"currentProjectSettings", "当前项目设置"}, {"automationGroup", "自动化"}, {"configureGroup", "配置"},
        {"operatorGroup", "操作员"}, {"showSidebar", "显示侧栏"}, {"hideSidebar", "隐藏侧栏"},
        {"search", "搜索项目与运行记录"}, {"toolsSettings", "工具与设置"},
        {"enableDarkMode", "切换到夜间模式"}, {"enableLightMode", "切换到日间模式"},
        {"local", "本地"}, {"agentRunning", "Agent 运行中"}, {"evidenceReady", "证据已就绪"},
        {"workspaceDescription", "本地 DFT 自动化控制台。"}, {"activeProject", "当前项目"},
        {"chooseProject", "选择项目"}, {"agentState", "Agent 状态"}, {"working", "正在执行"},
        {"standingBy", "待命"}, {"quickAction", "快捷操作"}, {"runSelected", "运行当前项目"},
        {"capabilitiesEnabled", "项能力已启用"}, {"manage", "管理"}, {"addProjectFolder", "添加项目目录"},
        {"addProject", "添加项目"}, {"browse", "浏览"}, {"removeProject", "移除项目"}, {"project", "项目"}, {"noProjectNote", "暂无项目说明"},
        {"projectFolder", "项目目录"}, {"rtlFolder", "RTL 目录"}, {"topModule", "顶层模块"},
        {"projectType", "项目类型"}, {"typeRtl", "RTL 项目"}, {"typeNetlist", "门级网表"},
        {"typeAtpg", "ATPG"}, {"typeSimulation", "功能仿真"}, {"typeVerification", "证据核查"}, {"typeCustom", "通用"},
        {"model", "模型"}, {"agentGoal", "Agent 目标"}, {"save", "保存"}, {"runAgent", "运行 Agent"},
        {"flowEvidence", "每个阶段都要求可追溯的执行证据。"}, {"stopAgent", "停止 Agent"},
        {"noProjectSelected", "未选择项目"}, {"workspacePending", "受控运行启动后才会创建工作区。"},
        {"stageRead", "读取项目与约束"}, {"stageSelect", "选择已批准技能"},
        {"stageRun", "执行受控 DFT 操作"}, {"stageCheck", "交叉复核报告"}, {"stageEvidence", "记录执行证据"},
        {"complete", "完成"}, {"active", "执行中"}, {"needsReview", "需要复核"}, {"waiting", "等待中"},
        {"workerStream", "按时间查看 Agent 回答、工具调用和工具结果。"}, {"idle", "空闲"},
        {"detailedMode", "详细模式"}, {"noDetailedEvents", "暂无详细事件。开启详细模式后运行任务，可查看 IC 工具实时输出。"},
        {"icToolOutput", "IC 工具与终端输出"}, {"waitingToolOutput", "等待 IC 工具输出或终端指令..."},
        {"noWorkerEvents", "暂无 Worker 事件。请从项目或流程监控启动任务。"},
        {"disabledCapabilityNote", "关闭的能力会随下一次 Agent 运行传入受控 worker。"},
        {"enabled", "已启用"}, {"all", "全部"}, {"operatorOnly", "仅执行操作员明确输入的命令。"},
        {"clear", "清空"}, {"selectProjectTerminal", "选择项目以确定终端目录"},
        {"terminalOutput", "终端输出将显示在这里。"}, {"enterCommand", "输入操作员命令"}, {"run", "运行"},
        {"projectTerminal", "项目终端"}, {"projectWorkingDirectory", "项目工作目录"},
        {"openProjectTerminal", "打开项目终端"}, {"terminalOpened", "项目终端已打开"},
        {"terminalUnavailable", "没有找到可用的终端程序"},
        {"workspacePath", "工作区根目录"}, {"workspaceSuffix", "工作区后缀"},
        {"createWorkspacePerRun", "每次新建后缀工作区（关闭后使用项目同名目录）"},
        {"evidenceRule", "按项目、会话和报告类型查阅运行记录与设计总结。"},
        {"noEvidence", "本会话尚未记录已验证的证据。"}, {"modelDescription", "选择并编辑本地模型；可指定给当前项目。"},
        {"errorsFound", "发现错误"}, {"reportReady", "报告已就绪"},
        {"supervisorWarning", "检查运行报告"}, {"supervisorWarningDetail", "本轮尚未得到完整验证结论。请依据最新报告确认根因：工具、库、路径、约束或脚本问题应修复对应配置；只有本轮报告与源码分析共同证明 RTL 存在设计缺陷时，才对项目 RTL 做最小必要修改并重新验证。"},
        {"modelPersisted", "勾选后，模型设置会随当前项目保存。"}, {"useSelectedModel", "用于当前项目"},
        {"chooseProjectFolder", "选择项目目录"}, {"simplifiedChinese", "简体中文"}, {"english", "English"},
        {"modelResponsive", "快速本地响应"}, {"modelCompact", "紧凑指令模型"}, {"modelLongContext", "64K 长上下文"},
        {"synthesisSettings", "综合设置"}, {"constraintFile", "约束文件"}, {"loadConstraintFile", "载入约束文件"},
        {"clockName", "时钟端口"}, {"clockPeriod", "时钟周期 (ns)"}, {"resetName", "复位端口"},
        {"synthesisOutput", "综合输出目录"}, {"dftOutput", "DFT 输出目录"}, {"compileEffort", "Compile effort"},
        {"rtlSources", "RTL 补充源文件（每行一个，相对 RTL 目录）"}, {"rtlFilelist", "RTL 文件清单（设置后优先读取）"},
        {"sourceLanguage", "RTL 语言"}, {"runTimeout", "运行超时（秒）"},
        {"inferenceSettings", "本地模型推理"}, {"inferenceMode", "推理模式"},
        {"gpuOnly", "GPU 全层"}, {"cpuGpu", "GPU + CPU 自动分层"}, {"cpuOnly", "仅 CPU（备选）"}, {"gpuMemory", "显存预算 (GiB)"},
        {"cpuMemory", "CPU 内存 (GiB)"}, {"inputLimit", "历史记录容量"}, {"outputLimit", "输出上限"}, {"maximumAvailable", "最大值"}, {"recommendedAllocation", "设置推荐值"},
        {"globalScale", "界面缩放"}, {"globalScaleDescription", "调整整个工作台的文字大小"},
        {"refreshFiles", "刷新文件"}, {"openFile", "打开"}, {"saveFile", "保存文件"}, {"agentComplete", "Agent 补全"},
        {"relatedFiles", "关联文件"}, {"noEditableFiles", "当前项目中没有可编辑的 TCL、SDC 或 RTL 文件。"}
    };
    return catalog;
}

const Catalog &englishCatalog() {
    static const Catalog catalog{
        {"workspace", "Workspace"}, {"overview", "Overview"}, {"projects", "Projects"},
        {"flowMonitor", "Flow Monitor"}, {"agentMonitor", "Agent Session"},
        {"skillsTools", "Skills & Tools"}, {"modelSettings", "Models & inference"}, {"manualDebug", "Manual Debug"},
        {"settingsGroup", "SETTINGS"}, {"studioSettings", "All Settings"}, {"appearanceSettings", "Appearance & editor"},
        {"skillsSettings", "Skills"}, {"agentToolsSettings", "Agent tools"}, {"modelsContextSettings", "Models & context"},
        {"settingsHomeDescription", "Configure Studio appearance, models, and available tools."},
        {"about", "About"}, {"aboutDescription", "Configure projects, run DFT flows, and review execution evidence in one workspace."},
        {"applicationVersion", "Version"}, {"qtVersion", "Qt version"},
        {"applicationLicense", "Application license"}, {"licenseNotDeclared", "MIT License"},
        {"licenseNotDeclaredDetail", "Copyright Little-W. Licensed under the MIT License."},
        {"applicationAuthor", "Author"}, {"applicationAuthorName", "Little-W"},
        {"thirdPartyNotices", "Third-party notices"},
        {"appearanceSettingsDescription", "Adjust editor behavior, terminal text, and interface scale."},
        {"skillsSettingsDescription", "Manage the project skills shared with Chat and Agent capability switches."},
        {"agentToolsSettingsDescription", "Manage the tools available to the DFT Agent."},
        {"modelsContextSettingsDescription", "Manage models, API context limits, sampling, and compaction."},
        {"evidence", "Run Report"}, {"models", "Models"}, {"fileEditor", "File Editor"}, {"workspaceGroup", "WORKSPACE"},
        {"projectsGroup", "PROJECTS"}, {"currentProjectSettings", "Current Project Settings"}, {"automationGroup", "AUTOMATION"}, {"configureGroup", "CONFIGURE"},
        {"operatorGroup", "OPERATOR"}, {"showSidebar", "Show sidebar"}, {"hideSidebar", "Hide sidebar"},
        {"search", "Search projects and runs"}, {"toolsSettings", "Tools and settings"},
        {"enableDarkMode", "Switch to dark mode"}, {"enableLightMode", "Switch to light mode"},
        {"local", "Local"}, {"agentRunning", "Agent running"}, {"evidenceReady", "Evidence ready"},
        {"workspaceDescription", "Local DFT automation control center."}, {"activeProject", "Active project"},
        {"chooseProject", "Choose a project"}, {"agentState", "Agent state"}, {"working", "Working"},
        {"standingBy", "Standing by"}, {"quickAction", "Quick action"}, {"runSelected", "Run selected"},
        {"capabilitiesEnabled", "capabilities enabled"}, {"manage", "Manage"}, {"addProjectFolder", "Add project folder"},
        {"addProject", "Add project"}, {"browse", "Browse"}, {"removeProject", "Remove project"}, {"project", "Project"}, {"noProjectNote", "No project note"},
        {"projectFolder", "Project folder"}, {"rtlFolder", "RTL folder"}, {"topModule", "Top module"},
        {"projectType", "Project type"}, {"typeRtl", "RTL project"}, {"typeNetlist", "Gate-level netlist"},
        {"typeAtpg", "ATPG"}, {"typeSimulation", "Functional simulation"}, {"typeVerification", "Evidence review"}, {"typeCustom", "General"},
        {"model", "Model"}, {"agentGoal", "Agent goal"}, {"save", "Save"}, {"runAgent", "Run Agent"},
        {"flowEvidence", "Every stage requires traceable execution evidence."}, {"stopAgent", "Stop Agent"},
        {"noProjectSelected", "No project selected"}, {"workspacePending", "Workspace will be created only when the controlled run starts."},
        {"stageRead", "Read project and constraints"}, {"stageSelect", "Select approved skill"},
        {"stageRun", "Run controlled DFT operation"}, {"stageCheck", "Cross-check reports"}, {"stageEvidence", "Record evidence"},
        {"complete", "Complete"}, {"active", "Active"}, {"needsReview", "Needs review"}, {"waiting", "Waiting"},
        {"workerStream", "Review Agent responses, tool calls, and tool results in time order."}, {"idle", "Idle"},
        {"detailedMode", "Detailed mode"}, {"noDetailedEvents", "No detailed events yet. Run with detailed mode to view live IC tool output."},
        {"icToolOutput", "IC tool and terminal output"}, {"waitingToolOutput", "Waiting for IC tool output or a terminal command..."},
        {"noWorkerEvents", "No worker events yet. Start a project from Projects or Flow Monitor."},
        {"disabledCapabilityNote", "Disabled capabilities are passed to the controlled worker on the next Agent run."},
        {"enabled", "enabled"}, {"all", "All"}, {"operatorOnly", "Only commands explicitly entered by the operator are executed."},
        {"clear", "Clear"}, {"selectProjectTerminal", "Select a project to choose its terminal directory"},
        {"terminalOutput", "Terminal output appears here."}, {"enterCommand", "Enter an operator command"}, {"run", "Run"},
        {"projectTerminal", "Project terminal"}, {"projectWorkingDirectory", "Project working directory"},
        {"openProjectTerminal", "Open project terminal"}, {"terminalOpened", "Project terminal opened"},
        {"terminalUnavailable", "No terminal application is available"},
        {"workspacePath", "Workspace root"}, {"workspaceSuffix", "Workspace suffix"},
        {"createWorkspacePerRun", "New suffixed workspace per run (off: project folder)"},
        {"evidenceRule", "Browse execution records and design summaries by project, session, and report type."},
        {"noEvidence", "No verified evidence has been recorded in this session."}, {"modelDescription", "Select and edit local models, then assign one to the current project."},
        {"errorsFound", "Errors found"}, {"reportReady", "Report ready"},
        {"supervisorWarning", "Review the run report"}, {"supervisorWarningDetail", "This run has not reached a fully verified result. Use the latest report to identify the root cause: repair tool, library, path, constraint, or script problems in their configuration. RTL source files may be edited when fresh report evidence and source analysis confirm a design defect; make the smallest functional fix and rerun verification."},
        {"modelPersisted", "A checked model is saved with the current project."}, {"useSelectedModel", "Use for selected project"},
        {"chooseProjectFolder", "Choose a project folder"}, {"simplifiedChinese", "Simplified Chinese"}, {"english", "English"},
        {"modelResponsive", "responsive local"}, {"modelCompact", "compact instruct"}, {"modelLongContext", "64K long context"},
        {"synthesisSettings", "Synthesis Settings"}, {"constraintFile", "Constraint file"}, {"loadConstraintFile", "Load constraint file"},
        {"clockName", "Clock port"}, {"clockPeriod", "Clock period (ns)"}, {"resetName", "Reset port"},
        {"synthesisOutput", "Synthesis output directory"}, {"dftOutput", "DFT output directory"}, {"compileEffort", "Compile effort"},
        {"rtlSources", "Additional RTL sources (one relative path per line)"}, {"rtlFilelist", "RTL filelist (used first when set)"},
        {"sourceLanguage", "RTL language"}, {"runTimeout", "Run timeout (seconds)"},
        {"inferenceSettings", "Local model inference"}, {"inferenceMode", "Inference mode"},
        {"gpuOnly", "GPU all layers"}, {"cpuGpu", "GPU + CPU auto"}, {"cpuOnly", "CPU only (fallback)"}, {"gpuMemory", "VRAM budget (GiB)"},
        {"cpuMemory", "CPU memory (GiB)"}, {"inputLimit", "History capacity"}, {"outputLimit", "Output limit"}, {"maximumAvailable", "Maximum"}, {"recommendedAllocation", "Set recommended"},
        {"globalScale", "Interface scale"}, {"globalScaleDescription", "Adjust text across the entire workspace"},
        {"refreshFiles", "Refresh files"}, {"openFile", "Open"}, {"saveFile", "Save file"}, {"agentComplete", "Agent complete"},
        {"relatedFiles", "Related files"}, {"noEditableFiles", "No editable TCL, SDC, or RTL files in the current project."}
    };
    return catalog;
}

const Catalog &capabilityTitlesZh() {
    static const Catalog catalog{
        {"project_dft_execution_readiness", "项目就绪检查"}, {"run_and_verify_project_dft_flow", "项目 DFT 流程"},
        {"run_and_verify_external_dft_flow", "开源 RTL 流程"},
        {"inspect_benchmark", "检查基准电路"}, {"optimize_atpg_goal", "优化 ATPG"},
        {"project_registry", "项目注册表"}, {"evidence_validator", "证据验证器"},
        {"isolated_terminal_diagnostics", "隔离终端诊断"}, {"agent_shell_and_files", "Agent Shell 与文件读取"},
        {"studio_configuration", "DFT Studio 配置"}
    };
    return catalog;
}

const Catalog &capabilityDescriptionsZh() {
    static const Catalog catalog{
        {"project_dft_execution_readiness", "只读检查已配置 RTL、SDC、库与 dc_shell。"}, {"run_and_verify_project_dft_flow", "隔离执行配置化扫描链 DFT，并双轮验证。"},
        {"run_and_verify_external_dft_flow", "运行生成式 DFT 流程并分析新鲜报告；仅在证据定位到设计缺陷时修复 RTL。"},
        {"inspect_benchmark", "读取 FAN_ATPG 白名单电路说明信息。"}, {"optimize_atpg_goal", "在批准 profile 中选择满足目标的配置。"},
        {"project_registry", "读写项目目标与执行目录。"}, {"evidence_validator", "执行、报告和反证的双轮验证。"},
        {"isolated_terminal_diagnostics", "检查受控流程的新鲜日志与报告，并将已确认的问题追踪到项目配置或 RTL。"},
        {"agent_shell_and_files", "使用隔离 Shell、文件读取和检索工具检查项目，不把完整大文件塞入上下文。"},
        {"studio_configuration", "读取和修改 DFT Agent Studio 的项目、模型和能力配置。"}
    };
    return catalog;
}

} // namespace

LanguageSettings::LanguageSettings(QObject *parent)
    : QObject(parent),
      m_settings(studioUiSettingsPath(), QSettings::IniFormat),
      m_language(normalizedLanguage(m_settings.value("ui/language", "zh-CN").toString())),
      m_darkModeEnabled(m_settings.value("ui/dark_mode", false).toBool()),
      m_windowWidth(qMax(1024, m_settings.value("ui/window_width", 1440).toInt())),
      m_windowHeight(qMax(680, m_settings.value("ui/window_height", 900).toInt())),
      m_sidebarProjectListExpanded(m_settings.value("ui/sidebar_project_list_expanded", true).toBool()),
      m_toolOutputPanelWidth(qBound(300, m_settings.value("ui/tool_output_panel_width", 440).toInt(), 1200)),
      m_toolOutputFontSize(qBound(8, m_settings.value("ui/tool_output_font_size", 11).toInt(), 28)),
      m_terminalFontSize(qBound(8, m_settings.value("ui/terminal_font_size", 11).toInt(), 28)),
      m_uiScale(qBound(0.8, m_settings.value("ui/global_scale", 1.0).toDouble(), 1.4)),
      m_selectedProjectId(m_settings.value("projects/selected_id").toString().trimmed()),
      m_chatModelId(m_settings.value("chat/model_id").toString().trimmed()),
      m_chatModelApiBase(m_settings.value("chat/model_api_base").toString().trimmed()),
      m_chatMultiAgentEnabled(m_settings.value("chat/multi_agent_enabled", false).toBool()),
      m_editorFontFamily(m_settings.value("editor/font_family", "Monospace").toString().trimmed()),
      m_editorFontSize(qBound(10, m_settings.value("editor/font_size", 13).toInt(), 24)),
      m_editorLineHeight(qBound(14, m_settings.value("editor/line_height", 19).toInt(), 42)),
      m_editorTabSize(qBound(1, m_settings.value("editor/tab_size", 4).toInt(), 8)),
      m_editorInsertSpaces(m_settings.value("editor/insert_spaces", true).toBool()),
      m_editorWordWrap(m_settings.value("editor/word_wrap", false).toBool()),
      m_editorSmoothScrolling(m_settings.value("editor/smooth_scrolling", true).toBool()),
      m_editorShowLineNumbers(m_settings.value("editor/show_line_numbers", true).toBool()),
      m_editorMaximumLines(qBound(1'000, m_settings.value("editor/maximum_lines", 10'000).toInt(), 100'000)),
      m_editorUseVim(m_settings.value(
          "editor/use_vim",
          m_settings.value("editor/vim_mode", false)
      ).toBool()) {
    if (m_editorFontFamily.isEmpty())
        m_editorFontFamily = QStringLiteral("Monospace");
    m_editorLineHeight = qMax(m_editorFontSize + 2, m_editorLineHeight);
}

QString LanguageSettings::language() const { return m_language; }

bool LanguageSettings::darkModeEnabled() const { return m_darkModeEnabled; }

void LanguageSettings::setDarkModeEnabled(bool value) {
    if (m_darkModeEnabled == value)
        return;
    m_darkModeEnabled = value;
    m_settings.setValue("ui/dark_mode", m_darkModeEnabled);
    m_settings.sync();
    emit darkModeEnabledChanged();
}

void LanguageSettings::setLanguage(const QString &value) {
    const QString normalized = normalizedLanguage(value);
    if (m_language == normalized)
        return;
    m_language = normalized;
    m_settings.setValue("ui/language", m_language);
    m_settings.sync();
    emit languageChanged();
}

int LanguageSettings::windowWidth() const { return m_windowWidth; }

void LanguageSettings::setWindowWidth(int value) {
    const int normalized = qMax(1024, value);
    if (m_windowWidth == normalized)
        return;
    m_windowWidth = normalized;
    m_settings.setValue("ui/window_width", m_windowWidth);
    m_settings.sync();
    emit windowSizeChanged();
}

int LanguageSettings::windowHeight() const { return m_windowHeight; }

void LanguageSettings::setWindowHeight(int value) {
    const int normalized = qMax(680, value);
    if (m_windowHeight == normalized)
        return;
    m_windowHeight = normalized;
    m_settings.setValue("ui/window_height", m_windowHeight);
    m_settings.sync();
    emit windowSizeChanged();
}

void LanguageSettings::saveWindowSize(int width, int height) {
    const int normalizedWidth = qMax(1024, width);
    const int normalizedHeight = qMax(680, height);
    m_windowWidth = normalizedWidth;
    m_windowHeight = normalizedHeight;
    m_settings.setValue("ui/window_width", normalizedWidth);
    m_settings.setValue("ui/window_height", normalizedHeight);
    m_settings.sync();
    emit windowSizeChanged();
}

bool LanguageSettings::sidebarProjectListExpanded() const { return m_sidebarProjectListExpanded; }

void LanguageSettings::setSidebarProjectListExpanded(bool value) {
    if (m_sidebarProjectListExpanded == value)
        return;
    m_sidebarProjectListExpanded = value;
    m_settings.setValue("ui/sidebar_project_list_expanded", m_sidebarProjectListExpanded);
    m_settings.sync();
    emit sidebarProjectListExpandedChanged();
}

int LanguageSettings::toolOutputPanelWidth() const { return m_toolOutputPanelWidth; }

void LanguageSettings::setToolOutputPanelWidth(int value) {
    const int normalized = qBound(300, value, 1200);
    if (m_toolOutputPanelWidth == normalized)
        return;
    m_toolOutputPanelWidth = normalized;
    m_settings.setValue("ui/tool_output_panel_width", m_toolOutputPanelWidth);
    m_settings.sync();
    emit toolOutputPanelWidthChanged();
}

int LanguageSettings::toolOutputFontSize() const { return m_toolOutputFontSize; }

void LanguageSettings::setToolOutputFontSize(int value) {
    const int normalized = qBound(8, value, 28);
    if (m_toolOutputFontSize == normalized)
        return;
    m_toolOutputFontSize = normalized;
    m_settings.setValue("ui/tool_output_font_size", m_toolOutputFontSize);
    m_settings.sync();
    emit toolOutputFontSizeChanged();
}

int LanguageSettings::terminalFontSize() const { return m_terminalFontSize; }

void LanguageSettings::setTerminalFontSize(int value) {
    const int normalized = qBound(8, value, 28);
    if (m_terminalFontSize == normalized)
        return;
    m_terminalFontSize = normalized;
    m_settings.setValue("ui/terminal_font_size", m_terminalFontSize);
    m_settings.sync();
    emit terminalFontSizeChanged();
}

double LanguageSettings::uiScale() const { return m_uiScale; }

void LanguageSettings::setUiScale(double value) {
    const double normalized = qBound(0.8, value, 1.4);
    if (qFuzzyCompare(m_uiScale, normalized))
        return;
    m_uiScale = normalized;
    m_settings.setValue("ui/global_scale", m_uiScale);
    m_settings.sync();
    emit uiScaleChanged();
}

QString LanguageSettings::selectedProjectId() const { return m_selectedProjectId; }

void LanguageSettings::setSelectedProjectId(const QString &value) {
    const QString normalized = value.trimmed();
    if (m_selectedProjectId == normalized)
        return;
    m_selectedProjectId = normalized;
    m_settings.setValue("projects/selected_id", m_selectedProjectId);
    m_settings.sync();
    emit selectedProjectIdChanged();
}

QString LanguageSettings::chatModelId() const { return m_chatModelId; }

void LanguageSettings::setChatModelId(const QString &value) {
    const QString normalized = value.trimmed();
    if (m_chatModelId == normalized)
        return;
    m_chatModelId = normalized;
    m_settings.setValue("chat/model_id", m_chatModelId);
    m_settings.sync();
    emit chatModelChanged();
}

QString LanguageSettings::chatModelApiBase() const { return m_chatModelApiBase; }

void LanguageSettings::setChatModelApiBase(const QString &value) {
    QString normalized = value.trimmed();
    while (normalized.endsWith(QLatin1Char('/')))
        normalized.chop(1);
    if (m_chatModelApiBase == normalized)
        return;
    m_chatModelApiBase = normalized;
    m_settings.setValue("chat/model_api_base", m_chatModelApiBase);
    m_settings.sync();
    emit chatModelChanged();
}

bool LanguageSettings::chatMultiAgentEnabled() const { return m_chatMultiAgentEnabled; }

void LanguageSettings::setChatMultiAgentEnabled(bool value) {
    if (m_chatMultiAgentEnabled == value)
        return;
    m_chatMultiAgentEnabled = value;
    m_settings.setValue("chat/multi_agent_enabled", m_chatMultiAgentEnabled);
    m_settings.sync();
    emit chatModelChanged();
}

QString LanguageSettings::editorFontFamily() const { return m_editorFontFamily; }

void LanguageSettings::setEditorFontFamily(const QString &value) {
    const QString normalized = value.trimmed().isEmpty() ? QStringLiteral("Monospace") : value.trimmed();
    if (m_editorFontFamily == normalized)
        return;
    m_editorFontFamily = normalized;
    saveEditorSettings();
    emit editorSettingsChanged();
}

int LanguageSettings::editorFontSize() const { return m_editorFontSize; }

void LanguageSettings::setEditorFontSize(int value) {
    const int normalized = qBound(10, value, 24);
    if (m_editorFontSize == normalized)
        return;
    m_editorFontSize = normalized;
    m_editorLineHeight = qMax(m_editorLineHeight, m_editorFontSize + 2);
    saveEditorSettings();
    emit editorSettingsChanged();
}

int LanguageSettings::editorLineHeight() const { return m_editorLineHeight; }

void LanguageSettings::setEditorLineHeight(int value) {
    const int normalized = qBound(m_editorFontSize + 2, value, 42);
    if (m_editorLineHeight == normalized)
        return;
    m_editorLineHeight = normalized;
    saveEditorSettings();
    emit editorSettingsChanged();
}

int LanguageSettings::editorTabSize() const { return m_editorTabSize; }

void LanguageSettings::setEditorTabSize(int value) {
    const int normalized = qBound(1, value, 8);
    if (m_editorTabSize == normalized)
        return;
    m_editorTabSize = normalized;
    saveEditorSettings();
    emit editorSettingsChanged();
}

bool LanguageSettings::editorInsertSpaces() const { return m_editorInsertSpaces; }

void LanguageSettings::setEditorInsertSpaces(bool value) {
    if (m_editorInsertSpaces == value)
        return;
    m_editorInsertSpaces = value;
    saveEditorSettings();
    emit editorSettingsChanged();
}

bool LanguageSettings::editorWordWrap() const { return m_editorWordWrap; }

void LanguageSettings::setEditorWordWrap(bool value) {
    if (m_editorWordWrap == value)
        return;
    m_editorWordWrap = value;
    saveEditorSettings();
    emit editorSettingsChanged();
}

bool LanguageSettings::editorSmoothScrolling() const { return m_editorSmoothScrolling; }

void LanguageSettings::setEditorSmoothScrolling(bool value) {
    if (m_editorSmoothScrolling == value)
        return;
    m_editorSmoothScrolling = value;
    saveEditorSettings();
    emit editorSettingsChanged();
}

bool LanguageSettings::editorShowLineNumbers() const { return m_editorShowLineNumbers; }

void LanguageSettings::setEditorShowLineNumbers(bool value) {
    if (m_editorShowLineNumbers == value)
        return;
    m_editorShowLineNumbers = value;
    saveEditorSettings();
    emit editorSettingsChanged();
}

int LanguageSettings::editorMaximumLines() const { return m_editorMaximumLines; }

void LanguageSettings::setEditorMaximumLines(int value) {
    const int normalized = qBound(1'000, value, 100'000);
    if (m_editorMaximumLines == normalized)
        return;
    m_editorMaximumLines = normalized;
    saveEditorSettings();
    emit editorSettingsChanged();
}

bool LanguageSettings::editorUseVim() const { return m_editorUseVim; }

void LanguageSettings::setEditorUseVim(bool value) {
    if (m_editorUseVim == value)
        return;
    m_editorUseVim = value;
    saveEditorSettings();
    emit editorSettingsChanged();
}

QString LanguageSettings::text(const QString &key) const {
    const auto &catalog = isEnglish() ? englishCatalog() : chineseCatalog();
    return catalog.value(key, key);
}

QString LanguageSettings::statusText(const QString &status) const {
    const Catalog chinese{{"ready", "就绪"}, {"running", "运行中"}, {"verified", "已验证"}, {"stopped", "已停止"}, {"failed", "失败"}, {"unavailable", "不可用"}};
    const Catalog english{{"ready", "Ready"}, {"running", "Running"}, {"verified", "Verified"}, {"stopped", "Stopped"}, {"failed", "Failed"}, {"unavailable", "Unavailable"}};
    return (isEnglish() ? english : chinese).value(status, status);
}

QString LanguageSettings::phaseText(const QString &phase) const {
    const Catalog chinese{
        {"Ready", "就绪"}, {"Starting", "启动中"}, {"Queued", "排队中"}, {"Reading project", "读取项目"}, {"Selecting approved skill", "选择已批准技能"},
        {"Staging workspace", "准备隔离工作区"}, {"Collecting reports", "收集报告"}, {"Double-checking evidence", "双重核验执行证据"},
        {"Evidence recorded", "证据已记录"}, {"Completed with errors", "完成但发现错误"}, {"Verified", "已验证"}, {"Failed", "失败"}, {"Stopped by operator", "已由操作员停止"},
        {"Finished without evidence", "执行结束但未返回证据"},
        {"agent_started", "读取项目"}, {"source_discovery", "检查项目输入"},
        {"source_discovery_completed", "项目输入已就绪"}, {"model_tool_selection", "选择执行工具"},
        {"staged", "隔离工作目录已就绪"}, {"dc_shell", "读取 RTL 并执行综合"},
        {"read_link_completed", "RTL 读取与链接完成"}, {"constraints_applied", "时钟与约束已应用"},
        {"compile_started", "开始执行综合"}, {"compile_mapping", "综合：逻辑转换"},
        {"compile_implementation", "综合：选择实现"}, {"compile_optimization", "综合：逻辑优化"},
        {"compile_timing", "综合：时序优化"}, {"compile_finishing", "综合：生成结果"},
        {"synthesis_completed", "综合完成"},
        {"scan_configuration", "建立 Scan 配置"}, {"pre_dft_drc_completed", "DFT DRC 检查完成"},
        {"scan_inserted", "Scan 已插入"}, {"scan_completed", "Scan 报告已生成"},
        {"execution_finished", "综合与 Scan 工具执行结束"}, {"mbist_simulation", "运行 MBIST 仿真"},
        {"mbist_simulation_finished", "MBIST 仿真结束"}, {"testmax", "运行 TestMAX ATPG"},
        {"testmax_finished", "TestMAX ATPG 结束"}, {"evidence_written", "整理运行结果"},
        {"cross_validation", "检查运行结果"}, {"api_connecting", "连接推理 API"},
        {"api_planning", "推理 API 正在规划"}, {"api_completed", "API 回合完成"},
        {"completed", "任务完成"}
    };
    return isEnglish() ? phase : chinese.value(phase, phase);
}

QString LanguageSettings::categoryText(const QString &category) const {
    if (category == "Skills")
        return isEnglish() ? QStringLiteral("Skills") : QStringLiteral("技能");
    if (category == "Agent tools")
        return isEnglish() ? QStringLiteral("Agent tools") : QStringLiteral("Agent 工具");
    return category;
}

QString LanguageSettings::capabilityTitle(const QString &id, const QString &fallback) const {
    return isEnglish() ? fallback : capabilityTitlesZh().value(id, fallback);
}

QString LanguageSettings::capabilityDescription(const QString &id, const QString &fallback) const {
    return isEnglish() ? fallback : capabilityDescriptionsZh().value(id, fallback);
}

void LanguageSettings::saveEditorSettings() {
    m_settings.setValue("editor/font_family", m_editorFontFamily);
    m_settings.setValue("editor/font_size", m_editorFontSize);
    m_settings.setValue("editor/line_height", m_editorLineHeight);
    m_settings.setValue("editor/tab_size", m_editorTabSize);
    m_settings.setValue("editor/insert_spaces", m_editorInsertSpaces);
    m_settings.setValue("editor/word_wrap", m_editorWordWrap);
    m_settings.setValue("editor/smooth_scrolling", m_editorSmoothScrolling);
    m_settings.setValue("editor/show_line_numbers", m_editorShowLineNumbers);
    m_settings.setValue("editor/maximum_lines", m_editorMaximumLines);
    m_settings.setValue("editor/use_vim", m_editorUseVim);
    m_settings.remove("editor/vim_mode");
    m_settings.sync();
}

bool LanguageSettings::isEnglish() const { return m_language == "en"; }

QString LanguageSettings::normalizedLanguage(const QString &value) {
    return value.trimmed().toLower().startsWith("en") ? QStringLiteral("en") : QStringLiteral("zh-CN");
}
