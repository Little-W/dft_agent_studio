import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "components" as UI

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 680
    visible: true
    title: "DFT Agent Studio"
    color: window.darkMode ? "#171b20" : "#f5f6f8"

    property int selectedPage: 0
    property int displayedPage: selectedPage
    property bool sidebarExpanded: true
    property real sidebarProgress: sidebarExpanded ? 1.0 : 0.0
    property bool sidebarProjectListExpanded: languageSettings.sidebarProjectListExpanded
    property bool sidebarSettingsExpanded: true
    property real sidebarSettingsProgress: sidebarSettingsExpanded ? 1.0 : 0.0
    property real requestedToolOutputPanelWidth: languageSettings.toolOutputPanelWidth
    property real sidebarProjectListProgress: sidebarProjectListExpanded ? 1.0 : 0.0
    property var expandedFlowStageKeys: []
    property string capabilityFilter: "All"
    property var currentProject: ({})
    property string projectSearch: ""
    property string projectSaveMessage: ""
    property string terminalLaunchStatus: ""
    property string editorSuggestion: ""
    property var localEditorSuggestions: []
    property int localEditorSuggestionIndex: 0
    property string fileFilter: ""
    property bool localCompletionEnabled: true
    property bool aiCompletionEnabled: true
    property bool relatedFilesExpanded: true
    property bool windowSizeRestored: false
    property int rememberedWindowWidth: languageSettings.windowWidth
    property int rememberedWindowHeight: languageSettings.windowHeight
    property string tclImportNotice: ""
    property string projectImportToast: ""
    property bool projectImportRunning: false
    property string projectImportSessionId: ""
    property string projectImportProjectId: ""
    property var projectImportProject: ({})
    property var projectRelatedDocumentsValue: []
    property string relatedDocumentDialogRowId: ""
    property string selectedDftTool: "testmax"
    property string tessentDofile: ""
    property string pendingChatPrompt: ""
    property bool qaFrameProfile: false
    property int qaAnimationTickCount: 0
    property string agentPermissionMode: "approval"
    property bool agentConversationRequested: false
    property string codexSessionId: ""
    property string codexSessionProjectId: ""
    property var codexSessions: []
    property var flowSessionSummaries: []
    property var codexSubagentSessions: []
    property string codexSessionParentThreadId: ""
    property string flowProgressSessionId: ""
    property string flowProgressSessionName: ""
    property string codexSessionGoal: ""
    property string codexSessionGoalStatus: "inactive"
    property var codexSessionPlan: []
    property string codexSessionPlanUpdatedAt: ""
    property bool codexSessionRecoveryPending: false
    property string codexSessionRecoveryGoal: ""
    property string codexSessionRecoveryReason: ""
    property string codexSessionRecoveryUpdatedAt: ""
    // The selected Chat model is populated from the configured API's
    // /models response.  Keep it empty until that response arrives instead
    // of presenting a baked-in provider model as if it were available.
    property string codexChatModel: ""
    property bool codexChatUsesDefaultModel: true
    property string codexChatReasoningEffort: "medium"
    property bool codexChatPreferencesInitialized: false
    property string codexChatPreferenceApiBase: ""
    property bool codexPlanRequestPending: false
    // Workspace reads use a short-lived helper process; never poll them on
    // the GUI thread while a long transcript is being scrolled.
    property bool codexWorkspaceRefreshing: false
    property var codexWorkspaceCatalog: ({})
    property string codexWorkspaceMessage: ""
    property string codexSessionSearch: ""
    property string codexSessionProjectFilter: "*"
    property string codexSessionArchiveFilter: "all"
    // Hydrate history in large, turn-aligned batches.  The backend may expand
    // a page further when its boundary falls inside a tool group.  Tool-group
    // children are instantiated only after the group is opened, so a larger
    // page does not create thousands of hidden child delegates up front.
    readonly property int codexActivityPageLimit: 9600
    property int workspaceActionRequestSerial: 0
    property var workspaceActionCallbacks: ({})
    ListModel { id: projectRelatedDocumentsModel }
    Timer { id: projectImportToastTimer; interval: 4200; onTriggered: window.projectImportToast = "" }
    property bool codexSessionMultiSelect: false
    property var codexSelectedSessionIds: ({})
    property bool codexChatSidebarExpanded: true
    property real codexChatSidebarWidth: 280
    property bool codexSessionLoading: false
    property var codexActivityMeta: ({ has_more: false, next_before: 0, total_count: 0 })
    property bool codexActivityPrepending: false
    property bool codexActivityScrollLocked: false
    property bool codexChatScrollbarActive: false

    property bool codexChatDisplayRebuildScheduled: false
    property bool codexActivityLoadScheduled: false
    property bool codexActivityRefreshing: false
    property bool codexActivityHistoryExpanded: false
    // Expanded thinking must survive dataChanged/reused delegates while the
    // provider appends streamed reasoning deltas. Prefer the stable stream id
    // over the source index because prepending history shifts source indexes.
    property string codexExpandedThinkingKey: ""
    // Group expansion must be independent of a recycled ListView delegate.
    // A streaming update changes the group's height and can make Qt reuse the
    // row; keeping this key in the window prevents the child tree from
    // blinking closed and being rebuilt on every delta.
    property string codexExpandedActivityGroupKey: ""
    property real codexActivityPrependAnchorY: 0
    property real codexActivityPrependAnchorHeight: 0
    property real codexActivityPrependLastHeight: -1
    property int codexActivityPrependStableFrames: 0
    property int codexActivityPrependSettlePasses: 0
    property int codexActivityPrependExpectedEntries: 0
    property int codexActivityPrependSourceShift: 0
    property int codexActivityPrependAnchorIndex: -1
    property int codexActivityPrependAnchorDisplayCount: 0
    property real codexActivityPrependAnchorViewportY: 0
    property int codexScrollAcceptanceAttempts: 0
    property var codexChatDisplayEntries: []
    // Fingerprints let streamed token updates patch the affected tail row
    // without copying/re-grouping the entire 2000-entry history page.
    property var codexChatRawFingerprints: []
    property var runReports: []
    property string selectedReportPath: ""
    property string reportProjectFilter: "*"
    property string reportSessionFilter: "*"
    property string reportCategoryFilter: "all"
    property int modelSettingsIndex: -1
    property string modelSettingsMessageZh: ""
    property string modelSettingsMessageEn: ""
    readonly property string modelSettingsMessage: languageSettings.language === "en" ? modelSettingsMessageEn : modelSettingsMessageZh
    property string modelCatalogError: ""
    property string codexModelContextAutofilledId: ""
    property bool modelContextRefreshPending: false
    property string modelContextRefreshTargetId: ""
    property string modelContextRefreshApiBase: ""
    readonly property bool darkMode: languageSettings.darkModeEnabled
    readonly property color accent: "#0a84ff"
    readonly property color graphite: darkMode ? "#e5eaf0" : "#252a31"
    readonly property color muted: darkMode ? "#a0aab5" : "#667384"
    readonly property color chatCanvas: darkMode ? "#171b20" : "#f4f7fa"
    readonly property color chatSurface: darkMode ? "#20262d" : "#ffffff"
    readonly property color chatPanel: darkMode ? "#1b2127" : "#fbfcfe"
    readonly property color chatBorder: darkMode ? "#39434e" : "#dbe4ec"
    readonly property color chatHover: darkMode ? "#29333d" : "#eef5fb"
    readonly property color chatSelected: darkMode ? "#203e59" : "#dcecf9"
    readonly property color chatText: darkMode ? "#e0e7ee" : "#253443"
    readonly property color chatMuted: darkMode ? "#a0acb8" : "#6d7b89"
    readonly property string defaultWorkspacePath: "/media/6/Projects/DFT_agent_project_workspaces"

    ListModel { id: synthesisClockModel }
    ListModel { id: generatedClockModel }
    ListModel { id: ioDelayModel }
    ListModel { id: timingExceptionModel }
    // Chat history is backed by a C++ QAbstractListModel. Older pages are
    // inserted with one beginInsertRows/endInsertRows transaction, while
    // streaming updates emit dataChanged only for rows that changed.
    property var codexChatDisplayModel: chatDisplayModel

    onWidthChanged: {
        if (windowSizeRestored && visibility === Window.Windowed) {
            rememberedWindowWidth = Math.round(width)
            languageSettings.windowWidth = Math.round(width)
        }
    }

    onHeightChanged: {
        if (windowSizeRestored && visibility === Window.Windowed) {
            rememberedWindowHeight = Math.round(height)
            languageSettings.windowHeight = Math.round(height)
        }
    }

    onVisibilityChanged: {
        if (windowSizeRestored && visibility === Window.Windowed) {
            rememberedWindowWidth = Math.round(width)
            rememberedWindowHeight = Math.round(height)
        }
    }

    onClosing: function(close) {
        if (windowSizeRestored)
            languageSettings.saveWindowSize(rememberedWindowWidth, rememberedWindowHeight)
    }

    Behavior on sidebarProgress {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    Behavior on sidebarProjectListProgress {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    Behavior on sidebarSettingsProgress {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    onSelectedPageChanged: {
        if (selectedPage === 3)
            Qt.callLater(refreshCodexWorkspace)
        if (selectedPage === 10)
            Qt.callLater(refreshCodexWorkspaceCatalog)
        if (selectedPage === 6)
            Qt.callLater(refreshRunReports)
        if (selectedPage !== displayedPage && !pageTransition.running)
            pageTransition.start()
    }

    Connections {
        target: agentController
        function onReportFilesChanged() {
            if (window.selectedPage === 6)
                Qt.callLater(window.refreshRunReports)
        }
    }

    SequentialAnimation {
        id: pageTransition

        onStopped: {
            if (window.selectedPage !== window.displayedPage)
                pageTransition.start()
        }

        ParallelAnimation {
            NumberAnimation { target: pageStack; property: "opacity"; to: 0; duration: 90; easing.type: Easing.OutCubic }
            NumberAnimation { target: pageStackTranslation; property: "y"; to: 8; duration: 90; easing.type: Easing.OutCubic }
        }
        ScriptAction {
            script: {
                window.displayedPage = window.selectedPage
                pageStackTranslation.y = -8
            }
        }
        ParallelAnimation {
            NumberAnimation { target: pageStack; property: "opacity"; to: 1; duration: 170; easing.type: Easing.OutCubic }
            NumberAnimation { target: pageStackTranslation; property: "y"; to: 0; duration: 170; easing.type: Easing.OutCubic }
        }
    }

    function pageTitle() {
        const titleKeys = [
            "workspace",
            "projects",
            "flowMonitor",
            "agentMonitor",
            "appearanceSettings",
            "manualDebug",
            "evidence",
            "fileEditor",
            "modelsContextSettings",
            "studioSettings",
            "skillsSettings",
            "agentToolsSettings",
            "about"
        ]
        return window.localizedText(titleKeys[selectedPage] || "workspace")
    }

    function qaOpenDialog(name) {
        const dialogs = {
            "projectGeneralDialog": projectGeneralDialog,
            "synthesisSettingsDialog": synthesisSettingsDialog,
            "dftSettingsDialog": dftSettingsDialog,
            "agentProjectSettingsDialog": agentProjectSettingsDialog,
            "addProjectDialog": addProjectDialog,
            "sourceEditorDialog": sourceEditorDialog,
            "atpgCellModelEditorDialog": atpgCellModelEditorDialog,
            "projectLaunchSessionDialog": projectLaunchSessionDialog,
            "subagentSessionDialog": subagentSessionDialog,
            "codexSessionDialog": codexSessionDialog,
            "aboutDialog": aboutDialog
        }
        const dialog = dialogs[name]
        if (!dialog)
            return false
        dialog.open()
        return true
    }

    readonly property var englishUiStrings: ({
        "更快响应，较少推理": "Faster responses with less reasoning",
        "平衡速度与分析深度": "Balances speed and analysis depth",
        "更充分分析复杂 DFT 问题": "More thorough analysis for complex DFT problems",
        "选择历史会话或从空白新会话开始": "Choose a previous session or start a blank one",
        "会话管理": "Session management",
        "DFT Agent 对话": "DFT Agent Chat",
        "刷新会话": "Refresh sessions",
        "导入会话": "Import session",
        "新建对话": "New chat",
        "搜索会话": "Search sessions",
        "会话": "Sessions",
        "批量选择": "Select multiple",
        "选择多个会话进行归档或删除": "Select sessions to archive or delete",
        "已选 ": "Selected ",
        "全选可操作会话": "Select all available sessions",
        "归档所选会话": "Archive selected sessions",
        "删除所选会话": "Delete selected sessions",
        "取消多选": "Cancel multi-select",
        "会话操作": "Session actions",
        "返回主 session": "Return to parent session",
        "查看当前会话的历史子智能体会话": "View this session's subagent history",
        "清空当前显示记录": "Clear the displayed transcript",
        "在下方描述目标；模型回复、工具调用和修改候选会连续显示在这里。": "Describe your goal below. Model replies, tool calls, and proposed edits will appear here.",
        "输出": "Output",
        "查看 EDA 终端输出": "View EDA terminal output",
        "执行结果\n": "Execution result\n",
        "Shell": "Shell",
        "复制输出": "Copy output",
        "命令输出为空": "Command produced no output",
        "操作已完成": "Operation completed",
        "操作失败": "Operation failed",
        "状态：": "Status: ",
        "作业：": "Job: ",
        "行数：": "Lines: ",
        "字节：": "Bytes: ",
        "匹配项：": "Matches: ",
        "创建：": "Created: ",
        "更新：": "Updated: ",
        "已编辑的文件": "Edited files",
        "查看文件差异": "View file diff",
        "复制命令": "Copy command",
        "复制": "Copy",
        "复制回复": "Copy response",
        "工作目录：": "Working directory: ",
        "退出码：": "Exit code: ",
        "复制文件名": "Copy file name",
        "错误：": "Error: ",
        "EDA 作业": "EDA job",
        "操作：": "Operation: ",
        "撤销": "Undo",
        "批准": "Approve",
        "拒绝": "Reject",
        "验证": "Verify",
        "描述下一步工作，或插入一个引导…": "Describe the next step or send an instruction…",
        "Goal 与 Plan": "Goal and Plan",
        "设置当前会话持续追踪的工程目标": "Set the engineering goal tracked throughout this session",
        "生成执行计划": "Generate an execution plan",
        "重命名会话": "Rename session",
        "从此处创建分支": "Branch from here",
        "回退最近一轮": "Undo the last turn",
        "压缩较早上下文": "Compact earlier context",
        "删除会话": "Delete session",
        "永久删除本地会话记录和工具事件": "Permanently delete the local transcript and tool events",
        "会话名称": "Session name",
        "取消": "Cancel",
        "保存": "Save",
        "此操作只删除本机保存的会话文本、Plan 和工具事件，无法撤销。": "This permanently deletes the locally stored transcript, plan, and tool events.",
        "确定删除“": "Delete \"",
        "”吗？": "\"?",
        "批量删除会话": "Delete sessions",
        "将永久删除所选会话的文本、Plan 和工具事件，且无法撤销。": "The selected transcripts, plans, and tool events will be permanently deleted.",
        "确定永久删除选中的 ": "Permanently delete the selected ",
        " 个会话吗？": " sessions?",
        "EDA 工具输出": "EDA tool output",
        " · 终端日志": " · terminal log",
        "dc_shell / TestMAX 实时终端日志": "Live dc_shell / TestMAX terminal log",
        "作业": "Job",
        "未返回作业 ID": "No job ID returned",
        "复制输出": "Copy output",
        "可留空；也可在下一项直接输入密钥": "Optional; you may enter the key in the next field instead",
        "本地模型或 GGUF 路径": "Local model or GGUF path",
        "可选": "Optional",
        "编辑排队消息": "Edit queued message",
        "保存后会更新队列中的原消息，不会额外创建一条。": "Saving updates the queued message in place; it will not create another message.",
        "导入会话": "Import session",
        "选择一个 JSON 会话副本，导入后会作为独立会话保存在本地。": "Choose a JSON session export. It will be imported as a separate local session.",
        "会话文件": "Session file",
        "选择 .json 会话文件": "Choose a .json session file",
        "选择文件": "Choose file",
        "支持 DFT Agent 会话 JSON；导入不会覆盖当前会话。": "DFT Agent session JSON is supported. Importing will not overwrite the current session.",
        "导入": "Import",
        "选择会话 JSON": "Choose session JSON",
        "导出会话": "Export session",
        "Goal 会持续进入后续回合上下文，不会作为临时文字插入输入框。": "The Goal remains in context for later turns instead of being inserted as temporary prompt text.",
        "描述可验收的最终目标、约束和停止条件。": "Describe the verifiable end goal, constraints, and stop conditions.",
        "例如：修复当前工程的 DFT DRC，完成扫描链插入，并在真实 ATPG 证据下达到覆盖率目标。": "Example: fix the project's DFT DRC, complete scan insertion, and meet the coverage target with real ATPG evidence.",
        "清除 Goal": "Clear Goal",
        "保存 Goal": "Save Goal",
        "当前 Plan": "Current Plan",
        "尚未生成 Plan。": "No plan has been generated yet.",
        "会话选择": "Select session",
        "选择会话": "Choose session",
        "选择会话可切换到所属项目并查看流程进度；选择新建会话会清空当前对话，运行时再创建会话。": "Choose a session to switch to its project and view progress. Choose New session to clear this chat; a session will be created when you run it.",
        "新建会话": "New session",
        "DFT 会话": "DFT session",
        "目前没有历史根会话。": "There are no previous root sessions.",
        "历史子智能体会话": "Subagent history",
        "按最近更新排序。打开后可查看该子智能体自己的完整对话和工具轨迹。": "Sorted by most recently updated. Open a subagent to view its full conversation and tool history.",
        "子智能体": "Subagent",
        "打开": "Open",
        "当前会话还没有子智能体会话。": "This session has no subagent sessions yet.",
        "Agent 会话": "Agent sessions",
        "当前会话已选定": "A session is selected",
        "将继续最近的未归档会话": "The most recent unarchived session will be continued",
        "批量选择会话": "Select multiple sessions",
        "全选": "Select all",
        "归档 (": "Archive (",
        "删除 (": "Delete (",
        "导出": "Export",
        "从 JSON 文件导入会话副本": "Import a session export from a JSON file",
        "导出当前会话及工具事件": "Export the current session and its tool events",
        "新建": "New",
        "继续": "Continue",
        "分支": "Branch",
        "回退": "Undo",
        "撤回最近一轮会话状态，不修改工程文件": "Undo the latest session turn without changing project files",
        "压缩": "Compact",
        "把较早的会话记录转入可管理的记忆": "Move older session history into manageable memory",
        "恢复": "Restore",
        "归档": "Archive",
        "尚未建立会话。首次发送消息后会自动创建。": "No session yet. One will be created when you send the first message.",
        "Agent 工作区": "Agent workspace",
        "Rules 和 Skills 仅在启用后加入下一轮上下文；外部 Hooks 仅供审查，不会由独立 Runtime 自动执行。": "Rules and Skills enter the next turn's context only when enabled. External Hooks are for review and are not run by the standalone runtime.",
        "添加": "Add",
        "记忆标题": "Memory title",
        "只记录会在后续工程任务中复用的事实或偏好。": "Record only facts or preferences that will be useful in future engineering tasks.",
        "保存记忆": "Save memory",
        "编辑记忆": "Edit memory",
        "停用记忆": "Disable memory",
        "启用记忆": "Enable memory",
        "删除记忆": "Delete memory",
        "Memories、Skills、Rules、Hooks": "Memories, Skills, Rules, and Hooks",
        "思考过程（点击收起）": "Reasoning (click to collapse)",
        "思考中…（点击展开）": "Thinking… (click to expand)",
        "已保存": "Saved",
        "保存失败": "Save failed",
        "请先保存项目目录。": "Save a project folder first.",
        "AI 正在后台导入设置；需要查看过程可到归档会话中打开。": "AI import is running in the background. Find its session in Archived.",
        "项目设置导入完成。": "Project settings import completed.",
        "设置导入未能完成，请查看已归档会话中的结果。": "Project settings import stopped; review its archived session.",
        "会话归档失败。": "Session archive failed.",
        " 会话归档失败。": " Session archive failed.",
        "项目终端已打开": "Project terminal opened",
        "没有找到可用的终端程序": "No terminal application is available",
        "请先保存有效的项目目录。": "Save a valid project folder first.",
        "工作台操作失败。": "Workspace action failed.",
        "当前会话已更新。": "Session updated.",
        "权限请求处理失败。": "Failed to handle the access request.",
        "独立 Agent Runtime 不会自动执行外部 Hook。": "The standalone Agent Runtime does not run external Hooks automatically.",
        "会话所属项目当前未注册，无法安全打开。": "The session's project is not registered, so it cannot be opened safely.",
        "会话所属项目当前未注册，无法修改。": "The session's project is not registered, so it cannot be modified.",
        "批量操作一次只能选择同一项目的会话。": "A batch action can only include sessions from one project.",
        "该会话所属项目未加入 Studio 项目列表。": "The session's project is not registered in Studio.",
        "上下文压缩检查点": "Context compaction checkpoints",
        "检查点会随下一次会话恢复一并注入；完整工具证据仍保存在会话事件记录中。": "Checkpoints are restored with the next session; complete tool evidence remains in the session event log.",
        "已压缩 ": "Compacted ",
        " 条消息": " messages",
        "选择会话后可查看其压缩检查点。": "Select a session to view its compaction checkpoints.",
        "当前会话尚未压缩历史。": "This session has no compacted history.",
        "未发现 AGENTS.md 或规则文件。": "No AGENTS.md or rule files found.",
        "未发现可用 SKILL.md。": "No available SKILL.md found.",
        "未发现 hooks.json。": "No hooks.json found.",
        "云端 API 模型": "Cloud API model",
        "本地 llama.cpp 模型": "Local llama.cpp model",
        "工作台": "Workspace",
        "未命名对话": "Untitled chat",
        "返回主会话": "Back to parent session",
        "当前对话": "Current chat",
        "开始新的 DFT 对话": "Start a new DFT chat",
        "未选择项目": "No project selected",
        "查看当前 Plan": "View current Plan",
        "尚未生成 Plan": "No Plan yet",
        "工具操作": "Tool operation",
        " 项": " items",
        "思考过程": "Reasoning",
        "思考中…": "Thinking…",
        "思考": "Thinking",
        "执行中": "Running",
        "失败": "Failed",
        "完成": "Completed",
        "待批准": "Awaiting approval",
        " 条待发送": " queued",
        "Enter 立即引导 · Ctrl+Enter 排队": "Enter to steer · Ctrl+Enter to queue",
        "Enter 发送": "Enter to send",
        "编辑 Goal": "Edit Goal",
        "创建 Goal": "Create Goal",
        "创建 Plan": "Create Plan",
        "创建会话 Goal": "Create session Goal",
        "编辑会话 Goal": "Edit session Goal",
        "删除": "Delete",
        "更新 Plan": "Update Plan",
        "恢复会话": "Restore session",
        "归档会话": "Archive session",
        "多选": "Multi-select",
        " 轮": " turns",
        "历史会话": "Previous session",
        "记忆 · 技能": "Memories · Skills",
        "状态：": "Status: ",
        "日志：": "Log: ",
        "至少保留一个模型配置。": "At least one model configuration must remain.",
        "无法读取模型配置：": "Failed to read model configuration: ",
        "模型配置 JSON 格式无效。": "The model configuration JSON is invalid.",
        "模型配置不能为空。": "The model configuration cannot be empty.",
        "无法写入模型配置：": "Failed to write model configuration: ",
        "保存模型配置失败：": "Failed to save model configuration: ",
        "模型 ID 不能为空、不能包含控制字符，且长度不能超过 512 个字符。": "Model ID is required, cannot contain control characters, and must be at most 512 characters.",
        "模型名称和运行时不能为空。": "Model name and runtime are required.",
        "模型 API 地址必须以 http:// 或 https:// 开头。": "Model API URL must start with http:// or https://.",
        "上下文窗口必须在 2048 到 262144 之间。": "Context window must be between 2,048 and 262,144.",
        "有效上下文比例必须在 1 到 100 之间。": "Effective context percentage must be between 1 and 100.",
        "自动压缩阈值必须在 1024 到模型上下文窗口的 90% 之间。": "Auto-compaction threshold must be between 1,024 and 90% of the model context window.",
        "推理提供者必须是 legacy（本地）或 api（云端/API）。": "Inference provider must be legacy (local) or api (cloud/API).",
        "断联重试次数必须在 1 到 20 次之间。": "Reconnect attempts must be between 1 and 20.",
        "断联重试间隔必须在 0.1 到 60 秒之间。": "Reconnect interval must be between 0.1 and 60 seconds.",
        "temperature 必须在 0 到 2 之间。": "Temperature must be between 0 and 2.",
        "top_k 必须在 0 到 100000 之间。": "top_k must be between 0 and 100,000.",
        "top_p 和 min_p 必须在 0 到 1 之间。": "top_p and min_p must be between 0 and 1.",
        "重复惩罚必须在 1 到 2 之间。": "Repeat penalty must be between 1 and 2.",
        "重复窗口必须在 0 到 4096 之间。": "Repeat window must be between 0 and 4,096.",
        "DRY 防复读强度必须在 0 到 5 之间。": "DRY repetition-control strength must be between 0 and 5.",
        "presence/frequency penalty 必须在 -2 到 2 之间。": "Presence/frequency penalty must be between -2 and 2.",
        "历史记录容量必须在 1024 到 ": "History capacity must be between 1,024 and ",
        "云端模型需要设置 API 地址。": "Cloud models require an API URL.",
        "云端模型思考深度必须是 low、medium、high、xhigh、max 或 ultra。": "Cloud model reasoning effort must be low, medium, high, xhigh, max, or ultra.",
        "云端输出上限必须在 1 到上下文窗口之间。": "Cloud output limit must be between 1 and the context window.",
        "本地模型缺少所需的模型文件路径。": "The local model is missing its required model file path.",
        "推理模式必须是仅 GPU、CPU+GPU 或仅 CPU。": "Inference mode must be GPU-only, CPU+GPU, or CPU-only.",
        "GPU+CPU 自动分层模式需要设置有效的显存预算。": "GPU+CPU auto-layering requires a valid VRAM budget.",
        "旧版混合推理需要设置有效的 CPU 内存额度。": "Legacy mixed inference requires a valid CPU memory budget.",
        "仅 CPU 模式需要设置有效的 CPU 内存额度。": "CPU-only mode requires a valid CPU memory budget.",
        "输出上限必须在 1 到 ": "Output limit must be between 1 and ",
        "本地模型仅支持 llama_cpp。": "Local models use llama_cpp only.",
        "模型 ID 已存在：": "Model ID already exists: ",
        "项目目录不可用：": "Project directory is unavailable: ",
        "内置项目不能移除。": "Built-in projects cannot be removed.",
        "无法读取项目注册表：": "Failed to read project registry: ",
        "项目注册表 JSON 格式无效。": "The project registry JSON is invalid.",
        "无法写入项目注册表：": "Failed to write project registry: ",
        "保存项目注册表失败：": "Failed to save project registry: ",
        "只能打开当前项目中的受支持文本文件。": "Only supported text files inside the current project can be opened.",
        "无法确定文件位置：": "Unable to resolve file location: ",
        "无法读取文件：": "Unable to read file: ",
        "读取文件时发生错误：": "Error while reading file: ",
        "文件预览达到 2 MiB 上限": "File preview reached the 2 MiB limit",
        "文件包含超过 65,536 字节的单行": "File contains a line longer than 65,536 bytes",
        "文件超过 ": "File exceeds ",
        "；当前仅载入前 ": "; only the first ",
        " 行并以只读方式显示。": " lines were loaded as read-only.",
        "请先打开当前项目中的文件。": "Open a file in the current project first.",
        "当前文件只载入了部分内容，不能保存。": "This file was only partially loaded and cannot be saved.",
        "保存失败：": "Save failed: ",
        "本地模型配置不可用，已提供语言补全。": "Local model configuration is unavailable; language completion was provided.",
        "无法保存 capability 配置。": "Unable to save capability configuration.",
        "保存 capability 配置失败。": "Failed to save capability configuration.",
        "无法启动 Tcl 配置导入程序：": "Unable to start the Tcl configuration importer: ",
        "导入类别必须是 synthesis 或 dft。": "Import category must be synthesis or dft.",
        "请选择可读取的 Tcl 或 SDC 文件。": "Select a readable Tcl or SDC file.",
        "模型配置文件不可用。": "Model configuration file is unavailable.",
        "正在启动本地模型并分析 Tcl…": "Starting the local model and analyzing Tcl…",
        "已取消导入": "Import cancelled",
        "已从 Tcl 提取设置，请检查后保存": "Settings were extracted from Tcl. Review and save them.",
        "导入失败": "Import failed",
        "技能": "skill",
        "正在读取 ": "Reading ",
        " 技能": " skill",
        "技能读取失败 ": "Failed to read skill ",
        "正在搜索可用技能": "Searching available skills",
        "已列出可用技能": "Listed available skills",
        "正在运行 ": "Running ",
        "命令失败 ": "Command failed: ",
        "已运行 ": "Ran ",
        "Shell": "Shell",
        "已读取 ": "Read ",
        "项目文件": "project file",
        " · 第 ": " · line ",
        " 行起": " onward",
        "已检索 ": "Searched for ",
        "项目内容": "project content",
        "已查看图像": "Viewed image",
        "已编辑文件": "Edited file",
        "已创建文件": "Created file",
        "已运行命令": "Ran command",
        "已读取项目或文件": "Read project or file",
        "已执行 DFT 工具流程": "Ran DFT flow",
        "已检查项目配置": "Checked project settings",
        "已检查 DFT 配置": "Checked DFT configuration",
        "已分析 DFT 结果": "Analyzed DFT results",
        "已检查 DFT 问题": "Reviewed DFT failure",
        "已启动 DFT 流程": "Started DFT flow",
        "已运行 DFT 验证": "Ran DFT verification",
        "已等待 DFT 作业": "Waited for DFT job",
        "已检查 DFT 作业": "Checked DFT job",
        "已停止 DFT 作业": "Stopped DFT job",
        "已读取 DRC 报告": "Read DRC report",
        "已读取 ATPG 报告": "Read ATPG report",
        "已核验 DFT 指标": "Validated DFT criteria",
        "已交叉核验 DFT 结果": "Cross-checked DFT results",
        "已查看项目列表": "Viewed project list",
        "已查看项目设置": "Viewed project settings",
        "已查看 Studio 设置": "Viewed Studio settings",
        "已更新项目设置": "Updated project settings",
        "已更新模型设置": "Updated model settings",
        "已切换模型": "Switched model",
        "已更新工具设置": "Updated tool settings",
        "已检索项目记忆": "Searched project memory",
        "已保存项目记忆": "Saved project memory",
        "已保存运行报告": "Saved run report",
        "已启动子智能体": "Started subagent",
        "已等待子智能体": "Waited for subagent",
        "已查看子智能体": "Viewed subagents",
        "已向子智能体发送消息": "Messaged subagent",
        "已更新子智能体任务": "Updated subagent task",
        "已停止子智能体": "Stopped subagent",
        "已处理技能": "Handled skill",
        "已处理项目记忆": "Handled project memory",
        "已处理子智能体任务": "Handled subagent task",
        "已完成操作": "Completed operation",
        "文件内容已读取": "File contents read",
        "项目检索已完成": "Project search completed",
        "文件修改已完成": "File change completed",
        "DFT 流程结果已更新": "DFT flow result updated",
        "DFT 报告已分析": "DFT report analyzed",
        "工具调用 · ": "Tool call · ",
        "已调用工具": "Called tool",
        "无参数": "No arguments",
        "请求访问路径": "Path access request",
        "等待人工批准": "Awaiting approval",
        "运行中": "Running",
        "已完成": "Completed",
        "已中断": "Interrupted",
        "子任务": "Subtask",
        "独立复核": "Independent review",
        "问题诊断": "Issue diagnosis",
        "资料调查": "Research",
        "加载了技能": "Loaded skills",
        "搜索了技能": "Searched skills",
        "编辑了文件": "Edited files",
        "创建了文件": "Created files",
        "执行了命令": "Ran commands",
        "读取了项目或文件": "Read project or files",
        "执行了 DFT 工具": "Ran DFT tools",
        "调用了工具": "Called tools",
        "工作中": "Working",
        "正在处理…": "Working…",
        "已完成工作": "Work completed",
        "正在运行命令…": "Running command…",
        "正在读取项目或文件…": "Reading project or file…",
        "正在执行 DFT 工具…": "Running DFT tool…",
        "正在调用工具…": "Running tool…",
        "读取操作详情\n": "Read operation details\n",
        "调用参数\n": "Arguments\n",
        "调用参数：": "Arguments: ",
        "打开 %1 会话": "Open %1 session",
        "独立 Agent Runtime 不会自动执行外部 Hook": "The standalone Agent Runtime does not run external Hooks automatically",
        "正在重新连接": "Reconnecting"
    })

    function localizedUiText(chineseText) {
        const currentLanguage = languageSettings.language
        const chinese = String(chineseText)
        return currentLanguage === "en" ? (englishUiStrings[chinese] || chinese) : chinese
    }

    function localizedStoredMessage(message) {
        const currentLanguage = languageSettings.language
        const original = String(message || "")
        let sourcePrefix = ""
        let translatedPrefix = ""
        for (const chinese in englishUiStrings) {
            const english = String(englishUiStrings[chinese] || "")
            const source = currentLanguage === "en" ? chinese : english
            const target = currentLanguage === "en" ? english : chinese
            if (source.length > sourcePrefix.length && original.startsWith(source)) {
                sourcePrefix = source
                translatedPrefix = target
            }
        }
        let translated = sourcePrefix.length > 0 ? translatedPrefix + original.slice(sourcePrefix.length) : original
        for (const chinese in englishUiStrings) {
            if (!/^\s/.test(chinese))
                continue
            const english = String(englishUiStrings[chinese] || "")
            const source = currentLanguage === "en" ? chinese : english
            const target = currentLanguage === "en" ? english : chinese
            if (source.length > 0 && translated.indexOf(source) >= 0)
                translated = translated.split(source).join(target)
        }
        if (currentLanguage === "en")
            translated = translated.replace(/；当前仅载入前\s*(\d+)\s*行并以只读方式显示。/g,
                                            "; only the first $1 lines were loaded as read-only.")
        else
            translated = translated.replace(/; only the first\s*(\d+)\s*lines were loaded as read-only\./g,
                                            "；当前仅载入前 $1 行并以只读方式显示。")
        if (currentLanguage === "en")
            translated = translated.replace(/正在重新连接（(\d+)\/(\d+)）/g, "Reconnecting ($1/$2)")
        else
            translated = translated.replace(/Reconnecting \((\d+)\/(\d+)\)/g, "正在重新连接（$1/$2）")
        return translated
    }

    function localizedText(key) {
        if (languageSettings.language.length === 0)
            return String(key)
        return languageSettings.text(key)
    }

    function localizedStatus(status) {
        if (languageSettings.language.length === 0)
            return String(status)
        return languageSettings.statusText(status)
    }

    function localizedPhase(phase) {
        if (languageSettings.language.length === 0)
            return String(phase)
        return languageSettings.phaseText(phase)
    }

    function localizedCategory(category) {
        if (languageSettings.language.length === 0)
            return String(category)
        return languageSettings.categoryText(category)
    }

    function localizedCapabilityTitle(id, fallback) {
        if (languageSettings.language.length === 0)
            return String(fallback)
        return languageSettings.capabilityTitle(id, fallback)
    }

    function localizedCapabilityDescription(id, fallback) {
        if (languageSettings.language.length === 0)
            return String(fallback)
        return languageSettings.capabilityDescription(id, fallback)
    }

    function setModelSettingsMessage(chineseText, englishText) {
        modelSettingsMessageZh = String(chineseText || "")
        modelSettingsMessageEn = String(englishText || "")
    }

    Connections {
        target: languageSettings

        function onLanguageChanged() {
            window.projectSaveMessage = window.localizedStoredMessage(window.projectSaveMessage)
            window.terminalLaunchStatus = window.localizedStoredMessage(window.terminalLaunchStatus)
            window.projectImportToast = window.localizedStoredMessage(window.projectImportToast)
            window.codexWorkspaceMessage = window.localizedStoredMessage(window.codexWorkspaceMessage)
        }
    }

    function refreshProject() {
        currentProject = projectModel.currentProject()
        if (!projectGeneralDialog || !projectGeneralDialog.visible)
            projectRelatedDocumentsValue = currentProject.relatedDocuments || []
        selectedDftTool = String(window.executionValue("dft_tool", "testmax")).toLowerCase() === "tessent"
            ? "tessent" : "testmax"
        tessentDofile = String(window.executionValue("tessent_dofile", ""))
        const savedPermission = String(window.executionValue("agent_permission_mode", "approval")).toLowerCase()
        agentPermissionMode = ["full_access", "full", "danger-full-access"].indexOf(savedPermission) >= 0
            ? "full_access"
            : savedPermission === "autonomous" ? "autonomous" : "approval"
        terminalLaunchStatus = ""
        const projectId = String(currentProject.id || "")
        const projectChanged = codexSessionProjectId !== projectId
        if (projectId.length > 0)
            languageSettings.selectedProjectId = projectId
        Qt.callLater(syncProjectTypeBox)
        Qt.callLater(syncWorkspaceSettings)
        Qt.callLater(syncSynthesisModels)
        if (projectChanged) {
            codexSessionProjectId = projectId
            clearCodexSessionState()
            // Session records are scoped by both project id and root.  Do not
            // leave the previous project's transcript visible while the
            // catalog for the new project is being loaded.
            if (selectedPage === 3)
                Qt.callLater(refreshCodexWorkspace)
        }
    }

    function codexProjectContext(startNewSession, projectOverride) {
        const context = ({})
        const sourceProject = projectOverride || currentProject
        for (const key in sourceProject)
            context[key] = sourceProject[key]
        // Give the Agent explicit, host-owned catalog locations so it can
        // inspect and update Studio configuration through dedicated tools
        // instead of guessing paths from the project workspace.
        context.platformProjectsPath = String(projectModel.storagePath || "")
        context.platformModelsPath = String(modelCatalog.storagePath || "")
        context.platformCapabilitiesPath = String(capabilityModel.storagePath || "")
        const catalogModel = window.activeModel()
        const localModelSelected = String(catalogModel.provider || "") === "legacy"
        const model = localModelSelected ? catalogModel : window.codexApiModel()
        context.modelName = String(localModelSelected ? model.modelId : (window.codexChatModel || model.modelId || ""))
        context.modelProvider = localModelSelected ? "legacy" : "api"
        context.modelApiBase = String(model.apiBase || "")
        context.modelApiKeyFile = String(model.apiKeyFile || "")
        context.modelApiKey = String(model.apiKey || "")
        context.modelBasePath = String(model.baseModelPath || "")
        context.modelAdapterPath = String(model.adapterPath || "")
        context.modelLlamaServerPath = String(model.llamaServerPath || "")
        context.modelInferenceMode = String(model.inferenceMode || "cpu_gpu")
        context.modelGpuMemoryGiB = Number(model.gpuMemoryGiB || 0)
        context.modelReasoningEffort = String(window.codexChatReasoningEffort || model.reasoningEffort || "medium")
        context.multiAgentEnabled = Boolean(languageSettings.chatMultiAgentEnabled)
        context.modelReconnectMaxAttempts = Math.max(1, Math.min(20, Number(model.reconnectMaxAttempts || 10)))
        context.modelReconnectDelaySeconds = Math.max(0.1, Math.min(60, Number(model.reconnectDelaySeconds || 1.0)))
        context.modelRuntime = localModelSelected ? "llama_cpp" : "openai_compatible"
        // Pass the same allocation that is visible in Model settings to the
        // direct Responses runtime.  If Chat selected a discovered provider
        // model, prefer its advertised context over a stale catalog value.
        let selectedContext = Number(modelSettingsContextField.text || model.contextWindow || 65536)
        const selectedRemote = localModelSelected ? null : (agentController.codexModels || []).find(function(candidate) {
            return String(candidate.id || "") === String(window.codexChatModel || "")
        })
        const remoteAllocation = localModelSelected ? ({}) : window.apiModelContextWindowInfo(window.codexChatModel)
        if (selectedRemote && Number(selectedRemote.contextWindow || 0) >= 2048)
            selectedContext = Number(selectedRemote.contextWindow)
        context.modelContextWindow = Math.max(2048, Math.min(262144, Math.floor(selectedContext)))
        const isSelectedDeployment = !localModelSelected && String(window.codexChatModel || "") !== ""
            && String(window.codexChatModel || "") !== String(model.modelId || "")
        context.modelEffectiveContextWindowPercent = Math.max(1, Math.min(100, Number(
            isSelectedDeployment ? remoteAllocation.effectivePercent
                : modelSettingsEffectivePercentField.text || remoteAllocation.effectivePercent || model.effectiveContextWindowPercent || 95)))
        context.modelAutoCompactTokenLimit = Math.max(1024, Math.min(Math.floor(context.modelContextWindow * 0.9), Number(
            isSelectedDeployment ? remoteAllocation.autoCompactTokenLimit
                : modelSettingsAutoCompactField.text || remoteAllocation.autoCompactTokenLimit || model.autoCompactTokenLimit || Math.floor(context.modelContextWindow * 0.9))))
        const effectiveWindow = context.modelContextWindow * context.modelEffectiveContextWindowPercent / 100
        const runtimeReserve = Math.min(4096, Math.max(512, Math.floor(effectiveWindow * 0.05)))
        const derivedOutput = Math.max(1024, Math.min(16384, Math.floor(effectiveWindow * 0.25)))
        const derivedHistory = Math.max(1024, context.modelAutoCompactTokenLimit - runtimeReserve - derivedOutput)
        context.modelInputContextTokens = Math.max(1024, Number(
            isSelectedDeployment ? derivedHistory : modelSettingsHistoryField.text || model.inputContextTokens || derivedHistory))
        context.modelMaximumNewTokens = Math.max(1, Number(
            isSelectedDeployment ? derivedOutput : modelSettingsOutputField.text || model.maximumNewTokens || derivedOutput))
        context.modelTemperature = Number(modelSettingsTemperatureField.text || model.temperature || 0.6)
        context.modelTopK = Math.max(0, Number(modelSettingsTopKField.text || model.topK || 40))
        context.modelTopP = Number(modelSettingsTopPField.text || model.topP || 0.9)
        context.modelMinP = Number(modelSettingsMinPField.text || model.minP || 0.05)
        context.modelRepeatPenalty = Number(modelSettingsRepeatPenaltyField.text || model.repeatPenalty || 1.1)
        context.modelRepeatLastN = Math.max(0, Number(modelSettingsRepeatLastNField.text || model.repeatLastN || 256))
        context.modelDryMultiplier = Math.max(0, Number(modelSettingsDryMultiplierField.text || model.dryMultiplier || 0.5))
        context.modelPresencePenalty = Number(modelSettingsPresencePenaltyField.text || model.presencePenalty || 0)
        context.modelFrequencyPenalty = Number(modelSettingsFrequencyPenaltyField.text || model.frequencyPenalty || 0)
        context.agentPermissionMode = window.agentPermissionMode
        context.relatedDocuments = sourceProject.relatedDocuments || []
        // A stale QML selection must never be sent to another project.  The
        // runtime rejects cross-project ids, but dropping it here also avoids
        // a race while the project catalog is refreshing.
        if (codexSessionId.length > 0 && codexSessionProjectId === String(currentProject.id || ""))
            context.codexThreadId = codexSessionId
        if (startNewSession) {
            context.startNewCodexSession = true
            // A project launch is an execution request, not a conversational turn.
            context.agentRun = true
            context.executionMode = "autonomous_run"
            context.agentInteractionMode = "goal"
        }
        return context
    }

    function projectRelatedDocumentPaths() {
        const paths = []
        for (let index = 0; index < projectRelatedDocumentsModel.count; ++index) {
            const path = String(projectRelatedDocumentsModel.get(index).path || "").trim()
            if (path.length > 0 && paths.indexOf(path) < 0)
                paths.push(path)
        }
        return paths
    }

    function showProjectImportToast(message, systemNotification) {
        projectImportToast = String(message || "")
        projectImportToastTimer.restart()
        if (systemNotification)
            agentController.showDesktopNotification("DFT Agent Studio", projectImportToast)
    }

    function startProjectSettingsImport() {
        const project = Object.assign({}, currentProject)
        if (projectImportRunning)
            return
        if (!project.id || !project.root) {
            showProjectImportToast(languageSettings.language === "en" ? "Save a project folder first." : "请先保存项目目录。", false)
            return
        }
        const context = codexProjectContext(true, project)
        delete context.codexThreadId
        context.goal = "读取当前项目的配置脚本、配置文件以及项目相关文档，基于实际内容完善 DFT Agent Studio 的项目设置。先用 read_file/search_project_text 等工具检查已配置路径、文件清单、Tcl/SDC 脚本和 relatedDocuments；必要时定位项目中的配置脚本和设计说明。将能够可靠判断的顶层模块、RTL 根目录、源文件/文件清单、约束文件、时钟/复位、流程模块、工艺库与 DFT/ATPG 参数通过 update_studio_project 的结构化 changes 更新。不要猜测或覆盖无法从证据确认的值；未知设置保留原样，并在最终回复中列出已读取证据、实际修改字段和仍需人工确认的项。不要运行综合/DFT 作业，只做资料读取与设置导入。"
        context.agentRun = true
        context.executionMode = "autonomous_run"
        context.agentInteractionMode = "goal"
        context.startNewCodexSession = true
        context.agentPermissionMode = "autonomous"
        projectImportProjectId = String(project.id)
        projectImportProject = project
        projectImportSessionId = ""
        projectImportRunning = true
        showProjectImportToast(languageSettings.language === "en"
            ? "AI import is running in the background. Find its session in Archived."
            : "AI 正在后台导入设置；需要查看过程可到归档会话中打开。", false)
        agentController.runInBackground(context, capabilityModel.disabledIds())
    }

    function codexWorkspaceAction(action, payload, projectOverride) {
        const project = projectOverride || currentProject
        if (!project.id || !project.root) {
            codexWorkspaceMessage = "请先保存有效的项目目录。"
            return ({ ok: false })
        }
        const response = agentController.workspaceAction(project, action, payload || ({}))
        if (!response.ok) {
            codexWorkspaceMessage = String(response.message || "工作台操作失败。")
            return response
        }
        applyCodexWorkspaceResult(action, response.result || ({}), String(project.id || ""))
        return response
    }

    function applyCodexWorkspaceResult(action, result, projectId) {
        if (result.sessions !== undefined) {
            if (action === "sessions_all")
                flowSessionSummaries = window.preserveLiveSessionSummaries(result.sessions, flowSessionSummaries)
            else if (!projectId || projectId === String(currentProject.id || ""))
                codexSessions = window.preserveLiveSessionSummaries(result.sessions, codexSessions)
        }
        if (result.children !== undefined)
            codexSubagentSessions = result.children
        if (result.session !== undefined) {
            const resultSessionId = String(result.session.id || "")
            if (action === "session_new" || resultSessionId === codexSessionId) {
                const session = result.session
                applyCodexSessionState(session)
                codexWorkspaceMessage = "当前会话已更新。"
            }
        }
        if (result.memories !== undefined || result.rules !== undefined || result.skills !== undefined)
            codexWorkspaceCatalog = result
        if (result.selection !== undefined)
            codexWorkspaceCatalog = Object.assign({}, codexWorkspaceCatalog, { selection: result.selection })
    }

    function decidePathPermission(entry, choice) {
        const requestId = String((entry || ({})).requestId || "")
        if (!requestId || !codexSessionId)
            return
        codexWorkspaceActionAsync("path_permission_decide", ({
            thread_id: codexSessionId,
            request_id: requestId,
            choice: String(choice || "deny")
        }), function(response) {
            if (!response.ok) {
                codexWorkspaceMessage = String(response.message || "权限请求处理失败。")
                return
            }
            const result = response.result || ({})
            const status = String(result.status || "")
            agentController.updatePathPermissionEntry(requestId, status)
            if (Boolean(result.resume_required)) {
                pendingChatPrompt = "已批准访问路径：" + String(result.path || "")
                    + "。请从上次未完成的步骤继续，重新尝试相关文件读取/搜索，然后继续完成原任务。"
                submitAgentPrompt(false)
            }
        })
    }

    function decideNativeTool(entry, approved) {
        const requestId = String((entry || ({})).requestId || "")
        if (!requestId)
            return
        const response = agentController.decideNativeTool(requestId, Boolean(approved))
        if (!response.ok) {
            codexWorkspaceMessage = String(response.message || "工具审批处理失败。")
            return
        }
        codexWorkspaceMessage = approved ? "已批准工具操作。" : "已拒绝工具操作。"
    }

    function codexWorkspaceActionAsync(action, payload, callback, projectOverride) {
        const project = projectOverride || currentProject
        if (!project.id || !project.root) {
            codexWorkspaceMessage = "请先保存有效的项目目录。"
            if (typeof callback === "function")
                callback(({ ok: false, message: codexWorkspaceMessage }))
            return ""
        }
        const requestId = String(++workspaceActionRequestSerial)
        const callbacks = Object.assign({}, workspaceActionCallbacks)
        callbacks[requestId] = ({
            action: String(action || ""),
            projectId: String(project.id || ""),
            callback: typeof callback === "function" ? callback : null
        })
        workspaceActionCallbacks = callbacks
        agentController.workspaceActionAsync(requestId, project, action, payload || ({}))
        return requestId
    }

    function applyCodexSessionState(session) {
        const value = session || ({})
        codexSessionId = String(value.id || codexSessionId)
        codexSessionProjectId = String(value.project_id || codexSessionProjectId)
        codexSessionParentThreadId = String(value.parent_thread_id || "")
        agentController.setActivitySession(codexSessionId)
        codexSessionGoal = String(value.goal || "")
        codexSessionGoalStatus = String(value.goal_status || "inactive")
        codexSessionPlan = value.latest_plan === undefined ? [] : value.latest_plan
        codexSessionPlanUpdatedAt = String(value.plan_updated_at || "")
        codexSessionRecoveryPending = Boolean(value.recovery_pending)
        codexSessionRecoveryGoal = String(value.recovery_goal || "")
        codexSessionRecoveryReason = String(value.recovery_reason || "")
        codexSessionRecoveryUpdatedAt = String(value.recovery_updated_at || "")
    }

    // Keep every session-read path on the same bounded, lazy-loading contract.
    // This prevents a Goal/Plan edit or a session switch from silently
    // replacing the current pagination cursor with an unbounded transcript.
    function hydrateCodexSessionRead(result, prepend) {
        const value = result || ({})
        const preserveExpandedHistory = !Boolean(prepend)
            && codexActivityHistoryExpanded
            && agentController.activityEntries.length > 0
        const replacingCurrentPage = !Boolean(prepend) && agentController.activityEntries.length > 0
        const prependingPage = Boolean(prepend) && agentController.activityEntries.length > 0
        const oldActivityCount = agentController.activityEntries.length
        const oldContentY = replacingCurrentPage ? Number(codexChatActivityList.contentY || 0) : 0
        const oldMinimum = replacingCurrentPage ? Number(codexChatActivityList.originY || 0) : 0
        const oldMaximum = replacingCurrentPage
            ? Math.max(oldMinimum, oldMinimum + codexChatActivityList.contentHeight - codexChatActivityList.height)
            : 0
        const wasAtEnd = replacingCurrentPage && oldContentY >= oldMaximum - 24
        if (replacingCurrentPage)
            codexActivityRefreshing = true
        if (!Boolean(prepend)) {
            applyCodexSessionState(value.session || ({}))
            if (!preserveExpandedHistory)
                codexActivityMeta = value.activity_meta || ({ has_more: false, next_before: 0, total_count: 0 })
        }
        if (!preserveExpandedHistory)
            agentController.loadSessionActivity(value.activity || [], Boolean(prepend))
        if (prependingPage) {
            codexActivityScrollLocked = true
            codexActivityPrependSourceShift = Math.max(0,
                agentController.activityEntries.length - oldActivityCount)
            codexActivityPrependStableFrames = 0
            codexActivityPrependSettlePasses = 0
            codexActivityPrependExpectedEntries = agentController.activityEntries.length
            // Apply the batched prepend immediately after receiving the page.
            // Waiting for the streaming coalescer leaves a painted frame with
            // shifted history before scroll correction can run.
            codexChatDisplayRebuildTimer.stop()
            codexChatDisplayRebuildScheduled = false
            rebuildCodexChatDisplayEntries()
            codexActivityPrependSettleTimer.start()
        }
        if (replacingCurrentPage) {
            codexActivityRefreshing = false
            Qt.callLater(function() {
                if (wasAtEnd) {
                    codexChatActivityList.positionViewAtEnd()
                    return
                }
                const minimum = Number(codexChatActivityList.originY || 0)
                const maximum = Math.max(minimum, minimum
                    + codexChatActivityList.contentHeight - codexChatActivityList.height)
                codexChatActivityList.contentY = Math.max(minimum, Math.min(maximum, oldContentY))
            })
        }
    }

    function settleCodexActivityPrepend() {
        if (!codexActivityPrepending)
            return
        ++codexActivityPrependSettlePasses
        if (codexActivityPrependSettlePasses > 30) {
            codexActivityPrependSettleTimer.stop()
            codexActivityPrepending = false
            codexActivityScrollLocked = false
            codexActivityPrependExpectedEntries = 0
            codexActivityPrependAnchorIndex = -1
            codexActivityPrependAnchorDisplayCount = 0
            codexActivityPrependStableFrames = 0
            codexActivityPrependSettlePasses = 0
            codexActivityPrependSourceShift = 0
            return
        }
        if (agentController.activityEntries.length < codexActivityPrependExpectedEntries) {
            codexActivityPrependSettleTimer.start()
            return
        }
        const list = codexChatActivityList
        const insertedRows = Math.max(0, codexChatDisplayModel.count - codexActivityPrependAnchorDisplayCount)
        const anchorIndex = codexActivityPrependAnchorIndex < 0
            ? -1 : codexActivityPrependAnchorIndex + insertedRows
        const anchor = anchorIndex >= 0 ? list.itemAtIndex(anchorIndex) : null
        if (anchor) {
            const actualY = anchor.mapToItem(list, 0, 0).y
            const delta = actualY - codexActivityPrependAnchorViewportY
            if (Math.abs(delta) > 0.5) {
                const minimum = Number(list.originY || 0)
                const maximum = Math.max(minimum, minimum + list.contentHeight - list.height)
                const target = Math.max(minimum, Math.min(maximum, Number(list.contentY || 0) + delta))
                if (chatWheelAnimator.scrolling)
                    chatWheelAnimator.cancel()
                list.contentY = target
                codexActivityPrependStableFrames = 0
                codexActivityPrependSettleTimer.start()
                return
            }
        } else {
            // Virtualized rows are not guaranteed to be instantiated after a
            // prepend. Preserve the approximate viewport using the added
            // height instead of waiting forever on a missing delegate.
            const currentHeight = Number(list.contentHeight || 0)
            const insertedHeight = Math.max(0, currentHeight - codexActivityPrependAnchorHeight)
            const minimum = Number(list.originY || 0)
            const maximum = Math.max(minimum, minimum + currentHeight - list.height)
            const target = Math.max(minimum, Math.min(maximum, codexActivityPrependAnchorY + insertedHeight))
            if (chatWheelAnimator.scrolling)
                chatWheelAnimator.cancel()
            list.contentY = target
        }
        ++codexActivityPrependStableFrames
        if (codexActivityPrependStableFrames < 3) {
            codexActivityPrependSettleTimer.start()
            return
        }
        codexActivityPrependSettleTimer.stop()
        codexActivityPrepending = false
        codexActivityScrollLocked = false
        codexActivityPrependExpectedEntries = 0
        codexActivityPrependAnchorIndex = -1
        codexActivityPrependAnchorDisplayCount = 0
        codexActivityPrependStableFrames = 0
        codexActivityPrependSettlePasses = 0
        codexActivityPrependSourceShift = 0
    }

    function clearCodexSessionState() {
        resetCodexSessionViewport()
        codexSessionLoading = false
        codexSessionId = ""
        agentController.setActivitySession("")
        codexSessionGoal = ""
        codexSessionGoalStatus = "inactive"
        codexSessionPlan = []
        codexSessionPlanUpdatedAt = ""
        codexSessionRecoveryPending = false
        codexSessionRecoveryGoal = ""
        codexSessionRecoveryReason = ""
        codexSessionRecoveryUpdatedAt = ""
        codexActivityMeta = ({ has_more: false, next_before: 0, total_count: 0 })
        codexActivityHistoryExpanded = false
        codexActivityPrepending = false
        codexActivityScrollLocked = false
        codexActivityLoadScheduled = false
        codexActivityPrependExpectedEntries = 0
        codexExpandedThinkingKey = ""
        codexExpandedActivityGroupKey = ""
    }

    // A session switch is a replacement, never a prepend.  Reset the
    // viewport/model bookkeeping before hydrating the target thread so an
    // in-flight older-page load cannot merge the two transcripts.
    function resetCodexSessionViewport() {
        codexActivityPrepending = false
        codexActivityScrollLocked = false
        codexActivityMeta = ({ has_more: false, next_before: 0, total_count: 0 })
        codexActivityRefreshing = false
        codexActivityLoadScheduled = false
        codexActivityPrependExpectedEntries = 0
        codexActivityPrependSourceShift = 0
        codexActivityPrependStableFrames = 0
        codexActivityPrependSettlePasses = 0
        codexActivityPrependLastHeight = -1
        codexActivityPrependAnchorIndex = -1
        codexActivityPrependAnchorDisplayCount = 0
        codexActivityPrependAnchorViewportY = 0
        codexActivityPrependSettleTimer.stop()
        codexExpandedThinkingKey = ""
        codexExpandedActivityGroupKey = ""
        if (chatWheelAnimator !== null && chatWheelAnimator.scrolling)
            chatWheelAnimator.cancel()
        if (codexChatDisplayModel !== null)
            codexChatDisplayModel.sync([])
        codexChatDisplayEntries = []
        codexChatRawFingerprints = []
        if (codexChatActivityList !== null) {
            codexChatActivityList.contentY = 0
        }
    }

    function thinkingEntryKey(entry) {
        const value = entry || ({})
        const stable = String(value.streamId || value.model_stream_id || "").trim()
        const source = String(value.sourceIndex === undefined ? "" : value.sourceIndex)
        return String(codexSessionId || "") + "|" + (stable.length > 0 ? stable : source)
    }

    function toggleThinkingEntry(entry) {
        const key = thinkingEntryKey(entry)
        codexExpandedThinkingKey = codexExpandedThinkingKey === key ? "" : key
    }

    function activityGroupKey(entry) {
        const value = entry || ({})
        const children = value.children || []
        // A group normally contains a thinking stream.  That stream id is
        // stable while its text grows, unlike the source index of a page.
        for (let index = 0; index < children.length; ++index) {
            const child = children[index] || ({})
            const stream = String(child.streamId || child.model_stream_id || "").trim()
            if (stream.length > 0)
                return String(codexSessionId || "") + "|group|" + stream
        }
        const groupId = String(value.groupId || "").trim()
        const source = String(value.sourceIndex === undefined ? "" : value.sourceIndex)
        return String(codexSessionId || "") + "|group|" + (groupId.length > 0 ? groupId : source)
    }

    function toggleActivityGroup(entry) {
        const key = activityGroupKey(entry)
        codexExpandedActivityGroupKey = codexExpandedActivityGroupKey === key ? "" : key
    }

    function resumeCodexSession() {
        if (!codexSessionRecoveryPending || codexSessionId.length === 0 || agentController.running)
            return
        if (!persistEditor())
            return
        const recoveryGoal = codexSessionRecoveryGoal.trim()
        if (recoveryGoal.length === 0)
            return
        const context = window.codexProjectContext(false)
        context.resumeAgentTurn = true
        context.codexThreadId = codexSessionId
        context.agentRun = true
        context.executionMode = "autonomous_run"
        agentConversationRequested = true
        selectedPage = 3
        agentController.runPrompt(context, recoveryGoal, capabilityModel.disabledIds())
    }

    function ensureCodexChatSession() {
        if (codexSessionId.length > 0)
            return true
        const response = codexWorkspaceAction("session_new", ({ name: "新建 DFT 对话" }))
        if (!response.ok)
            return false
        agentController.loadSessionActivity([])
        return codexSessionId.length > 0
    }

    function openCodexGoalEditor() {
        if (!ensureCodexChatSession())
            return
        codexGoalEditor.text = codexSessionGoal
        codexGoalDialog.open()
        codexGoalEditor.forceActiveFocus()
    }

    function codexPlanText() {
        if (typeof codexSessionPlan === "string")
            return codexSessionPlan
        if (codexSessionPlan === undefined || codexSessionPlan === null)
            return ""
        if (Array.isArray(codexSessionPlan) && codexSessionPlan.length === 0)
            return ""
        return JSON.stringify(codexSessionPlan, null, 2)
    }

    function requestCodexPlan() {
        if (!ensureCodexChatSession())
            return
        const context = codexProjectContext(false)
        context.codexInteractionMode = "plan"
        codexPlanRequestPending = true
        agentConversationRequested = true
        selectedPage = 3
        agentController.runPrompt(
            context,
            "请为当前会话目标创建或更新可执行 Plan。若尚未设置独立 Goal，请以当前项目目标和本回合请求为准。只规划，不执行工具或修改文件。",
            capabilityModel.allIds()
        )
    }

    function refreshCodexWorkspace() {
        if (codexWorkspaceRefreshing)
            return
        codexWorkspaceRefreshing = true
        refreshFlowSessionList()
        const projectId = String(currentProject.id || "")
        codexWorkspaceActionAsync("sessions", ({}), function(sessionsResponse) {
            codexWorkspaceRefreshing = false
            if (projectId !== String(currentProject.id || "")) {
                Qt.callLater(refreshCodexWorkspace)
                return
            }
            if (!sessionsResponse || !sessionsResponse.ok)
                return

            // Resolve the current transcript from the already cached session
            // summaries, then load just its bounded first page in the worker.
            let selectedSession = null
            for (const session of (codexSessions || [])) {
                if (String(session.project_id || "") !== projectId || Boolean(session.archived))
                    continue
                if (String(session.id || "") === codexSessionId) {
                    selectedSession = session
                    break
                }
                if (!selectedSession)
                    selectedSession = session
            }
            const selectedId = selectedSession ? String(selectedSession.id || "") : ""
            if (selectedId !== codexSessionId) {
                resetCodexSessionViewport()
                codexSessionId = selectedId
                codexSessionProjectId = projectId
                agentController.setActivitySession(selectedId)
                codexActivityHistoryExpanded = false
            }
            if (selectedId.length > 0) {
                codexWorkspaceActionAsync("session_read", ({
                    thread_id: selectedId,
                    activity_limit: codexActivityPageLimit
                }), function(sessionResponse) {
                    if (sessionResponse && sessionResponse.ok
                            && String(codexSessionId || "") === selectedId)
                        hydrateCodexSessionRead(sessionResponse.result || ({}), false)
                })
            } else {
                clearCodexSessionState()
                agentController.loadSessionActivity([])
            }
            codexWorkspaceActionAsync("catalog", ({ thread_id: codexSessionId }), function(response) {
                if (response && response.ok && projectId === String(currentProject.id || ""))
                    codexWorkspaceMessage = ""
            })
        })
        if (!projectId) {
            codexWorkspaceRefreshing = false
        }
    }

    function refreshCodexWorkspaceCatalog() {
        const projectId = String(currentProject.id || "")
        if (!projectId || !currentProject.root)
            return
        codexWorkspaceActionAsync("catalog", ({ thread_id: "" }), function(response) {
            if (response && response.ok && projectId === String(currentProject.id || ""))
                codexWorkspaceCatalog = response.result || ({})
        })
    }

    function filteredCodexSessions() {
        const query = codexSessionSearch.trim().toLowerCase()
        const sessions = (flowSessionSummaries || []).filter(function(session) {
            if (codexSessionProjectFilter !== "*"
                    && String(session.project_id || "") !== codexSessionProjectFilter)
                return false
            if (codexSessionArchiveFilter === "active" && Boolean(session.archived))
                return false
            if (codexSessionArchiveFilter === "archived" && !Boolean(session.archived))
                return false
            if (query.length === 0)
                return true
            return String(session.name || "").toLowerCase().indexOf(query) >= 0
                || String(session.preview || "").toLowerCase().indexOf(query) >= 0
                || String(session.updated_at || "").toLowerCase().indexOf(query) >= 0
        })
        sessions.sort(function(left, right) {
            const byTime = String(right.updated_at || "").localeCompare(String(left.updated_at || ""))
            return byTime !== 0 ? byTime : String(left.id || "").localeCompare(String(right.id || ""))
        })
        return sessions
    }

    function codexSessionProjectOptions() {
        const options = [{ value: "*", label: languageSettings.language === "en" ? "All projects" : "全部项目" }]
        const projects = ({})
        for (const session of flowSessionSummaries || []) {
            const id = String(session.project_id || "").trim()
            if (id.length > 0)
                projects[id] = window.projectNameForCodexSession(session)
        }
        const ids = Object.keys(projects).sort(function(a, b) { return projects[a].localeCompare(projects[b]) })
        for (const id of ids)
            options.push({ value: id, label: projects[id] })
        return options
    }

    function createCodexChatSession() {
        const response = codexWorkspaceAction("session_new", ({ name: "新建 DFT 对话" }))
        if (!response.ok)
            return
        codexSessionLoading = false
        resetCodexSessionViewport()
        codexActivityHistoryExpanded = false
        applyCodexSessionState(((response.result || ({})).session || ({})))
        agentController.loadSessionActivity([])
        refreshCodexWorkspace()
    }

    function importCodexSession(path) {
        const response = codexWorkspaceAction("session_import", ({ path: path }))
        if (!response.ok)
            return
        resetCodexSessionViewport()
        codexActivityHistoryExpanded = false
        const result = response.result || ({})
        hydrateCodexSessionRead(result, false)
        codexSessionDialog.close()
        selectedPage = 3
        refreshCodexWorkspace()
    }

    function exportCodexSession(threadId, path) {
        const session = findCodexSession(threadId)
        const project = projectForCodexSession(session)
        const response = codexWorkspaceAction("session_export", ({
            thread_id: String(threadId || codexSessionId),
            path: path
        }), project)
        if (response.ok)
            codexWorkspaceMessage = "会话已导出：" + String((response.result || ({})).path || path)
    }

    function selectCodexChatSession(threadId, projectId) {
        const requestedId = String(threadId || "").trim()
        const session = findCodexSession(requestedId)
        const ownerProjectId = String(projectId || (session || ({})).project_id || "")
        const projectIndex = projectModel.indexOfProjectId(ownerProjectId)
        const project = projectIndex >= 0 ? projectModel.projectAt(projectIndex) : ({})
        if (requestedId.length === 0 || !session || !project.id || !project.root) {
            codexWorkspaceMessage = "会话所属项目当前未注册，无法安全打开。"
            return
        }
        if (projectIndex >= 0 && projectModel.currentIndex !== projectIndex)
            projectModel.currentIndex = projectIndex
        if (requestedId === codexSessionId && ownerProjectId === String(currentProject.id || ""))
            return
        resetCodexSessionViewport()
        codexSessionLoading = true
        codexSessionId = requestedId
        codexSessionProjectId = ownerProjectId
        agentController.setActivitySession(requestedId)
        agentController.setFlowDisplaySession(requestedId)
        codexActivityHistoryExpanded = false
        agentController.loadSessionActivity([])
        codexWorkspaceActionAsync("session_read", ({
            thread_id: requestedId,
            activity_limit: codexActivityPageLimit
        }), function(response) {
            if (String(codexSessionId || "") !== requestedId)
                return
            codexSessionLoading = false
            if (!response || !response.ok) {
                refreshCodexWorkspace()
                return
            }
            hydrateCodexSessionRead(response.result || ({}), false)
        }, project)
        selectedPage = 3
    }

    function loadOlderCodexActivity() {
        const cursor = Number(codexActivityMeta.next_before || 0)
        if (codexActivityPrepending)
            return
        if (!codexSessionId || !Boolean(codexActivityMeta.has_more) || cursor <= 0) {
            codexActivityScrollLocked = false
            return
        }
        // Keep this flag set to suppress duplicate page requests; scrolling
        // remains available until the response is ready to be prepended.
        codexActivityPrepending = true
        if (chatWheelAnimator.scrolling)
            chatWheelAnimator.cancel()
        loadOlderCodexActivityPage(String(codexSessionId), cursor, 0)
    }

    function captureCodexActivityPrependAnchor() {
        const list = codexChatActivityList
        codexActivityPrependAnchorDisplayCount = codexChatDisplayModel.count
        codexActivityPrependAnchorIndex = -1
        if (!list)
            return
        codexActivityPrependAnchorY = Number(list.contentY || 0)
        codexActivityPrependAnchorHeight = Number(list.contentHeight || 0)
        codexActivityPrependAnchorViewportY = 0
        if (codexChatDisplayModel.count === 0)
            return
        let index = list.indexAt(list.width * 0.5, list.height * 0.5)
        if (index < 0) {
            for (let y = 12; y < list.height; y += 12) {
                index = list.indexAt(list.width * 0.5, y)
                if (index >= 0)
                    break
            }
        }
        if (index < 0)
            return
        const item = list.itemAtIndex(index)
        if (!item)
            return
        codexActivityPrependAnchorIndex = index
        codexActivityPrependAnchorViewportY = item.mapToItem(list, 0, 0).y
        codexActivityPrependAnchorY = Number(list.contentY || 0)
        codexActivityPrependAnchorHeight = Number(list.contentHeight || 0)
    }

    function loadOlderCodexActivityPage(sessionId, cursor, emptyPages) {
        codexWorkspaceActionAsync("session_read", ({
            thread_id: sessionId,
            activity_limit: codexActivityPageLimit,
            activity_before: cursor
        }), function(response) {
            if (String(codexSessionId || "") !== sessionId) {
                codexActivityPrepending = false
                codexActivityScrollLocked = false
                return
            }
            if (!response || !response.ok) {
                codexActivityPrepending = false
                codexActivityScrollLocked = false
                return
            }
            const result = response.result || ({})
        let pageMeta = result.activity_meta || ({})
        let nextCursor = Number(pageMeta.next_before || 0)
        let activity = result.activity || []
        let hasOlder = Boolean(pageMeta.has_more)
            && nextCursor > 0
            && nextCursor < cursor
            // Skip bookkeeping-only pages without blocking the GUI thread.
            if (activity.length === 0 && hasOlder && emptyPages < 12) {
                loadOlderCodexActivityPage(sessionId, nextCursor, emptyPages + 1)
                return
            }
            if (activity.length > 0)
                captureCodexActivityPrependAnchor()
            if (activity.length > 0)
                hydrateCodexSessionRead(result, true)
            if (activity.length > 0)
                codexActivityHistoryExpanded = true
            codexActivityMeta = {
                has_more: hasOlder,
                next_before: hasOlder ? nextCursor : 0,
                total_count: Number(pageMeta.total_count || 0),
                loaded_count: Number(pageMeta.loaded_count || 0)
            }
            if (activity.length === 0) {
                codexActivityPrepending = false
                codexActivityScrollLocked = false
            }
        })
    }

    function scheduleOlderCodexActivityLoad() {
        if (codexActivityLoadScheduled || codexActivityPrepending
                || !Boolean(codexActivityMeta.has_more)
                || Number(codexActivityMeta.next_before || 0) <= 0)
            return
        // The scheduled flag prevents duplicate requests; keep the viewport
        // interactive while disk/session data is being fetched.
        codexActivityLoadScheduled = true
        Qt.callLater(function() {
            codexActivityLoadScheduled = false
            loadOlderCodexActivity()
        })
    }

    // Acceptance hook used only by the nested Wayland visual test. It follows
    // the same top-edge path as a wheel/scrollbar gesture without exposing a
    // test-only control in the production UI.
    function runChatScrollAcceptance() {
        if (selectedPage !== 3)
            return
        if (agentController.activityEntries.length === 0
                && codexScrollAcceptanceAttempts < 60) {
            ++codexScrollAcceptanceAttempts
            codexScrollAcceptanceRetryTimer.restart()
            return
        }
        codexScrollAcceptanceAttempts = 0
        if (agentController.activityEntries.length === 0)
            return
        codexChatActivityList.positionViewAtBeginning()
        scheduleOlderCodexActivityLoad()
    }

    function manageCodexChatSession(action, threadId, archived) {
        const session = findCodexSession(threadId)
        let project = projectForCodexSession(session)
        if ((!project.id || !project.root) && session
                && String(session.project_id || "") && String(session.workspace || "")) {
            project = ({ id: String(session.project_id), root: String(session.workspace) })
        }
        if (!project.id || !project.root) {
            codexWorkspaceMessage = "会话所属项目当前未注册，无法修改。"
            return
        }
        const response = codexWorkspaceAction(action, ({ thread_id: threadId }), project)
        if (!response.ok)
            return
        if (action === "session_fork") {
            const forked = ((response.result || ({})).session || ({}))
            const forkId = String(forked.id || "")
            if (forkId.length > 0) {
                flowSessionSummaries = [forked].concat((flowSessionSummaries || []).filter(function(item) {
                    return String(item.id || "") !== forkId
                }))
                selectCodexChatSession(forkId, String(forked.project_id || ""))
            }
            return
        }
        if ((action === "session_archive" || action === "session_delete")
                && String(threadId) === codexSessionId) {
            clearCodexSessionState()
            agentController.loadSessionActivity([])
        } else if (String(threadId) === codexSessionId && !archived) {
            const selected = codexWorkspaceAction("session_read", ({ thread_id: threadId, activity_limit: codexActivityPageLimit }), project)
            if (selected.ok) {
                hydrateCodexSessionRead(selected.result || ({}), false)
            }
        }
        refreshFlowSessionList()
        refreshCodexWorkspace()
    }

    function selectedCodexChatSessionIds() {
        return Object.keys(codexSelectedSessionIds || {}).filter(function(id) {
            return Boolean(codexSelectedSessionIds[id])
        })
    }

    function setCodexChatSessionSelected(threadId, selected) {
        const id = String(threadId || "")
        if (!id)
            return
        const session = findCodexSession(id)
        if (!session || !String(session.project_id || "") || !String(session.workspace || "")) {
            codexWorkspaceMessage = "会话缺少项目或工作目录信息，无法安全执行批量操作。"
            return
        }
        const next = Object.assign({}, codexSelectedSessionIds || ({}))
        if (selected)
            next[id] = true
        else
            delete next[id]
        codexSelectedSessionIds = next
    }

    function toggleCodexChatSessionMultiSelect() {
        codexSessionMultiSelect = !codexSessionMultiSelect
        codexSelectedSessionIds = ({})
        codexWorkspaceMessage = ""
    }

    function performCodexChatSessionBatch(action) {
        const ids = selectedCodexChatSessionIds()
        if (ids.length === 0)
            return
        const groups = ({})
        for (const id of ids) {
            const session = findCodexSession(id) || ({})
            const projectId = String(session.project_id || "")
            const workspace = String(session.workspace || "")
            if (!projectId || !workspace) {
                codexWorkspaceMessage = "所选会话缺少项目或工作目录信息，无法安全执行批量操作。"
                return
            }
            const key = projectId + "\n" + workspace
            if (!groups[key])
                groups[key] = ({ project: ({ id: projectId, root: workspace }), ids: [] })
            groups[key].ids.push(id)
        }
        const entries = Object.keys(groups).map(function(key) { return groups[key] })
        if (entries.length === 0)
            return
        let remaining = entries.length
        let failed = false
        const containsCurrentSession = ids.indexOf(String(codexSessionId || "")) >= 0
        for (const entry of entries) {
            codexWorkspaceActionAsync(action, ({ thread_ids: entry.ids }), function(response) {
                if (!response || !response.ok) {
                    failed = true
                    if (response && response.message)
                        codexWorkspaceMessage = String(response.message)
                }
                --remaining
                if (remaining !== 0)
                    return
                if (!failed && containsCurrentSession) {
                    clearCodexSessionState()
                    codexSessionProjectId = String(currentProject.id || "")
                    agentController.loadSessionActivity([])
                }
                codexSessionMultiSelect = false
                codexSelectedSessionIds = ({})
                refreshFlowSessionList()
                refreshCodexWorkspace()
            }, entry.project)
        }
    }

    function requestCodexChatSessionBatchDelete() {
        const ids = selectedCodexChatSessionIds()
        if (ids.length === 0)
            return
        codexSessionBatchDeleteDialog.selectedCount = ids.length
        codexSessionBatchDeleteDialog.open()
    }

    function renameCodexChatSession(threadId, currentName) {
        if (!threadId)
            return
        chatSessionRenameDialog.sessionId = String(threadId)
        chatSessionRenameField.text = String(currentName || "").trim()
        chatSessionRenameDialog.open()
        chatSessionRenameField.forceActiveFocus()
        chatSessionRenameField.selectAll()
    }

    function insertChatGuidance(text) {
        const existing = pendingChatPrompt.trim()
        pendingChatPrompt = existing.length > 0 ? existing + "\n" + text : text
        agentPromptEditor.forceActiveFocus()
        agentPromptEditor.cursorPosition = agentPromptEditor.length
    }

    function shellDisplayCommand(entry) {
        const value = entry || ({})
        const saved = String(value.shellDisplayCommand || "").trim()
        if (saved.length > 0)
            return saved
        let argumentsObject = null
        try { argumentsObject = JSON.parse(String(value.argumentsRaw || "{}")) } catch (error) {}
        const argv = argumentsObject && (argumentsObject.command || argumentsObject.cmd || argumentsObject.argv)
        if (Array.isArray(argv)) {
            const executable = String(argv[0] || "").split("/").pop().toLowerCase()
            if ((executable === "bash" || executable === "sh")
                    && (String(argv[1] || "") === "-lc" || String(argv[1] || "") === "-c"))
                return String(argv[2] || "").trim()
            return argv.map(function(argument) {
                const text = String(argument)
                return /^[A-Za-z0-9_./:@%+=,-]+$/.test(text)
                    ? text : "'" + text.replace(/'/g, "'\\''") + "'"
            }).join(" ")
        }
        const command = String(value.shellCommand || value.text || "").trim()
        const wrapped = command.match(/^'?(?:[^ '\\/]+\/)*bash'?'\s+'-lc'\s+'([\s\S]*)'$/)
        return wrapped ? wrapped[1].replace(/'\\''/g, "'") : command
    }

    function terminalOutputText(value) {
        const output = String(value || "").replace(/\r\n/g, "\n").replace(/\r/g, "\n")
        if (output.indexOf("\n") >= 0)
            return output
        return output.replace(/\\r\\n/g, "\n").replace(/\\n/g, "\n").replace(/\\t/g, "\t")
    }

    function chatToolSummary(entry) {
        const name = String(entry.name || "").toLowerCase()
        const label = entry.name ? " · " + String(entry.name) : ""
        const rawArguments = String(entry.argumentsRaw || entry.text || "{}").trim()
        if (name === "skills_read") {
            let args = ({})
            let result = ({})
            try { args = JSON.parse(rawArguments) } catch (error) {}
            try { result = JSON.parse(String(entry.result || "{}")) } catch (error) {}
            const skillId = String(args.skill_id || args.skillId || args.name
                || result.name || result.skill_name || "技能")
            return String(entry.status || "") === "running"
                ? window.localizedUiText("正在读取 ") + skillId + window.localizedUiText(" 技能")
                : String(entry.status || "") === "failed"
                    ? window.localizedUiText("技能读取失败 ") + skillId
                    : window.localizedUiText("已读取 ") + skillId + window.localizedUiText(" 技能")
        }
        if (name === "skills_list")
            return window.localizedUiText(String(entry.status || "") === "running" ? "正在搜索可用技能" : "已列出可用技能")
        if (entry.shell === true || name === "shell" || name === "shell_execute" || name === "shell_poll"
                || name === "terminal_execute" || name === "terminal_poll" || name === "terminal"
                || name === "exec_command" || name === "write_stdin") {
            const command = window.shellDisplayCommand(entry)
            return command || window.localizedUiText("Shell 命令")
        }
        if (name === "read_file" || name === "read_project_excerpt") {
            let args = ({})
            try { args = JSON.parse(rawArguments) } catch (error) {}
            const path = String(args.path || window.localizedUiText("项目文件"))
            const range = args.start_line !== undefined
                ? window.localizedUiText(" · 第 ") + String(args.start_line) + window.localizedUiText(" 行起")
                : ""
            return window.localizedUiText("已读取 ") + path + range
        }
        if (name === "search_project_text") {
            let args = ({})
            try { args = JSON.parse(rawArguments) } catch (error) {}
            return window.localizedUiText("已检索 ") + String(args.query || window.localizedUiText("项目内容"))
        }
        const toolTitles = {
            apply_patch: "已编辑文件",
            create_file: "已创建文件",
            inspect_project: "已检查项目配置",
            check_dft_readiness: "已检查 DFT 配置",
            analyze_dft_results: "已分析 DFT 结果",
            diagnose_dft_failure: "已检查 DFT 问题",
            run_dft_flow: "已启动 DFT 流程",
            run_dft_iteration: "已运行 DFT 验证",
            wait_dft_job: "已等待 DFT 作业",
            status_dft_job: "已检查 DFT 作业",
            interrupt_dft_job: "已停止 DFT 作业",
            dft_report_parse_drc: "已读取 DRC 报告",
            dft_report_parse_atpg: "已读取 ATPG 报告",
            dft_report_validate_acceptance: "已核验 DFT 指标",
            dft_evidence_cross_validate: "已交叉核验 DFT 结果",
            list_studio_projects: "已查看项目列表",
            inspect_studio_project: "已查看项目设置",
            inspect_studio_settings: "已查看 Studio 设置",
            update_studio_project: "已更新项目设置",
            update_studio_model: "已更新模型设置",
            set_studio_active_model: "已切换模型",
            set_studio_capability: "已更新工具设置",
            memory_recall: "已检索项目记忆",
            memory_remember: "已保存项目记忆",
            save_run_report: "已保存运行报告",
            spawn_agent: "已启动子智能体",
            wait_agent: "已等待子智能体",
            list_agents: "已查看子智能体",
            send_message: "已向子智能体发送消息",
            followup_task: "已更新子智能体任务",
            interrupt_agent: "已停止子智能体"
        }
        if (toolTitles[name])
            return window.localizedUiText(toolTitles[name])
        if (name.indexOf("image") >= 0)
            return window.localizedUiText("已查看图像") + label
        if (name.indexOf("patch") >= 0 || name.indexOf("edit") >= 0 || name.indexOf("write") >= 0)
            return window.localizedUiText("已编辑文件") + label
        if (name === "create_file")
            return window.localizedUiText("已创建文件") + label
        if (name.indexOf("terminal") >= 0 || name.indexOf("command") >= 0 || name.indexOf("shell") >= 0)
            return window.localizedUiText("已运行命令") + label
        if (name.indexOf("read") >= 0 || name.indexOf("inspect") >= 0 || name.indexOf("source") >= 0)
            return window.localizedUiText("已读取项目或文件") + label
        if (name.indexOf("run") >= 0 || name.indexOf("flow") >= 0 || name.indexOf("atpg") >= 0)
            return window.localizedUiText("已执行 DFT 工具流程") + label
        if (name.indexOf("skill") >= 0)
            return window.localizedUiText("已处理技能")
        if (name.indexOf("memory") >= 0)
            return window.localizedUiText("已处理项目记忆")
        if (name.indexOf("agent") >= 0 || name.indexOf("subagent") >= 0)
            return window.localizedUiText("已处理子智能体任务")
        return window.localizedUiText("已完成操作")
    }

    function isShellActivity(entry) {
        if (!entry)
            return false
        const name = String(entry.name || "").toLowerCase()
        return entry.shell === true || ["shell", "shell_execute", "shell_poll", "terminal_execute",
            "terminal_poll", "terminal", "exec_command", "write_stdin"].indexOf(name) >= 0
    }

    function chatToolResult(entry) {
        const value = entry || ({})
        const raw = String(value.result || "").trim()
        if (String(value.errorText || "").trim().length > 0)
            return window.localizedUiText("错误：") + String(value.errorText).trim()
        if (raw.length === 0)
            return ""
        let parsed = null
        try { parsed = JSON.parse(raw) } catch (error) {}
        if (parsed === null) {
            const looksLikeJson = (raw.startsWith("{") && raw.endsWith("}"))
                || (raw.startsWith("[") && raw.endsWith("]"))
            return looksLikeJson ? window.localizedUiText("操作已完成") : raw
        }
        function textValue(candidate) {
            if (typeof candidate === "string") {
                const text = candidate.trim()
                if ((text.startsWith("{") && text.endsWith("}"))
                        || (text.startsWith("[") && text.endsWith("]"))) {
                    try { return textValue(JSON.parse(text)) } catch (error) {}
                }
                return text
            }
            if (Array.isArray(candidate))
                return candidate.map(function(item) { return textValue(item) }).filter(function(item) { return item.length > 0 }).join("\n")
            if (candidate && typeof candidate === "object") {
                for (const key of ["content", "text", "output", "stdout", "stderr", "summary", "message", "error"])
                    if (candidate[key] !== undefined) {
                        const nested = textValue(candidate[key])
                        if (nested.length > 0)
                            return nested
                    }
            }
            return ""
        }
        if (isShellActivity(value)) {
            const output = String(value.shellOutput || textValue(parsed) || "").trim()
            if (output.length > 0)
                return output
            const error = textValue(parsed && typeof parsed === "object" ? parsed.error : "")
                || textValue(parsed && typeof parsed === "object" ? parsed.message : "")
            return error.length > 0 ? window.localizedUiText("错误：") + error
                : window.localizedUiText("命令输出为空")
        }
        if (String(value.status || "") === "failed"
                || String(parsed.status || parsed.state || "").toLowerCase() === "failed"
                || String(parsed.error || "").trim().length > 0) {
            const error = textValue(parsed && typeof parsed === "object" ? parsed.error : "")
                || textValue(parsed && typeof parsed === "object" ? parsed.message : "")
            return window.localizedUiText("错误：") + (error || window.localizedUiText("操作失败"))
        }
        const name = String(value.name || "").toLowerCase()
        if (["read_file", "read_project_excerpt"].indexOf(name) >= 0)
            return window.localizedUiText("文件内容已读取")
        if (name === "search_project_text")
            return window.localizedUiText("项目检索已完成")
        if (["apply_patch", "create_file"].indexOf(name) >= 0)
            return window.localizedUiText("文件修改已完成")
        if (["run_dft_flow", "run_dft_iteration", "wait_dft_job"].indexOf(name) >= 0)
            return window.localizedUiText("DFT 流程结果已更新")
        if (["dft_report_parse_drc", "dft_report_parse_atpg", "analyze_dft_results"].indexOf(name) >= 0)
            return window.localizedUiText("DFT 报告已分析")
        if (typeof parsed === "string" || Array.isArray(parsed))
            return textValue(parsed)
        if (!parsed || typeof parsed !== "object")
            return String(parsed)
        const output = textValue(parsed)
        if (output.length > 0)
            return output
        return window.localizedUiText("操作已完成")
    }

    function patchFilePath(entry) {
        const value = entry || ({})
        const files = value.files || []
        return String(value.createPath || (files.length > 0 ? files[0] : "") || value.path || "")
    }

    function patchFileName(entry) {
        const path = patchFilePath(entry)
        const separator = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return separator >= 0 ? path.slice(separator + 1) : path
    }

    function patchLineStats(entry) {
        const value = entry || ({})
        const stored = value.lineStats || value.line_stats || ({})
        let added = Number(stored.added || 0)
        let removed = Number(stored.removed || 0)
        if (added > 0 || removed > 0)
            return ({ added: added, removed: removed })
        const patch = String(value.patchText || "")
        if (patch.length === 0)
            return ({ added: 0, removed: 0 })
        const lines = patch.split("\n")
        for (let index = 0; index < lines.length; ++index) {
            const line = lines[index]
            if (line.indexOf("+++") !== 0 && line.indexOf("+") === 0)
                ++added
            else if (line.indexOf("---") !== 0 && line.indexOf("-") === 0)
                ++removed
        }
        return ({ added: added, removed: removed })
    }

    function patchErrorText(entry) {
        return String((entry || ({})).errorText || "").trim()
    }

    function chatActivityTitle(entry) {
        if (entry.kind === "tool" && String(entry.name || "") === "save_run_report")
            return languageSettings.language === "en" ? "Save run report" : "保存运行报告"
        if (entry.kind === "tool")
            return chatToolSummary(entry)
        if (entry.kind === "approval" && String(entry.permissionPath || "").length > 0)
            return window.localizedUiText("请求访问路径")
        if (entry.kind === "patch") {
            const stats = patchLineStats(entry)
            const path = patchFileName(entry)
            const action = window.localizedUiText(String(entry.name || "").toLowerCase() === "create_file" ? "已创建文件" : "已编辑文件")
            return (path.length > 0 ? path : action)
                + "  +" + String(stats.added || 0)
                + "  -" + String(stats.removed || 0)
        }
        if (entry.kind === "approval" && String(entry.patchText || "").length > 0) {
            const stats = patchLineStats(entry)
            const path = patchFileName(entry)
            return (path.length > 0 ? path : window.localizedUiText("等待人工批准"))
                + "  +" + String(stats.added || 0)
                + "  -" + String(stats.removed || 0)
        }
        if (entry.kind === "approval")
            return window.localizedUiText("等待人工批准")
        return activityTitle(entry)
    }

    function isRedundantAgentStatusEntry(entry) {
        if (!entry || entry.kind !== "agent")
            return false
        const text = String(entry.text || "")
            .replace(/\s+/g, "")
            .replace(/[，。,.、!！?？:：;；]+/g, "")
            .toLowerCase()
        if (text === "运行了命令执行了dft工具"
                || text === "已运行命令已执行dft工具流程"
                || text === "执行了dft工具")
            return true
        // Older runtimes emitted several slight variants of this narration.
        // They duplicate the tool-group title and are not an LLM response.
        return text.indexOf("dft工具") >= 0
            && (text.indexOf("运行了命令") >= 0
                || text.indexOf("执行了命令") >= 0
                || text.indexOf("已运行命令") >= 0
                || text.indexOf("执行了dft工具") === 0)
    }

    function isAgentStatusEntry(entry) {
        if (!entry)
            return false
        if (entry.kind === "reconnect")
            return true
        if (entry.kind !== "agent")
            return false
        if (String(entry.role || "") === "status" || String(entry.role || "") === "thinking")
            return true
        const text = String(entry.text || "")
            .replace(/\s+/g, "")
            .replace(/[，。,.、!！?？:：;；]+/g, "")
            .toLowerCase()
        return text.indexOf("agent已休眠") >= 0
            || text.indexOf("eda作业已返回") >= 0
            || text.indexOf("agent恢复分析") >= 0
            || text.indexOf("agent请求中断") >= 0
            || text.indexOf("作业等待") >= 0
    }

    function subagentDisplayName(entry) {
        const value = entry || ({})
        const name = String(value.subagentName || value.subagentRole || "子智能体").trim()
        const role = String(value.subagentRole || "").trim()
        if (name.length > 0 && name !== role && name !== "子智能体")
            return name
        const id = String(value.subagentId || role || "agent")
        const nicknames = ["Aoi", "Akari", "Hana", "Hina", "Kaori", "Mei", "Rin", "Sakura", "Yui", "Yuna", "Misaki", "Ayaka", "Chihiro", "Rena", "Yui", "Hikari"]
        let hash = 0
        for (let i = 0; i < id.length; ++i)
            hash = ((hash * 31) + id.charCodeAt(i)) | 0
        const nickname = nicknames[Math.abs(hash) % nicknames.length]
        return nickname + " " + id.slice(-4).toUpperCase()
    }

    function subagentColor(entry) {
        const palette = ["#2d79a8", "#9b4f72", "#38856a", "#a56c32", "#5969ae", "#9a633e", "#357e83", "#8b5aa1", "#677c35", "#bf5a45", "#4670a3", "#8e693c"]
        const key = String((entry || ({})).subagentId || (entry || ({})).subagentRole || "agent")
        let hash = 0
        for (let i = 0; i < key.length; ++i)
            hash = ((hash * 31) + key.charCodeAt(i)) | 0
        return palette[Math.abs(hash) % palette.length]
    }

    function subagentInitials(entry) {
        const name = subagentDisplayName(entry).replace(/[_-]+/g, " ").trim()
        const words = name.split(/\s+/)
        if (words.length > 1)
            return (words[0].charAt(0) + words[1].charAt(0)).toUpperCase()
        return name.substring(0, 2)
    }

    function subagentStatusText(entry) {
        const status = String((entry || ({})).status || "")
        if (status === "subagent_running") return window.localizedUiText("运行中")
        if (status === "subagent_completed") return window.localizedUiText("已完成")
        if (status === "subagent_failed") return window.localizedUiText("失败")
        if (status === "subagent_interrupted") return window.localizedUiText("已中断")
        return window.localizedUiText("子任务")
    }

    function subagentSpecialty(entry) {
        const value = entry || ({})
        const title = String(value.subagentSpecialty || "").trim()
        if (title.length > 0)
            return window.localizedUiText(title)
        const role = String(value.subagentRole || "").toLowerCase()
        if (role.indexOf("review") >= 0 || role.indexOf("审查") >= 0)
            return window.localizedUiText("独立复核")
        if (role.indexOf("diagnos") >= 0 || role.indexOf("诊断") >= 0)
            return window.localizedUiText("问题诊断")
        return window.localizedUiText("资料调查")
    }

    function subagentDetail(entry) {
        return String((entry || ({})).text || "")
            .replace(/^子智能体已启动（[^）]*）：?\s*/, "")
            .replace(/^子智能体继续任务（[^）]*）：?\s*/, "")
            .replace(/^子智能体复核(?:完成|失败)：\s*/, "")
    }

    function subagentIcon(entry) {
        const role = String((entry || ({})).subagentRole || "").toLowerCase()
        if (role.indexOf("review") >= 0 || role.indexOf("审查") >= 0)
            return "check"
        if (role.indexOf("diagnos") >= 0 || role.indexOf("诊断") >= 0)
            return "search"
        return "agent"
    }

    function openCodexSubagentSession(entry) {
        const childId = String((entry || ({})).subagentId || "").trim()
        if (childId.length === 0)
            return
        window.selectCodexChatSession(childId)
    }

    function isFullHtmlDocument(text) {
        const value = String(text || "").trim()
        return /^<!doctype\s+html\b/i.test(value)
            || /^<html\b/i.test(value)
            || /^&lt;!doctype\s+html\b/i.test(value)
            || /^&lt;<html\b/i.test(value)
    }

    function activityUsesRichText(entry) {
        // User prompts and Agent replies share Codex's Markdown renderer.
        // Complete HTML documents are persisted gateway/error pages; those
        // are normalized to plain text before they reach TextEdit.
        return entry
            && (entry.kind === "user" || entry.kind === "agent")
            && !isFullHtmlDocument(entry.text)
    }

    function renderActivityText(entry) {
        const source = String(entry && entry.text || "")
        if (isFullHtmlDocument(source))
            return agentController.renderPlainText(source)
        let rendered = agentController.renderMarkdown(source)
        // Keep compatibility with transcripts rendered by an older binary
        // whose Markdown helper returned QTextDocument's full HTML wrapper.
        const lower = rendered.toLowerCase()
        const bodyOpen = lower.indexOf("<body")
        if (bodyOpen >= 0) {
            const contentStart = rendered.indexOf(">", bodyOpen)
            const bodyClose = lower.lastIndexOf("</body>")
            if (contentStart >= 0 && bodyClose > contentStart)
                rendered = rendered.slice(contentStart + 1, bodyClose)
        }
        // A malformed or escaped gateway page must never expose its document
        // wrapper as the visible assistant response.
        if (/^<!doctype\s+html\b/i.test(rendered) || /^<html\b/i.test(rendered))
            rendered = String(source).replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim()
        return rendered
    }

    function isChatToolEntry(entry) {
        if (!entry)
            return false
        // Keep patch/approval cards independent so their approve, reject and
        // rollback actions remain directly available. Codex groups ordinary
        // tool work (edits, reads, shell and EDA calls) into one disclosure.
        return entry.kind === "tool"
    }

    function chatToolGroupVerb(entry) {
        // Thinking is an expandable child of the activity group, not an
        // action label. Keep it out of the group title so the header describes
        // the work being performed rather than the model's internal state.
        if (entry && entry.kind === "agent" && String(entry.role || "") === "thinking")
            return ""
        const name = String(entry.name || "").toLowerCase()
        if (name === "skills_read")
            return window.localizedUiText("加载了技能")
        if (name === "skills_list")
            return window.localizedUiText("搜索了技能")
        if (name === "create_file")
            return window.localizedUiText("创建了文件")
        if (entry.kind === "patch" || name.indexOf("patch") >= 0
                || name.indexOf("edit") >= 0 || name.indexOf("write") >= 0)
            return window.localizedUiText("编辑了文件")
        if (entry.shell === true || name.indexOf("terminal") >= 0
                || name.indexOf("command") >= 0 || name.indexOf("shell") >= 0)
            return window.localizedUiText("执行了命令")
        if (name.indexOf("read") >= 0 || name.indexOf("inspect") >= 0
                || name.indexOf("search") >= 0 || name.indexOf("source") >= 0)
            return window.localizedUiText("读取了项目或文件")
        if (isEdaActivity(entry))
            return window.localizedUiText("执行了 DFT 工具")
        return window.localizedUiText("调用了工具")
    }

    // Keep the raw activity list durable, but present contiguous tool work as
    // one Codex-style disclosure row.  Each child keeps its source index so
    // EDA output and patch actions still target the original activity entry.
    function chatToolGroupSummary(children) {
        const verbs = []
        for (let index = 0; index < children.length; ++index) {
            const verb = chatToolGroupVerb(children[index])
            if (verb.length > 0 && verbs.indexOf(verb) < 0)
                verbs.push(verb)
        }
        return verbs.join(languageSettings.language === "en" ? ", " : "，")
    }

    function isChatActivityGroupMember(entry) {
        return isChatToolEntry(entry)
            || (entry && entry.kind === "agent" && String(entry.role || "") === "thinking")
    }

    function chatActivityGroupSummary(children, liveHint) {
        if (!children || children.length === 0)
            return window.localizedUiText("工作中")
        const latest = children[children.length - 1]
        const actionSummary = chatToolGroupSummary(children)
        if (latest && latest.kind === "agent" && String(latest.role || "") === "thinking")
            return actionSummary || window.localizedUiText(liveHint ? "正在处理…" : "已完成工作")
        if (latest && String(latest.status || "") === "running") {
            const name = String(latest.name || "").toLowerCase()
            if (latest.shell === true || name.indexOf("shell") >= 0 || name.indexOf("command") >= 0 || name.indexOf("terminal") >= 0)
                return window.localizedUiText("正在运行命令…")
            if (name.indexOf("read") >= 0 || name.indexOf("inspect") >= 0 || name.indexOf("search") >= 0 || name.indexOf("source") >= 0)
                return window.localizedUiText("正在读取项目或文件…")
            if (isEdaActivity(latest))
                return window.localizedUiText("正在执行 DFT 工具…")
            return window.localizedUiText("正在调用工具…")
        }
        return actionSummary || window.localizedUiText("已完成工作")
    }

    function chatActivityFingerprint(entry) {
        if (!entry)
            return ""
        function tail(value) {
            const text = String(value || "")
            return String(text.length) + ":" + text.slice(-64)
        }
        return [
            String(entry.kind || ""), String(entry.role || ""),
            String(entry.status || ""), String(entry.name || ""),
            tail(entry.text), tail(entry.result), tail(entry.shellOutput),
            tail(entry.edaOutput), String(entry.streamId || "")
        ].join("|")
    }

    function patchCodexChatTail(raw, changedIndex) {
        if (codexActivityPrepending || codexActivityRefreshing
                || !codexChatDisplayEntries || codexChatDisplayEntries.length === 0
                || changedIndex < 0)
            return false
        let displayIndex = -1
        let childIndex = -1
        for (let index = codexChatDisplayEntries.length - 1; index >= 0; --index) {
            const candidate = codexChatDisplayEntries[index]
            const children = candidate.children || []
            for (let child = children.length - 1; child >= 0; --child) {
                if (Number(children[child].sourceIndex) === changedIndex) {
                    displayIndex = index
                    childIndex = child
                    break
                }
            }
            if (displayIndex >= 0)
                break
            if (Number(candidate.sourceIndex) === changedIndex) {
                displayIndex = index
                break
            }
        }
        if (displayIndex < 0)
            return false
        const source = raw[changedIndex]
        const replacement = ({})
        for (const key in source)
            replacement[key] = source[key]
        replacement.sourceIndex = changedIndex
        const next = []
        for (let index = 0; index < codexChatDisplayEntries.length; ++index)
            next.push(codexChatDisplayEntries[index])
        if (childIndex >= 0) {
            const group = codexChatDisplayEntries[displayIndex]
            const children = (group.children || []).slice(0)
            children[childIndex] = replacement
            const updatedGroup = ({})
            for (const key in group)
                updatedGroup[key] = group[key]
            updatedGroup.children = children
            updatedGroup.text = chatActivityGroupSummary(children, String(group.status || "") === "running")
            updatedGroup.status = children.some(function(item) { return String(item.status || "") === "running" })
                || (String(group.status || "") === "running"
                    && children.length > 0
                    && children[children.length - 1].kind === "agent"
                    && String(children[children.length - 1].role || "") === "thinking")
                ? "running"
                : children.some(function(item) { return String(item.status || "") === "failed" }) ? "failed" : "completed"
            next[displayIndex] = updatedGroup
        } else {
            next[displayIndex] = replacement
        }
        codexChatRawFingerprints[changedIndex] = chatActivityFingerprint(source)
        codexChatDisplayEntries = next
        codexChatDisplayModel.replaceAt(displayIndex, next[displayIndex])
        return true
    }

    // Activity signals can arrive once per streamed token. Coalesce them so
    // the delegate tree is rebuilt at a bounded cadence instead of once per
    // provider event, which keeps text streaming responsive at 60 Hz.
    function scheduleCodexChatDisplayRebuild() {
        if (codexChatDisplayRebuildScheduled)
            return
        codexChatDisplayRebuildScheduled = true
        codexChatDisplayRebuildTimer.restart()
    }

    function revealCodexChatScrollbar() {
        codexChatScrollbarActive = true
        codexChatScrollbarHideTimer.restart()
    }

    function syncCodexChatDisplayModel(display) {
        if (codexActivityPrepending && codexChatDisplayModel.count > 0) {
            const sourceShift = Math.max(0, Number(codexActivityPrependSourceShift || 0))
            const previousDisplay = codexChatDisplayEntries || []
            const existingFirstSource = previousDisplay.length > 0
                ? Number(previousDisplay[0].sourceIndex) + sourceShift
                : Number.POSITIVE_INFINITY
            const older = []
            let overlappingFirstGroup = null
            if (isFinite(existingFirstSource)) {
                for (let index = 0; index < display.length; ++index) {
                    const item = display[index]
                    if (Number(item.sourceIndex) >= existingFirstSource)
                        continue
                    if (item.kind === "tool_group" || item.kind === "activity_group") {
                        const children = item.children || []
                        const crossesExistingBoundary = children.some(function(child) {
                            return Number(child.sourceIndex) >= existingFirstSource
                        })
                        if (crossesExistingBoundary) {
                            // The newly hydrated page contains the complete
                            // group, while the old model already contains its
                            // newer tail.  Skip insertion here and replace the
                            // shifted old row below; inserting both made the
                            // command count grow on every upward scroll.
                            overlappingFirstGroup = item
                            continue
                        }
                    }
                    older.push(item)
                }
            }
            // One C++ beginInsertRows/endInsertRows transaction preserves the
            // current viewport while all older delegates are added at once.
            codexChatDisplayModel.prepend(older, sourceShift)
            if (overlappingFirstGroup !== null)
                codexChatDisplayModel.replaceAt(older.length, overlappingFirstGroup)
            codexActivityPrependSourceShift = 0
            return
        }

        // The C++ model compares roles and emits only changed rows, so a
        // streamed response does not reset ListView or recreate delegates.
        codexChatDisplayModel.sync(display)
    }

    function rebuildCodexChatDisplayEntries() {
        const raw = agentController.activityEntries || []
        const previousFingerprints = codexChatRawFingerprints || []
        if (!codexActivityPrepending && !codexActivityRefreshing
                && previousFingerprints.length === raw.length
                && raw.length > 0) {
            const changed = []
            for (let index = raw.length - 1; index >= 0; --index) {
                if (chatActivityFingerprint(raw[index]) !== previousFingerprints[index])
                    changed.push(index)
                if (changed.length > 1)
                    break
            }
            if (changed.length === 0)
                return
            if (changed.length === 1 && patchCodexChatTail(raw, changed[0])) {
                if (codexChatActivityList !== null
                        && codexChatActivityList.contentY >= Math.max(
                            Number(codexChatActivityList.originY || 0),
                            Number(codexChatActivityList.originY || 0)
                                + codexChatActivityList.contentHeight
                                - codexChatActivityList.height
                        ) - 24) {
                    if (codexExpandedThinkingKey.length === 0) {
                        Qt.callLater(function() {
                            if (!codexActivityPrepending && !codexActivityRefreshing)
                                codexChatActivityList.positionViewAtEnd()
                        })
                    }
                }
                return
            }
        }
        const display = []
        const listReady = codexChatActivityList !== null
        const minimum = listReady ? Number(codexChatActivityList.originY || 0) : 0
        const maximum = listReady
            ? Math.max(minimum, minimum + codexChatActivityList.contentHeight - codexChatActivityList.height)
            : 0
        const followBottom = listReady
            && !codexActivityPrepending
            && !codexActivityRefreshing
            && codexExpandedThinkingKey.length === 0
            && codexChatActivityList.contentY >= maximum - 24
        let pending = []
        function normalizedActivityText(value) {
            return String(value || "")
                .replace(/\s+/g, "")
                .replace(/[，。,.、!！?？:：;；]+/g, "")
                .toLowerCase()
        }
        function flushTools(liveHint) {
            if (pending.length === 0)
                return
            const hasThinking = pending.some(function(item) {
                return item.kind === "agent" && String(item.role || "") === "thinking"
            })
            const hasShell = pending.some(function(item) { return isShellActivity(item) })
            if (pending.length === 1 && !hasThinking && !hasShell) {
                display.push(pending[0])
            } else {
                const live = Boolean(liveHint) && pending.some(function(item) {
                    return String(item.status || "") === "running"
                        || (item.kind === "agent" && String(item.role || "") === "thinking")
                })
                display.push({
                    kind: "activity_group",
                    groupId: "activity-group-" + String(pending[0].sourceIndex),
                    sourceIndex: pending[0].sourceIndex,
                    children: pending,
                    text: chatActivityGroupSummary(pending, live),
                    status: live
                        ? "running"
                        : pending.some(function(item) { return String(item.status || "") === "failed" })
                            ? "failed" : "completed"
                })
            }
            pending = []
        }
        for (let index = 0; index < raw.length; ++index) {
            const source = raw[index]
            if (isChatActivityGroupMember(source)) {
                const child = ({})
                for (const key in source)
                    child[key] = source[key]
                child.sourceIndex = index
                pending.push(child)
                continue
            }
            flushTools(false)
            const item = ({})
            for (const key in source)
                item[key] = source[key]
            item.sourceIndex = index
            if (item.kind === "agent") {
                // Older runtimes persisted this tool narration as an agent
                // response. It is presentation chrome, never a second LLM
                // response; actual tool records remain untouched.
                if (isRedundantAgentStatusEntry(item))
                    continue
                const text = normalizedActivityText(item.text)
                if (text.length > 0 && display.some(function(candidate) {
                    return (candidate.kind === "tool_group" || candidate.kind === "activity_group")
                        && normalizedActivityText(candidate.text) === text
                }))
                    continue
            }
            display.push(item)
        }
        flushTools(true)
        syncCodexChatDisplayModel(display)
        codexChatDisplayEntries = display
        const fingerprints = []
        for (let index = 0; index < raw.length; ++index)
            fingerprints.push(chatActivityFingerprint(raw[index]))
        codexChatRawFingerprints = fingerprints
        if (followBottom) {
            // Delegate heights settle after the model row update. Positioning
            // in the next event turn avoids the thumb jumping between those
            // two layout passes while still following a live response.
            Qt.callLater(function() {
                if (!codexActivityPrepending && !codexActivityRefreshing)
                    codexChatActivityList.positionViewAtEnd()
            })
        }
    }

    function isEdaToolActivity(entry) {
        if (!entry || entry.kind !== "tool")
            return false
        const name = String(entry.name || "").toLowerCase()
        return name === "run_dft_flow"
            || name === "run_dft_iteration"
            || name === "run_dft_optimization"
            || name === "run_approved_patch"
            || name === "wait_dft_job"
            || name === "status_dft_job"
            || name === "interrupt_dft_job"
    }

    function workspaceSelectionIds(kind) {
        const selection = codexWorkspaceCatalog.selection || ({})
        if (kind === "skill") {
            const selected = (selection.skill_ids || []).slice()
            if (!selection.skills_explicit) {
                for (const skill of (codexWorkspaceCatalog.skills || [])) {
                    if ((Boolean(skill.auto_load) || String(skill.scope || "") === "dft-agent")
                            && selected.indexOf(String(skill.id || "")) < 0)
                        selected.push(String(skill.id || ""))
                }
            }
            return selected
        }
        if (selection.rules_explicit)
            return selection.rule_ids || []
        const defaults = []
        const rules = codexWorkspaceCatalog.rules || []
        for (let index = 0; index < rules.length; ++index) {
            if (String(rules[index].scope) === "project")
                defaults.push(String(rules[index].id))
        }
        return defaults
    }

    function workspaceSelectionContains(kind, sourceId) {
        return workspaceSelectionIds(kind).indexOf(sourceId) >= 0
    }

    function workspaceSkillEnabled(skill) {
        return window.workspaceSelectionContains("skill", String((skill || {}).id || ""))
    }

    function workspaceEnabledSkillCount() {
        let count = 0
        for (const skill of (codexWorkspaceCatalog.skills || [])) {
            if (workspaceSkillEnabled(skill))
                count += 1
        }
        return count
    }

    function setWorkspaceSourceEnabled(kind, sourceId, enabled) {
        let skills = workspaceSelectionIds("skill").slice()
        let rules = workspaceSelectionIds("rule").slice()
        let target = kind === "skill" ? skills : rules
        const index = target.indexOf(sourceId)
        if (enabled && index < 0)
            target.push(sourceId)
        if (!enabled && index >= 0)
            target.splice(index, 1)
        const selection = ({
            skill_ids: skills,
            skills_explicit: kind === "skill"
                ? true : Boolean((codexWorkspaceCatalog.selection || ({})).skills_explicit),
            rule_ids: rules,
            rules_explicit: true,
            apply_hooks: false,
            hooks_enabled: Boolean((codexWorkspaceCatalog.selection || ({})).hooks_enabled)
        })
        codexWorkspaceCatalog = Object.assign({}, codexWorkspaceCatalog, {
            selection: Object.assign({}, codexWorkspaceCatalog.selection || ({}), selection)
        })
        const projectId = String(currentProject.id || "")
        codexWorkspaceActionAsync("selection_update", selection, function(response) {
            if (projectId !== String(currentProject.id || ""))
                return
            if (response && response.ok)
                codexWorkspaceCatalog = Object.assign({}, codexWorkspaceCatalog, response.result || ({}))
            else
                window.refreshCodexWorkspaceCatalog()
        })
    }

    function setWorkspaceHooksEnabled(enabled) {
        codexWorkspaceMessage = "独立 Agent Runtime 不会自动执行外部 Hook。"
    }

    function syncWorkspaceSettings() {
        if (!workspacePathField || workspacePathField.editorHasFocus)
            return
        const configuredPath = String(window.executionValue("workspace_path", "")).trim()
        workspacePathField.text = configuredPath.length > 0
            ? configuredPath : window.defaultWorkspacePath
        workspaceSuffixCheck.checked = Boolean(window.executionValue("workspace_suffix_enabled", true))
    }

    function restoreSelectedProject() {
        const savedIndex = projectModel.indexOfProjectId(languageSettings.selectedProjectId)
        if (savedIndex >= 0)
            projectModel.currentIndex = savedIndex
        refreshProject()
    }

    function syncProjectTypeBox() {
        if (!projectTypeBox)
            return
        const kind = currentProject.kind || "rtl"
        const index = projectTypeIndex(kind)
        if (projectTypeBox.currentIndex !== index)
            projectTypeBox.currentIndex = index
    }

    function chooseProject(index) {
        projectModel.currentIndex = index
        refreshProject()
        selectedPage = 1
    }

    function toggleSidebarProjectList() {
        sidebarProjectListExpanded = !sidebarProjectListExpanded
        languageSettings.sidebarProjectListExpanded = sidebarProjectListExpanded
    }

    function openStudioSettings() {
        sidebarSettingsExpanded = true
        selectedPage = 9
    }

    function openStudioSettingsSubpage(page) {
        sidebarSettingsExpanded = true
        if (page === 8)
            Qt.callLater(window.syncModelSettingsEditor)
        if (page === 4)
            Qt.callLater(window.syncEditorImplementation)
        selectedPage = page
    }

    function toggleSidebarSettings() {
        sidebarSettingsExpanded = !sidebarSettingsExpanded
    }

    function openContextUsageDetails() {
        contextUsageIndicator.openDetails()
    }

    function projectTypeOptions() {
        return [
            { label: window.localizedText("typeRtl"), value: "rtl" },
            { label: window.localizedText("typeNetlist"), value: "netlist" },
            { label: window.localizedText("typeAtpg"), value: "atpg" },
            { label: window.localizedText("typeSimulation"), value: "simulation" },
            { label: window.localizedText("typeVerification"), value: "verification" },
            { label: window.localizedText("typeCustom"), value: "custom" }
        ]
    }

    function projectTypeIndex(kind) {
        const options = projectTypeOptions()
        for (let index = 0; index < options.length; ++index) {
            if (options[index].value === kind)
                return index
        }
        return options.length - 1
    }

    function projectTypeLabel(kind) {
        const options = projectTypeOptions()
        for (let index = 0; index < options.length; ++index) {
            if (options[index].value === kind)
                return options[index].label
        }
        return kind || ""
    }

    function isConfigurableHardwareProject(kind) {
        return kind === "rtl" || kind === "rtl-dft" || kind === "netlist"
    }

    function flowModuleValue(name, fallback) {
        const modules = window.currentProject.flowModules || ({})
        return modules[name] === undefined || modules[name] === null ? fallback : Boolean(modules[name])
    }

    function persistEditor(patchReviewOverride) {
        if (projectModel.currentIndex < 0)
            return false
        const values = {
            kind: projectTypeBox.currentValue,
            workspacePath: workspacePathField.text,
            workspaceSuffixEnabled: workspaceSuffixCheck.checked,
            root: rootField.text,
            rtlRoot: rtlField.text,
            top: topField.text,
            goal: goalEditor.text,
            notes: projectNotesEditor.text,
            relatedDocuments: window.projectRelatedDocumentsValue,
            flowProfile: "modular",
            libraryDir: libraryDirField.text,
            libraryFile: libraryFileField.text,
            libraryProfile: libraryProfileField.text,
            minimumCoverage: minimumCoverageField.text.trim().length > 0 ? Number(minimumCoverageField.text) : -1,
            maximumDftDrcViolations: maximumDrcField.text.trim().length > 0 ? Number(maximumDrcField.text) : -1,
            patchReviewEnabled: patchReviewOverride === undefined
                ? Boolean(window.executionValue("patch_review_enabled", false))
                : Boolean(patchReviewOverride)
        }
        if (window.isConfigurableHardwareProject(projectTypeBox.currentValue)) {
            values.flowModules = {
                synthesis: synthesisModuleCheck.checked,
                dft: dftModuleCheck.checked,
                scan: dftModuleCheck.checked && scanModuleCheck.checked,
                mbist: dftModuleCheck.checked && mbistModuleCheck.checked,
                atpg: dftModuleCheck.checked && atpgModuleCheck.checked,
                lbist: dftModuleCheck.checked && lbistModuleCheck.checked
            }
            values.constraintFile = constraintFileField.text
            values.fileList = fileListField.text
            values.sourceFiles = sourceFilesEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 })
            values.sourceLanguage = sourceLanguageBox.currentValue
            values.useConstraintFile = loadConstraintCheck.checked
            const firstClock = synthesisClockModel.count > 0 ? synthesisClockModel.get(0) : ({})
            values.clockName = String(firstClock.source || firstClock.name || clockNameField.text)
            values.clockPeriodNs = Number(firstClock.period || clockPeriodField.text || 1000)
            values.resetName = resetNameField.text
            values.resetActiveState = resetActiveStateBox.currentValue
            values.scanChainCount = scanChainCountField.text.trim().length > 0 ? Number(scanChainCountField.text) : 1
            values.maxChainLength = maxChainLengthField.text.trim().length > 0 ? Number(maxChainLengthField.text) : 1000
            values.mapEffort = mapEffortBox.currentValue
            values.areaEffort = areaEffortBox.currentValue
            values.powerEffort = powerEffortBox.currentValue
            values.synthesisSettings = window.synthesisSettingsFromEditor()
            values.synthesisOutputDir = synthesisOutputField.text
            values.dftOutputDir = dftOutputField.text
            values.timeoutSeconds = timeoutSecondsField.text.trim().length > 0 ? Number(timeoutSecondsField.text) : 1800
            values.agentTerminalEnabled = agentTerminalCheck.checked
            values.agentTerminalMaximumSeconds = agentTerminalMaximumSecondsField.text.trim().length > 0
                ? Number(agentTerminalMaximumSecondsField.text) : 300
            values.atpgCellModelFiles = atpgCellModelFilesEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 })
            values.atpgTimeoutSeconds = atpgTimeoutSecondsField.text.trim().length > 0 ? Number(atpgTimeoutSecondsField.text) : 1800
            values.dftTool = window.selectedDftTool
            values.tessentDofile = window.tessentDofile
            values.iterationLimit = atpgModuleCheck.checked
                ? (iterationLimitField.text.trim().length > 0 ? Number(iterationLimitField.text) : 1)
                : Number(window.executionValue("iteration_limit", 1))
            values.drcAutofixEnabled = scanModuleCheck.checked && drcAutofixCheck.checked
            values.drcAutofixTestModePort = drcAutofixPortField.text
        }
        projectSaveMessage = ""
        const saved = projectModel.updateProject(projectModel.currentIndex, values)
        refreshProject()
        if (saved)
            projectSaveMessage = languageSettings.language === "en" ? "Saved" : "已保存"
        else if (projectSaveMessage.length === 0)
            projectSaveMessage = languageSettings.language === "en" ? "Save failed" : "保存失败"
        projectSaveMessageTimer.restart()
        return saved
    }

    function activeModel() {
        return modelCatalog.count > 0 ? modelCatalog.modelAt(modelCatalog.activeModelIndex) : ({})
    }

    function codexApiModel() {
        // Chat follows the model selected in the catalog.  Previously this
        // loop started at row 0 and always returned the first API entry,
        // which made a Bonsai selection silently use the GPT endpoint.
        const selected = window.activeModel()
        if (Boolean(selected.enabled) && String(selected.provider || "") === "api")
            return selected
        for (let index = 0; index < modelCatalog.count; ++index) {
            const candidate = modelCatalog.modelAt(index)
            if (Boolean(candidate.enabled) && String(candidate.provider || "") === "api")
                return candidate
        }
        return window.activeModel()
    }

    function codexChatModels() {
        // This list is deliberately a direct view of the provider response.
        // The configured default is represented separately below, so an API
        // that exposes one model (or a completely different model family) is
        // not polluted with local assumptions.
        return agentController.codexModels || []
    }

    function codexChatModelOptions() {
        const models = window.codexChatModels()
        const defaultId = window.codexDefaultModelId()
        // Once /models has answered, expose only models that actually exist at
        // the current endpoint. A stale catalog entry must not remain as a
        // selectable "Default" row after a provider deployment changes.
        const includeDefault = models.length === 0 || models.some(function(candidate) {
            return String(candidate.id || "") === defaultId
        })
        const options = includeDefault ? [{
            id: "__configured_default__",
            label: languageSettings.language === "en" ? "Default" : "默认",
            isDefault: true,
            modelId: defaultId,
            modelData: window.codexApiModel()
        }] : []
        return options.concat(models)
    }

    function codexChatReasoningLevels() {
        const models = window.codexChatModels()
        for (const model of models) {
            if (String(model.id || "") === String(window.codexChatModel || "")) {
                const levels = model.reasoningLevels || []
                if (levels.length > 0)
                    return levels
            }
        }
        return [{ effort: "low", description: window.localizedUiText("更快响应，较少推理") }, { effort: "medium", description: window.localizedUiText("平衡速度与分析深度") }, { effort: "high", description: window.localizedUiText("更充分分析复杂 DFT 问题") }]
    }

    function codexChatModelLabel() {
        const models = window.codexChatModels()
        for (const model of models) {
            if (String(model.id || "") === String(window.codexChatModel || ""))
                return String(model.label || model.id || window.codexChatModel)
        }
        const configured = window.codexApiModel()
        return String(configured.label || configured.modelId || window.codexChatModel || (languageSettings.language === "en" ? "No model" : "未选择模型"))
    }

    function codexDefaultModelId() {
        const configured = String(modelCatalog.defaultModelId || "").trim()
        for (let index = 0; index < modelCatalog.count; ++index) {
            const candidate = modelCatalog.modelAt(index)
            if (String(candidate.modelId || "") === configured
                    && Boolean(candidate.enabled)
                    && String(candidate.provider || "") === "api")
                return configured
        }
        const apiModel = window.codexApiModel()
        if (String(apiModel.modelId || "").length > 0)
            return String(apiModel.modelId)
        const discovered = window.codexChatModels()
        return discovered.length > 0 ? String(discovered[0].id || "") : ""
    }

    function selectCodexChatModel(modelId, modelData, useDefault) {
        const requestedId = String(modelId || "")
        const liveModels = window.codexChatModels()
        let selectedId = requestedId
        let selectedDefault = Boolean(useDefault)
        if (liveModels.length > 0) {
            const live = liveModels.find(function(candidate) {
                return String(candidate.id || "") === requestedId
            })
            if (!live) {
                selectedId = String(liveModels[0].id || "")
                selectedDefault = false
                modelData = liveModels[0]
            }
        }
        window.codexChatModel = selectedId
        window.codexChatUsesDefaultModel = selectedDefault
        const levels = modelData && modelData.reasoningLevels ? modelData.reasoningLevels : []
        if (levels.length > 0 && !levels.some(function(level) { return String(level.effort || "") === String(window.codexChatReasoningEffort || "") }))
            window.codexChatReasoningEffort = String(levels[Math.min(1, levels.length - 1)].effort || "medium")
        window.persistCodexChatModelSelection()
    }

    function codexChatReasoningIndex() {
        const levels = window.codexChatReasoningLevels()
        for (let index = 0; index < levels.length; ++index) {
            if (String(levels[index].effort || "") === String(window.codexChatReasoningEffort || ""))
                return index
        }
        return Math.max(0, Math.floor(levels.length / 2))
    }

    function codexChatReasoningLabel() {
        const levels = window.codexChatReasoningLevels()
        const index = window.codexChatReasoningIndex()
        if (levels.length === 0)
            return "中"
        const effort = String(levels[index].effort || "medium")
        return ({ low: "低", medium: "中", high: "高", xhigh: "极高", max: "最大", ultra: "极限" })[effort] || effort
    }

    function refreshCodexModels() {
        const model = window.codexApiModel()
        if (String(model.provider || "") !== "api")
            return
        agentController.refreshCodexModels(String(model.apiBase || ""), String(model.apiKeyFile || ""), String(model.apiKey || ""))
    }

    function refreshModelContextFromApi() {
        if (String(modelSettingsProviderBox.currentValue || "api") !== "api")
            return
        const modelId = String(modelSettingsIdField.text || "").trim()
        const apiBase = String(modelSettingsApiBaseField.text || "").trim()
        if (modelId.length === 0 || apiBase.length === 0) {
            window.setModelSettingsMessage("请先填写模型 ID 和 API 地址。", "Enter a model ID and API base URL first.")
            return
        }
        if (agentController.codexModelsLoading)
            return

        modelContextRefreshTargetId = modelId
        modelContextRefreshApiBase = window.normalizedCodexApiBase(apiBase)
        modelContextRefreshPending = true
        window.setModelSettingsMessage(`正在从 API 获取 ${modelId} 的上下文信息...`, `Fetching context metadata for ${modelId}...`)
        agentController.refreshCodexModels(
            apiBase,
            String(modelSettingsApiKeyField.text || "").trim(),
            String(modelSettingsApiKeyValueField.text || "").trim())
        Qt.callLater(window.finishModelContextRefresh)
    }

    function resolveModelContextRefresh() {
        if (!modelContextRefreshPending)
            return false
        const targetId = String(modelContextRefreshTargetId || "")
        if (String(modelSettingsIdField.text || "").trim() !== targetId
                || window.normalizedCodexApiBase(modelSettingsApiBaseField.text || "") !== modelContextRefreshApiBase
                || String(modelSettingsProviderBox.currentValue || "api") !== "api") {
            modelContextRefreshPending = false
            modelContextRefreshTargetId = ""
            modelContextRefreshApiBase = ""
            return false
        }
        const models = agentController.codexModels || []
        const candidate = models.find(function(model) {
            return String(model.id || "").trim() === targetId
        })
        if (!candidate)
            return false
        let reportedContext = Math.floor(Number(candidate.contextWindow || 0))
        if (!Number.isFinite(reportedContext) || reportedContext < 2048)
            reportedContext = Math.floor(Number(candidate.maxContextWindow || 0))
        if (!Number.isFinite(reportedContext) || reportedContext < 2048)
            return false

        const supportedContext = Math.min(262144, reportedContext)
        modelSettingsContextField.text = String(supportedContext)
        // Keep the form's selected catalog model authoritative if the chat's
        // separately selected deployment differs from it.
        codexModelContextAutofilledId = String(codexChatModel || "")
        modelContextRefreshPending = false
        modelContextRefreshTargetId = ""
        modelContextRefreshApiBase = ""
        window.setModelSettingsMessage(
            reportedContext > supportedContext
                ? `API 返回 ${reportedContext.toLocaleString()} tokens；Studio 当前上限为 ${supportedContext.toLocaleString()}，已按此上限填写，点击保存后生效。`
                : `已从 API 更新上下文长度：${supportedContext.toLocaleString()} tokens；点击保存后生效。`,
            reportedContext > supportedContext
                ? `API reports ${reportedContext.toLocaleString()} tokens; Studio supports up to ${supportedContext.toLocaleString()}. Save to keep this limit.`
                : `Updated the context field to ${supportedContext.toLocaleString()} tokens. Save to apply it.`)
        return true
    }

    function finishModelContextRefresh() {
        if (!modelContextRefreshPending || agentController.codexModelsLoading)
            return
        if (window.resolveModelContextRefresh())
            return
        if (!modelContextRefreshPending)
            return
        modelContextRefreshPending = false
        modelContextRefreshTargetId = ""
        modelContextRefreshApiBase = ""
        window.setModelSettingsMessage("读取上下文上限失败，请检查 API 连接及 /models 返回的模型元数据。", "Failed to read a context limit. Check the API connection and its /models metadata.")
    }

    function normalizedCodexApiBase(value) {
        let normalized = String(value || "").trim()
        while (normalized.endsWith("/"))
            normalized = normalized.slice(0, -1)
        return normalized
    }

    function persistCodexChatModelSelection() {
        languageSettings.chatModelId = String(window.codexChatModel || "")
        languageSettings.chatModelApiBase = window.normalizedCodexApiBase(window.codexApiModel().apiBase || "")
    }

    function syncCodexChatPreferences() {
        const model = window.codexApiModel()
        const models = window.codexChatModels()
        const apiBase = window.normalizedCodexApiBase(model.apiBase || "")
        const defaultId = window.codexDefaultModelId()
        const endpointChanged = String(codexChatPreferenceApiBase || "") !== apiBase
        if (!codexChatPreferencesInitialized || endpointChanged) {
            const storedId = String(languageSettings.chatModelId || "")
            const storedBase = window.normalizedCodexApiBase(languageSettings.chatModelApiBase || "")
            if (storedId.length > 0 && storedBase === apiBase) {
                codexChatModel = storedId
                codexChatUsesDefaultModel = false
            } else {
                codexChatModel = defaultId
                codexChatUsesDefaultModel = true
            }
            codexChatPreferenceApiBase = apiBase
        }
        const hasCurrent = models.some(function(candidate) {
            return String(candidate.id || "") === String(codexChatModel || "")
        })
        if (models.length > 0 && !hasCurrent) {
            // /models is authoritative. Replace an old Bonsai (or any other
            // retired deployment) immediately and persist the live choice.
            codexChatModel = String(models[0].id || "")
            codexChatUsesDefaultModel = false
            window.persistCodexChatModelSelection()
        }
        const effort = String(model.reasoningEffort || "medium")
        const levels = window.codexChatReasoningLevels()
        const available = levels.some(function(level) { return String(level.effort || "") === String(codexChatReasoningEffort || "") })
        if (!codexChatPreferencesInitialized || !available)
            codexChatReasoningEffort = levels.some(function(level) { return String(level.effort || "") === effort }) ? effort : String((levels[Math.min(1, Math.max(0, levels.length - 1))] || {}).effort || "medium")
        // A provider may advertise a larger context window than the value
        // persisted in the local model catalog.  Fill it once per selected
        // model after /models returns, but never overwrite a later manual
        // adjustment on every refresh tick.
        const selectedRemote = models.find(function(candidate) {
            return String(candidate.id || "") === String(codexChatModel || "")
        })
        const detectedContext = selectedRemote ? Number(selectedRemote.contextWindow || 0) : 0
        if (detectedContext >= 2048 && codexModelContextAutofilledId !== String(codexChatModel || "")) {
            modelSettingsContextField.text = String(Math.min(262144, Math.floor(detectedContext)))
            modelSettingsEffectivePercentField.text = String(Number(selectedRemote.effectiveContextWindowPercent || 95))
            modelSettingsAutoCompactField.text = String(Number(selectedRemote.autoCompactTokenLimit || Math.floor(detectedContext * 0.9)))
            codexModelContextAutofilledId = String(codexChatModel || "")
            window.setModelSettingsMessage(
                `已从当前 API 模型探测到 ${Math.floor(detectedContext).toLocaleString()} token 上下文窗口。`,
                `Detected ${Math.floor(detectedContext).toLocaleString()} token context from the active API model.`)
        }
        codexChatPreferencesInitialized = true
    }

    function refreshRunReports() {
        const reports = agentController.reportFiles || []
        runReports = reports
        const filtered = window.filteredRunReports()
        if (filtered.length === 0) {
            selectedReportPath = ""
            return
        }
        for (const report of filtered) {
            if (String(report.path || "") === selectedReportPath)
                return
        }
        selectedReportPath = String(filtered[0].path || "")
    }

    function reportProjectOptions() {
        const options = [{ value: "*", label: languageSettings.language === "en" ? "All projects" : "全部项目" }]
        const projects = ({})
        for (const report of runReports) {
            const id = String(report.project_id || "").trim()
            if (!id || projects[id])
                continue
            projects[id] = String(report.project_name || id)
        }
        const ids = Object.keys(projects).sort(function(a, b) {
            return projects[a].localeCompare(projects[b])
        })
        for (const id of ids)
            options.push({ value: id, label: projects[id] })
        return options
    }

    function reportSessionOptions() {
        const options = [{ value: "*", label: languageSettings.language === "en" ? "All sessions" : "全部会话" }]
        const sessions = ({})
        for (const report of runReports) {
            if (reportProjectFilter !== "*" && String(report.project_id || "") !== reportProjectFilter)
                continue
            const id = String(report.session_id || "").trim()
            if (!id || sessions[id])
                continue
            const title = String(report.session_title || id)
            const modified = String(report.modified || "")
            sessions[id] = title + (modified.length > 0 ? " · " + modified.slice(0, 10) : "")
        }
        const ids = Object.keys(sessions).sort(function(a, b) {
            return sessions[a].localeCompare(sessions[b])
        })
        for (const id of ids)
            options.push({ value: id, label: sessions[id] })
        return options
    }

    function filteredRunReports() {
        const filtered = runReports.filter(function(report) {
            if (reportProjectFilter !== "*" && String(report.project_id || "") !== reportProjectFilter)
                return false
            if (reportSessionFilter !== "*" && String(report.session_id || "") !== reportSessionFilter)
                return false
            if (reportCategoryFilter !== "all" && String(report.category || "evidence") !== reportCategoryFilter)
                return false
            return true
        })
        filtered.sort(function(left, right) {
            if (String(left.path || "") === "__current_report__") return -1
            if (String(right.path || "") === "__current_report__") return 1
            return String(right.modified || "").localeCompare(String(left.modified || ""))
        })
        return filtered
    }

    function selectFirstFilteredReport() {
        const filtered = window.filteredRunReports()
        selectedReportPath = filtered.length > 0 ? String(filtered[0].path || "") : ""
    }

    function selectedReportEntry() {
        for (const report of window.filteredRunReports()) {
            if (String(report.path || "") === selectedReportPath)
                return report
        }
        return ({})
    }

    function selectedReportText() {
        if (selectedReportPath.length === 0)
            return window.localizedText("noEvidence")
        return agentController.readReport(selectedReportPath)
    }

    function selectedReportHtml() {
        return agentController.renderMarkdown(selectedReportText())
    }

    function syncModelSettingsEditor() {
        if (modelCatalog.count < 1)
            return
        modelSettingsIndex = modelCatalog.activeModelIndex
        const model = window.activeModel()
        modelSettingsIdField.text = String(model.modelId || "")
        modelSettingsLabelField.text = String(model.label || "")
        modelSettingsProviderBox.currentIndex = String(model.provider || "api") === "legacy" ? 0 : 1
        modelSettingsApiBaseField.text = String(model.apiBase || "")
        modelSettingsApiKeyField.text = String(model.apiKeyFile || "")
        modelSettingsApiKeyValueField.text = String(model.apiKey || "")
        modelSettingsReasoningBox.currentIndex = Math.max(0, ["low", "medium", "high", "xhigh", "max", "ultra"].indexOf(String(model.reasoningEffort || "medium")))
        modelSettingsReconnectAttemptsField.text = String(Number(model.reconnectMaxAttempts || 10))
        modelSettingsReconnectDelayField.text = String(Number(model.reconnectDelaySeconds || 1.0))
        modelSettingsTemperatureField.text = String(Number(model.temperature ?? 0.6))
        modelSettingsTopKField.text = String(Number(model.topK ?? 40))
        modelSettingsTopPField.text = String(Number(model.topP ?? 0.9))
        modelSettingsMinPField.text = String(Number(model.minP ?? 0.05))
        modelSettingsRepeatPenaltyField.text = String(Number(model.repeatPenalty ?? 1.1))
        modelSettingsRepeatLastNField.text = String(Number(model.repeatLastN ?? 256))
        modelSettingsDryMultiplierField.text = String(Number(model.dryMultiplier ?? 0.5))
        modelSettingsPresencePenaltyField.text = String(Number(model.presencePenalty ?? 0))
        modelSettingsFrequencyPenaltyField.text = String(Number(model.frequencyPenalty ?? 0))
        modelSettingsBasePathField.text = String(model.baseModelPath || "")
        modelSettingsAdapterPathField.text = String(model.adapterPath || "")
        modelSettingsLlamaServerPathField.text = String(model.llamaServerPath || "")
        modelSettingsContextField.text = String(Number(model.contextWindow || 65536))
        modelSettingsEffectivePercentField.text = String(Number(model.effectiveContextWindowPercent || 95))
        modelSettingsAutoCompactField.text = String(Math.min(
            Math.floor(Number(model.contextWindow || 65536) * 0.9),
            Number(model.autoCompactTokenLimit || Math.floor(Number(model.contextWindow || 65536) * 0.9))))
        modelSettingsHistoryField.text = String(Number(model.inputContextTokens || 16384))
        modelSettingsOutputField.text = String(Number(model.maximumNewTokens || 4096))
        modelSettingsModeBox.currentIndex = String(model.inferenceMode || "gpu") === "cpu_gpu"
            ? 1 : String(model.inferenceMode || "gpu") === "cpu" ? 2 : 0
        modelSettingsGpuField.text = String(Number(model.gpuMemoryGiB || 3.5))
        modelSettingsCpuField.text = String(Number(model.cpuMemoryGiB || 16))
        modelSettingsEnabledCheck.checked = Boolean(model.enabled)
    }

    function saveModelSettings() {
        const index = modelCatalog.activeModelIndex
        if (index < 0)
            return false
        const provider = modelSettingsProviderBox.currentValue
        const values = {
            modelId: modelSettingsIdField.text.trim(),
            label: modelSettingsLabelField.text.trim(),
            provider: provider,
            runtime: provider === "api" ? "openai_compatible" : "llama_cpp",
            apiBase: modelSettingsApiBaseField.text.trim(),
            apiKeyFile: modelSettingsApiKeyField.text.trim(),
            apiKey: modelSettingsApiKeyValueField.text.trim(),
            reasoningEffort: modelSettingsReasoningBox.currentValue,
            reconnectMaxAttempts: Number(modelSettingsReconnectAttemptsField.text),
            reconnectDelaySeconds: Number(modelSettingsReconnectDelayField.text),
            temperature: Number(modelSettingsTemperatureField.text),
            topK: Number(modelSettingsTopKField.text),
            topP: Number(modelSettingsTopPField.text),
            minP: Number(modelSettingsMinPField.text),
            repeatPenalty: Number(modelSettingsRepeatPenaltyField.text),
            repeatLastN: Number(modelSettingsRepeatLastNField.text),
            dryMultiplier: Number(modelSettingsDryMultiplierField.text),
            presencePenalty: Number(modelSettingsPresencePenaltyField.text),
            frequencyPenalty: Number(modelSettingsFrequencyPenaltyField.text),
            baseModelPath: modelSettingsBasePathField.text.trim(),
            adapterPath: modelSettingsAdapterPathField.text.trim(),
            llamaServerPath: modelSettingsLlamaServerPathField.text.trim(),
            contextWindow: Number(modelSettingsContextField.text),
            effectiveContextWindowPercent: Number(modelSettingsEffectivePercentField.text),
            autoCompactTokenLimit: Math.min(Math.floor(Number(modelSettingsContextField.text) * 0.9), Number(modelSettingsAutoCompactField.text)),
            inputContextTokens: Number(modelSettingsHistoryField.text),
            maximumNewTokens: Number(modelSettingsOutputField.text),
            inferenceMode: modelSettingsModeBox.currentValue,
            gpuMemoryGiB: Number(modelSettingsGpuField.text),
            cpuMemoryGiB: Number(modelSettingsCpuField.text),
            enabled: modelSettingsEnabledCheck.checked
        }
        if (provider === "api")
            codexChatPreferencesInitialized = false
        window.modelCatalogError = ""
        const saved = modelCatalog.updateModel(index, values)
        window.setModelSettingsMessage(
            saved ? "模型已保存，下次运行将使用当前选择。" : (window.modelCatalogError || "模型保存失败，请检查字段。"),
            saved ? "Model saved; it will be used by the next run." : (window.modelCatalogError || "Model save failed; check the fields."))
        return saved
    }

    function maximumAvailableModelResponseTokens() {
        const contextWindow = Number(modelSettingsContextField.text || window.activeModel().contextWindow || 65536)
        const inputLimit = Number(modelSettingsHistoryField.text || window.activeModel().inputContextTokens || 16384)
        return Math.max(1, contextWindow - inputLimit)
    }

    function apiModelContextWindowInfo(modelIdOverride) {
        const active = window.activeModel()
        const configuredContext = Number(modelSettingsContextField.text || active.contextWindow || 65536)
        const fallback = Math.max(2048, Math.min(262144, Number.isFinite(configuredContext) ? configuredContext : 65536))
        const modelId = String(modelIdOverride || modelSettingsIdField.text || active.modelId || "").trim()
        const discovered = agentController.codexModels || []
        for (const candidate of discovered) {
            if (String(candidate.id || "").trim() !== modelId)
                continue
            const detected = Number(candidate.contextWindow || candidate.maxContextWindow || 0)
            if (Number.isFinite(detected) && detected >= 2048) {
                const maxContext = Number(candidate.maxContextWindow || detected)
                const effectivePercent = Number(candidate.effectiveContextWindowPercent || 95)
                const autoCompact = Number(candidate.autoCompactTokenLimit || Math.floor(detected * 0.9))
                return {
                    contextWindow: Math.min(262144, Math.floor(detected)),
                    maxContextWindow: Math.min(262144, Math.floor(maxContext)),
                    effectivePercent: Math.max(1, Math.min(100, Math.floor(effectivePercent))),
                    autoCompactTokenLimit: Math.max(1024, Math.min(Math.floor(detected * 0.9), Math.floor(autoCompact))),
                    detected: true
                }
            }
        }
        return {
            contextWindow: fallback,
            maxContextWindow: fallback,
            effectivePercent: 95,
            autoCompactTokenLimit: Math.max(1024, Math.floor(fallback * 0.9)),
            detected: false
        }
    }

    function roundTokenBudget(value) {
        const numeric = Number(value)
        if (!Number.isFinite(numeric) || numeric <= 0)
            return 1
        return Math.max(1, Math.floor(numeric / 1024) * 1024)
    }

    function recommendedContextAllocation() {
        const info = String(modelSettingsProviderBox.currentValue || "api") === "api"
            ? window.apiModelContextWindowInfo()
            : ({contextWindow: Math.max(2048, Math.min(262144,
                    Number(modelSettingsContextField.text || window.activeModel().contextWindow || 65536))),
                    effectivePercent: 95, autoCompactTokenLimit: null, detected: false})
        const contextWindow = Math.max(2048, Math.floor(info.contextWindow))
        // Codex treats only this percentage as usable input capacity. The
        // default is 95%, leaving headroom for instructions, tool schemas and
        // provider-side output accounting.
        const effectivePercent = Math.max(1, Math.min(100, Number(info.effectivePercent || 95)))
        const effectiveWindow = Math.max(1024, Math.floor(contextWindow * effectivePercent / 100))
        // Auto compaction defaults to 90% of the resolved model window and is
        // clamped by model metadata when a provider supplies its own limit.
        const autoCompactLimit = Math.max(1024, Math.min(
            effectiveWindow,
            Math.floor(contextWindow * 0.9),
            Math.floor(Number(info.autoCompactTokenLimit || contextWindow * 0.9))
        ))
        // Codex does not reserve an arbitrary 25% split. Keep a bounded
        // response budget, then give all remaining tokens to durable history
        // while retaining a small fixed-prompt/runtime headroom.
        const runtimeReserve = Math.min(4096, Math.max(512, Math.floor(effectiveWindow * 0.05)))
        const desiredOutput = Math.min(16384, Math.max(1024, Math.floor(effectiveWindow * 0.25)))
        const safeOutput = Math.max(1, Math.min(
            roundTokenBudget(desiredOutput),
            Math.max(1, contextWindow - runtimeReserve - 1024)
        ))
        const historyBudget = Math.max(1024, Math.min(
            autoCompactLimit - runtimeReserve - safeOutput,
            contextWindow - runtimeReserve - safeOutput
        ))
        const safeHistory = Math.min(
            Math.max(1024, roundTokenBudget(historyBudget)),
            Math.max(1024, contextWindow - runtimeReserve - safeOutput)
        )
        modelSettingsContextField.text = String(contextWindow)
        modelSettingsEffectivePercentField.text = String(effectivePercent)
        modelSettingsAutoCompactField.text = String(autoCompactLimit)
        modelSettingsHistoryField.text = String(safeHistory)
        modelSettingsOutputField.text = String(safeOutput)
        const detectedTextZh = info.detected ? "已探测" : "当前配置"
        window.setModelSettingsMessage(
            `DFT Agent 上下文分配（${detectedTextZh} ${contextWindow.toLocaleString()}）：有效窗口 ${effectivePercent}%，自动压缩阈值 ${autoCompactLimit.toLocaleString()}，历史 ${safeHistory.toLocaleString()}，输出 ${safeOutput.toLocaleString()}。`,
            `DFT Agent context allocation (${info.detected ? "detected" : "configured fallback"} ${contextWindow.toLocaleString()}): ${effectivePercent}% effective, auto-compact at ${autoCompactLimit.toLocaleString()}, history ${safeHistory.toLocaleString()}, output ${safeOutput.toLocaleString()}.`)
    }

    // Kept as a compatibility alias for external visual-QA hooks.
    function recommendedApiContextAllocation() {
        window.recommendedContextAllocation()
    }

    function addModel(provider) {
        const cloud = provider === "api"
        const suffix = String(Date.now())
        const added = modelCatalog.addModel({
            modelId: cloud ? "cloud-model-" + suffix : "local-model-" + suffix,
            label: cloud ? window.localizedUiText("云端 API 模型") : window.localizedUiText("本地 llama.cpp 模型"),
            provider: provider,
            runtime: cloud ? "openai_compatible" : "llama_cpp",
            apiBase: cloud ? "https://api.openai.com/v1" : "http://127.0.0.1:11503",
            baseModelPath: cloud ? "" : "models/gguf/model.gguf",
            contextWindow: 65536,
            inputContextTokens: 32768,
            maximumNewTokens: 16384,
            inferenceMode: "cpu_gpu",
            gpuMemoryGiB: 3.5,
            cpuMemoryGiB: 16,
            enabled: true
        })
        if (added) {
            modelCatalog.setActiveModelIndex(modelCatalog.count - 1)
            Qt.callLater(window.syncModelSettingsEditor)
        }
        return added
    }

    function executionValue(name, fallback) {
        const execution = window.currentProject.dftExecution || ({})
        return execution[name] === undefined || execution[name] === null ? fallback : execution[name]
    }

    function setAgentPermissionMode(mode) {
        const requested = String(mode || "approval").toLowerCase()
        const normalized = ["full_access", "full", "danger-full-access"].indexOf(requested) >= 0
            ? "full_access"
            : requested === "autonomous" ? "autonomous" : "approval"
        agentPermissionMode = normalized
        const index = projectModel.currentIndex
        if (index >= 0)
            projectModel.updateProject(index, ({agentPermissionMode: normalized}))
    }

    function dftToolDisplayName() {
        return window.selectedDftTool === "tessent" ? "Tessent" : "TestMAX"
    }

    function synthesisSettings() {
        const settings = window.executionValue("synthesis_settings", {})
        return settings && typeof settings === "object" ? settings : ({})
    }

    function synthesisValue(name, fallback) {
        const settings = window.synthesisSettings()
        return settings[name] === undefined || settings[name] === null ? fallback : settings[name]
    }

    function replaceListModel(targetModel, rows) {
        targetModel.clear()
        if (!rows || rows.length === undefined)
            return
        for (let index = 0; index < rows.length; ++index)
            targetModel.append(rows[index])
    }

    function syncSynthesisModels() {
        window.replaceListModel(synthesisClockModel, window.synthesisValue("clocks", []))
        window.replaceListModel(generatedClockModel, window.synthesisValue("generated_clocks", []))
        window.replaceListModel(ioDelayModel, window.synthesisValue("io_delays", []))
        window.replaceListModel(timingExceptionModel, window.synthesisValue("timing_exceptions", []))
        if (synthesisClockModel.count === 0) {
            const legacyClock = String(window.executionValue("clock", "")).trim()
            if (legacyClock.length > 0) {
                const period = Number(window.executionValue("clock_period_ns", 1000))
                synthesisClockModel.append({
                    name: legacyClock,
                    source: legacyClock,
                    period: period,
                    rise: 0,
                    fall: period / 2,
                    setup_uncertainty: 0,
                    hold_uncertainty: 0,
                    transition: 0,
                    source_latency: 0,
                    network_latency: 0
                })
            }
        }
    }

    function listModelRows(sourceModel) {
        const rows = []
        for (let index = 0; index < sourceModel.count; ++index) {
            const row = sourceModel.get(index)
            const copy = ({})
            for (const key in row)
                copy[key] = row[key]
            rows.push(copy)
        }
        return rows
    }

    function synthesisSettingsFromEditor() {
        return {
            constraint_files: synthesisConstraintFilesEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 }),
            pre_scripts: synthesisPreScriptsEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 }),
            post_scripts: synthesisPostScriptsEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 }),
            clocks: window.listModelRows(synthesisClockModel),
            generated_clocks: window.listModelRows(generatedClockModel),
            io_delays: window.listModelRows(ioDelayModel),
            timing_exceptions: window.listModelRows(timingExceptionModel),
            additional_tcl_commands: synthesisAdditionalTclEditor.text.split(/\r?\n/).map(function(command) { return command.trim() }).filter(function(command) { return command.length > 0 }),
            clock_groups_tcl: clockGroupsEditor.text,
            operating_condition: operatingConditionField.text.trim(),
            min_library: minimumLibraryField.text.trim(),
            max_cores: Number(maxCoresField.text || 1),
            compile_command: compileCommandBox.currentValue,
            incremental: incrementalCompileCheck.checked,
            retime: retimeCompileCheck.checked,
            gate_clock: gateClockCompileCheck.checked,
            scan_ready: scanReadyCompileCheck.checked,
            boundary_optimization: boundaryOptimizationCheck.checked,
            auto_ungroup: autoUngroupBox.currentValue,
            max_transition: maxTransitionField.text.trim(),
            max_fanout: maxFanoutField.text.trim(),
            max_capacitance: maxCapacitanceField.text.trim(),
            driving_cell: drivingCellField.text.trim(),
            output_load: outputLoadField.text.trim(),
            reports: synthesisReportOptions()
        }
    }

    function synthesisReportOptions() {
        const reports = []
        if (reportQorCheck.checked) reports.push("qor")
        if (reportTimingCheck.checked) reports.push("timing")
        if (reportAreaCheck.checked) reports.push("area")
        if (reportPowerCheck.checked) reports.push("power")
        if (reportConstraintsCheck.checked) reports.push("constraints")
        if (reportResourcesCheck.checked) reports.push("resources")
        return reports
    }

    function hasImportedValue(settings, name) {
        return settings && settings[name] !== undefined && settings[name] !== null
    }

    function applyImportedSynthesisConfiguration(result) {
        const settings = result && result.settings ? result.settings : ({})
        if (window.hasImportedValue(settings, "library_dir"))
            synthesisLibraryDirField.text = String(settings.library_dir)
        if (window.hasImportedValue(settings, "target_library"))
            synthesisLibraryFileField.text = String(settings.target_library)
        if (window.hasImportedValue(settings, "library_profile"))
            synthesisLibraryProfileField.text = String(settings.library_profile)
        if (window.hasImportedValue(settings, "constraint_file"))
            synthesisConstraintField.text = String(settings.constraint_file)
        if (window.hasImportedValue(settings, "use_constraint_file"))
            synthesisLoadConstraintCheck.checked = Boolean(settings.use_constraint_file)
        if (window.hasImportedValue(settings, "constraint_files"))
            synthesisConstraintFilesEditor.text = settings.constraint_files.join("\n")
        if (window.hasImportedValue(settings, "pre_scripts"))
            synthesisPreScriptsEditor.text = settings.pre_scripts.join("\n")
        if (window.hasImportedValue(settings, "post_scripts"))
            synthesisPostScriptsEditor.text = settings.post_scripts.join("\n")
        if (window.hasImportedValue(settings, "clocks"))
            window.replaceListModel(synthesisClockModel, settings.clocks)
        if (window.hasImportedValue(settings, "generated_clocks"))
            window.replaceListModel(generatedClockModel, settings.generated_clocks)
        if (window.hasImportedValue(settings, "io_delays"))
            window.replaceListModel(ioDelayModel, settings.io_delays)
        if (window.hasImportedValue(settings, "timing_exceptions"))
            window.replaceListModel(timingExceptionModel, settings.timing_exceptions)
        if (window.hasImportedValue(settings, "additional_tcl_commands"))
            synthesisAdditionalTclEditor.text = settings.additional_tcl_commands.join("\n")
        if (window.hasImportedValue(settings, "clock_groups_tcl"))
            clockGroupsEditor.text = String(settings.clock_groups_tcl)
        if (window.hasImportedValue(settings, "operating_condition"))
            operatingConditionField.text = String(settings.operating_condition)
        if (window.hasImportedValue(settings, "min_library"))
            minimumLibraryField.text = String(settings.min_library)
        if (window.hasImportedValue(settings, "max_cores") && Number(settings.max_cores) > 0)
            maxCoresField.text = String(settings.max_cores)
        if (window.hasImportedValue(settings, "compile_command"))
            compileCommandBox.currentIndex = String(settings.compile_command) === "compile_ultra" ? 1 : 0
        if (window.hasImportedValue(settings, "map_effort"))
            synthesisMapEffortBox.currentIndex = Math.max(0, ["low", "medium", "high"].indexOf(String(settings.map_effort)))
        if (window.hasImportedValue(settings, "area_effort"))
            synthesisAreaEffortBox.currentIndex = Math.max(0, ["none", "low", "medium", "high"].indexOf(String(settings.area_effort)))
        if (window.hasImportedValue(settings, "power_effort"))
            synthesisPowerEffortBox.currentIndex = Math.max(0, ["none", "low", "medium", "high"].indexOf(String(settings.power_effort)))
        if (window.hasImportedValue(settings, "incremental"))
            incrementalCompileCheck.checked = Boolean(settings.incremental)
        if (window.hasImportedValue(settings, "retime"))
            retimeCompileCheck.checked = Boolean(settings.retime)
        if (window.hasImportedValue(settings, "gate_clock"))
            gateClockCompileCheck.checked = Boolean(settings.gate_clock)
        if (window.hasImportedValue(settings, "scan_ready"))
            scanReadyCompileCheck.checked = Boolean(settings.scan_ready)
        if (window.hasImportedValue(settings, "boundary_optimization"))
            boundaryOptimizationCheck.checked = Boolean(settings.boundary_optimization)
        if (window.hasImportedValue(settings, "auto_ungroup"))
            autoUngroupBox.currentIndex = Math.max(0, ["none", "area", "delay", "all"].indexOf(String(settings.auto_ungroup)))
        if (window.hasImportedValue(settings, "max_transition"))
            maxTransitionField.text = String(settings.max_transition)
        if (window.hasImportedValue(settings, "max_fanout"))
            maxFanoutField.text = String(settings.max_fanout)
        if (window.hasImportedValue(settings, "max_capacitance"))
            maxCapacitanceField.text = String(settings.max_capacitance)
        if (window.hasImportedValue(settings, "driving_cell"))
            drivingCellField.text = String(settings.driving_cell)
        if (window.hasImportedValue(settings, "output_load"))
            outputLoadField.text = String(settings.output_load)
        if (window.hasImportedValue(settings, "reports")) {
            const reports = settings.reports
            reportQorCheck.checked = reports.indexOf("qor") >= 0
            reportTimingCheck.checked = reports.indexOf("timing") >= 0
            reportAreaCheck.checked = reports.indexOf("area") >= 0
            reportPowerCheck.checked = reports.indexOf("power") >= 0
            reportConstraintsCheck.checked = reports.indexOf("constraints") >= 0
            reportResourcesCheck.checked = reports.indexOf("resources") >= 0
        }
        const warnings = result && result.warnings ? result.warnings : []
        window.tclImportNotice = warnings.length > 0
            ? (languageSettings.language === "en" ? "Imported; check: " : "已导入，请检查：") + warnings.join("；")
            : (languageSettings.language === "en" ? "Imported into the form; review and save" : "已填入表单，请检查后保存")
    }

    function applyImportedDftConfiguration(result) {
        const settings = result && result.settings ? result.settings : ({})
        if (window.hasImportedValue(settings, "dft_enabled"))
            dftEnabledDialogCheck.checked = Boolean(settings.dft_enabled)
        if (window.hasImportedValue(settings, "scan_enabled")) {
            if (Boolean(settings.scan_enabled)) dftEnabledDialogCheck.checked = true
            scanEnabledDialogCheck.checked = Boolean(settings.scan_enabled)
        }
        if (window.hasImportedValue(settings, "mbist_enabled")) {
            if (Boolean(settings.mbist_enabled)) dftEnabledDialogCheck.checked = true
            mbistEnabledDialogCheck.checked = Boolean(settings.mbist_enabled)
        }
        if (window.hasImportedValue(settings, "atpg_enabled")) {
            if (Boolean(settings.atpg_enabled)) dftEnabledDialogCheck.checked = true
            atpgEnabledDialogCheck.checked = Boolean(settings.atpg_enabled)
        }
        if (window.hasImportedValue(settings, "lbist_enabled")) {
            if (Boolean(settings.lbist_enabled)) dftEnabledDialogCheck.checked = true
            lbistEnabledDialogCheck.checked = Boolean(settings.lbist_enabled)
        }
        if (window.hasImportedValue(settings, "scan_clock"))
            dftClockNameField.text = String(settings.scan_clock)
        if (window.hasImportedValue(settings, "reset"))
            dftResetNameField.text = String(settings.reset)
        if (window.hasImportedValue(settings, "reset_active_state"))
            dftResetActiveBox.currentIndex = Number(settings.reset_active_state) === 1 ? 1 : 0
        if (window.hasImportedValue(settings, "scan_chain_count") && Number(settings.scan_chain_count) > 0)
            dftScanChainCountField.text = String(settings.scan_chain_count)
        if (window.hasImportedValue(settings, "max_chain_length") && Number(settings.max_chain_length) > 0)
            dftMaxChainLengthField.text = String(settings.max_chain_length)
        if (window.hasImportedValue(settings, "drc_autofix_enabled"))
            dftAutofixDialogCheck.checked = Boolean(settings.drc_autofix_enabled)
        if (window.hasImportedValue(settings, "drc_autofix_test_mode_port"))
            dftAutofixPortField.text = String(settings.drc_autofix_test_mode_port)
        if (window.hasImportedValue(settings, "atpg_cell_model_files"))
            dftCellModelEditor.text = settings.atpg_cell_model_files.join("\n")
        if (window.hasImportedValue(settings, "atpg_timeout_seconds") && Number(settings.atpg_timeout_seconds) > 0)
            dftAtpgTimeoutField.text = String(settings.atpg_timeout_seconds)
        const warnings = result && result.warnings ? result.warnings : []
        window.tclImportNotice = warnings.length > 0
            ? (languageSettings.language === "en" ? "Imported; check: " : "已导入，请检查：") + warnings.join("；")
            : (languageSettings.language === "en" ? "Imported into the form; review and save" : "已填入表单，请检查后保存")
    }

    function drcAutofixValue(name, fallback) {
        const repair = window.executionValue("drc_autofix", {})
        return repair && repair[name] !== undefined && repair[name] !== null ? repair[name] : fallback
    }

    function agentTerminalValue(name, fallback) {
        const terminal = window.executionValue("agent_terminal", {})
        return terminal && terminal[name] !== undefined && terminal[name] !== null ? terminal[name] : fallback
    }

    function sourceFilesText() {
        const files = window.executionValue("source_files", [])
        if (!files || files.length === undefined)
            return ""
        const rows = []
        for (let index = 0; index < files.length; ++index)
            rows.push(String(files[index]))
        return rows.join("\n")
    }

    function atpgCellModelFilesText() {
        const files = window.executionValue("atpg_cell_model_files", [])
        if (!files || files.length === undefined)
            return ""
        const rows = []
        for (let index = 0; index < files.length; ++index)
            rows.push(String(files[index]))
        return rows.join("\n")
    }

    function refreshEditorRoots() {
        fileEditor.setProjectLocations(window.editorRootLocations())
        window.editorSuggestion = ""
        window.localEditorSuggestions = []
        window.localEditorSuggestionIndex = 0
        window.fileFilter = ""
    }

    function normalizedDirectory(path) {
        const value = String(path || "").trim()
        if (value === "/")
            return value
        return value.replace(/\/+$/, "")
    }

    function pathIsInside(path, parentPath) {
        const child = window.normalizedDirectory(path)
        const parent = window.normalizedDirectory(parentPath)
        return parent.length > 0 && (child === parent || child.startsWith(parent + "/"))
    }

    function configuredEditorWorkspace() {
        const base = window.normalizedDirectory(window.executionValue("workspace_path", window.defaultWorkspacePath))
        if (base.length === 0)
            return ""
        if (!Boolean(window.executionValue("workspace_suffix_enabled", true)) && String(window.currentProject.id || "").length > 0)
            return base + "/" + window.currentProject.id
        return base
    }

    function additionalWorkspaceFolders() {
        const configured = window.executionValue("additional_workspace_folders", [])
        if (!configured || configured.length === undefined)
            return []
        const folders = []
        for (let index = 0; index < configured.length; ++index) {
            const path = window.normalizedDirectory(configured[index])
            if (path.length > 0 && folders.indexOf(path) < 0)
                folders.push(path)
        }
        return folders
    }

    function appendExternalEditorRoot(locations, label, path, kind, searchable) {
        const candidate = window.normalizedDirectory(path)
        if (candidate.length === 0)
            return
        for (let index = 0; index < locations.length; ++index) {
            if (window.pathIsInside(candidate, locations[index].path))
                return
        }
        locations.push({label: label, path: candidate, kind: kind, searchable: searchable})
    }

    function editorRootLocations() {
        const locations = []
        const workspace = window.configuredEditorWorkspace()
        const projectRoot = window.normalizedDirectory(window.currentProject.root || "")
        const rtlRoot = window.normalizedDirectory(window.currentProject.rtlRoot || "")
        const libraryRoot = window.normalizedDirectory(window.currentProject.libraryDir || "")
        if (workspace.length > 0)
            locations.push({label: languageSettings.language === "en" ? "Workspace" : "工作区", path: workspace, kind: "workspace", searchable: true})
        if (projectRoot.length > 0)
            locations.push({label: languageSettings.language === "en" ? "Source project" : "原始项目", path: projectRoot, kind: "project", searchable: true})
        if (rtlRoot.length > 0 && !window.pathIsInside(rtlRoot, projectRoot))
            locations.push({label: "RTL", path: rtlRoot, kind: "rtl", searchable: true})
        if (libraryRoot.length > 0)
            locations.push({label: languageSettings.language === "en" ? "Technology library" : "工艺库", path: libraryRoot, kind: "library", searchable: false})
        window.appendExternalEditorRoot(
            locations,
            languageSettings.language === "en" ? "Synthesis output" : "综合输出",
            window.absoluteProjectPath(window.executionValue("synthesis_output_dir", ""), projectRoot),
            "output",
            true
        )
        window.appendExternalEditorRoot(
            locations,
            languageSettings.language === "en" ? "DFT output" : "DFT 输出",
            window.absoluteProjectPath(window.executionValue("dft_output_dir", ""), projectRoot),
            "output",
            true
        )
        const extras = window.additionalWorkspaceFolders()
        for (let index = 0; index < extras.length; ++index) {
            const parts = extras[index].split("/")
            const folderName = parts.length > 0 && parts[parts.length - 1].length > 0 ? parts[parts.length - 1] : extras[index]
            locations.push({label: folderName, path: extras[index], kind: "additional", searchable: true})
        }
        return locations
    }

    function addEditorWorkspaceFolder(path) {
        const folder = window.normalizedDirectory(path)
        if (folder.length === 0 || projectModel.currentIndex < 0)
            return false
        const folders = window.additionalWorkspaceFolders()
        if (folders.indexOf(folder) >= 0)
            return true
        folders.push(folder)
        return projectModel.updateProject(projectModel.currentIndex, {additionalWorkspaceFolders: folders})
    }

    function removeEditorWorkspaceFolder(path) {
        if (projectModel.currentIndex < 0)
            return false
        const target = window.normalizedDirectory(path)
        const folders = window.additionalWorkspaceFolders().filter(function(folder) {
            return window.normalizedDirectory(folder) !== target
        })
        return projectModel.updateProject(projectModel.currentIndex, {additionalWorkspaceFolders: folders})
    }

    function clearEditorCompletions(cancelAgent) {
        window.editorSuggestion = ""
        window.localEditorSuggestions = []
        window.localEditorSuggestionIndex = 0
        aiCompletionTimer.stop()
        if (cancelAgent)
            fileEditor.cancelAgentCompletion()
    }

    function editorDirectory(path) {
        const value = String(path || "")
        const separator = value.lastIndexOf("/")
        if (separator === 0)
            return "/"
        if (separator > 0)
            return value.slice(0, separator)
        return String(window.currentProject.root || "")
    }

    function openEditorFile(path) {
        window.clearEditorCompletions(true)
        return languageSettings.editorUseVim
            ? fileEditor.selectFile(path)
            : fileEditor.openFile(path)
    }

    function syncEditorImplementation() {
        const path = String(fileEditor.filePath || "")
        window.clearEditorCompletions(true)
        if (path.length === 0)
            return
        if (languageSettings.editorUseVim)
            fileEditor.selectFile(path)
        else
            fileEditor.openFile(path)
    }

    function requestEditorCompletion(explicitRequest) {
        if (languageSettings.editorUseVim || !window.aiCompletionEnabled || !editorText || editorText.text.length === 0)
            return
        const model = window.activeModel()
        window.editorSuggestion = ""
        fileEditor.requestAgentCompletion(
            model.modelId || "",
            model.apiBase || "",
            Number(model.contextWindow || 65536),
            editorText.text,
            editorText.cursorPosition,
            explicitRequest === true
        )
    }

    function updateLocalEditorSuggestions(explicitRequest) {
        if (languageSettings.editorUseVim || !window.localCompletionEnabled || !editorText || !editorText.editorHasFocus || editorText.text.length === 0) {
            window.localEditorSuggestions = []
            window.localEditorSuggestionIndex = 0
            return
        }
        const beforeCursor = editorText.text.slice(0, editorText.cursorPosition)
        const match = beforeCursor.match(/[`A-Za-z_$][A-Za-z0-9_$`-]*$/)
        const prefix = match ? match[0] : ""
        if (explicitRequest !== true && prefix.length < 2 && prefix !== "`") {
            window.localEditorSuggestions = []
            window.localEditorSuggestionIndex = 0
            return
        }
        window.localEditorSuggestions = fileEditor.completionItems(editorText.text, editorText.cursorPosition)
        if (window.localEditorSuggestionIndex >= window.localEditorSuggestions.length)
            window.localEditorSuggestionIndex = 0
    }

    function activeEditorSuggestion() {
        if (!languageSettings.editorUseVim && window.aiCompletionEnabled && window.editorSuggestion.length > 0 && editorText
                && fileEditor.agentCompletionCursor === editorText.cursorPosition
                && fileEditor.content === editorText.text)
            return window.editorSuggestion
        return ""
    }

    function absoluteProjectPath(path, basePath) {
        const value = String(path || "").trim()
        if (value.length === 0)
            return ""
        if (value.startsWith("/"))
            return value
        const base = String(basePath || "").trim()
        return base.length > 0 ? base.replace(/\/$/, "") + "/" + value : value
    }

    function projectRelativePath(path, basePath) {
        const base = String(basePath || "").trim().replace(/\/$/, "")
        const value = String(path || "").trim()
        if (base.length > 0 && value.startsWith(base + "/"))
            return value.slice(base.length + 1)
        return value
    }

    function localPathFromUrl(urlValue) {
        let value = String(urlValue || "")
        if (value.startsWith("file://localhost/"))
            value = value.slice(16)
        else if (value.startsWith("file://"))
            value = value.slice(7)
        try {
            return decodeURIComponent(value)
        } catch (error) {
            return value
        }
    }

    function openConfiguredFile(path, basePath) {
        const resolved = window.absoluteProjectPath(path, basePath)
        if (resolved.length === 0)
            return false
        fileEditor.setProjectLocations(window.editorRootLocations())
        if (!window.openEditorFile(resolved))
            return false
        window.selectedPage = 7
        return true
    }

    function chooseFileForField(field, basePath, filters) {
        configuredFileDialog.targetField = field
        configuredFileDialog.basePath = basePath || ""
        configuredFileDialog.nameFilters = filters
        configuredFileDialog.open()
    }

    function chooseFolderForField(field) {
        configuredFolderDialog.targetField = field
        configuredFolderDialog.open()
    }

    function appendSelectedSourceFiles(files) {
        const activeEditor = projectGeneralDialog.visible ? projectGeneralSourceFilesEditor : sourceFilesEditor
        const rows = activeEditor.text.split(/\r?\n/).map(function(path) { return path.trim() }).filter(function(path) { return path.length > 0 })
        const base = window.currentProject.rtlRoot || window.currentProject.root || ""
        for (let index = 0; index < files.length; ++index) {
            const path = window.localPathFromUrl(files[index])
            const value = window.projectRelativePath(path, base)
            if (value.length > 0 && rows.indexOf(value) < 0)
                rows.push(value)
        }
        activeEditor.text = rows.join("\n")
    }

    function projectMatches(project) {
        const query = projectSearch.trim().toLowerCase()
        if (query.length === 0)
            return false
        const text = [project.name, project.id, project.kind, project.root, project.rtlRoot, project.top, project.goal, project.notes, project.flowProfile].join(" ").toLowerCase()
        return text.indexOf(query) >= 0
    }

    function selectFirstSearchResult() {
        for (let index = 0; index < projectModel.rowCount(); ++index) {
            if (projectMatches(projectModel.projectAt(index))) {
                chooseProject(index)
                projectSearch = ""
                return true
            }
        }
        return false
    }

    function matchingProjectCount() {
        let count = 0
        for (let index = 0; index < projectModel.rowCount(); ++index) {
            if (projectMatches(projectModel.projectAt(index)))
                ++count
        }
        return count
    }

    function insertGoalFragment(fragment) {
        if (!fragment || fragment.length === 0)
            return

        goalEditor.forceActiveFocus()
        const cursor = goalEditor.cursorPosition
        const before = goalEditor.text.slice(0, cursor)
        const after = goalEditor.text.slice(cursor)
        const separator = languageSettings.language === "en" ? "; " : "；"
        const leading = before.trim().length > 0 && !/[\s；;。.!?]$/.test(before) ? separator : ""
        const trailing = after.length > 0 && !/^[\s；;。.!?]/.test(after) ? separator : ""

        goalEditor.text = before + leading + fragment + trailing + after
        goalEditor.cursorPosition = before.length + leading.length + fragment.length
    }

    function launchCurrentProject() {
        if (!persistEditor())
            return
        if (!currentProject.goal || currentProject.goal.trim().length === 0) {
            selectedPage = 1
            goalEditor.forceActiveFocus()
            return
        }
        if (!refreshFlowSessionList())
            return
        projectLaunchSessionDialog.open()
    }

    function refreshFlowSessionList() {
        codexWorkspaceActionAsync("sessions_all", ({}), function(response) {
            if (!response || !response.ok) {
                codexWorkspaceMessage = String(response ? response.message || "" : "")
                return
            }
            flowSessionSummaries = window.preserveLiveSessionSummaries(
                ((response.result || ({})).sessions || []), flowSessionSummaries)
        })
        return true
    }

    function preserveLiveSessionSummaries(fresh, previous) {
        const previousById = ({})
        for (const session of previous || [])
            previousById[String(session.id || "")] = session
        return (fresh || []).map(function(session) {
            const id = String(session.id || "")
            const cached = previousById[id]
            if (!cached || (!agentController.sessionRunning(id) && !agentController.sessionPaused(id)))
                return session
            return Object.assign({}, session, {
                progress: Number(cached.progress || 0),
                phase: String(cached.phase || session.phase || "Ready"),
                execution_status: agentController.sessionPaused(id) ? "paused" : "running",
                execution_state: String(cached.execution_state || session.execution_state || "in_progress"),
                turn_id: String(cached.turn_id || session.turn_id || ""),
                flow_stage_states: cached.flow_stage_states || session.flow_stage_states || ({})
            })
        })
    }

    function findCodexSession(threadId) {
        const id = String(threadId || "")
        for (const session of flowSessionSummaries || []) {
            if (String(session.id || "") === id)
                return session
        }
        for (const session of codexSessions || []) {
            if (String(session.id || "") === id)
                return session
        }
        for (const session of codexSubagentSessions || []) {
            if (String(session.id || "") === id)
                return session
        }
        return null
    }

    function projectForCodexSession(sessionOrId) {
        const session = typeof sessionOrId === "string"
            ? findCodexSession(sessionOrId) : sessionOrId
        if (!session)
            return ({})
        const index = projectModel.indexOfProjectId(String(session.project_id || ""))
        return index >= 0 ? projectModel.projectAt(index) : ({})
    }

    function projectNameForCodexSession(session) {
        const project = projectForCodexSession(session)
        return String(project.name || (session || ({})).project_id || "未知项目")
    }

    function selectNewFlowSession() {
        flowProgressSessionId = ""
        flowProgressSessionName = "新建会话"
        agentController.setFlowDisplaySession("")
        clearCodexSessionState()
        codexSessionProjectId = String(currentProject.id || "")
        agentController.loadSessionActivity([])
        agentController.loadFlowProgressSnapshot(0, "Ready", ({}))
        projectLaunchSessionDialog.close()
        selectedPage = 2
    }

    function startProjectRunInSession(threadId, createNew) {
        const context = window.codexProjectContext(Boolean(createNew))
        context.codexThreadId = createNew ? "" : String(threadId || "")
        context.startNewCodexSession = Boolean(createNew)
        context.agentRun = true
        context.executionMode = "autonomous_run"
        context.agentInteractionMode = "goal"
        flowProgressSessionId = createNew ? "" : String(threadId || "")
        const selectedSession = (flowSessionSummaries.length > 0 ? flowSessionSummaries : codexSessions || []).find(function(session) {
            return String(session.id || "") === String(threadId || "")
        })
        flowProgressSessionName = createNew ? "新建会话" : String(selectedSession ? selectedSession.name || "" : "")
        agentController.setFlowDisplaySession(flowProgressSessionId)
        agentConversationRequested = false
        selectedPage = 2
        projectLaunchSessionDialog.close()
        agentController.run(context, capabilityModel.disabledIds())
    }

    function applyFlowProgressSession(session, threadId) {
        const id = String(threadId || "").trim()
        if (!id || !session)
            return
        const projectId = String(session.project_id || "")
        const projectIndex = projectModel.indexOfProjectId(projectId)
        if (projectIndex < 0) {
            codexWorkspaceMessage = "该会话所属项目未加入 Studio 项目列表。"
            return
        }
        if (projectModel.currentIndex !== projectIndex)
            projectModel.currentIndex = projectIndex
        const sessionChanged = String(codexSessionId || "") !== id
            || String(codexSessionProjectId || "") !== projectId
        flowProgressSessionId = String(session.id || id)
        flowProgressSessionName = String(session.name || "DFT 会话")
        agentController.setFlowDisplaySession(flowProgressSessionId)
        if (sessionChanged) {
            resetCodexSessionViewport()
            codexSessionId = flowProgressSessionId
            codexSessionProjectId = projectId
            codexSessionParentThreadId = String(session.parent_thread_id || "")
            agentController.setActivitySession(codexSessionId)
            codexActivityHistoryExpanded = false
            codexWorkspaceActionAsync("session_read", ({
                thread_id: codexSessionId,
                activity_limit: codexActivityPageLimit
            }), function(historyResponse) {
                if (String(codexSessionId || "") !== id)
                    return
                if (historyResponse && historyResponse.ok)
                    hydrateCodexSessionRead(historyResponse.result || ({}), false)
                else
                    agentController.loadSessionActivity([])
            })
        }
        agentController.loadFlowProgressSnapshot(
            Number(session.progress || 0),
            String(session.phase || "Ready"),
            session.flow_stage_states || ({})
        )
        projectLaunchSessionDialog.close()
        selectedPage = 2
    }

    function selectFlowProgressSession(threadId) {
        const id = String(threadId || "").trim()
        if (!id)
            return
        const cached = (flowSessionSummaries || []).find(function(session) {
            return String(session.id || "") === id
        })
        if (cached) {
            applyFlowProgressSession(cached, id)
            return
        }
        codexWorkspaceActionAsync("session_progress", ({ thread_id: id }), function(response) {
            if (response && response.ok)
                applyFlowProgressSession((response.result || ({})).session || ({}), id)
        })
    }

    function refreshFlowProgressSnapshot() {
        const id = String(flowProgressSessionId || "").trim()
        if (!id)
            return
        const cached = (flowSessionSummaries || []).find(function(session) {
            return String(session.id || "") === id
        })
        if (cached) {
            flowProgressSessionName = String(cached.name || flowProgressSessionName || "DFT 会话")
            agentController.loadFlowProgressSnapshot(
                Number(cached.progress || 0),
                String(cached.phase || "Ready"),
                cached.flow_stage_states || ({})
            )
            return
        }
        codexWorkspaceActionAsync("session_progress", ({ thread_id: id }), function(response) {
            if (!response || !response.ok || String(flowProgressSessionId || "") !== id)
                return
            const session = (response.result || ({})).session || ({})
            flowProgressSessionName = String(session.name || flowProgressSessionName || "DFT 会话")
            agentController.loadFlowProgressSnapshot(
                Number(session.progress || 0),
                String(session.phase || "Ready"),
                session.flow_stage_states || ({})
            )
        })
    }

    function flowSessionIsRunning(threadId) {
        const anyRunIsActive = agentController.running
        return anyRunIsActive && agentController.sessionRunning(String(threadId || ""))
    }

    function mergeSessionProgressSummary(source, projectId, sessionId, progress, phase, stages) {
        const id = String(sessionId || "")
        if (!id)
            return source || []
        const summaries = (source || []).slice()
        let found = false
        for (let index = 0; index < summaries.length; ++index) {
            if (String(summaries[index].id || "") !== id)
                continue
            const liveStatus = agentController.sessionPaused(id) ? "paused"
                : agentController.sessionRunning(id) ? "running" : ""
            summaries[index] = Object.assign({}, summaries[index], {
                project_id: String(summaries[index].project_id || projectId || ""),
                progress: Number(progress || 0),
                phase: String(phase || "Ready"),
                execution_status: liveStatus || String(summaries[index].execution_status || "idle"),
                flow_stage_states: stages || ({})
            })
            found = true
            break
        }
        if (!found) {
            const liveStatus = agentController.sessionPaused(id) ? "paused"
                : agentController.sessionRunning(id) ? "running" : "idle"
            summaries.unshift({
                id: id,
                name: String(projectId || "DFT 会话"),
                project_id: String(projectId || ""),
                progress: Number(progress || 0),
                phase: String(phase || "Ready"),
                execution_status: liveStatus,
                flow_stage_states: stages || ({})
            })
        }
        return summaries
    }

    function runSelectedFlowSession() {
        const id = String(flowProgressSessionId || "").trim()
        if (!id) {
            if (!currentProject.goal || currentProject.goal.trim().length === 0) {
                selectedPage = 1
                goalEditor.forceActiveFocus()
                return
            }
            startProjectRunInSession("", true)
            return
        }
        const session = (flowSessionSummaries.length > 0 ? flowSessionSummaries : codexSessions || []).find(function(item) {
            return String(item.id || "") === id
        })
        const projectIndex = projectModel.indexOfProjectId(String(session ? session.project_id || "" : ""))
        if (projectIndex < 0) {
            codexWorkspaceMessage = session
                ? "该会话所属项目未加入 Studio 项目列表，无法启动。"
                : "会话摘要正在刷新，请稍后再运行。"
            return
        }
        const project = projectModel.projectAt(projectIndex)
        const context = codexProjectContext(false, project)
        context.codexThreadId = id
        context.startNewCodexSession = false
        context.agentRun = true
        context.executionMode = "autonomous_run"
        context.agentInteractionMode = "goal"
        agentController.setFlowDisplaySession(id)
        agentConversationRequested = false
        selectedPage = 2
        agentController.run(context, capabilityModel.disabledIds())
    }

    function openSubagentSessions() {
        if (!codexSessionId)
            return
        subagentSessionDialog.open()
        codexWorkspaceActionAsync("session_children", ({ thread_id: codexSessionId }), function(response) {
            if (response && response.ok)
                codexSubagentSessions = (response.result || ({})).children || []
        })
    }

    function launchCodexGoal(goalText) {
        const goal = String(goalText || "").trim()
        if (goal.length === 0 || !persistEditor() || !ensureCodexChatSession())
            return
        const context = window.codexProjectContext(false)
        context.agentRun = true
        context.executionMode = "autonomous_run"
        context.agentInteractionMode = "goal"
        if (codexSessionId.length > 0)
            context.codexThreadId = codexSessionId
        agentConversationRequested = true
        selectedPage = 3
        agentController.runPrompt(context, goal, capabilityModel.disabledIds())
    }

    function submitAgentPrompt(immediate) {
        const prompt = pendingChatPrompt.trim()
        if (prompt.length === 0 || !persistEditor() || !ensureCodexChatSession())
            return
        let disabledCapabilities = capabilityModel.disabledIds()
        agentConversationRequested = true
        selectedPage = 3
        const context = window.codexProjectContext(false)
        // A follow-up typed after a worker crash continues with the
        // checkpointed context, but remains a new user rollout item.  Only
        // the dedicated recovery button below sets resumeAgentTurn=true.
        if (window.codexSessionRecoveryPending) {
            context.agentRun = true
            context.executionMode = "autonomous_run"
        }
        if (Boolean(immediate) && currentChatSessionRunning())
            agentController.runPromptNow(context, prompt, disabledCapabilities)
        else
            agentController.runPrompt(context, prompt, disabledCapabilities)
        pendingChatPrompt = ""
    }

    // Enter steers an active turn immediately; Ctrl+Enter queues a follow-up.
    function handleChatPromptKey(event) {
        const isSubmitKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (!isSubmitKey)
            return false
        if (event.modifiers & (Qt.ShiftModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        if (event.modifiers & Qt.ControlModifier) {
            submitAgentPrompt(false)
            return true
        }
        submitAgentPrompt(true)
        return true
    }

    function verifyApprovedPatch(proposalId, purpose) {
        const identifier = String(proposalId || "").trim()
        if (identifier.length === 0 || !persistEditor())
            return
        const label = String(purpose || "").trim()
        const prompt = "用户已批准隔离补丁候选 " + identifier
            + (label.length > 0 ? "（" + label + "）" : "")
            + "。仅验证此候选：调用 run_approved_patch，并根据该受控执行的真实结果总结；不要修改源工程、不要提出其他补丁。"
        agentConversationRequested = true
        selectedPage = 3
        agentController.runPrompt(window.codexProjectContext(false), prompt, capabilityModel.disabledIds())
    }

    function statusForAgent() {
        if (agentController.paused)
            return "paused"
        if (agentController.running)
            return "running"
        if (agentController.hasError)
            return "failed"
        if (agentController.phase === "Evidence recorded" || agentController.phase === "Verified")
            return "verified"
        if (agentController.phase.indexOf("Stopped") >= 0)
            return "stopped"
        if (agentController.phase === "Failed")
            return "failed"
        return "ready"
    }

    function currentChatSessionRunning() {
        // Reading the Q_PROPERTY establishes a QML binding dependency;
        // calling sessionRunning() alone does not subscribe to its signal.
        const anyRunActive = agentController.running
        if (!anyRunActive)
            return false
        // A brand-new Chat turn is already running before the worker creates
        // its durable session id. The controller treats an empty id as that
        // in-flight session, so do not hide its state behind an id check.
        return agentController.sessionRunning(codexSessionId)
    }

    function currentChatSessionPaused() {
        const rootPaused = agentController.paused
        const anyRunActive = agentController.running
        return agentController.sessionPaused(codexSessionId)
    }

    function stageState(key) {
        const record = agentController.flowStageStates[key] || ({})
        return record && record.state ? record.state : "ready"
    }

    function substepState(stageKey, index, count) {
        const record = agentController.flowStageStates[stageKey] || ({})
        const state = record && record.state ? record.state : "ready"
        const progress = record && Number(record.progress) >= 0 ? Number(record.progress) : 0
        if (state === "failed")
            return "failed"
        if (state === "verified" || progress >= Math.round(100 * (index + 1) / Math.max(1, count)))
            return "verified"
        if (state === "running" && progress >= Math.round(100 * index / Math.max(1, count)))
            return "running"
        return "ready"
    }

    function substepProgress(stageKey, index, count) {
        const record = agentController.flowStageStates[stageKey] || ({})
        const state = record && record.state ? record.state : "ready"
        const progress = record && Number(record.progress) >= 0 ? Number(record.progress) : 0
        if (state === "verified")
            return 100
        const start = 100 * index / Math.max(1, count)
        const end = 100 * (index + 1) / Math.max(1, count)
        if (progress <= start)
            return 0
        if (progress >= end)
            return 100
        return Math.round((progress - start) * 100 / Math.max(1, end - start))
    }

    // The worker emits stage-local percentages. Convert them through the same
    // dynamic stage boundaries used by the flow cards instead of comparing
    // unrelated raw percentages (for example, a new iteration's 20% with a
    // previous Scan run's 78%).
    function flowOverallProgress() {
        const stages = window.flowStages()
        if (!stages || stages.length === 0)
            return Math.max(0, Math.min(100, Number(agentController.progress || 0)))
        let start = 0
        let best = 0
        let observed = false
        for (let index = 0; index < stages.length; ++index) {
            const stage = stages[index]
            const boundary = Number(stage.boundary || start)
            const record = agentController.flowStageStates[stage.key] || ({})
            const state = String(record.state || "")
            const local = Math.max(0, Math.min(100, Number(record.progress || 0)))
            if (state.length > 0)
                observed = true
            if (state === "verified" || state === "completed") {
                best = Math.max(best, boundary)
            } else if (state === "running" || state === "waiting") {
                best = Math.max(best, start + (boundary - start) * local / 100)
            } else if (state === "failed" || state === "blocked" || state === "needs_review") {
                best = Math.max(best, start + (boundary - start) * Math.min(local, 99) / 100)
            }
            start = boundary
        }
        if (!observed)
            return Math.max(0, Math.min(100, Number(agentController.progress || 0)))
        return Math.round(Math.max(0, Math.min(100, best)))
    }

    function activityTitle(entry) {
        if (entry.kind === "reconnect")
            return window.localizedStoredMessage(String(entry.text || window.localizedUiText("正在重新连接")))
        if (entry.kind === "user")
            return languageSettings.language === "en" ? "You" : "你的请求"
        if (entry.kind === "agent")
            return entry.role === "final"
                ? (languageSettings.language === "en" ? "Agent result" : "Agent 结论")
                : entry.role === "goal"
                    ? (languageSettings.language === "en" ? "Session goal" : "会话 Goal")
                : entry.role === "plan"
                    ? (languageSettings.language === "en" ? "Agent plan" : "Agent 计划")
                : (languageSettings.language === "en" ? "Agent response" : "Agent 回答")
        if (entry.kind === "tool")
            return (languageSettings.language === "en" ? "Tool call" : "工具调用")
                + (entry.name ? " · " + entry.name : "")
        if (entry.kind === "patch")
            return languageSettings.language === "en" ? "Patch proposal" : "补丁候选"
        if (entry.kind === "approval" && String(entry.permissionPath || "").length > 0)
            return languageSettings.language === "en" ? "Path access request" : "请求访问路径"
        if (entry.kind === "approval")
            return (languageSettings.language === "en" ? "Approval required" : "需要人工确认")
                + (entry.name ? " · " + entry.name : "")
        return languageSettings.language === "en" ? "Error" : "错误"
    }

    function isEdaActivity(entry) {
        if (!entry)
            return false
        if (Boolean(entry.eda))
            return true
        const name = String(entry.name || "").toLowerCase()
        return ["run_dft_flow", "wait_dft_job", "status_dft_job", "interrupt_dft_job",
                "run_dft_iteration", "run_dft_optimization", "run_approved_patch"].indexOf(name) >= 0
    }

    function openEdaOutput(entry, index) {
        chatEdaOutputDialog.activityIndex = Number(index) >= 0 ? Number(index) : -1
        chatEdaOutputDialog.syncFromEntry(entry || ({}))
        // Open after the card's TapHandler finishes dispatching the click. This
        // prevents the expandable tool row from consuming the popup action.
        Qt.callLater(function() { chatEdaOutputDialog.open() })
    }

    function activityIcon(entry) {
        return entry.kind === "reconnect" ? "flow" : entry.kind === "user" ? "agent" : entry.kind === "agent" ? "agent" : entry.kind === "tool" ? "skills"
            : entry.kind === "patch" ? "file" : entry.kind === "approval" ? "warning" : "warning"
    }

    function activityAccent(entry) {
        return entry.kind === "reconnect" ? "#397fbd" : entry.kind === "user" ? "#0a84ff" : entry.kind === "agent" ? "#8068ad" : entry.kind === "tool" ? "#397fbd"
            : entry.kind === "patch" ? "#b67a27" : entry.kind === "approval" ? "#b67a27" : "#c34b45"
    }

    function stageSubsteps(key, startBoundary, endBoundary) {
        const labels = {
            readProject: languageSettings.language === "en"
                ? ["Check project folder", "Read project settings", "Collect input files"]
                : ["确认项目目录", "读取项目配置", "整理输入文件"],
            executor: languageSettings.language === "en"
                ? ["Check enabled modules", "Check tool commands", "Prepare isolated workspace"]
                : ["检查已启用模块", "检查工具命令", "准备隔离工作目录"],
            synthesis: languageSettings.language === "en"
                ? ["Read RTL and filelist", "Apply clocks and constraints", "Run synthesis and read reports"]
                : ["读取 RTL 与 filelist", "应用时钟与约束", "执行综合并读取报告"],
            scan: languageSettings.language === "en"
                ? ["Build scan settings", "Run DFT DRC", "Insert scan and read reports"]
                : ["建立扫描链配置", "运行 DFT DRC", "插入扫描链并读取报告"],
            mbist: languageSettings.language === "en"
                ? ["Identify memory macros", "Build MBIST settings", "Run MBIST simulation"]
                : ["识别存储器宏", "建立 MBIST 配置", "运行 MBIST 仿真"],
            atpg: languageSettings.language === "en"
                ? ["Read netlist and models", "Run DRC and add faults", "Generate patterns and coverage report"]
                : ["读取网表与模型", "运行 DRC 并加入故障", "生成测试向量与覆盖率报告"],
            lbist: languageSettings.language === "en"
                ? ["Build PRPG and MISR settings", "Insert LBIST structure", "Run simulation and read results"]
                : ["建立 PRPG 与 MISR", "插入 LBIST 结构", "运行仿真并读取结果"],
            report: languageSettings.language === "en"
                ? ["Collect tool reports", "Check errors and goals", "Generate final run report"]
                : ["收集工具报告", "检查错误与目标", "生成最终运行报告"]
        }[key] || []
        const rows = []
        let previous = startBoundary
        for (let index = 0; index < labels.length; ++index) {
            const next = Math.round(startBoundary + (endBoundary - startBoundary) * (index + 1) / labels.length)
            rows.push({ label: labels[index], startBoundary: previous, boundary: next })
            previous = next
        }
        return rows
    }

    function flowStageExpanded(key) {
        return expandedFlowStageKeys.indexOf(key) >= 0
    }

    function toggleFlowStage(key) {
        const next = expandedFlowStageKeys.slice()
        const index = next.indexOf(key)
        if (index >= 0)
            next.splice(index, 1)
        else
            next.push(key)
        expandedFlowStageKeys = next
    }

    function flowStages() {
        const candidates = [
            { key: "synthesis", zh: "综合", en: "Synthesis", icon: "synthesis", accent: "#3078b7", surface: "#e7f1fb" },
            { key: "scan", zh: "扫描链插入", en: "Scan insertion", icon: "scan", accent: "#16877f", surface: "#e4f5f2" },
            { key: "mbist", zh: "MBIST 仿真", en: "MBIST simulation", icon: "mbist", accent: "#7655b6", surface: "#f0ebfa" },
            { key: "atpg", zh: window.dftToolDisplayName() + " ATPG", en: window.dftToolDisplayName() + " ATPG", icon: "atpg", accent: "#bd7022", surface: "#fbefe1" },
            { key: "lbist", zh: "LBIST 仿真", en: "LBIST simulation", icon: "lbist", accent: "#b44f75", surface: "#faeaf0" }
        ]
        const stages = []
        stages.push({
            key: "readProject",
            label: languageSettings.language === "en" ? "Read project" : "读取项目",
            icon: "projects",
            accent: "#2f8a63",
            surface: "#e7f5ee",
            boundary: 12,
            substeps: stageSubsteps("readProject", 0, 12)
        })
        stages.push({
            key: "executor",
            label: languageSettings.language === "en" ? "Select executor" : "选择执行器",
            icon: "skills",
            accent: "#a06c24",
            surface: "#f8f0e4",
            boundary: 30,
            substeps: stageSubsteps("executor", 12, 30)
        })
        const enabled = candidates.filter(function(stage) { return window.flowModuleValue(stage.key, false) })
        const spacing = enabled.length > 0 ? 56 / (enabled.length + 1) : 56
        let previousBoundary = 30
        for (let index = 0; index < enabled.length; ++index) {
            const stage = enabled[index]
            const stageBoundary = Math.round(30 + spacing * (index + 1))
            stages.push({
                key: stage.key,
                label: languageSettings.language === "en" ? stage.en : stage.zh,
                icon: stage.icon,
                accent: stage.accent,
                surface: stage.surface,
                boundary: stageBoundary,
                substeps: stageSubsteps(stage.key, previousBoundary, stageBoundary)
            })
            previousBoundary = stageBoundary
        }
        stages.push({
            key: "report",
            label: languageSettings.language === "en" ? "Check results" : "检查结果",
            icon: "report",
            accent: "#37845f",
            surface: "#e8f5ee",
            boundary: 100,
            substeps: stageSubsteps("report", previousBoundary, 100)
        })
        return stages
    }

    Connections {
        target: projectModel

        function onCurrentIndexChanged() {
            window.refreshProject()
            window.refreshEditorRoots()
            if (window.selectedPage === 3)
                Qt.callLater(window.refreshCodexWorkspace)
            else if (window.selectedPage === 10)
                Qt.callLater(window.refreshCodexWorkspaceCatalog)
        }

        function onProjectSaved() {
            window.refreshProject()
            window.refreshEditorRoots()
            if (window.selectedPage === 3)
                Qt.callLater(window.refreshCodexWorkspace)
            else if (window.selectedPage === 10)
                Qt.callLater(window.refreshCodexWorkspaceCatalog)
        }

        function onErrorOccurred(message) {
            window.projectSaveMessage = message
            projectSaveMessageTimer.restart()
        }
    }

    Connections {
        target: modelCatalog

        function onErrorOccurred(message) {
            window.modelCatalogError = String(message || "")
        }

        function onCatalogChanged() {
            Qt.callLater(window.syncModelSettingsEditor)
            Qt.callLater(window.syncCodexChatPreferences)
            Qt.callLater(window.refreshCodexModels)
        }

        function onActiveModelChanged() {
            Qt.callLater(window.syncModelSettingsEditor)
            Qt.callLater(window.syncCodexChatPreferences)
            Qt.callLater(window.refreshCodexModels)
        }
    }

    Timer {
        id: projectSaveMessageTimer
        interval: 2600
        repeat: false
        onTriggered: window.projectSaveMessage = ""
    }

    Timer {
        id: codexScrollAcceptanceRetryTimer
        interval: 500
        repeat: false
        onTriggered: window.runChatScrollAcceptance()
    }

    Timer {
        id: codexChatDisplayRebuildTimer
        // Bound model/layout work while a provider streams token deltas.
        interval: 32
        repeat: false
        onTriggered: {
            window.codexChatDisplayRebuildScheduled = false
            window.rebuildCodexChatDisplayEntries()
        }
    }

    Timer {
        id: codexActivityPrependSettleTimer
        interval: 16
        repeat: false
        onTriggered: window.settleCodexActivityPrepend()
    }

    Timer {
        id: codexChatScrollbarHideTimer
        interval: 850
        repeat: false
        onTriggered: window.codexChatScrollbarActive = false
    }

    Connections {
        target: agentController

        function onWorkspaceActionCompleted(requestId, response) {
            const key = String(requestId || "")
            const callbacks = Object.assign({}, window.workspaceActionCallbacks)
            const request = callbacks[key] || ({})
            const callback = request.callback
            delete callbacks[key]
            window.workspaceActionCallbacks = callbacks
            const result = response && response.ok ? (response.result || ({})) : ({})
            if (response && response.ok) {
                if (result.sessions !== undefined) {
                    if (request.action === "sessions_all")
                        window.flowSessionSummaries = window.preserveLiveSessionSummaries(
                            result.sessions, window.flowSessionSummaries)
                    else if (request.projectId === String(window.currentProject.id || ""))
                        window.codexSessions = window.preserveLiveSessionSummaries(
                            result.sessions, window.codexSessions)
                }
                if (result.children !== undefined)
                    window.codexSubagentSessions = result.children
                if (result.session !== undefined && request.action === "session_new")
                    window.applyCodexSessionState(result.session)
                if (request.projectId === String(window.currentProject.id || "")) {
                    if (result.memories !== undefined || result.rules !== undefined || result.skills !== undefined)
                        window.codexWorkspaceCatalog = result
                    if (result.selection !== undefined)
                        window.codexWorkspaceCatalog = Object.assign({}, window.codexWorkspaceCatalog, {
                            selection: result.selection
                        })
                }
                window.codexWorkspaceMessage = ""
            } else if (response) {
                window.codexWorkspaceMessage = String(response.message || "工作台操作失败。")
            }
            if (typeof callback === "function")
                callback(response || ({ ok: false, message: "工作台操作失败。" }))
        }

        function onSessionTitleUpdated(threadId, name) {
                function updateList(source) {
                    const updated = (source || []).slice()
                    for (let i = 0; i < updated.length; ++i) {
                        if (String(updated[i].id || "") !== String(threadId))
                            continue
                        updated[i] = Object.assign({}, updated[i], { name: String(name || "") })
                        return updated
                    }
                    return updated
                }
                window.codexSessions = updateList(window.codexSessions)
                window.flowSessionSummaries = updateList(window.flowSessionSummaries)
        }

        function onParallelSessionStarted(projectId, sessionId) {
            if (String(projectId || "") === window.projectImportProjectId) {
                window.projectImportSessionId = String(sessionId || "")
                return
            }
            if (!window.flowProgressSessionId || window.flowProgressSessionName === "新建会话") {
                window.flowProgressSessionId = String(sessionId)
                window.flowProgressSessionName = String(projectId || "DFT 会话")
                agentController.setFlowDisplaySession(String(sessionId))
            }
            Qt.callLater(window.refreshCodexWorkspace)
            if (window.selectedPage === 2 && window.flowProgressSessionId === String(sessionId))
                Qt.callLater(window.refreshFlowProgressSnapshot)
        }

        function onSessionProgressUpdated(projectId, sessionId, progress, phase, stages) {
            window.flowSessionSummaries = window.mergeSessionProgressSummary(
                window.flowSessionSummaries, projectId, sessionId, progress, phase, stages)
            if (String(projectId || "") === String(window.currentProject.id || ""))
                window.codexSessions = window.mergeSessionProgressSummary(
                    window.codexSessions, projectId, sessionId, progress, phase, stages)
            if (String(sessionId || "") !== String(window.flowProgressSessionId || ""))
                return
            agentController.loadFlowProgressSnapshot(Number(progress || 0), String(phase || "Ready"), stages || ({}))
        }

        function onParallelSessionFinished(projectId, sessionId, successful) {
            if (String(projectId || "") !== window.projectImportProjectId
                    || String(sessionId || "") !== window.projectImportSessionId)
                return
            const project = window.projectImportProject
            const finishedId = String(sessionId || "")
            window.codexWorkspaceActionAsync("session_archive", {thread_id: finishedId}, function(response) {
                if (String(window.currentProject.id || "") === String(projectId || ""))
                    Qt.callLater(window.refreshCodexWorkspace)
                const message = successful
                    ? (languageSettings.language === "en" ? "Project settings import completed." : "项目设置导入完成。")
                    : (languageSettings.language === "en" ? "Project settings import stopped; review its archived session." : "设置导入未能完成，请查看已归档会话中的结果。")
                if (!response || !response.ok)
                    window.showProjectImportToast(message + (languageSettings.language === "en" ? " Session archive failed." : " 会话归档失败。"), true)
                else
                    window.showProjectImportToast(message, true)
                window.projectImportSessionId = ""
                window.projectImportProjectId = ""
                window.projectImportProject = ({})
                window.projectImportRunning = false
            }, project)
        }

        function onRunningChanged() {
            if (!agentController.running && window.selectedPage === 3)
                Qt.callLater(window.refreshCodexWorkspace)
        }

        function onActivityEntriesChanged() {
            window.scheduleCodexChatDisplayRebuild()
            const entries = agentController.activityEntries || []
            if (entries.length === 0)
                return
            const latest = entries[entries.length - 1] || ({})
            const configurationTools = [
                "update_studio_project", "update_studio_model", "set_studio_active_model", "set_studio_capability"
            ]
            if (latest.kind !== "tool" || latest.status !== "completed"
                    || configurationTools.indexOf(String(latest.name || "")) < 0)
                return
            // Agent configuration tools persist through the same catalogs used
            // by the visible pages. Reload those models only after the tool
            // result, so streaming tool-call rows do not block the chat.
            projectModel.reload()
            modelCatalog.reload()
            capabilityModel.reload()
            Qt.callLater(window.syncModelSettingsEditor)
            Qt.callLater(window.syncCodexChatPreferences)
        }

        function onReportFilesChanged() {
            if (window.selectedPage === 6)
                Qt.callLater(window.refreshRunReports)
        }

        function onCodexModelsChanged() {
            window.resolveModelContextRefresh()
            Qt.callLater(window.syncCodexChatPreferences)
        }

        function onCodexModelsLoadingChanged() {
            if (!agentController.codexModelsLoading)
                Qt.callLater(window.finishModelContextRefresh)
        }

        function onFinished(successful) {
            // A provider interruption is resumable even for a flow-launched
            // turn. Keep Chat visible so the checkpoint and Resume action are
            // not hidden by the normal flow-monitor page switch.
            window.selectedPage = window.codexSessionRecoveryPending
                ? 3 : (window.agentConversationRequested ? 3 : 6)
            // The worker persists a recovery checkpoint before provider/tool
            // work.  Refresh it even for flow-monitor runs so an abnormal
            // exit immediately exposes the Resume action in Chat.
            Qt.callLater(window.refreshCodexWorkspace)
            if (window.agentConversationRequested) {
                Qt.callLater(function() {
                    window.refreshCodexWorkspace()
                    if (window.codexPlanRequestPending && !agentController.running) {
                        window.codexPlanRequestPending = false
                        codexPlanDialog.open()
                    }
                })
            }
            if (agentController.requiresSupervisorReview)
                supervisorWarningDialog.open()
        }

        function onCodexSessionReady(projectId, sessionId) {
            if (String(projectId) !== String(window.currentProject.id || ""))
                return
            window.codexSessionId = String(sessionId)
            agentController.setActivitySession(String(sessionId))
            if (!window.flowProgressSessionId || window.flowProgressSessionName === "新建会话") {
                window.flowProgressSessionId = String(sessionId)
                window.flowProgressSessionName = String(window.currentProject.name || "DFT 会话")
                agentController.setFlowDisplaySession(String(sessionId))
            }
            window.refreshCodexWorkspace()
        }

        function onCodexSessionRecoveryRequired(projectId, sessionId, goal, reason) {
            if (String(projectId) !== String(window.currentProject.id || ""))
                return
            const requestedSession = String(sessionId || "").trim()
            if (requestedSession.length > 0) {
                window.codexSessionId = requestedSession
                agentController.setActivitySession(requestedSession)
            }
            window.codexSessionRecoveryPending = true
            window.codexSessionRecoveryGoal = String(goal || window.codexSessionRecoveryGoal || window.codexSessionGoal || window.pendingChatPrompt || "")
            window.codexSessionRecoveryReason = String(reason || "provider_error")
            window.selectedPage = 3
            // The checkpoint is written before the worker emits this signal;
            // refresh the durable session without waiting for a later click.
            Qt.callLater(window.refreshCodexWorkspace)
        }
    }

    Connections {
        target: fileEditor

        function onAgentCompletionChanged() {
            window.editorSuggestion = window.aiCompletionEnabled && editorText
                    && fileEditor.agentCompletionCursor === editorText.cursorPosition
                    && fileEditor.content === editorText.text
                ? fileEditor.agentCompletion : ""
        }

        function onFilePathChanged() {
            window.editorSuggestion = ""
            window.localEditorSuggestions = []
            window.localEditorSuggestionIndex = 0
        }
    }

    Binding {
        target: fileEditor
        property: "maximumLoadedLines"
        value: languageSettings.editorMaximumLines
    }

    Timer {
        id: localCompletionTimer
        interval: 140
        repeat: false
        onTriggered: window.updateLocalEditorSuggestions(false)
    }

    Timer {
        id: aiCompletionTimer
        interval: 480
        repeat: false
        onTriggered: window.requestEditorCompletion(false)
    }

    header: Rectangle {
        height: 52
        color: window.darkMode ? "#20262d" : "#fbfbfc"
        border.color: window.darkMode ? "#39434e" : "#e3e6ea"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            UI.ToolbarButton {
                iconName: "grid"
                implicitWidth: 32
                text: ""
                tooltipText: window.sidebarExpanded ? window.localizedText("hideSidebar") : window.localizedText("showSidebar")
                onClicked: window.sidebarExpanded = !window.sidebarExpanded
            }

            Text {
                text: window.pageTitle()
                color: window.graphite
                font.pixelSize: Math.round(15 * languageSettings.uiScale)
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

            UI.StyledTextField {
                id: globalSearchField
                Layout.preferredWidth: 240
                Layout.minimumWidth: 160
                Layout.preferredHeight: 30
                visible: true
                color: window.chatText
                leftPadding: 33
                rightPadding: 8
                verticalAlignment: TextInput.AlignVCenter
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                placeholderText: languageSettings.language === "en" ? "Search projects and goals" : "搜索项目、目标或顶层模块"
                placeholderTextColor: "#8c97a4"
                selectByMouse: true
                text: window.projectSearch
                onTextEdited: window.projectSearch = text
                Keys.onReturnPressed: window.selectFirstSearchResult()
                Keys.onEscapePressed: {
                    window.projectSearch = ""
                    globalSearchField.clear()
                }
                background: Rectangle {
                    radius: 7
                    color: window.darkMode ? "#171b20" : "#f0f2f5"
                    border.width: globalSearchField.activeFocus ? 2 : 1
                    border.color: globalSearchField.activeFocus ? "#78afe8" : globalSearchField.pointerHovered ? (window.darkMode ? "#5c7892" : "#b4cbe2") : window.chatBorder
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                UI.FlatIcon {
                    anchors.left: parent.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    color: window.chatMuted
                    width: 16
                    height: 16
                }
            }

            UI.StyledComboBox {
                id: languageBox
                Layout.preferredWidth: 144
                Layout.preferredHeight: 30
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: window.localizedText("simplifiedChinese"), value: "zh-CN" },
                    { label: window.localizedText("english"), value: "en" }
                ]
                currentIndex: languageSettings.language === "en" ? 1 : 0
                onActivated: languageSettings.language = currentValue
            }
            Connections {
                target: languageSettings
                function onLanguageChanged() {
                    languageBox.currentIndex = Qt.binding(function() {
                        return languageSettings.language === "en" ? 1 : 0
                    })
                }
            }

            Item {
                id: themeToggle
                Layout.preferredWidth: 68
                Layout.preferredHeight: 32
                Accessible.name: window.darkMode ? window.localizedText("enableLightMode") : window.localizedText("enableDarkMode")

                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: window.darkMode ? "#20262d" : "#e8edf2"
                    border.width: 1
                    border.color: window.darkMode ? "#414d58" : "#cbd4dd"

                    Rectangle {
                        id: themeToggleThumb
                        x: window.darkMode ? parent.width - width - 3 : 3
                        y: Math.round((parent.height - height) / 2)
                        width: 30
                        height: 26
                        radius: 6
                        color: window.darkMode ? "#394651" : "#ffffff"
                        border.width: 1
                        border.color: window.darkMode ? "#566370" : "#cbd4dd"
                        Behavior on x {
                            NumberAnimation { duration: 190; easing.type: Easing.OutCubic }
                        }
                        Behavior on color {
                            ColorAnimation { duration: 150; easing.type: Easing.OutCubic }
                        }
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 2

                        Button {
                            id: lightThemeButton
                            width: 30
                            height: 26
                            hoverEnabled: false
                            Accessible.name: window.localizedText("enableLightMode")
                            Accessible.role: Accessible.Button
                            background: UI.HoverSurface {
                                hovered: lightThemeHover.hovered
                                selected: !window.darkMode
                                cornerRadius: 6
                                idleColor: "transparent"
                                hoverColor: window.darkMode ? "#303a44" : "#dce3eb"
                                selectedColor: "transparent"
                                selectedHoverColor: "transparent"
                                selectedBorderColor: "transparent"
                                pressedColor: window.darkMode ? "#4a5b6b" : "#d3dce5"
                                transitionDuration: 110
                            }
                            contentItem: UI.FlatIcon {
                                anchors.centerIn: parent
                                name: "sun"
                                color: !window.darkMode ? "#c47c20"
                                    : (lightThemeHover.hovered ? "#c5ced6" : "#8995a0")
                                width: 16
                                height: 16
                            }
                            HoverHandler { id: lightThemeHover }
                            onClicked: languageSettings.darkModeEnabled = false
                        }

                        Button {
                            id: darkThemeButton
                            width: 30
                            height: 26
                            hoverEnabled: false
                            Accessible.name: window.localizedText("enableDarkMode")
                            Accessible.role: Accessible.Button
                            background: UI.HoverSurface {
                                hovered: darkThemeHover.hovered
                                selected: window.darkMode
                                cornerRadius: 6
                                idleColor: "transparent"
                                hoverColor: window.darkMode ? "#303a44" : "#dce3eb"
                                selectedColor: "transparent"
                                selectedHoverColor: "transparent"
                                selectedBorderColor: "transparent"
                                pressedColor: window.darkMode ? "#4a5b6b" : "#d3dce5"
                                transitionDuration: 110
                            }
                            contentItem: UI.FlatIcon {
                                anchors.centerIn: parent
                                name: "moon"
                                color: window.darkMode ? "#c3dbef"
                                    : (darkThemeHover.hovered ? "#5c6a77" : "#8995a0")
                                width: 15
                                height: 15
                            }
                            HoverHandler { id: darkThemeHover }
                            onClicked: languageSettings.darkModeEnabled = true
                        }
                    }
                }
                UI.ThemedToolTip {
                    target: lightThemeButton
                    message: lightThemeButton.Accessible.name
                    active: lightThemeHover.hovered
                }
                UI.ThemedToolTip {
                    target: darkThemeButton
                    message: darkThemeButton.Accessible.name
                    active: darkThemeHover.hovered
                }
            }

            UI.ToolbarButton {
                iconName: "settings"
                implicitWidth: 32
                text: ""
                tooltipText: window.localizedText("toolsSettings")
                onClicked: window.openStudioSettings()
            }
        }
    }

    Popup {
        id: searchResultsPopup
        parent: window.contentItem
        x: globalSearchField.mapToItem(window.contentItem, 0, globalSearchField.height + 6).x
        y: globalSearchField.mapToItem(window.contentItem, 0, globalSearchField.height + 6).y
        width: globalSearchField.width
        height: Math.min(292, searchResultsColumn.implicitHeight + 12)
        padding: 6
        visible: window.projectSearch.trim().length > 0
        focus: false
        modal: false
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            radius: 8
            color: window.chatSurface
            border.color: window.chatBorder
            border.width: 1
        }

        contentItem: Column {
            id: searchResultsColumn
            width: searchResultsPopup.availableWidth
            spacing: 2

            Repeater {
                model: projectModel

                delegate: ItemDelegate {
                    required property int index
                    required property string name
                    required property string projectId
                    required property string kind
                    required property string root
                    required property string rtlRoot
                    required property string topModule
                    required property string goal
                    required property string flowProfile

                    readonly property var searchProject: ({
                        name: name,
                        id: projectId,
                        kind: kind,
                        root: root,
                        rtlRoot: rtlRoot,
                        top: topModule,
                        goal: goal,
                        flowProfile: flowProfile
                    })
                    visible: window.projectMatches(searchProject)
                    width: parent.width
                    height: visible ? 42 : 0
                    hoverEnabled: false
                    leftPadding: 8
                    rightPadding: 8
                    HoverHandler { id: searchResultHover }
                    background: Rectangle {
                        radius: 7
                        color: searchResultHover.hovered ? (window.darkMode ? "#29333d" : "#e8f2fb") : "transparent"
                        border.width: searchResultHover.hovered ? 1 : 0
                        border.color: "#c8dff2"
                        Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    }
                    onClicked: {
                        window.chooseProject(index)
                        window.projectSearch = ""
                        globalSearchField.clear()
                    }
                    contentItem: Column {
                        width: parent.width
                        spacing: 2
                        Text {
                            width: parent.width
                            text: name
                            color: window.chatText
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: topModule.length > 0 ? topModule + "  -  " + window.projectTypeLabel(kind) : window.projectTypeLabel(kind)
                            color: window.chatMuted
                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Text {
                width: parent.width
                height: visible ? 38 : 0
                visible: window.matchingProjectCount() === 0
                text: languageSettings.language === "en" ? "No matching project" : "没有匹配的项目"
                color: window.chatMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar

            Layout.fillHeight: true
            Layout.preferredWidth: 58 + 180 * window.sidebarProgress
            color: window.darkMode ? "#1b2127" : "#f0f2f5"
            border.color: window.darkMode ? "#39434e" : "#e1e5ea"
            border.width: 1
            clip: true

            UI.SmoothScrollView {
                anchors.fill: parent
                anchors.margins: 8
                anchors.bottomMargin: 50
                clip: true
                contentWidth: availableWidth

                Column {
                    width: 42 + 178 * window.sidebarProgress
                    spacing: 2

                    Text {
                        text: window.localizedText("workspaceGroup")
                        visible: window.sidebarProgress > 0.01
                        opacity: window.sidebarProgress
                        height: implicitHeight * window.sidebarProgress
                        clip: true
                        color: window.chatMuted
                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                        leftPadding: 10
                        topPadding: 8
                        bottomPadding: 4
                    }
                    UI.SidebarItem {
                        text: window.localizedText("overview")
                        labelProgress: window.sidebarProgress
                        iconName: "grid"
                        selected: window.selectedPage === 0
                        onClicked: window.selectedPage = 0
                    }

                    Row {
                        width: parent.width
                        visible: window.sidebarProgress > 0.01
                        opacity: window.sidebarProgress
                        height: 30 * window.sidebarProgress
                        clip: true

                        Text {
                            width: parent.width - 30
                            height: parent.height
                            text: window.localizedText("settingsGroup")
                            color: "#8b96a4"
                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                            leftPadding: 10
                            bottomPadding: 4
                            verticalAlignment: Text.AlignBottom
                        }
                        UI.ToolbarButton {
                            width: 26
                            height: 26
                            y: Math.max(0, parent.height - height)
                            text: ""
                            iconName: "chevron"
                            iconItem.rotation: window.sidebarSettingsExpanded ? 90 : 0
                            tooltipText: window.sidebarSettingsExpanded
                                ? (languageSettings.language === "en" ? "Hide settings" : "收起设置")
                                : (languageSettings.language === "en" ? "Show settings" : "展开设置")
                            onClicked: window.toggleSidebarSettings()
                        }
                    }
                    UI.SidebarItem {
                        text: window.localizedText("studioSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "settings"
                        selected: window.selectedPage === 9
                        tooltipText: window.localizedText("studioSettings")
                        onClicked: window.openStudioSettings()
                    }
                    UI.SidebarItem {
                        height: 34 * window.sidebarSettingsProgress
                        visible: window.sidebarSettingsProgress > 0.01
                        opacity: window.sidebarSettingsProgress
                        clip: true
                        text: window.localizedText("appearanceSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "editor"
                        selected: window.selectedPage === 4
                        onClicked: window.openStudioSettingsSubpage(4)
                    }
                    UI.SidebarItem {
                        height: 34 * window.sidebarSettingsProgress
                        visible: window.sidebarSettingsProgress > 0.01
                        opacity: window.sidebarSettingsProgress
                        clip: true
                        text: window.localizedText("skillsSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "skills"
                        selected: window.selectedPage === 10
                        onClicked: window.openStudioSettingsSubpage(10)
                    }
                    UI.SidebarItem {
                        height: 34 * window.sidebarSettingsProgress
                        visible: window.sidebarSettingsProgress > 0.01
                        opacity: window.sidebarSettingsProgress
                        clip: true
                        text: window.localizedText("agentToolsSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "terminal"
                        selected: window.selectedPage === 11
                        onClicked: window.openStudioSettingsSubpage(11)
                    }
                    UI.SidebarItem {
                        height: 34 * window.sidebarSettingsProgress
                        visible: window.sidebarSettingsProgress > 0.01
                        opacity: window.sidebarSettingsProgress
                        clip: true
                        text: window.localizedText("modelsContextSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "agent"
                        selected: window.selectedPage === 8
                        onClicked: window.openStudioSettingsSubpage(8)
                    }
                    Row {
                        width: parent.width
                        visible: window.sidebarProgress > 0.01
                        opacity: window.sidebarProgress
                        height: 30 * window.sidebarProgress
                        clip: true

                        Text {
                            width: parent.width - 30
                            height: parent.height
                            text: window.localizedText("projectsGroup")
                            color: "#8b96a4"
                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                            leftPadding: 10
                            bottomPadding: 4
                            verticalAlignment: Text.AlignBottom
                        }
                        UI.ToolbarButton {
                            width: 26
                            height: 26
                            y: Math.max(0, parent.height - height)
                            text: ""
                            iconName: "chevron"
                            iconItem.rotation: window.sidebarProjectListExpanded ? 90 : 0
                            tooltipText: window.sidebarProjectListExpanded
                                ? (languageSettings.language === "en" ? "Hide projects" : "收起项目列表")
                                : (languageSettings.language === "en" ? "Show projects" : "展开项目列表")
                            onClicked: window.toggleSidebarProjectList()
                        }
                    }
                    UI.SidebarItem {
                        text: window.localizedText("currentProjectSettings")
                        labelProgress: window.sidebarProgress
                        iconName: "settings"
                        selected: window.selectedPage === 1
                        tooltipText: window.localizedText("currentProjectSettings")
                        onClicked: window.selectedPage = 1
                    }
                    Repeater {
                        model: projectModel

                        delegate: UI.SidebarItem {
                            required property int index
                            required property string name
                            required property string status

                            height: 36 * window.sidebarProjectListProgress
                            visible: window.sidebarProjectListProgress > 0.01
                            opacity: window.sidebarProjectListProgress
                            clip: true
                            text: name
                            labelProgress: window.sidebarProgress
                            iconName: "projects"
                            isCurrentProject: projectModel.currentIndex === index
                            badge: status === "ready" ? 0 : 1
                            tooltipText: name
                            onClicked: window.chooseProject(index)
                        }
                    }

                    Text {
                        text: window.localizedText("automationGroup")
                        visible: window.sidebarProgress > 0.01
                        opacity: window.sidebarProgress
                        height: implicitHeight * window.sidebarProgress
                        clip: true
                        color: "#8b96a4"
                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                        leftPadding: 10
                        topPadding: 16
                        bottomPadding: 4
                    }
                    UI.SidebarItem {
                        text: window.localizedText("flowMonitor")
                        labelProgress: window.sidebarProgress
                        iconName: "flow"
                        selected: window.selectedPage === 2
                        onClicked: window.selectedPage = 2
                    }
                    UI.SidebarItem {
                        text: window.localizedText("agentMonitor")
                        labelProgress: window.sidebarProgress
                        iconName: "agent"
                        selected: window.selectedPage === 3
                        badge: agentController.running ? 1 : 0
                        onClicked: {
                            window.selectedPage = 3
                            window.refreshCodexWorkspace()
                        }
                    }

                    Text {
                        text: window.localizedText("operatorGroup")
                        visible: window.sidebarProgress > 0.01
                        opacity: window.sidebarProgress
                        height: implicitHeight * window.sidebarProgress
                        clip: true
                        color: "#8b96a4"
                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                        leftPadding: 10
                        topPadding: 16
                        bottomPadding: 4
                    }
                    UI.SidebarItem {
                        text: window.localizedText("manualDebug")
                        labelProgress: window.sidebarProgress
                        iconName: "terminal"
                        selected: window.selectedPage === 5
                        onClicked: window.selectedPage = 5
                    }
                    UI.SidebarItem {
                        text: window.localizedText("fileEditor")
                        labelProgress: window.sidebarProgress
                        iconName: "editor"
                        selected: window.selectedPage === 7
                        onClicked: {
                            window.refreshEditorRoots()
                            window.selectedPage = 7
                        }
                    }
                    UI.SidebarItem {
                        text: window.localizedText("evidence")
                        labelProgress: window.sidebarProgress
                        iconName: "report"
                        selected: window.selectedPage === 6
                        onClicked: window.selectedPage = 6
                    }
                }
            }

            Rectangle {
                x: 10
                y: aboutSidebarItem.y - height - 5
                width: parent.width - 20
                height: 1
                z: 100
                color: window.darkMode ? "#39434e" : "#d8dfe6"
            }
            UI.SidebarItem {
                id: aboutSidebarItem
                x: 8
                y: parent.height - height - 8
                width: parent.width - 16
                height: 36
                z: 100
                text: window.localizedText("about")
                labelProgress: window.sidebarProgress
                iconName: "info"
                selected: aboutDialog.visible
                onClicked: aboutDialog.open()
            }
        }

        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Chat shares the capability scene so provider state stays alive.
            // Settings subpages 4, 10, and 11 share their editor/list controls.
            currentIndex: window.displayedPage === 3 || window.displayedPage === 4
                    || window.displayedPage === 10 || window.displayedPage === 11
                ? 4 : window.displayedPage === 12 ? 10 : window.displayedPage
            clip: true
            transform: Translate {
                id: pageStackTranslation
            }

            Item {
                UI.SmoothScrollView {
                    anchors.fill: parent
                    anchors.margins: 28
                    contentWidth: availableWidth

                    Column {
                        width: parent.width
                        spacing: 18

                        Text {
                            text: window.localizedText("workspace")
                            color: window.graphite
                            font.pixelSize: Math.round(28 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: window.localizedText("workspaceDescription")
                            color: window.muted
                            font.pixelSize: Math.round(14 * languageSettings.uiScale)
                        }

                        GridLayout {
                            width: parent.width
                            columns: width > 1040 ? 3 : 1
                            columnSpacing: 14
                            rowSpacing: 14

                            UI.SectionCard {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 144

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 8

                                    Text {
                                        text: window.localizedText("activeProject")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        width: parent.width
                                        text: window.currentProject.name || window.localizedText("chooseProject")
                                        color: window.graphite
                                        font.pixelSize: Math.round(19 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Row {
                                        spacing: 8

                                        UI.StatusPill {
                                            status: window.currentProject.status || "ready"
                                            label: window.localizedStatus(window.currentProject.status || "ready")
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: window.projectTypeLabel(window.currentProject.kind || "")
                                            color: window.muted
                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: window.currentProject.root || ""
                                        color: "#7b8795"
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }

                            UI.SectionCard {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 144

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 8

                                    Text {
                                        text: window.localizedText("agentState")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        font.weight: Font.Medium
                                    }
                                    Row {
                                        spacing: 10

                                        UI.FlatIcon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "agent"
                                            color: "#0a84ff"
                                            width: 24
                                            height: 24
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: agentController.running ? window.localizedText("working") : window.localizedText("standingBy")
                                            color: window.graphite
                                            font.pixelSize: Math.round(19 * languageSettings.uiScale)
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    ProgressBar {
                                        id: overviewProgress

                                        width: parent.width
                                        from: 0
                                        to: 100
                                    value: window.flowOverallProgress()
                                        background: Rectangle {
                                            implicitHeight: 6
                                            radius: 3
                                            color: "#e7ebf0"
                                        }
                                        contentItem: Item {
                                            implicitHeight: 6

                                            Rectangle {
                                                width: overviewProgress.visualPosition * parent.width
                                                height: parent.height
                                                radius: 3
                                                color: window.accent

                                                Behavior on width {
                                                    NumberAnimation { duration: 220 }
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        text: window.localizedPhase(agentController.phase)
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                }
                            }

                            UI.SectionCard {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 144

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 9

                                    Text {
                                        text: window.localizedText("quickAction")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        font.weight: Font.Medium
                                    }
                                    UI.ToolbarButton {
                                        text: window.localizedText("runSelected")
                                        iconName: "play"
                                        emphasized: true
                                        enabled: !agentController.running && Boolean(window.currentProject.goal)
                                        onClicked: window.launchCurrentProject()
                                    }
                                    Text {
                                        text: capabilityModel.enabledCount() + " " + window.localizedText("capabilitiesEnabled")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                }
                            }
                        }

                        UI.SectionCard {
                            width: parent.width
                            implicitHeight: 92 + projectsRepeater.count * 42

                            Column {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 8

                                RowLayout {
                                    width: parent.width

                                    Text {
                                        text: window.localizedText("projects")
                                        color: window.graphite
                                        font.pixelSize: Math.round(16 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    UI.ToolbarButton {
                                        text: window.localizedText("manage")
                                        iconName: "projects"
                                        onClicked: window.selectedPage = 1
                                    }
                                }

                                Repeater {
                                    id: projectsRepeater
                                    model: projectModel

                                    delegate: UI.HoverSurface {
                                        required property int index
                                        required property string name
                                        required property string kind
                                        required property string root
                                        required property string status
                                        width: parent.width
                                        height: 36
                                        cornerRadius: 6
                                        idleColor: window.chatSurface
                                        hovered: projectHover.hovered
                                        selected: projectModel.currentIndex === index
                                        hoverColor: window.darkMode ? "#29333d" : "#eaf5ff"
                                        selectedColor: window.darkMode ? "#203e59" : "#f0f6ff"
                                        selectedHoverColor: window.darkMode ? "#294a68" : "#e2f1ff"
                                        pressedColor: window.darkMode ? "#31516b" : "#d8e8f7"
                                        idleBorderWidth: selected ? 1 : 0
                                        idleBorderColor: "transparent"
                                        hoverBorderColor: window.darkMode ? "#465666" : "#c9dff1"
                                        selectedBorderColor: window.darkMode ? "#315675" : "#d0e4fc"
                                        transitionDuration: 180
                                        HoverHandler { id: projectHover }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            spacing: 10

                                            UI.FlatIcon {
                                                name: "projects"
                                                color: "#0a84ff"
                                                width: 17
                                                height: 17
                                            }
                                            Text {
                                                Layout.preferredWidth: 210
                                                text: name
                                                color: window.graphite
                                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.preferredWidth: 110
                                                text: kind
                                                color: window.muted
                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: root
                                                color: window.muted
                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                elide: Text.ElideMiddle
                                            }
                                            UI.StatusPill {
                                                status: status
                                                label: window.localizedStatus(status)
                                            }
                                        }

                                        MouseArea {
                                            id: homeProjectPointer
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: window.chooseProject(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                SplitView {
                    anchors.fill: parent
                    anchors.margins: 20
                    orientation: Qt.Horizontal

                    handle: Rectangle {
                        id: projectSplitHandle
                        implicitWidth: 14
                        color: "transparent"

                        HoverHandler {
                            id: projectSplitHover
                            cursorShape: Qt.SizeHorCursor
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 1
                            height: parent.height
                            color: projectSplitHover.hovered ? "#8cbef4" : "#e3e7ec"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                    }

                    UI.SectionCard {
                        SplitView.preferredWidth: 300
                        SplitView.minimumWidth: 250

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: window.localizedText("projects")
                                    color: window.graphite
                                    font.pixelSize: Math.round(16 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton {
                                    text: ""
                                    iconName: "plus"
                                    implicitWidth: 30
                                    tooltipText: window.localizedText("addProjectFolder")
                                    onClicked: addProjectDialog.open()
                                }
                            }

                            UI.SmoothListView {
                                objectName: "qaProjectList"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: projectModel
                                clip: true
                                spacing: 3

                                delegate: UI.SidebarItem {
                                    required property int index
                                    required property string name
                                    required property string status

                                    text: name
                                    iconName: "projects"
                                    selected: projectModel.currentIndex === index
                                    badge: status === "ready" ? 0 : 1
                                    onClicked: {
                                        projectModel.currentIndex = index
                                        window.refreshProject()
                                    }
                                }
                            }

                            UI.ToolbarButton {
                                Layout.fillWidth: true
                                text: window.localizedText("removeProject")
                                iconName: "stop"
                                enabled: window.currentProject.managed === false
                                onClicked: projectModel.removeProject(projectModel.currentIndex)
                            }
                        }
                    }

                    UI.SmoothScrollView {
                        id: projectEditorScroll
                        SplitView.fillWidth: true
                        leftPadding: 16
                        rightPadding: 4
                        topPadding: 2
                        bottomPadding: 2
                        contentWidth: availableWidth

                        Column {
                            width: projectEditorScroll.availableWidth
                            spacing: 14

                            RowLayout {
                                width: parent.width

                                Text {
                                    Layout.fillWidth: true
                                    text: window.currentProject.name || window.localizedText("project")
                                    color: window.graphite
                                    font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: window.projectTypeLabel(window.currentProject.kind || "")
                                    color: window.muted
                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                }
                                Text {
                                    visible: window.projectSaveMessage.length > 0
                                    text: window.projectSaveMessage
                                    color: window.projectSaveMessage === "已保存" || window.projectSaveMessage === "Saved" ? "#167245" : "#b43a34"
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 220
                                }
                                UI.ToolbarButton {
                                    text: window.localizedText("save")
                                    iconName: "save"
                                    tooltipText: languageSettings.language === "en" ? "Save this project" : "保存当前项目"
                                    onClicked: window.persistEditor()
                                }
                                UI.ToolbarButton {
                                    text: window.localizedText("runAgent")
                                    iconName: "play"
                                    emphasized: true
                                    enabled: !agentController.running
                                    tooltipText: languageSettings.language === "en" ? "Save and run the current project" : "保存并运行当前项目"
                                    onClicked: window.launchCurrentProject()
                                }
                            }

                            UI.StyledTextArea {
                                id: projectNotesEditor
                                width: parent.width
                                height: 0
                                visible: false
                                text: window.currentProject.notes || ""
                                placeholderText: window.localizedText("noProjectNote")
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                            }

                            Text {
                                width: parent.width
                                text: window.currentProject.notes || window.localizedText("noProjectNote")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }

                            GridLayout {
                                width: parent.width
                                columns: width >= 760 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12

                                UI.SettingsLauncher {
                                    Layout.fillWidth: true
                                    title: languageSettings.language === "en" ? "Project and flow" : "项目与流程"
                                    description: languageSettings.language === "en" ? "Type, workspace, inputs and enabled stages" : "项目类型、工作区、输入文件与启用阶段"
                                    iconName: "projects"
                                    accentColor: "#397fbd"
                                    onClicked: projectGeneralDialog.open()
                                }
                                UI.SettingsLauncher {
                                    Layout.fillWidth: true
                                    title: languageSettings.language === "en" ? "Synthesis" : "综合设置"
                                    description: languageSettings.language === "en" ? "Libraries, clocks, I/O timing, exceptions and compile" : "工艺库、时钟、I/O 时序、时序例外与编译"
                                    iconName: "synthesis"
                                    accentColor: "#2e78b7"
                                    statusText: synthesisModuleCheck.checked ? (languageSettings.language === "en" ? "Enabled" : "已启用") : (languageSettings.language === "en" ? "Disabled" : "未启用")
                                    onClicked: synthesisSettingsDialog.open()
                                }
                                UI.SettingsLauncher {
                                    Layout.fillWidth: true
                                    title: "DFT"
                                    description: languageSettings.language === "en" ? "Scan, MBIST, ATPG, LBIST and DRC controls" : "扫描链、MBIST、ATPG、LBIST 与 DRC 控制"
                                    iconName: "scan"
                                    accentColor: "#2e9a77"
                                    statusText: dftModuleCheck.checked ? (languageSettings.language === "en" ? "Enabled" : "已启用") : (languageSettings.language === "en" ? "Disabled" : "未启用")
                                    onClicked: dftSettingsDialog.open()
                                }
                                UI.SettingsLauncher {
                                    Layout.fillWidth: true
                                    title: languageSettings.language === "en" ? "Agent target" : "Agent 目标"
                                    description: languageSettings.language === "en" ? "Target, limits, workspace terminal and outputs" : "目标、验收要求、隔离终端与输出目录"
                                    iconName: "agent"
                                    accentColor: "#8567b8"
                                    onClicked: agentProjectSettingsDialog.open()
                                }
                            }

                            UI.SectionCard {
                                width: parent.width
                                visible: false
                                height: 0
                                implicitHeight: 74

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 18
                                    anchors.rightMargin: 18
                                    spacing: 16

                                    Text {
                                        text: window.localizedText("projectType")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledComboBox {
                                        id: projectTypeBox
                                        Layout.preferredWidth: 290
                                        Layout.preferredHeight: 36
                                        textRole: "label"
                                        valueRole: "value"
                                        model: window.projectTypeOptions()
                                        Component.onCompleted: window.syncProjectTypeBox()
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }

                            UI.SectionCard {
                                width: parent.width
                                visible: false
                                height: 0
                                implicitHeight: visible ? 158 : 0

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 10

                                    Text {
                                        text: languageSettings.language === "en" ? "Flow modules" : "流程模块"
                                        color: window.graphite
                                        font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 6
                                        columnSpacing: 12
                                        rowSpacing: 8

                                        UI.StyledCheckBox {
                                            id: synthesisModuleCheck
                                            Layout.row: 0
                                            Layout.column: 0
                                            Layout.fillWidth: true
                                            text: languageSettings.language === "en" ? "Synthesis" : "综合"
                                            checked: window.flowModuleValue("synthesis", true)
                                        }
                                        UI.StyledCheckBox {
                                            id: dftModuleCheck
                                            Layout.row: 0
                                            Layout.column: 1
                                            Layout.fillWidth: true
                                            text: "DFT"
                                            checked: window.flowModuleValue("dft", true)
                                            onToggled: {
                                                if (!checked) {
                                                    scanModuleCheck.checked = false
                                                    mbistModuleCheck.checked = false
                                                    atpgModuleCheck.checked = false
                                                    lbistModuleCheck.checked = false
                                                }
                                            }
                                        }
                                        Item {
                                            Layout.row: 0
                                            Layout.column: 2
                                            Layout.columnSpan: 4
                                            Layout.fillWidth: true
                                        }

                                        UI.StyledCheckBox {
                                            id: scanModuleCheck
                                            Layout.row: 1
                                            Layout.column: 0
                                            Layout.fillWidth: true
                                            enabled: dftModuleCheck.checked
                                            opacity: enabled ? 1 : 0.42
                                            text: languageSettings.language === "en" ? "Scan" : "扫描链"
                                            checked: window.flowModuleValue("scan", true)

                                            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        }
                                        UI.StyledCheckBox {
                                            id: mbistModuleCheck
                                            Layout.row: 1
                                            Layout.column: 1
                                            Layout.fillWidth: true
                                            enabled: dftModuleCheck.checked
                                            opacity: enabled ? 1 : 0.42
                                            text: "MBIST"
                                            checked: window.flowModuleValue("mbist", false)

                                            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        }
                                        UI.StyledCheckBox {
                                            id: atpgModuleCheck
                                            Layout.row: 1
                                            Layout.column: 2
                                            Layout.fillWidth: true
                                            enabled: dftModuleCheck.checked
                                            opacity: enabled ? 1 : 0.42
                                            text: "ATPG"
                                            checked: window.flowModuleValue("atpg", false)

                                            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        }
                                        UI.StyledCheckBox {
                                            id: lbistModuleCheck
                                            Layout.row: 1
                                            Layout.column: 3
                                            Layout.fillWidth: true
                                            enabled: dftModuleCheck.checked
                                            opacity: enabled ? 1 : 0.42
                                            text: "LBIST"
                                            checked: window.flowModuleValue("lbist", false)

                                            Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                }
                            }

                            UI.SectionCard {
                                width: parent.width
                                visible: false
                                height: 0
                                implicitHeight: visible ? projectExecutionGrid.implicitHeight + 36 : 0

                                GridLayout {
                                    id: projectExecutionGrid
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    columns: 2
                                    columnSpacing: 16
                                    rowSpacing: 12

                                    Text { text: window.localizedText("workspacePath"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: workspacePathField
                                        Layout.fillWidth: true
                                        enabled: true
                                        readOnly: false
                                        placeholderText: languageSettings.language === "en"
                                            ? "Choose a workspace folder" : "请选择工作区目录"
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(workspacePathField)
                                    }
                                    Text { text: window.localizedText("workspaceSuffix"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledCheckBox {
                                        id: workspaceSuffixCheck
                                        Layout.fillWidth: true
                                        compact: true
                                        text: window.localizedText("createWorkspacePerRun")
                                    }
                                    Rectangle {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: "#e7ebf0"
                                    }
                                    Text { text: window.localizedText("projectFolder"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: rootField
                                        Layout.fillWidth: true
                                        text: window.currentProject.root || ""
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(rootField)
                                    }
                                    Text { text: window.localizedText("rtlFolder"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: rtlField
                                        Layout.fillWidth: true
                                        text: window.currentProject.rtlRoot || ""
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(rtlField)
                                    }
                                    Text { text: window.localizedText("topModule"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: topField
                                        Layout.fillWidth: true
                                        text: window.currentProject.top || ""
                                        selectByMouse: true
                                    }
                                    Text { text: languageSettings.language === "en" ? "Minimum coverage (%)" : "最低覆盖率 (%)"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: minimumCoverageField
                                        Layout.fillWidth: true
                                        text: window.currentProject.minimumCoverage === undefined || window.currentProject.minimumCoverage === null ? "" : String(window.currentProject.minimumCoverage)
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        placeholderText: languageSettings.language === "en" ? "Optional" : "可选"
                                        selectByMouse: true
                                    }
                                    Text { text: languageSettings.language === "en" ? "Maximum DFT DRC" : "最大 DFT DRC 违规数"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: maximumDrcField
                                        Layout.fillWidth: true
                                        text: window.currentProject.maximumDftDrcViolations === undefined || window.currentProject.maximumDftDrcViolations === null ? "" : String(window.currentProject.maximumDftDrcViolations)
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        placeholderText: languageSettings.language === "en" ? "Optional" : "可选"
                                        selectByMouse: true
                                    }
                                    Text { text: languageSettings.language === "en" ? "Technology library directory" : "工艺库目录"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: libraryDirField
                                        Layout.fillWidth: true
                                        text: window.currentProject.libraryDir || ""
                                        placeholderText: "/path/to/library/db"
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(libraryDirField)
                                    }
                                    Text { text: languageSettings.language === "en" ? "Target library (.db)" : "目标标准单元库 (.db)"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: libraryFileField
                                        Layout.fillWidth: true
                                        text: window.currentProject.libraryFile || ""
                                        placeholderText: "sc_max.db"
                                        onBrowseRequested: window.chooseFileForField(libraryFileField, libraryDirField.text, ["Library database (*.db)", "All files (*)"])
                                    }
                                    Text { text: languageSettings.language === "en" ? "Library profile" : "工艺库 profile"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: libraryProfileField
                                        Layout.fillWidth: true
                                        text: window.currentProject.libraryProfile || ""
                                        placeholderText: "nangate45_scan_cells"
                                        selectByMouse: true
                                    }

                                    Rectangle {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: "#e7ebf0"
                                    }
                                    RowLayout {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true

                                        Text {
                                            text: languageSettings.language === "en" ? "RTL input" : "RTL 输入"
                                            color: window.graphite
                                            font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                            font.weight: Font.DemiBold
                                            topPadding: 4
                                        }
                                        Item { Layout.fillWidth: true }
                                        UI.ToolbarButton {
                                            text: languageSettings.language === "en" ? "Expand editor" : "放大编辑"
                                            iconName: "expand"
                                            onClicked: sourceEditorDialog.open()
                                        }
                                        UI.ToolbarButton {
                                            text: languageSettings.language === "en" ? "Add files" : "添加文件"
                                            iconName: "file"
                                            onClicked: sourceFilesDialog.open()
                                        }
                                    }
                                    Text { text: window.localizedText("rtlSources"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.ScrollableTextArea {
                                        id: sourceFilesEditor
                                        enabled: true
                                        readOnly: false
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 184
                                        text: window.sourceFilesText()
                                        selectByMouse: true
                                        wrapMode: TextEdit.NoWrap
                                        font.family: "Monospace"
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    Text { text: window.localizedText("rtlFilelist"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: fileListField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("filelist", ""))
                                        placeholderText: "filelist/top.f"
                                        editorAvailable: true
                                        onBrowseRequested: window.chooseFileForField(fileListField, window.currentProject.root || "", ["File lists (*.f)", "Supported text (*.f *.tcl *.sdc *.cfg)", "All files (*)"])
                                        onEditRequested: window.openConfiguredFile(fileListField.text, window.currentProject.root || "")
                                    }
                                    Text { text: window.localizedText("sourceLanguage"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledComboBox {
                                        id: sourceLanguageBox
                                        Layout.fillWidth: true
                                        textRole: "label"
                                        valueRole: "value"
                                        model: [
                                            { label: "SystemVerilog", value: "sverilog" },
                                            { label: "Verilog", value: "verilog" }
                                        ]
                                        currentIndex: String(window.executionValue("language", "sverilog")) === "verilog" ? 1 : 0
                                    }

                                    Rectangle {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: "#e7ebf0"
                                    }
                                    Text {
                                        Layout.columnSpan: 2
                                        text: window.localizedText("synthesisSettings")
                                        color: window.graphite
                                        font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                        topPadding: 4
                                    }
                                    Text { text: window.localizedText("constraintFile"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: constraintFileField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("constraint_file", window.executionValue("sdc", "")))
                                        placeholderText: "sdc/top.sdc"
                                        editorAvailable: true
                                        onBrowseRequested: window.chooseFileForField(constraintFileField, window.currentProject.root || "", ["Timing constraints (*.sdc)", "Tcl scripts (*.tcl)", "All files (*)"])
                                        onEditRequested: window.openConfiguredFile(constraintFileField.text, window.currentProject.root || "")
                                    }
                                    Text { text: window.localizedText("loadConstraintFile"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledCheckBox {
                                        id: loadConstraintCheck
                                        compact: true
                                        checked: Boolean(window.executionValue("use_constraint_file", false))
                                        text: ""
                                    }
                                    Text { text: window.localizedText("clockName"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: clockNameField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("clock", ""))
                                        placeholderText: "clk"
                                        selectByMouse: true
                                    }
                                    Text { text: window.localizedText("clockPeriod"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: clockPeriodField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("clock_period_ns", 1000))
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        selectByMouse: true
                                    }
                                    Text { text: window.localizedText("resetName"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: resetNameField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("reset", ""))
                                        placeholderText: "reset_n"
                                        selectByMouse: true
                                    }
                                    Text { text: languageSettings.language === "en" ? "Reset active state" : "复位有效状态"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledComboBox {
                                        id: resetActiveStateBox
                                        Layout.fillWidth: true
                                        textRole: "label"
                                        valueRole: "value"
                                        model: [
                                            { label: languageSettings.language === "en" ? "Active low (0)" : "低有效 (0)", value: 0 },
                                            { label: languageSettings.language === "en" ? "Active high (1)" : "高有效 (1)", value: 1 }
                                        ]
                                        currentIndex: Number(window.executionValue("reset_active_state", 0)) === 1 ? 1 : 0
                                    }
                                    Text { text: languageSettings.language === "en" ? "Scan chain count" : "扫描链数量"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: scanChainCountField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("scan_chain_count", 1))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        selectByMouse: true
                                    }
                                    Text { text: languageSettings.language === "en" ? "Maximum scan-chain length" : "最大扫描链长度"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: maxChainLengthField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("max_chain_length", 1000))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        selectByMouse: true
                                    }
                                    Text {
                                        visible: scanModuleCheck.checked
                                        text: languageSettings.language === "en" ? "DRC AutoFix trial" : "DRC AutoFix 尝试"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledCheckBox {
                                        id: drcAutofixCheck
                                        visible: scanModuleCheck.checked
                                        Layout.fillWidth: true
                                        compact: true
                                        checked: String(window.drcAutofixValue("mode", "off")) === "clock_reset_set"
                                        text: languageSettings.language === "en" ? "Allow generated clock, reset, and set control logic" : "允许生成时钟、复位和置位测试控制逻辑"
                                    }
                                    Text {
                                        visible: scanModuleCheck.checked && drcAutofixCheck.checked
                                        text: languageSettings.language === "en" ? "Existing test-mode port (optional)" : "已有测试模式端口（可选）"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledTextField {
                                        id: drcAutofixPortField
                                        visible: scanModuleCheck.checked && drcAutofixCheck.checked
                                        Layout.fillWidth: true
                                        text: String(window.drcAutofixValue("test_mode_port", ""))
                                        placeholderText: languageSettings.language === "en" ? "Leave empty to let DC create it" : "留空则由 DC 创建"
                                        selectByMouse: true
                                    }
                                    Text { text: window.localizedText("compileEffort"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        UI.StyledComboBox {
                                            id: mapEffortBox
                                            Layout.fillWidth: true
                                            textRole: "label"
                                            valueRole: "value"
                                            model: [{label: "Map: low", value: "low"}, {label: "Map: medium", value: "medium"}, {label: "Map: high", value: "high"}]
                                            currentIndex: ["low", "medium", "high"].indexOf(String(window.executionValue("map_effort", "low")))
                                        }
                                        UI.StyledComboBox {
                                            id: areaEffortBox
                                            Layout.fillWidth: true
                                            textRole: "label"
                                            valueRole: "value"
                                            model: [{label: "Area: none", value: "none"}, {label: "Area: low", value: "low"}, {label: "Area: medium", value: "medium"}, {label: "Area: high", value: "high"}]
                                            currentIndex: ["none", "low", "medium", "high"].indexOf(String(window.executionValue("area_effort", "low")))
                                        }
                                        UI.StyledComboBox {
                                            id: powerEffortBox
                                            Layout.fillWidth: true
                                            textRole: "label"
                                            valueRole: "value"
                                            model: [{label: "Power: none", value: "none"}, {label: "Power: low", value: "low"}, {label: "Power: medium", value: "medium"}, {label: "Power: high", value: "high"}]
                                            currentIndex: ["none", "low", "medium", "high"].indexOf(String(window.executionValue("power_effort", "none")))
                                        }
                                    }
                                    Text { text: window.localizedText("synthesisOutput"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: synthesisOutputField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("synthesis_output_dir", ""))
                                        placeholderText: "/media/6/Projects/DFT_agent_outputs/project/synthesis"
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(synthesisOutputField)
                                    }
                                    Text { text: window.localizedText("dftOutput"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.PathField {
                                        id: dftOutputField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("dft_output_dir", ""))
                                        placeholderText: "/media/6/Projects/DFT_agent_outputs/project/dft"
                                        folderMode: true
                                        onBrowseRequested: window.chooseFolderForField(dftOutputField)
                                    }
                                    Text { text: window.localizedText("runTimeout"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: timeoutSecondsField
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("timeout_seconds", 1800))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        selectByMouse: true
                                    }
                                    Text {
                                        text: languageSettings.language === "en" ? "Agent terminal" : "Agent 隔离终端"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledCheckBox {
                                        id: agentTerminalCheck
                                        Layout.fillWidth: true
                                        compact: true
                                        checked: Boolean(window.agentTerminalValue("enabled", true))
                                        text: languageSettings.language === "en" ? "Enable for this project" : "为当前项目启用"
                                    }
                                    Text {
                                        visible: agentTerminalCheck.checked
                                        text: languageSettings.language === "en" ? "Command wait limit (seconds)" : "命令等待上限（秒）"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledTextField {
                                        id: agentTerminalMaximumSecondsField
                                        visible: agentTerminalCheck.checked
                                        Layout.fillWidth: true
                                        text: String(window.agentTerminalValue("maximum_command_seconds", 300))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        placeholderText: "10 - 900"
                                        selectByMouse: true
                                    }
                                    Rectangle {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: "#e7ebf0"
                                        visible: atpgModuleCheck.checked
                                    }
                                    RowLayout {
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        visible: atpgModuleCheck.checked

                                        Text {
                                            text: window.dftToolDisplayName() + " ATPG"
                                            color: window.graphite
                                            font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                            font.weight: Font.DemiBold
                                            topPadding: 4
                                        }
                                        Item { Layout.fillWidth: true }
                                        UI.ToolbarButton {
                                            text: languageSettings.language === "en" ? "Expand editor" : "放大编辑"
                                            iconName: "expand"
                                            onClicked: atpgCellModelEditorDialog.open()
                                        }
                                    }
                                    Text {
                                        visible: atpgModuleCheck.checked
                                        text: languageSettings.language === "en" ? "Cell model files" : "标准单元 Verilog 文件"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.ScrollableTextArea {
                                        id: atpgCellModelFilesEditor
                                        visible: atpgModuleCheck.checked
                                        enabled: true
                                        readOnly: false
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 132
                                        text: window.atpgCellModelFilesText()
                                        placeholderText: "/media/6/IC/lib/Nangate45/verilog/NangateOpenCellLibrary.v"
                                        selectByMouse: true
                                        wrapMode: TextEdit.NoWrap
                                        font.family: "Monospace"
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    Text {
                                        visible: atpgModuleCheck.checked
                                        text: languageSettings.language === "en" ? "ATPG timeout (seconds)" : "ATPG 超时（秒）"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledTextField {
                                        id: atpgTimeoutSecondsField
                                        visible: atpgModuleCheck.checked
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("atpg_timeout_seconds", 1800))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        selectByMouse: true
                                    }
                                    Text {
                                        visible: atpgModuleCheck.checked
                                        text: languageSettings.language === "en" ? "Maximum iteration rounds" : "最大迭代轮数"
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.StyledTextField {
                                        id: iterationLimitField
                                        visible: atpgModuleCheck.checked
                                        Layout.fillWidth: true
                                        text: String(window.executionValue("iteration_limit", 1))
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        placeholderText: "1 - 20"
                                        validator: IntValidator { bottom: 1; top: 20 }
                                        selectByMouse: true
                                    }
                                }
                            }

                            UI.SectionCard {
                                width: parent.width
                                visible: false
                                height: 0
                                implicitHeight: 198 + goalDictionary.implicitHeight

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 8

                                    Text {
                                        text: window.localizedText("agentGoal")
                                        color: window.muted
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                    UI.GoalDictionary {
                                        id: goalDictionary
                                        width: parent.width
                                        language: languageSettings.language
                                        onInsertRequested: function(fragment) { window.insertGoalFragment(fragment) }
                                    }
                                    UI.StyledTextArea {
                                        id: goalEditor
                                        width: parent.width
                                        height: 134
                                        text: window.currentProject.goal || ""
                                        wrapMode: TextEdit.Wrap
                                        selectByMouse: true
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        UI.FlatIcon {
                            name: "flow"
                            color: "#4e8fd1"
                            width: 28
                            height: 28
                        }
                        Column {
                            Text {
                                text: window.localizedText("flowMonitor")
                                color: window.graphite
                                font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.localizedText("flowEvidence")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        RowLayout {
                            spacing: 8
                                Text {
                                id: flowSessionTitle
                                text: !window.flowProgressSessionName.length
                                    || window.flowProgressSessionName === "新建会话"
                                    ? "从新会话开始运行" : window.flowProgressSessionName
                                color: window.muted
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignRight
                                Layout.maximumWidth: Math.max(120, Math.min(250, window.width * 0.22))
                                MouseArea {
                                    id: sessionTitleHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }
                                UI.ThemedToolTip {
                                    target: flowSessionTitle
                                    active: sessionTitleHover.containsMouse
                                    message: flowSessionTitle.text
                                }
                            }
                            UI.ToolbarButton {
                                text: window.width > 1100 ? window.localizedUiText("会话") : ""
                                iconName: "history"
                                tooltipText: window.localizedUiText("选择历史会话或从空白新会话开始")
                                onClicked: {
                                    if (!window.refreshFlowSessionList())
                                        return
                                    projectLaunchSessionDialog.open()
                                }
                            }
                        }
                        UI.ToolbarButton {
                            text: window.flowSessionIsRunning(window.flowProgressSessionId)
                                ? window.localizedText("stopAgent") : window.localizedText("runAgent")
                            iconName: window.flowSessionIsRunning(window.flowProgressSessionId) ? "stop" : "play"
                            emphasized: !window.flowSessionIsRunning(window.flowProgressSessionId)
                            accentColor: window.flowSessionIsRunning(window.flowProgressSessionId) ? "#ff453a" : window.accent
                            onClicked: window.flowSessionIsRunning(window.flowProgressSessionId)
                                ? agentController.stopSession(window.flowProgressSessionId)
                                : window.runSelectedFlowSession()
                        }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 148

                        Column {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 10

                            RowLayout {
                                width: parent.width

                                Column {
                                    Text {
                                        text: window.currentProject.name || window.localizedText("noProjectSelected")
                                        color: window.graphite
                                        font.pixelSize: Math.round(18 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: window.localizedPhase(agentController.phase)
                                        color: window.muted
                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: window.flowOverallProgress() + "%"
                                    color: window.accent
                                    font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                            }

                            ProgressBar {
                                id: flowProgress

                                width: parent.width
                                from: 0
                                to: 100
                                value: window.flowOverallProgress()
                                background: Rectangle {
                                    implicitHeight: 9
                                    radius: 5
                                    color: "#e7ebf0"
                                }
                                contentItem: Item {
                                    implicitHeight: 9

                                    Rectangle {
                                        width: flowProgress.visualPosition * parent.width
                                        height: parent.height
                                        radius: 5
                                        color: window.accent

                                        Behavior on width {
                                            NumberAnimation {
                                                duration: 240
                                                easing.type: Easing.OutCubic
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: agentController.workspace.length > 0 ? agentController.workspace : window.localizedText("workspacePending")
                                color: "#7b8795"
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        UI.SmoothListView {
                            anchors.fill: parent
                            anchors.margins: 18
                            model: window.flowStages()
                            spacing: 8
                            clip: true

                            delegate: Rectangle {
                                id: stageDelegate
                                required property var modelData
                                readonly property bool expanded: window.flowStageExpanded(modelData.key)
                                readonly property string state: window.stageState(modelData.key)
                                property bool hovered: stageHover.hovered

                                width: ListView.view.width
                                height: 54 + (expanded ? 10 + modelData.substeps.length * 34 : 0)
                                radius: 7
                                color: window.darkMode
                                    ? expanded ? (hovered ? "#293b4b" : "#20262d") : hovered ? "#29333d" : "#1d2329"
                                    : expanded ? (hovered ? "#f3f9ff" : "#ffffff") : hovered ? "#eaf5ff" : "#fafbfc"
                                border.color: window.darkMode
                                    ? expanded ? "#3d5a72" : hovered ? "#465666" : "#39434e"
                                    : expanded ? "#cbdff3" : hovered ? "#c5def2" : "#e7ebef"
                                clip: true

                                Behavior on height { NumberAnimation { duration: 190; easing.type: Easing.OutCubic } }
                                Behavior on color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }
                                Behavior on border.color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }

                                HoverHandler {
                                    id: stageHover
                                    cursorShape: Qt.PointingHandCursor
                                }

                                MouseArea {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 54
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: window.toggleFlowStage(stageDelegate.modelData.key)
                                }

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 54
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 12

                                    Rectangle {
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 30
                                        radius: 6
                                        color: stageDelegate.state === "failed" ? "#fae9e8"
                                            : stageDelegate.state === "verified" ? "#e6f5ed"
                                            : stageDelegate.modelData.surface

                                        UI.FlatIcon {
                                            anchors.centerIn: parent
                                            name: modelData.icon
                                            color: stageDelegate.state === "failed" ? "#bd4540"
                                                : stageDelegate.state === "verified" ? "#24845a"
                                                : stageDelegate.modelData.accent
                                            width: 18
                                            height: 18
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: window.graphite
                                        font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                    }
                                    UI.StatusPill {
                                        status: stageDelegate.state === "ready" ? "disabled" : stageDelegate.state
                                        label: stageDelegate.state === "verified" ? window.localizedText("complete") : stageDelegate.state === "running" ? window.localizedText("active") : stageDelegate.state === "failed" ? window.localizedText("needsReview") : window.localizedText("waiting")
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "chevron"
                                        implicitWidth: 28
                                        implicitHeight: 28
                                        iconItem.rotation: stageDelegate.expanded ? 90 : 0
                                        tooltipText: stageDelegate.expanded
                                            ? (languageSettings.language === "en" ? "Hide substeps" : "收起细分步骤")
                                            : (languageSettings.language === "en" ? "Show substeps" : "展开细分步骤")
                                        onClicked: window.toggleFlowStage(stageDelegate.modelData.key)
                                    }
                                }

                                Item {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.topMargin: 54
                                    height: Math.max(0, parent.height - 54)
                                    opacity: stageDelegate.expanded ? 1 : 0
                                    clip: true

                                    Behavior on opacity { NumberAnimation { duration: 150 } }

                                    Rectangle {
                                        x: 23
                                        y: 2
                                        width: 1
                                        height: Math.max(0, parent.height - 10)
                                        color: "#d5e3f1"
                                    }

                                    Column {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 36
                                        anchors.rightMargin: 14
                                        anchors.top: parent.top
                                        anchors.topMargin: 4

                                        Repeater {
                                            model: stageDelegate.modelData.substeps

                                            delegate: Item {
                                                id: substepRow
                                                required property var modelData
                                                required property int index
                                                width: parent.width
                                                height: 34
                                                readonly property string state: window.substepState(
                                                    stageDelegate.modelData.key,
                                                    index,
                                                    stageDelegate.modelData.substeps.length
                                                )

                                                Rectangle {
                                                    x: -16
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: 7
                                                    height: 7
                                                    radius: 4
                                                    color: substepRow.state === "verified" ? "#30a46c" : substepRow.state === "running" ? window.accent : substepRow.state === "failed" ? "#d74b45" : "#b7c2ce"
                                                }

                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 10

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: substepRow.modelData.label
                                                        color: substepRow.state === "ready" ? window.muted : window.graphite
                                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                        elide: Text.ElideRight
                                                    }
                                                    ProgressBar {
                                                        id: substepProgressBar
                                                        Layout.preferredWidth: Math.min(180, stageDelegate.width * 0.24)
                                                        Layout.preferredHeight: 6
                                                        from: 0
                                                        to: 100
                                                        value: window.substepProgress(
                                                            stageDelegate.modelData.key,
                                                            substepRow.index,
                                                            stageDelegate.modelData.substeps.length
                                                        )
                                                        background: Rectangle { radius: 3; color: "#e7ebf0" }
                                                        contentItem: Item {
                                                            Rectangle {
                                                                width: parent.width * substepProgressBar.visualPosition
                                                                height: parent.height
                                                                radius: 3
                                                                color: substepRow.state === "verified" ? "#30a46c" : window.accent
                                                                Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                                                            }
                                                        }
                                                    }
                                                    Text {
                                                        Layout.preferredWidth: 52
                                                        horizontalAlignment: Text.AlignRight
                                                        text: substepRow.state === "verified"
                                                            ? window.localizedText("complete")
                                                            : substepRow.state === "running"
                                                                ? window.substepProgress(
                                                                    stageDelegate.modelData.key,
                                                                    substepRow.index,
                                                                    stageDelegate.modelData.substeps.length
                                                                ) + "%"
                                                                : substepRow.state === "failed"
                                                                    ? window.localizedText("needsReview")
                                                                    : window.localizedText("waiting")
                                                        color: substepRow.state === "verified" ? "#24845a" : substepRow.state === "failed" ? "#b43a34" : window.muted
                                                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

                    Item {
                        id: agentChatPage
                property bool legacyMonitorVisible: false

                ColumnLayout {
                    visible: agentChatPage.legacyMonitorVisible
                    anchors.fill: parent
                    anchors.margins: 26
                    anchors.rightMargin: agentController.detailedMode ? toolOutputPanel.width + 42 : 26
                    spacing: 16

                    Behavior on anchors.rightMargin {
                        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        UI.FlatIcon {
                            name: "agent"
                            color: "#8a67c7"
                            width: 28
                            height: 28
                        }
                        Column {
                            Text {
                                text: window.localizedText("agentMonitor")
                                color: window.graphite
                                font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.activeModel().provider === "api"
                                    ? (languageSettings.language === "en"
                                        ? "Direct API reasoning with DFT-Agent controlled execution."
                                        : "直接 API 推理，DFT-Agent 负责受控执行与人工确认。")
                                    : (languageSettings.language === "en"
                                        ? "Local llama.cpp inference with DFT-Agent controlled execution."
                                        : "本地 llama.cpp 推理，DFT-Agent 负责受控执行与人工确认。")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        UI.ToolbarButton {
                            text: window.width < 1210 ? "" : window.localizedUiText("会话")
                            iconName: "history"
                            tooltipText: window.localizedUiText("会话管理")
                            onClicked: {
                                window.refreshCodexWorkspace()
                                codexSessionDialog.open()
                            }
                        }
                        UI.ToolbarButton {
                            text: window.width < 1280 ? "" : window.localizedUiText("工作台")
                            iconName: "settings"
                            tooltipText: window.localizedUiText("Memories、Skills、Rules、Hooks")
                            onClicked: {
                                window.refreshCodexWorkspace()
                                codexWorkspaceDialog.open()
                            }
                        }
                        UI.ContextUsageIndicator {
                            id: contextUsageIndicator
                            Layout.preferredWidth: implicitWidth
                            Layout.preferredHeight: implicitHeight
                            language: languageSettings.language
                            totalCapacity: agentController.contextWindow
                            effectiveCapacity: agentController.contextEffectiveWindow
                            autoCompactLimit: agentController.contextAutoCompactLimit
                            inputUsed: agentController.contextInputTokens
                            inputCapacity: agentController.contextInputLimit
                            systemUsed: agentController.contextSystemTokens
                            historyUsed: agentController.contextHistoryTokens
                            toolsUsed: agentController.contextToolTokens
                            toolsCapacity: agentController.contextToolLimit
                            outputUsed: agentController.contextOutputTokens
                            outputCapacity: agentController.contextOutputLimit
                            compacted: agentController.contextCompacted
                            providerMeasured: agentController.contextProviderMeasured
                        }
                        Switch {
                            id: detailedModeSwitch
                            checked: agentController.detailedMode
                            text: window.width < 1180 ? "" : window.localizedText("detailedMode")
                            spacing: 8
                            onToggled: agentController.detailedMode = checked
                            indicator: Rectangle {
                                implicitWidth: 34
                                implicitHeight: 20
                                x: detailedModeSwitch.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 10
                                color: detailedModeSwitch.checked ? window.accent : "#c9d0d8"
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Rectangle {
                                    width: 16
                                    height: 16
                                    x: detailedModeSwitch.checked ? parent.width - width - 2 : 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 8
                                    color: "white"
                                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                }
                            }
                            contentItem: Text {
                                text: detailedModeSwitch.text
                                color: window.graphite
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: detailedModeSwitch.indicator.width + detailedModeSwitch.spacing
                            }
                        }
                        UI.ToolbarButton {
                            text: window.width < 1250 && agentController.detailedMode ? "" : window.localizedText("clear")
                            iconName: "trash"
                            tooltipText: text.length === 0 ? window.localizedText("clear") : ""
                            onClicked: agentController.clearLog()
                        }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 118

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 16

                            Rectangle {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                radius: 7
                                color: agentController.hasError ? "#fae9e8"
                                    : agentController.running ? "#e7f1fb" : "#edf1f5"

                                UI.FlatIcon {
                                    anchors.centerIn: parent
                                    name: agentController.hasError ? "warning" : "flow"
                                    color: agentController.hasError ? "#bd4540"
                                        : agentController.running ? window.accent : "#72808e"
                                    width: 21
                                    height: 21
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Text {
                                    text: languageSettings.language === "en" ? "Current stage" : "当前阶段"
                                    color: window.muted
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: window.activeModel().provider === "api"
                                        ? (languageSettings.language === "en" ? "Reasoning: direct Responses API" : "推理服务：直接 Responses API")
                                        : (languageSettings.language === "en" ? "Reasoning: local compatibility runtime" : "推理服务：本地兼容运行时")
                                    color: "#6d7783"
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: window.localizedPhase(agentController.phase)
                                    color: window.graphite
                                    font.pixelSize: Math.round(16 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: agentController.workspace.length > 0
                                        ? agentController.workspace
                                        : (languageSettings.language === "en" ? "Waiting for an isolated workspace" : "等待隔离工作目录")
                                    color: window.muted
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                    elide: Text.ElideMiddle
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: Math.min(220, parent.width * 0.24)
                                spacing: 6
                                Text {
                                    Layout.alignment: Qt.AlignRight
                                    text: window.flowOverallProgress() + "%"
                                    color: agentController.hasError ? "#bd4540" : window.accent
                                    font.pixelSize: Math.round(18 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                ProgressBar {
                                    id: agentStageProgress
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 7
                                    from: 0
                                    to: 100
                                    value: window.flowOverallProgress()
                                    background: Rectangle { radius: 4; color: "#e5eaf0" }
                                    contentItem: Item {
                                        Rectangle {
                                            width: parent.width * agentStageProgress.visualPosition
                                            height: parent.height
                                            radius: 4
                                            color: agentController.hasError ? "#d0524c" : window.accent
                                            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: languageSettings.language === "en" ? "Activity" : "工作记录"
                                    color: window.graphite
                                    font.pixelSize: Math.round(15 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: agentController.activityEntries.length
                                        + (languageSettings.language === "en" ? " entries" : " 项")
                                    color: window.muted
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Text {
                                    anchors.centerIn: parent
                                    visible: agentController.activityEntries.length === 0
                                    text: languageSettings.language === "en"
                                        ? "Agent responses and tool calls will appear here."
                                        : "Agent 回答和工具调用将在这里按时间显示。"
                                    color: window.muted
                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                }

                                UI.SmoothListView {
                                    id: activityList
                                    anchors.fill: parent
                                    visible: agentController.activityEntries.length > 0
                                    model: agentController.activityEntries
                                    spacing: 10
                                    clip: true
                                    onCountChanged: Qt.callLater(function() { activityList.positionViewAtEnd() })

                                    delegate: Rectangle {
                                        id: activityEntry
                                        required property var modelData
                                        property bool resultExpanded: false
                                        readonly property bool hasToolResult: modelData.kind === "tool"
                                            && String(modelData.result || "").length > 0
                                        readonly property bool showToolResult: hasToolResult && resultExpanded

                                        width: ListView.view.width
                                        height: activityContent.implicitHeight + 24
                                        radius: 7
                                        color: window.darkMode
                                            ? modelData.kind === "user" ? "#20384d" : modelData.kind === "agent" ? "#20262d"
                                                : modelData.kind === "tool" ? "#20282f"
                                                : modelData.kind === "patch" || modelData.kind === "approval" ? "#393225" : "#3c2929"
                                            : modelData.kind === "user" ? "#edf6ff" : modelData.kind === "agent" ? "#fbfafd"
                                                : modelData.kind === "tool" ? "#f8fafc"
                                                : modelData.kind === "patch" || modelData.kind === "approval" ? "#fffaf1" : "#fff7f6"
                                        border.width: 1
                                        border.color: window.darkMode
                                            ? modelData.kind === "user" ? "#315675" : modelData.kind === "agent" ? "#39434e"
                                                : modelData.kind === "tool" ? "#3d4a56"
                                                : modelData.kind === "patch" || modelData.kind === "approval" ? "#64543a" : "#684242"
                                            : modelData.kind === "user" ? "#b9d8fb" : modelData.kind === "agent" ? "#ddd5eb"
                                                : modelData.kind === "tool" ? "#d7e3ee"
                                                : modelData.kind === "patch" || modelData.kind === "approval" ? "#ebd9b6" : "#efd3d0"

                                        Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                                        ColumnLayout {
                                            id: activityContent
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 12
                                            spacing: 8

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                UI.FlatIcon {
                                                    name: window.activityIcon(activityEntry.modelData)
                                                    color: window.activityAccent(activityEntry.modelData)
                                                    width: 17
                                                    height: 17
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: window.activityTitle(activityEntry.modelData)
                                                    color: window.graphite
                                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    visible: modelData.kind === "user"
                                                        && (modelData.status === "queued"
                                                            || (modelData.status === "steering" && !modelData.steeringId))
                                                    text: modelData.status === "steering"
                                                        ? (languageSettings.language === "en" ? "Next to run" : "优先下一条")
                                                        : (languageSettings.language === "en" ? "In queue" : "队列中")
                                                    color: modelData.status === "steering" ? "#b96920" : "#a66819"
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                                Text {
                                                    visible: modelData.kind === "tool"
                                                    text: modelData.status === "running"
                                                        ? (languageSettings.language === "en" ? "Running" : "执行中")
                                                        : modelData.status === "failed"
                                                            ? (languageSettings.language === "en" ? "Failed" : "失败")
                                                            : (languageSettings.language === "en" ? "Completed" : "完成")
                                                    color: modelData.status === "failed" ? "#bd4540"
                                                        : modelData.status === "running" ? window.accent : "#24845a"
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                                Text {
                                                    visible: modelData.kind === "patch"
                                                    text: modelData.status === "approved"
                                                        ? (languageSettings.language === "en" ? "Approved in isolation" : "已批准到隔离副本")
                                                        : modelData.status === "rejected"
                                                            ? (languageSettings.language === "en" ? "Rejected" : "已拒绝")
                                                            : modelData.status === "failed"
                                                                ? (languageSettings.language === "en" ? "Proposal failed" : "候选生成失败")
                                                                : modelData.status === "running"
                                                                    ? (languageSettings.language === "en" ? "Preparing proposal" : "正在生成候选")
                                                                    : (languageSettings.language === "en" ? "Awaiting approval" : "等待确认")
                                                    color: modelData.status === "approved" ? "#24845a"
                                                        : modelData.status === "rejected" || modelData.status === "failed" ? "#bd4540"
                                                        : modelData.status === "running" ? window.accent : "#a66819"
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                                Text {
                                                    visible: modelData.kind === "approval" && String(modelData.permissionPath || "").length > 0
                                                    text: modelData.permissionStatus === "pending"
                                                        ? (languageSettings.language === "en" ? "Waiting up to 60s" : "等待批准，最多 60 秒")
                                                        : modelData.permissionStatus === "approved"
                                                            ? (languageSettings.language === "en" ? "Access granted" : "已授权")
                                                            : modelData.permissionStatus === "denied"
                                                                ? (languageSettings.language === "en" ? "Denied" : "已拒绝")
                                                                : (languageSettings.language === "en" ? "Agent continued; request retained" : "Agent 已继续；请求仍保留")
                                                    color: modelData.permissionStatus === "approved" ? "#24845a"
                                                        : modelData.permissionStatus === "denied" ? "#bd4540" : "#a66819"
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                                Text {
                                                    text: String(modelData.time || "")
                                                    color: "#8a95a1"
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                                UI.ToolbarButton {
                                                    visible: activityEntry.hasToolResult
                                                    text: ""
                                                    iconName: "chevron"
                                                    implicitWidth: 26
                                                    implicitHeight: 26
                                                    iconItem.rotation: activityEntry.showToolResult ? 90 : 0
                                                    tooltipText: activityEntry.showToolResult
                                                        ? (languageSettings.language === "en" ? "Hide tool result" : "收起工具结果")
                                                        : (languageSettings.language === "en" ? "Show tool result" : "展开工具结果")
                                                    onClicked: activityEntry.resultExpanded = !activityEntry.resultExpanded
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "patch" && String(modelData.patchFile || "").length > 0
                                                    text: languageSettings.language === "en" ? "Diff" : "查看 Diff"
                                                    iconName: "code"
                                                    tooltipText: languageSettings.language === "en" ? "Review unified diff" : "查看统一 diff"
                                                    onClicked: {
                                                        patchDiffDialog.patchFile = String(modelData.patchFile)
                                                        patchDiffDialog.patchTitle = String(modelData.purpose || "")
                                                        patchDiffDialog.open()
                                                    }
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "patch" && modelData.status === "awaiting_approval"
                                                    text: languageSettings.language === "en" ? "Approve" : "批准"
                                                    iconName: "check"
                                                    emphasized: true
                                                    onClicked: {
                                                        const decision = agentController.decidePatch(String(modelData.proposalId), true)
                                                        if (!decision.ok) {
                                                            patchDecisionDialog.messageText = String(decision.message || "")
                                                            patchDecisionDialog.open()
                                                        }
                                                    }
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "approval"
                                                        && String(modelData.permissionPath || "").length > 0
                                                        && ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                    text: languageSettings.language === "en" ? "Allow once" : "允许一次"
                                                    iconName: "check"
                                                    emphasized: true
                                                    onClicked: window.decidePathPermission(modelData, "once")
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "approval"
                                                        && String(modelData.permissionPath || "").length > 0
                                                        && ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                    text: languageSettings.language === "en" ? "Allow directory" : "授权此目录"
                                                    iconName: "folder"
                                                    onClicked: window.decidePathPermission(modelData, "session")
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "approval"
                                                        && String(modelData.permissionPath || "").length > 0
                                                        && ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                    text: languageSettings.language === "en" ? "Deny" : "拒绝"
                                                    iconName: "close"
                                                    accentColor: "#bd4540"
                                                    onClicked: window.decidePathPermission(modelData, "deny")
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "approval"
                                                        && String(modelData.permissionPath || "").length === 0
                                                        && String(modelData.requestId || "").length > 0
                                                        && String(modelData.permissionStatus || "pending") === "pending"
                                                    text: languageSettings.language === "en" ? "Approve action" : "批准操作"
                                                    iconName: "check"
                                                    emphasized: true
                                                    onClicked: window.decideNativeTool(modelData, true)
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "approval"
                                                        && String(modelData.permissionPath || "").length === 0
                                                        && String(modelData.requestId || "").length > 0
                                                        && String(modelData.permissionStatus || "pending") === "pending"
                                                    text: languageSettings.language === "en" ? "Reject" : "拒绝"
                                                    iconName: "close"
                                                    accentColor: "#bd4540"
                                                    onClicked: window.decideNativeTool(modelData, false)
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "patch" && modelData.status === "approved"
                                                    text: languageSettings.language === "en" ? "Verify" : "验证"
                                                    iconName: "play"
                                                    emphasized: true
                                                    tooltipText: languageSettings.language === "en" ? "Validate the approved isolated patch" : "验证已批准的隔离补丁"
                                                    onClicked: window.verifyApprovedPatch(String(modelData.proposalId), String(modelData.purpose || ""))
                                                }
                                                UI.ToolbarButton {
                                                    visible: modelData.kind === "patch" && modelData.status === "awaiting_approval"
                                                    text: languageSettings.language === "en" ? "Reject" : "拒绝"
                                                    iconName: "close"
                                                    accentColor: "#bd4540"
                                                    onClicked: {
                                                        const decision = agentController.decidePatch(String(modelData.proposalId), false)
                                                        if (!decision.ok) {
                                                            patchDecisionDialog.messageText = String(decision.message || "")
                                                            patchDecisionDialog.open()
                                                        }
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                visible: String(modelData.text || "").length > 0
                                                text: String(modelData.text || "")
                                                color: "#34404d"
                                                font.family: modelData.kind === "tool" ? "Monospace" : window.font.family
                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                wrapMode: Text.WrapAnywhere
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: toolResultText.implicitHeight + 18
                                                visible: activityEntry.showToolResult
                                                radius: 6
                                                color: "#eef3f7"
                                                border.color: "#d7e0e8"

                                                Text {
                                                    id: toolResultText
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 9
                                                    text: (languageSettings.language === "en" ? "Tool result\n" : "工具结果\n")
                                                        + String(modelData.result || "")
                                                    color: "#43515e"
                                                    font.family: "Monospace"
                                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                    wrapMode: Text.WrapAnywhere
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 106

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10
                            readonly property bool hasPrompt: window.pendingChatPrompt.trim().length > 0
                            readonly property bool sessionRunning: window.currentChatSessionRunning()
                            readonly property bool sessionPaused: window.currentChatSessionPaused()
                            readonly property bool showingPause: sessionRunning && !hasPrompt
                            readonly property bool showingSteer: sessionRunning && hasPrompt
                            readonly property bool showingRecovery: window.codexSessionRecoveryPending
                                && !sessionRunning && !hasPrompt

                            UI.FlatIcon {
                                name: "agent"
                                color: window.accent
                                width: 20
                                height: 20
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 9
                            }
                            UI.StyledTextArea {
                                id: legacyAgentPromptEditor
                                property bool syncingPrompt: false
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 0
                                placeholderText: languageSettings.language === "en"
                                    ? "Ask the DFT Agent about this project..."
                                    : "向 DFT Agent 说明下一步需要处理的项目问题..."
                                wrapMode: TextEdit.Wrap
                                Component.onCompleted: text = window.pendingChatPrompt
                                onTextChanged: {
                                    if (!syncingPrompt && window.pendingChatPrompt !== text)
                                        window.pendingChatPrompt = text
                                }
                                Connections {
                                    target: window
                                    function onPendingChatPromptChanged() {
                                        if (legacyAgentPromptEditor.text === window.pendingChatPrompt)
                                            return
                                        legacyAgentPromptEditor.syncingPrompt = true
                                        legacyAgentPromptEditor.text = window.pendingChatPrompt
                                        legacyAgentPromptEditor.syncingPrompt = false
                                    }
                                }
                                Keys.onPressed: function(event) {
                                    event.accepted = window.handleChatPromptKey(event)
                                }
                            }
                            UI.ToolbarButton {
                                Layout.alignment: Qt.AlignBottom
                                Layout.fillWidth: false
                                Layout.preferredWidth: (parent.showingPause || parent.showingRecovery) ? 38 : implicitWidth
                                Layout.minimumWidth: (parent.showingPause || parent.showingRecovery) ? 38 : 0
                                Layout.preferredHeight: (parent.showingPause || parent.showingRecovery) ? 38 : 32
                                Layout.minimumHeight: (parent.showingPause || parent.showingRecovery) ? 38 : 32
                                text: parent.showingSteer
                                    ? (languageSettings.language === "en" ? "Steer" : "立即引导")
                                    : ((parent.showingPause || parent.showingRecovery) ? "" : (languageSettings.language === "en" ? "Send" : "发送"))
                                iconName: parent.showingRecovery ? "play"
                                    : (parent.showingSteer ? "bolt" : (parent.showingPause ? (parent.sessionPaused ? "play" : "stop") : "play"))
                                emphasized: !parent.showingSteer && !parent.showingPause && !parent.showingRecovery
                                enabled: parent.showingPause || parent.showingRecovery || parent.hasPrompt
                                opacity: enabled ? 1 : 0.55
                                iconColor: parent.showingRecovery ? (window.darkMode ? "#b4ccdf" : "#526b7d")
                                    : parent.showingSteer ? (window.darkMode ? "#f1ad60" : "#d9822b")
                                    : parent.sessionPaused ? (window.darkMode ? "#78c797" : "#3a8a63")
                                    : (window.darkMode ? "#b4ccdf" : "#526b7d")
                                useLabelColor: parent.showingSteer
                                labelColor: window.darkMode ? "#f1b56f" : "#a95d15"
                                idleSurfaceColor: (parent.showingPause || parent.showingRecovery)
                                    ? (window.darkMode ? (parent.sessionPaused ? "#263b31" : "#293846")
                                        : (parent.showingRecovery ? "#dce8f0" : (parent.sessionPaused ? "#e3f2e9" : "#dce8f0")))
                                    : "transparent"
                                hoverSurfaceColor: (parent.showingPause || parent.showingRecovery)
                                    ? (window.darkMode ? (parent.sessionPaused ? "#314b3d" : "#364c5e")
                                        : (parent.showingRecovery ? "#cddde8" : (parent.sessionPaused ? "#d5ebdf" : "#cddde8")))
                                    : (window.darkMode ? "#293b4b" : "#eef5fb")
                                surfaceRadius: (parent.showingPause || parent.showingRecovery) ? 18 : 7
                                tooltipText: parent.showingSteer
                                    ? (languageSettings.language === "en" ? "Send guidance to the active turn without stopping its tools" : "立即发送引导；当前工具不中断")
                                    : (parent.showingRecovery
                                       ? (languageSettings.language === "en" ? "Resume the interrupted Agent turn" : "从最近检查点恢复中断的 Agent 回合")
                                       : (parent.showingPause
                                          ? (parent.sessionPaused ? (languageSettings.language === "en" ? "Continue" : "继续运行") : (languageSettings.language === "en" ? "Pause" : "暂停当前回合"))
                                          : (languageSettings.language === "en" ? "Enter steers now; Ctrl+Enter queues; Shift+Enter newline" : "Enter 立即引导；Ctrl+Enter 排队；Shift+Enter 换行")))
                                onClicked: {
                                    if (parent.showingRecovery) {
                                        window.resumeCodexSession()
                                    } else if (parent.showingPause) {
                                        agentController.setFlowDisplaySession(window.codexSessionId)
                                        if (parent.sessionPaused)
                                            agentController.resume()
                                        else
                                            agentController.pause()
                                    } else if (parent.showingSteer) {
                                        window.submitAgentPrompt(true)
                                    } else {
                                        window.submitAgentPrompt()
                                    }
                                }
                            }
                        }
                    }
                }

                UI.SectionCard {
                    id: toolOutputPanel
                    readonly property real maximumPanelWidth: Math.max(300, parent.width - 680)
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 26
                    anchors.rightMargin: 26
                    anchors.bottomMargin: 26
                    width: agentChatPage.legacyMonitorVisible && agentController.detailedMode
                        ? Math.max(300, Math.min(window.requestedToolOutputPanelWidth, maximumPanelWidth)) : 0
                    opacity: agentController.detailedMode ? 1 : 0
                    visible: agentChatPage.legacyMonitorVisible && width > 1
                    clip: true

                    Behavior on width {
                        enabled: !toolOutputResizeDrag.active
                        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                    }
                    Behavior on opacity { NumberAnimation { duration: 120 } }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon {
                                name: "terminal"
                                color: "#4f8cc9"
                                width: 18
                                height: 18
                            }
                            Text {
                                Layout.fillWidth: true
                                text: window.localizedText("icToolOutput")
                                color: window.graphite
                                font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            UI.ToolbarButton {
                                text: window.localizedText("clear")
                                iconName: "trash"
                                onClicked: agentController.clearToolOutput()
                            }
                        }

                        UI.TerminalLogView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            outputText: agentController.toolOutput
                            emptyText: window.localizedText("waitingToolOutput")
                            fontPixelSize: languageSettings.toolOutputFontSize
                            onFontPixelSizeRequested: function(pixelSize) {
                                languageSettings.toolOutputFontSize = pixelSize
                            }
                        }
                    }
                }

                Item {
                    id: toolOutputResizeHandle
                    property real dragStartWidth: 0
                    x: toolOutputPanel.x - width / 2
                    y: toolOutputPanel.y
                    width: 16
                    height: toolOutputPanel.height
                    visible: agentChatPage.legacyMonitorVisible && agentController.detailedMode && toolOutputPanel.visible
                    z: 4

                    HoverHandler {
                        id: toolOutputResizeHover
                        cursorShape: Qt.SizeHorCursor
                    }
                    DragHandler {
                        id: toolOutputResizeDrag
                        target: null
                        xAxis.enabled: true
                        yAxis.enabled: false
                        onActiveChanged: {
                            if (active) {
                                toolOutputResizeHandle.dragStartWidth = toolOutputPanel.width
                            } else {
                                languageSettings.toolOutputPanelWidth = Math.round(toolOutputPanel.width)
                                window.requestedToolOutputPanelWidth = languageSettings.toolOutputPanelWidth
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            window.requestedToolOutputPanelWidth = Math.max(
                                300,
                                Math.min(
                                    toolOutputPanel.maximumPanelWidth,
                                    toolOutputResizeHandle.dragStartWidth - translation.x
                                )
                            )
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        width: toolOutputResizeHover.hovered || toolOutputResizeDrag.active ? 3 : 1
                        height: parent.height - 20
                        radius: width / 2
                        color: toolOutputResizeHover.hovered || toolOutputResizeDrag.active ? "#78afe8" : "#d9e1e9"
                        Behavior on width { NumberAnimation { duration: 90 } }
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                }
            }

                Item {
                UI.SmoothScrollView {
                    id: settingsPageScroll
                    anchors.fill: parent
                    anchors.margins: 26
                    contentWidth: availableWidth
                    visible: window.displayedPage !== 3

                    ColumnLayout {
                        width: settingsPageScroll.availableWidth
                        spacing: 16

                    RowLayout {
                        Layout.fillWidth: true

                        Column {
                            Layout.fillWidth: true
                            Text {
                                text: window.localizedText(window.displayedPage === 4
                                    ? "appearanceSettings"
                                    : window.displayedPage === 10 ? "skillsSettings" : "agentToolsSettings")
                                color: window.graphite
                                font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.localizedText(window.displayedPage === 4
                                    ? "appearanceSettingsDescription"
                                    : window.displayedPage === 10 ? "skillsSettingsDescription" : "agentToolsSettingsDescription")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }
                        }
                        UI.ToolbarButton {
                            text: window.localizedText("studioSettings")
                            iconName: "chevron"
                            iconItem.rotation: 180
                            onClicked: window.openStudioSettings()
                        }
                        Text {
                            visible: window.displayedPage === 10 || window.displayedPage === 11
                            text: window.displayedPage === 10
                                ? window.workspaceEnabledSkillCount() + " / " + (window.codexWorkspaceCatalog.skills || []).length + " " + window.localizedText("enabled")
                                : capabilityModel.enabledCount() + " / " + capabilityModel.totalCount() + " " + window.localizedText("enabled")
                            color: window.muted
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                        }
                    }

                    UI.SectionCard {
                        visible: window.displayedPage === 4
                        Layout.fillWidth: true
                        implicitHeight: editorSettingsContent.implicitHeight + 36

                        ColumnLayout {
                            id: editorSettingsContent
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                UI.FlatIcon { name: "code"; color: "#0a84ff"; width: 20; height: 20 }
                                Text {
                                    text: languageSettings.language === "en" ? "File editor" : "文件编辑器"
                                    color: window.graphite
                                    font.pixelSize: Math.round(15 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                columnSpacing: 12
                                rowSpacing: 10

                                Text { text: languageSettings.language === "en" ? "Font" : "字体"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: ["Monospace", "JetBrains Mono", "Fira Code", "Noto Sans Mono"]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.editorFontFamily))
                                    onActivated: languageSettings.editorFontFamily = currentText
                                }
                                Text { text: languageSettings.language === "en" ? "Text / Vim size" : "文本 / Vim 字号"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.editorFontSize))
                                    onActivated: languageSettings.editorFontSize = currentValue
                                }
                                Text { text: languageSettings.language === "en" ? "Line height" : "行高"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 30, 32]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.editorLineHeight))
                                    onActivated: languageSettings.editorLineHeight = currentValue
                                }
                                Text { text: languageSettings.language === "en" ? "Tab size" : "制表符"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [2, 3, 4, 5, 6, 7, 8]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.editorTabSize))
                                    onActivated: languageSettings.editorTabSize = currentValue
                                }

                                UI.StyledCheckBox {
                                    text: languageSettings.language === "en" ? "Spaces" : "插入空格"
                                    compact: true
                                    checked: languageSettings.editorInsertSpaces
                                    onToggled: languageSettings.editorInsertSpaces = checked
                                }
                                UI.StyledCheckBox {
                                    text: languageSettings.language === "en" ? "Wrap lines" : "自动换行"
                                    compact: true
                                    checked: languageSettings.editorWordWrap
                                    onToggled: languageSettings.editorWordWrap = checked
                                }
                                UI.StyledCheckBox {
                                    text: languageSettings.language === "en" ? "Smooth scrolling" : "平滑滚动"
                                    compact: true
                                    checked: languageSettings.editorSmoothScrolling
                                    onToggled: languageSettings.editorSmoothScrolling = checked
                                }
                                UI.StyledCheckBox {
                                    text: languageSettings.language === "en" ? "Line numbers" : "显示行号"
                                    compact: true
                                    checked: languageSettings.editorShowLineNumbers
                                    onToggled: languageSettings.editorShowLineNumbers = checked
                                }
                                Text {
                                    text: languageSettings.language === "en" ? "Long-file lines" : "长文件载入行数"
                                    color: window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [2000, 5000, 10000, 20000, 50000, 100000]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.editorMaximumLines))
                                    onActivated: languageSettings.editorMaximumLines = currentValue
                                }
                                UI.StyledCheckBox {
                                    text: languageSettings.language === "en" ? "Use Vim editor" : "使用 Vim 编辑器"
                                    compact: true
                                    checked: languageSettings.editorUseVim
                                    onToggled: {
                                        languageSettings.editorUseVim = checked
                                        window.syncEditorImplementation()
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    UI.SectionCard {
                        visible: window.displayedPage === 4
                        Layout.fillWidth: true
                        implicitHeight: terminalDisplaySettingsContent.implicitHeight + 36

                        ColumnLayout {
                            id: terminalDisplaySettingsContent
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                UI.FlatIcon { name: "terminal"; color: "#30a46c"; width: 20; height: 20 }
                                Text {
                                    text: languageSettings.language === "en" ? "Terminal display" : "终端显示"
                                    color: window.graphite
                                    font.pixelSize: Math.round(15 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: width >= 720 ? 4 : 2
                                columnSpacing: 12
                                rowSpacing: 10

                                Text { text: languageSettings.language === "en" ? "Project terminal size" : "项目终端字号"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.terminalFontSize))
                                    onActivated: languageSettings.terminalFontSize = currentValue
                                }
                                Text { text: languageSettings.language === "en" ? "IC tool output size" : "IC 工具输出字号"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28]
                                    currentIndex: Math.max(0, model.indexOf(languageSettings.toolOutputFontSize))
                                    onActivated: languageSettings.toolOutputFontSize = currentValue
                                }
                            }
                        }
                    }

                    UI.SectionCard {
                        visible: window.displayedPage === 4
                        Layout.fillWidth: true
                        implicitHeight: globalDisplaySettingsContent.implicitHeight + 36

                        ColumnLayout {
                            id: globalDisplaySettingsContent
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                UI.FlatIcon { name: "settings"; color: "#7b6bb2"; width: 20; height: 20 }
                                Text {
                                    Layout.fillWidth: true
                                    text: window.localizedText("globalScale")
                                    color: window.graphite
                                    font.pixelSize: Math.round(15 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: width >= 560 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 8
                                Text {
                                    text: window.localizedText("globalScaleDescription")
                                    color: window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                UI.StyledComboBox {
                                    id: globalScaleBox
                                    Layout.fillWidth: true
                                    textRole: "label"
                                    valueRole: "value"
                                    model: [
                                        { label: "80%", value: 0.8 },
                                        { label: "90%", value: 0.9 },
                                        { label: "100%", value: 1.0 },
                                        { label: "110%", value: 1.1 },
                                        { label: "120%", value: 1.2 },
                                        { label: "130%", value: 1.3 },
                                        { label: "140%", value: 1.4 }
                                    ]
                                    currentIndex: Math.max(0, Math.round((languageSettings.uiScale - 0.8) * 10))
                                    onActivated: languageSettings.uiScale = Number(currentValue)
                                }
                            }
                        }
                    }

                    UI.SectionCard {
                        visible: window.displayedPage === 10
                        Layout.fillWidth: true
                        implicitHeight: skillSettingsContent.implicitHeight + 30

                        ColumnLayout {
                            id: skillSettingsContent
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                UI.FlatIcon { name: "skills"; color: "#0a84ff"; width: 18; height: 18 }
                                Text {
                                    Layout.fillWidth: true
                                    text: languageSettings.language === "en" ? "Available skills" : "可用 Skills"
                                    color: window.graphite
                                    font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: String((window.codexWorkspaceCatalog.skills || []).length)
                                    color: window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: !currentProject.id
                                text: languageSettings.language === "en" ? "Select a project to manage the same skill selection used in Chat." : "选择项目后，可在此管理与 Chat 共享的 Skill 选择。"
                                color: window.muted
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }
                            Repeater {
                                model: window.codexWorkspaceCatalog.skills || []
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 60
                                    radius: 6
                                    color: window.darkMode ? "#20262d" : "#fbfcfe"
                                    border.color: window.darkMode ? "#39434e" : "#e1e7ed"

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 8
                                        spacing: 10
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.name || "") + " · "
                                                    + (String(modelData.scope || "") === "dft-agent"
                                                        ? (languageSettings.language === "en" ? "Built-in" : "内置")
                                                        : (languageSettings.language === "en" ? "Project" : "项目"))
                                                color: window.chatText
                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.description || "")
                                                color: window.chatMuted
                                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                        }
                                        UI.StyledCheckBox {
                                            text: languageSettings.language === "en" ? "Use in Chat" : "在 Chat 中启用"
                                            checked: window.workspaceSkillEnabled(modelData)
                                            enabled: Boolean(currentProject.id)
                                            onToggled: window.setWorkspaceSourceEnabled("skill", String(modelData.id), checked)
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: Boolean(currentProject.id) && (window.codexWorkspaceCatalog.skills || []).length === 0
                                text: languageSettings.language === "en" ? "No valid SKILL.md files were found." : "未发现有效的 SKILL.md。"
                                color: window.muted
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                            }
                        }
                    }

                    UI.SectionCard {
                        visible: window.displayedPage === 10 || window.displayedPage === 11
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(150,
                            capabilityModel.categoryCount(window.displayedPage === 10 ? "Skills" : "Agent tools") * 64
                            + Math.max(0, capabilityModel.categoryCount(window.displayedPage === 10 ? "Skills" : "Agent tools") - 1) * 5
                            + 36 + (window.displayedPage === 10 ? 34 : 0))

                        Text {
                            visible: window.displayedPage === 10
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.topMargin: 11
                            text: languageSettings.language === "en" ? "Agent capabilities" : "Agent 能力开关"
                            color: window.graphite
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                        }

                        UI.SmoothListView {
                            anchors.fill: parent
                            anchors.margins: 12
                            anchors.topMargin: window.displayedPage === 10 ? 38 : 12
                            model: capabilityModel
                            spacing: 5
                            clip: true

                            delegate: Rectangle {
                                id: capabilityRow
                                required property int index
                                required property string capabilityId
                                required property string title
                                required property string description
                                required property string category

                                visible: category === (window.displayedPage === 10 ? "Skills" : "Agent tools")
                                width: ListView.view.width
                                height: visible ? 64 : 0
                                radius: 7
                                HoverHandler { id: capabilityRowHover }
                                color: window.darkMode
                                    ? capabilityRowHover.hovered ? "#293b4b" : capabilityModel.isEnabled(index) ? "#20262d" : "#1d2329"
                                    : capabilityRowHover.hovered ? "#eaf5ff" : capabilityModel.isEnabled(index) ? "#ffffff" : "#f7f8fa"
                                border.color: window.darkMode
                                    ? capabilityRowHover.hovered ? "#496a86" : "#39434e"
                                    : capabilityRowHover.hovered ? "#c5def2" : "#e5e9ee"
                                Behavior on color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }
                                Behavior on border.color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 12

                                    UI.FlatIcon {
                                        name: category === "Skills" ? "skills" : "terminal"
                                        color: category === "Skills" ? "#0a84ff" : "#8a5cf6"
                                        width: 20
                                        height: 20
                                    }
                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 3

                                        Text {
                                            text: window.localizedCapabilityTitle(capabilityId, title)
                                            color: window.graphite
                                            font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                            font.weight: Font.Medium
                                        }
                                        Text {
                                            width: parent.width
                                            text: window.localizedCapabilityDescription(capabilityId, description)
                                            color: window.muted
                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Switch {
                                        id: capabilitySwitch
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 24
                                        checked: capabilityModel.isEnabled(index)
                                        leftPadding: 0
                                        rightPadding: 0
                                        topPadding: 0
                                        bottomPadding: 0
                                        hoverEnabled: true
                                        indicator: Rectangle {
                                            width: 38
                                            height: 22
                                            x: Math.round((capabilitySwitch.width - width) / 2)
                                            y: Math.round((capabilitySwitch.height - height) / 2)
                                            radius: 11
                                            color: capabilitySwitch.checked ? (capabilitySwitch.down ? "#006edc" : "#0a84ff") : "#d4dbe4"
                                            border.color: capabilitySwitch.checked ? "#0a84ff" : "#bcc6d2"
                                            Behavior on color { ColorAnimation { duration: 110 } }

                                            Rectangle {
                                                width: 18
                                                height: 18
                                                anchors.verticalCenter: parent.verticalCenter
                                                x: capabilitySwitch.checked ? parent.width - width - 2 : 2
                                                radius: 9
                                                color: "#ffffff"
                                                border.color: "#d4d9df"
                                                Behavior on x { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                                            }
                                        }
                                        contentItem: Item {}
                                        onToggled: capabilityModel.setEnabled(index, checked)
                                    }
                                }
                            }
                        }
                    }
                    }
                }

                Rectangle {
                    id: codexChatSurface
                    anchors.fill: parent
                    visible: window.displayedPage === 3
                    color: window.chatCanvas

                    // The Chat page is layered over the legacy monitor page. Capture
                    // unused surface clicks here so they cannot activate controls below.
                    MouseArea {
                        anchors.fill: parent
                        z: 0
                        acceptedButtons: Qt.AllButtons
                    }

                    SplitView {
                        id: codexChatSplitView
                        anchors.fill: parent
                        orientation: Qt.Horizontal
                        z: 1
                        handle: Rectangle {
                            implicitWidth: 7
                            color: SplitHandle.pressed ? "#dbe6ef"
                                : SplitHandle.hovered ? "#edf3f8" : "transparent"
                            Rectangle {
                                anchors.centerIn: parent
                                width: 1
                                height: parent.height
                                color: "#d2dce5"
                            }
                        }

                        Rectangle {
                            id: codexChatSidebar
                            SplitView.preferredWidth: window.codexChatSidebarExpanded
                                ? window.codexChatSidebarWidth : 0
                            SplitView.minimumWidth: window.codexChatSidebarExpanded ? 200 : 0
                            SplitView.maximumWidth: window.codexChatSidebarExpanded ? 520 : 0
                            clip: true
                            onWidthChanged: {
                                if (window.codexChatSidebarExpanded && width > 0)
                                    window.codexChatSidebarWidth = width
                            }
                            color: window.chatSurface
                            border.color: window.chatBorder
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    UI.FlatIcon { name: "agent"; color: "#9c7ad2"; width: 22; height: 22 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: window.localizedUiText("DFT Agent 对话")
                                        color: window.chatText
                                        font.pixelSize: Math.round(16 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "refresh"
                                        tooltipText: window.localizedUiText("刷新会话")
                                        onClicked: window.refreshCodexWorkspace()
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "file"
                                        tooltipText: window.localizedUiText("导入会话")
                                        onClicked: codexSessionImportDialog.open()
                                    }
                                }

                                UI.ToolbarButton {
                                    Layout.fillWidth: true
                                    text: window.localizedUiText("新建对话")
                                    iconName: "plus"
                                    emphasized: true
                                    onClicked: window.createCodexChatSession()
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    radius: 7
                                    color: window.chatPanel
                                    border.color: window.chatBorder
                                    UI.FlatIcon {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 9
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "search"
                                        color: window.chatMuted
                                        width: 15
                                        height: 15
                                    }
                                    TextField {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        anchors.leftMargin: 31
                                        anchors.rightMargin: 7
                                        placeholderText: window.localizedUiText("搜索会话")
                                        placeholderTextColor: "#98a5b2"
                                        color: window.chatText
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        selectByMouse: true
                                        background: Item {}
                                        onTextChanged: window.codexSessionSearch = text
                                    }
                                }

                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: window.codexSessionProjectOptions()
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        const options = window.codexSessionProjectOptions()
                                        const index = options.findIndex(function(item) {
                                            return String(item.value) === window.codexSessionProjectFilter
                                        })
                                        return Math.max(0, index)
                                    }
                                    onActivated: function(index) {
                                        window.codexSessionProjectFilter = String(currentValue || "*")
                                    }
                                }

                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [
                                        { value: "all", label: languageSettings.language === "en" ? "All sessions" : "全部会话" },
                                        { value: "active", label: languageSettings.language === "en" ? "Active sessions" : "未归档会话" },
                                        { value: "archived", label: languageSettings.language === "en" ? "Archived sessions" : "已归档会话" }
                                    ]
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        const values = ["all", "active", "archived"]
                                        return Math.max(0, values.indexOf(window.codexSessionArchiveFilter))
                                    }
                                    onActivated: function(index) {
                                        window.codexSessionArchiveFilter = String(currentValue || "all")
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: window.localizedUiText("会话")
                                    color: window.chatMuted
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: window.codexSessionMultiSelect && window.codexWorkspaceMessage.length > 0
                                    text: window.localizedStoredMessage(window.codexWorkspaceMessage)
                                    color: window.darkMode ? "#ffaaa4" : "#a83232"
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                    wrapMode: Text.Wrap
                                }

                                UI.ToolbarButton {
                                    Layout.fillWidth: true
                                    visible: !window.codexSessionMultiSelect
                                    text: window.localizedUiText("批量选择")
                                    iconName: "check"
                                    tooltipText: window.localizedUiText("选择多个会话进行归档或删除")
                                    onClicked: window.toggleCodexChatSessionMultiSelect()
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: window.codexSessionMultiSelect
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: window.localizedUiText("已选 ") + window.selectedCodexChatSessionIds().length
                                        color: window.chatMuted
                                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                        elide: Text.ElideRight
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "check"
                                        tooltipText: window.localizedUiText("全选可操作会话")
                                        onClicked: {
                                            const selected = ({})
                                            for (const session of window.filteredCodexSessions()) {
                                                if (String(session.project_id || "") && String(session.workspace || "")
                                                        && !agentController.sessionRunning(String(session.id || "")))
                                                    selected[String(session.id || "")] = true
                                            }
                                            window.codexSelectedSessionIds = selected
                                        }
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "archive"
                                        tooltipText: window.localizedUiText("归档所选会话")
                                        enabled: window.selectedCodexChatSessionIds().length > 0
                                        onClicked: window.performCodexChatSessionBatch("session_archive_many")
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "trash"
                                        tooltipText: window.localizedUiText("删除所选会话")
                                        accentColor: "#b84d55"
                                        enabled: window.selectedCodexChatSessionIds().length > 0
                                        onClicked: window.requestCodexChatSessionBatchDelete()
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "close"
                                        tooltipText: window.localizedUiText("取消多选")
                                        onClicked: window.toggleCodexChatSessionMultiSelect()
                                    }
                                }

                                UI.SmoothListView {
                                    id: codexChatSessionList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 130
                                    model: window.filteredCodexSessions()
                                    spacing: 4
                                    clip: true
                                    objectName: "qaChatSessionList"
                                    delegate: UI.HoverSurface {
                                        id: chatSessionDelegate
                                        required property var modelData
                                        width: codexChatSessionList.width
                                        height: 68
                                        cornerRadius: 7
                                        opacity: modelData.archived ? 0.55 : 1
                                        idleColor: "transparent"
                                        selected: window.codexSessionMultiSelect
                                            ? Boolean(window.codexSelectedSessionIds[String(modelData.id || "")])
                                            : String(modelData.id) === window.codexSessionId
                                        hoverColor: window.chatHover
                                        selectedColor: window.chatSelected
                                        selectedHoverColor: window.darkMode ? "#294a68" : "#e0f0ff"
                                        pressedColor: window.darkMode ? "#31516b" : "#d5e9fb"
                                        selectedBorderColor: window.darkMode ? "#3975aa" : "#b9d5ec"
                                        transitionDuration: 160
                                        HoverHandler { id: sessionHover }
                                        hovered: sessionHover.hovered
                                        TapHandler {
                                            enabled: window.codexSessionMultiSelect || !chatSessionDelegate.modelData.archived
                                            onTapped: function(eventPoint) {
                                                if (window.codexSessionMultiSelect) {
                                                    if (eventPoint.position.x >= 36)
                                                        window.setCodexChatSessionSelected(
                                                            String(chatSessionDelegate.modelData.id || ""),
                                                            !Boolean(window.codexSelectedSessionIds[String(chatSessionDelegate.modelData.id || "")]))
                                                } else {
                                                    window.selectCodexChatSession(
                                                        String(chatSessionDelegate.modelData.id),
                                                        String(chatSessionDelegate.modelData.project_id || ""))
                                                }
                                            }
                                        }
                                        UI.StyledCheckBox {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 7
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: window.codexSessionMultiSelect
                                            compact: true
                                            text: ""
                                            checked: Boolean(window.codexSelectedSessionIds[String(chatSessionDelegate.modelData.id || "")])
                                            enabled: !agentController.sessionRunning(String(chatSessionDelegate.modelData.id || ""))
                                            onToggled: window.setCodexChatSessionSelected(String(chatSessionDelegate.modelData.id || ""), checked)
                                        }
                                        Column {
                                            anchors.fill: parent
                                            anchors.leftMargin: window.codexSessionMultiSelect ? 32 : 9
                                            anchors.rightMargin: 9
                                            anchors.topMargin: 9
                                            anchors.bottomMargin: 9
                                            spacing: 4
                                            Row {
                                                width: parent.width
                                                height: 17
                                                spacing: 3
                                                Text {
                                                    width: parent.width - chatSessionActions.width - parent.spacing
                                                        - (sessionRunningIcon.visible ? sessionRunningIcon.width + parent.spacing : 0)
                                                    text: String(chatSessionDelegate.modelData.name || window.localizedUiText("未命名对话"))
                                                    color: window.chatText
                                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                UI.FlatIcon {
                                                    id: sessionRunningIcon
                                                    width: 14
                                                    height: 14
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    visible: {
                                                        const anyRunActive = agentController.running
                                                        return agentController.sessionRunning(String(chatSessionDelegate.modelData.id || ""))
                                                    }
                                                    name: "loader"
                                                    color: window.accent
                                                    RotationAnimator on rotation {
                                                        from: 0
                                                        to: 360
                                                        duration: 900
                                                        loops: Animation.Infinite
                                                        running: sessionRunningIcon.visible
                                                    }
                                                }
                                                UI.ToolbarButton {
                                                    id: chatSessionActions
                                                    width: 24
                                                    height: 22
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: ""
                                                    iconName: "more"
                                                    visible: !window.codexSessionMultiSelect
                                                        && (sessionHover.hovered || String(chatSessionDelegate.modelData.id) === window.codexSessionId)
                                                    tooltipText: window.localizedUiText("会话操作")
                                                    onClicked: {
                                                        chatSessionMenu.sessionId = String(chatSessionDelegate.modelData.id)
                                                        chatSessionMenu.sessionTurns = Number(chatSessionDelegate.modelData.turn_count || 0)
                                                        chatSessionMenu.sessionArchived = Boolean(chatSessionDelegate.modelData.archived)
                                                        chatSessionMenu.popup(chatSessionActions, -chatSessionMenu.implicitWidth + chatSessionActions.width, chatSessionActions.height + 4)
                                                    }
                                                }
                                            }
                                            Text {
                                                width: parent.width
                                                visible: String(chatSessionDelegate.modelData.preview || "").length > 0
                                                text: String(chatSessionDelegate.modelData.preview || "")
                                                color: window.chatMuted
                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                width: parent.width
                                                text: window.projectNameForCodexSession(chatSessionDelegate.modelData) + " · "
                                                    + String(chatSessionDelegate.modelData.updated_at || "") + " · "
                                                    + (chatSessionDelegate.modelData.has_turn_count
                                                        ? String(chatSessionDelegate.modelData.turn_count || 0) + " 轮" : "历史会话")
                                                color: "#8a98a6"
                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: workspaceLauncher.implicitHeight + 14
                                    radius: 7
                                    color: window.chatPanel
                                    border.color: window.chatBorder
                                    RowLayout {
                                        id: workspaceLauncher
                                        anchors.fill: parent
                                        anchors.margins: 7
                                        spacing: 6
                                        UI.FlatIcon { name: "settings"; color: "#397fbd"; width: 15; height: 15 }
                                        Text { Layout.fillWidth: true; text: window.localizedUiText("记忆 · 技能"); color: window.chatText; font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideRight }
                                        UI.ToolbarButton {
                                            text: ""
                                            iconName: "chevron"
                                            onClicked: { window.refreshCodexWorkspace(); codexWorkspaceDialog.open() }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: codexChatMain
                            SplitView.fillWidth: true
                            color: window.chatCanvas

                            Item {
                                anchors.fill: parent
                                anchors.margins: 20

                                RowLayout {
                                    id: chatHeader
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "chevron"
                                        iconItem.rotation: window.codexChatSidebarExpanded ? 180 : 0
                                        tooltipText: window.codexChatSidebarExpanded
                                            ? (languageSettings.language === "en" ? "Hide conversations" : "收起会话列表")
                                            : (languageSettings.language === "en" ? "Show conversations" : "展开会话列表")
                                        onClicked: window.codexChatSidebarExpanded = !window.codexChatSidebarExpanded
                                    }
                                    UI.ToolbarButton {
                                        visible: window.codexSessionParentThreadId.length > 0
                                        text: window.width > 1100 ? window.localizedUiText("返回主会话") : ""
                                        iconName: "arrow-left"
                                        tooltipText: window.localizedUiText("返回主 session")
                                        onClicked: window.selectCodexChatSession(window.codexSessionParentThreadId)
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: window.codexSessionId.length > 0 ? window.localizedUiText("当前对话") : window.localizedUiText("开始新的 DFT 对话")
                                            color: window.chatText
                                            font.pixelSize: Math.round(20 * languageSettings.uiScale)
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(window.currentProject.name || window.currentProject.id || window.localizedUiText("未选择项目"))
                                                + " · " + window.localizedPhase(agentController.phase)
                                            color: window.chatMuted
                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                            elide: Text.ElideRight
                                        }
                                    }
                                    UI.ContextUsageIndicator {
                                        id: codexChatContextUsage
                                        Layout.preferredWidth: implicitWidth
                                        Layout.preferredHeight: implicitHeight
                                        language: languageSettings.language
                                        totalCapacity: agentController.contextWindow
                                        effectiveCapacity: agentController.contextEffectiveWindow
                                        autoCompactLimit: agentController.contextAutoCompactLimit
                                        inputUsed: agentController.contextInputTokens
                                        inputCapacity: agentController.contextInputLimit
                                        systemUsed: agentController.contextSystemTokens
                                        historyUsed: agentController.contextHistoryTokens
                                        toolsUsed: agentController.contextToolTokens
                                        toolsCapacity: agentController.contextToolLimit
                                        outputUsed: agentController.contextOutputTokens
                                        outputCapacity: agentController.contextOutputLimit
                                        compacted: agentController.contextCompacted
                                        providerMeasured: agentController.contextProviderMeasured
                                    }
                                    UI.ToolbarButton {
                                        text: window.width > 1240 ? window.localizedUiText("子智能体") : ""
                                        iconName: "agent"
                                        tooltipText: window.localizedUiText("查看当前会话的历史子智能体会话")
                                        enabled: window.codexSessionId.length > 0
                                        onClicked: window.openSubagentSessions()
                                    }
                                    UI.ToolbarButton {
                                        text: window.width > 1240 ? window.localizedUiText("工作台") : ""
                                        iconName: "settings"
                                        tooltipText: window.localizedUiText("Memories、Skills、Rules、Hooks")
                                        onClicked: { window.refreshCodexWorkspace(); codexWorkspaceDialog.open() }
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "trash"
                                        tooltipText: window.localizedUiText("清空当前显示记录")
                                        onClicked: agentController.clearLog()
                                    }
                                }

                                Rectangle {
                                    id: chatGoalPlanStrip
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: chatHeader.bottom
                                    anchors.topMargin: 10
                                    height: visible ? 42 : 0
                                    visible: window.codexSessionId.length > 0
                                    radius: 8
                                    color: window.chatPanel
                                    border.color: window.chatBorder
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 7
                                        spacing: 8
                                        UI.FlatIcon { name: "projects"; color: "#397fbd"; width: 16; height: 16 }
                                        Text {
                                            Layout.fillWidth: true
                                            text: window.codexSessionGoal.length > 0
                                                ? "Goal · " + window.codexSessionGoal
                                                : "Goal · 尚未设置"
                                            color: window.chatText
                                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                            elide: Text.ElideRight
                                        }
                                        UI.ToolbarButton {
                                            text: "Goal"
                                            iconName: "editor"
                                            onClicked: window.openCodexGoalEditor()
                                        }
                                        UI.ToolbarButton {
                                            text: "Plan"
                                            iconName: "flow"
                                            enabled: window.codexPlanText().length > 0
                                            tooltipText: enabled ? window.localizedUiText("查看当前 Plan") : window.localizedUiText("尚未生成 Plan")
                                            onClicked: codexPlanDialog.open()
                                        }
                                    }
                                }

                                Rectangle {
                                    id: chatActivityPanel
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: chatGoalPlanStrip.visible ? chatGoalPlanStrip.bottom : chatHeader.bottom
                                    anchors.topMargin: chatGoalPlanStrip.visible ? 8 : 12
                                    anchors.bottom: chatComposerFrame.top
                                    anchors.bottomMargin: 12
                                    radius: 10
                                    color: window.chatSurface
                                    border.color: window.chatBorder
                                    clip: true

                                    Text {
                                        anchors.centerIn: parent
                                        visible: window.codexChatDisplayEntries.length === 0
                                            && !window.codexSessionLoading
                                        text: window.localizedUiText("在下方描述目标；模型回复、工具调用和修改候选会连续显示在这里。")
                                        color: window.chatMuted
                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                    }

                                    UI.ActivityShimmer {
                                        anchors.fill: parent
                                        running: window.codexSessionLoading
                                        tint: "#68aef4"
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: window.codexSessionLoading
                                        text: languageSettings.language === "en" ? "Loading conversation" : "正在加载对话"
                                        color: window.chatText
                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                        font.weight: Font.Medium
                                    }

                                    ListView {
                                        id: codexChatActivityList
                                        objectName: "codexChatActivityList"
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.top: parent.top
                                        anchors.topMargin: 16
                                        anchors.leftMargin: 16
                                        anchors.rightMargin: 16
                                        anchors.bottomMargin: 16
                                        visible: window.codexChatDisplayEntries.length > 0
                                        model: codexChatDisplayModel
                                        spacing: 10
                                        // Keep delegate creation bounded even though each
                                        // history request now hydrates a much larger page.
                                        // The backing model retains every loaded entry.
                                        cacheBuffer: Math.max(2400, height * 2)
                                        reuseItems: true
                                        clip: true
                                        interactive: false
                                        pixelAligned: true
                                        onContentYChanged: {
                                            const minimum = Number(originY || 0)
                                            if (!window.codexActivityPrepending
                                                    && !codexChatActivityScrollBar.pressed
                                                    && Boolean(window.codexActivityMeta.has_more)
                                                    && Number(window.codexActivityMeta.next_before || 0) > 0
                                                    && contentY <= minimum + 24)
                                                window.scheduleOlderCodexActivityLoad()
                                        }
                                        onOriginYChanged: {
                                            const minimum = Number(originY || 0)
                                            if (!window.codexActivityScrollLocked
                                                    && !window.codexActivityPrepending
                                                    && !codexChatActivityScrollBar.pressed
                                                    && contentY <= minimum + 24
                                                    && Boolean(window.codexActivityMeta.has_more)
                                                    && Number(window.codexActivityMeta.next_before || 0) > 0)
                                                window.scheduleOlderCodexActivityLoad()
                                        }
                                        // Variable-height delegates can move the
                                        // effective top edge without a scroll
                                        // gesture (for example when a tool bubble
                                        // expands). Recheck history at the top, and
                                        // also fill a viewport shorter than one row.
                                        onContentHeightChanged: {
                                            if (!window.codexActivityScrollLocked
                                                    && !window.codexActivityPrepending
                                                    && !codexChatActivityScrollBar.pressed
                                                    && (Number(contentHeight || 0) <= Number(height || 0) + 1
                                                        || contentY <= Number(originY || 0) + 24)
                                                    && Boolean(window.codexActivityMeta.has_more)
                                                    && Number(window.codexActivityMeta.next_before || 0) > 0)
                                                window.scheduleOlderCodexActivityLoad()
                                        }
                                        ScrollBar.vertical: ScrollBar {
                                            id: codexChatActivityScrollBar
                                            z: 2
                                            width: 10
                                            policy: ScrollBar.AsNeeded
                                            active: window.codexChatScrollbarActive
                                                || pressed
                                                || chatWheelAnimator.scrolling
                                                || window.codexActivityPrepending
                                            interactive: !window.codexActivityScrollLocked
                                            onPressedChanged: {
                                                if (!pressed && position <= 0.06
                                                        && !window.codexActivityScrollLocked
                                                        && Boolean(window.codexActivityMeta.has_more)
                                                        && Number(window.codexActivityMeta.next_before || 0) > 0)
                                                    window.scheduleOlderCodexActivityLoad()
                                            }
                                        }
                                        WheelScrollAnimator {
                                            id: chatWheelAnimator
                                            flickable: codexChatActivityList
                                        }
                                        FrameAnimation {
                                            running: chatWheelAnimator.scrolling
                                            onTriggered: chatWheelAnimator.advance(frameTime)
                                        }
                                        onCountChanged: {
                                            if (!window.codexActivityPrepending
                                                    && !window.codexActivityRefreshing
                                                    && window.codexExpandedThinkingKey.length === 0)
                                                Qt.callLater(function() { codexChatActivityList.positionViewAtEnd() })
                                        }

                                        delegate: Item {
                                            id: codexChatActivity
                                            required property var entry
                                            readonly property var modelData: entry
                                            required property int index
                                            property bool expanded: false
                                            readonly property bool groupExpanded: window.codexExpandedActivityGroupKey === window.activityGroupKey(modelData)
                                            ListView.onReused: {
                                                expanded = false
                                            }
                                            onModelDataChanged: {
                                                if (isToolGroup && typeof groupChildrenContent.syncGroupedChildrenModel === "function")
                                                    groupChildrenContent.syncGroupedChildrenModel()
                                            }
                                            onGroupExpandedChanged: {
                                                if (isToolGroup && typeof groupChildrenContent.syncGroupedChildrenModel === "function")
                                                    groupChildrenContent.syncGroupedChildrenModel()
                                            }
                                            readonly property bool isToolGroup: modelData.kind === "tool_group" || modelData.kind === "activity_group"
                                            readonly property bool isTool: modelData.kind === "tool" || modelData.kind === "patch" || modelData.kind === "approval"
                                            readonly property bool isRedundantAgentStatus: window.isRedundantAgentStatusEntry(modelData)
                                            readonly property bool isAgentStatus: window.isAgentStatusEntry(modelData)
                                            readonly property bool isReconnect: modelData.kind === "reconnect"
                                            readonly property bool isEda: window.isEdaActivity(modelData)
                                            readonly property bool isShell: isTool && window.isShellActivity(modelData)
                                            readonly property bool hasDetails: isTool && (String(modelData.text || "").length > 0 || String(modelData.result || "").length > 0 || String(modelData.shellCommand || "").length > 0 || isEda)
                                            width: ListView.view.width
                                            height: isRedundantAgentStatus ? 0 : chatEntryLayout.implicitHeight

                                            ColumnLayout {
                                                id: chatEntryLayout
                                                width: parent.width
                                                spacing: 6

                                                Rectangle {
                                                    id: shellActivityBubble
                                                    Layout.fillWidth: true
                                                    visible: codexChatActivity.isShell
                                                    implicitHeight: shellActivityContent.implicitHeight + 16
                                                    radius: 7
                                                    color: window.darkMode ? "#102c3d" : "#f0f7fc"
                                                    border.color: window.darkMode ? "#264a62" : "#d1e3ef"
                                                    clip: true
                                                    UI.ActivityShimmer {
                                                        anchors.fill: parent
                                                        running: String(modelData.status || "") === "running"
                                                            && !window.currentChatSessionPaused()
                                                    }
                                                    ColumnLayout {
                                                        id: shellActivityContent
                                                        anchors.fill: parent
                                                        anchors.margins: 8
                                                        spacing: 5
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            Text {
                                                                text: window.localizedUiText("Shell")
                                                                color: window.darkMode ? "#79b9df" : "#397fbd"
                                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                font.weight: Font.DemiBold
                                                            }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: modelData.status === "running"
                                                                    ? window.localizedUiText("执行中")
                                                                    : modelData.status === "failed" ? window.localizedUiText("失败")
                                                                    : window.localizedUiText("完成")
                                                                color: modelData.status === "running" ? window.accent
                                                                    : modelData.status === "failed" ? "#d36b63" : "#55b58b"
                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                horizontalAlignment: Text.AlignRight
                                                            }
                                                            UI.ToolbarButton {
                                                                text: ""
                                                                iconName: "copy"
                                                                tooltipText: window.localizedUiText("复制命令")
                                                                onClicked: agentController.copyText(window.shellDisplayCommand(modelData))
                                                            }
                                                        }
                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: "$ " + window.shellDisplayCommand(modelData)
                                                            color: window.darkMode ? "#83bddd" : "#365d78"
                                                            font.family: "Monospace"
                                                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                            wrapMode: Text.WrapAnywhere
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: String(modelData.shellOutput || "").length > 0
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: window.localizedUiText("输出")
                                                                color: window.darkMode ? "#9aabb7" : "#718296"
                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                            }
                                                            UI.ToolbarButton {
                                                                text: window.localizedUiText("复制输出")
                                                                iconName: "copy"
                                                                tooltipText: window.localizedUiText("复制输出")
                                                                onClicked: agentController.copyText(window.terminalOutputText(modelData.shellOutput))
                                                            }
                                                        }
                                                        UI.SmoothScrollView {
                                                            id: shellActivityOutputScroll
                                                            Layout.fillWidth: true
                                                            visible: String(modelData.shellOutput || "").length > 0
                                                            Layout.preferredHeight: Math.min(240, Math.max(72, shellActivityOutputText.implicitHeight))
                                                            clip: true
                                                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                                            TextEdit {
                                                                id: shellActivityOutputText
                                                                width: Math.max(1, shellActivityOutputScroll.availableWidth)
                                                                height: Math.max(shellActivityOutputScroll.availableHeight, implicitHeight)
                                                                readOnly: true
                                                                selectByMouse: true
                                                                persistentSelection: true
                                                                text: window.terminalOutputText(modelData.shellOutput)
                                                                color: window.darkMode ? "#c3d2dc" : "#40586b"
                                                                font.family: "Monospace"
                                                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                textFormat: TextEdit.PlainText
                                                                wrapMode: TextEdit.WrapAnywhere
                                                                cursorVisible: false
                                                            }
                                                        }
                                                        Text {
                                                            Layout.fillWidth: true
                                                            visible: String(modelData.shellOutput || "").length === 0
                                                                && String(window.chatToolResult(modelData) || "").length > 0
                                                            text: window.chatToolResult(modelData)
                                                            color: modelData.status === "failed" ? "#d36b63" : window.chatMuted
                                                            font.family: "Monospace"
                                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                            wrapMode: Text.WrapAnywhere
                                                        }
                                                        Text {
                                                            Layout.fillWidth: true
                                                            visible: modelData.exitCode !== undefined
                                                            text: window.localizedUiText("退出码：") + String(modelData.exitCode)
                                                            color: Number(modelData.exitCode) === 0 ? "#55b58b" : "#d36b63"
                                                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    id: activityGroupBubble
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: groupContent.implicitHeight + 18
                                                    visible: codexChatActivity.isToolGroup
                                                    radius: 8
                                                    color: window.chatPanel
                                                    border.color: window.chatBorder
                                                    clip: true

                                                    UI.ActivityShimmer {
                                                        anchors.fill: parent
                                                        running: !codexChatActivity.groupExpanded
                                                            && String(codexChatActivity.modelData.status || "") === "running"
                                                            && !window.currentChatSessionPaused()
                                                    }

                                                    ColumnLayout {
                                                        id: groupContent
                                                        anchors.left: parent.left
                                                        anchors.right: parent.right
                                                        anchors.top: parent.top
                                                        anchors.margins: 9
                                                        spacing: 7
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            TapHandler {
                                                                onTapped: window.toggleActivityGroup(codexChatActivity.modelData)
                                                            }
                                                            UI.FlatIcon {
                                                                name: modelData.kind === "activity_group" ? "flow" : "code"
                                                                color: "#397fbd"
                                                                width: 16
                                                                height: 16
                                                            }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: String(modelData.text || window.localizedUiText("工具操作"))
                                                                color: window.chatText
                                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                font.weight: Font.DemiBold
                                                                elide: Text.ElideRight
                                                            }
                                                            Text {
                                                                text: String((modelData.children || []).length) + window.localizedUiText(" 项")
                                                                color: window.chatMuted
                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                            }
                                                            UI.FlatIcon {
                                                                name: "chevron"
                                                                color: "#7d8b99"
                                                                width: 14
                                                                height: 14
                                                                rotation: codexChatActivity.groupExpanded ? 90 : 0
                                                            }
                                                        }
                                                        Item {
                                                            id: groupChildrenClip
                                                            Layout.fillWidth: true
                                                            Layout.preferredHeight: height
                                                            implicitHeight: height
                                                            height: codexChatActivity.groupExpanded ? groupChildrenContent.implicitHeight : 0
                                                            clip: true
                                                            Behavior on height {
                                                                NumberAnimation { duration: 190; easing.type: Easing.OutCubic }
                                                            }

                                                            ColumnLayout {
                                                            id: groupChildrenContent
                                                            anchors.left: parent.left
                                                            anchors.right: parent.right
                                                            anchors.top: parent.top
                                                            opacity: codexChatActivity.groupExpanded ? 1 : 0
                                                            spacing: 5
                                                            Behavior on opacity {
                                                                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                                            }
                                                            // Keep child delegates alive while a group is streaming.  Replacing
                                                            // the parent's JS array on every reasoning delta would otherwise
                                                            // destroy/recreate the whole child tree and visibly blink it.
                                                            ListModel {
                                                                id: groupedChildrenModel
                                                            }
                                                            Timer {
                                                                id: groupedChildrenCollapseTimer
                                                                interval: 210
                                                                onTriggered: {
                                                                    if (!codexChatActivity.groupExpanded)
                                                                        groupedChildrenModel.clear()
                                                                }
                                                            }
                                                            function syncGroupedChildrenModel() {
                                                                if (!codexChatActivity.groupExpanded) {
                                                                    groupedChildrenCollapseTimer.restart()
                                                                    return
                                                                }
                                                                groupedChildrenCollapseTimer.stop()
                                                                const children = codexChatActivity.modelData.children || []
                                                                while (groupedChildrenModel.count > children.length)
                                                                    groupedChildrenModel.remove(groupedChildrenModel.count - 1)
                                                                for (let childIndex = 0; childIndex < children.length; ++childIndex) {
                                                                    const child = children[childIndex] || ({})
                                                                    const fingerprint = window.chatActivityFingerprint(child)
                                                                    if (childIndex < groupedChildrenModel.count) {
                                                                        const current = groupedChildrenModel.get(childIndex)
                                                                        if (String(current.fingerprint || "") === fingerprint)
                                                                            continue
                                                                        groupedChildrenModel.setProperty(childIndex, "payload", child)
                                                                        groupedChildrenModel.setProperty(childIndex, "fingerprint", fingerprint)
                                                                    } else {
                                                                        groupedChildrenModel.append({ payload: child, fingerprint: fingerprint })
                                                                    }
                                                                }
                                                            }
                                                            Component.onCompleted: syncGroupedChildrenModel()
                                                            Repeater {
                                                                model: groupedChildrenModel
                                                                delegate: Rectangle {
                                                                    id: groupedToolEntry
                                                                    required property var payload
                                                                    readonly property var childData: payload || ({})
                                                                    property bool childExpanded: false
                                                                    Layout.fillWidth: true
                                                                    implicitHeight: groupedToolContent.implicitHeight + 12
                                                                    radius: 6
                                                                    clip: true
                                                                    color: (groupedToolEntry.isThinkingChild
                                                                        ? groupedToolEntry.thinkingExpanded
                                                                        : groupedToolEntry.childExpanded) ? "#f2f7fb" : "transparent"
                                                                    border.color: (groupedToolEntry.isThinkingChild
                                                                        ? groupedToolEntry.thinkingExpanded
                                                                        : groupedToolEntry.childExpanded) ? "#d7e3ed" : "transparent"
                                                                    UI.ActivityShimmer {
                                                                        anchors.fill: parent
                                                                        running: !groupedToolEntry.isThinkingChild
                                                                            && String(groupedToolEntry.childData.status || "") === "running"
                                                                            && !window.currentChatSessionPaused()
                                                                    }
                                                                    TapHandler {
                                                                        enabled: !groupedToolEntry.isEdaChild || !groupedEdaOutput.pointerHovered
                                                                        onTapped: {
                                                                            if (groupedToolEntry.isThinkingChild)
                                                                                window.toggleThinkingEntry(groupedToolEntry.childData)
                                                                            else
                                                                                groupedToolEntry.childExpanded = !groupedToolEntry.childExpanded
                                                                        }
                                                                    }
                                                                    HoverHandler { id: groupedToolHover }
                                                                    readonly property bool isThinkingChild: groupedToolEntry.childData.kind === "agent"
                                                                        && String(groupedToolEntry.childData.role || "") === "thinking"
                                                                    readonly property bool thinkingExpanded: groupedToolEntry.isThinkingChild
                                                                        && window.codexExpandedThinkingKey === window.thinkingEntryKey(groupedToolEntry.childData)
                                                                    readonly property bool isEdaChild: window.isEdaActivity(groupedToolEntry.childData)
                                                                    readonly property bool isShellChild: window.isShellActivity(groupedToolEntry.childData)

                                                                    ColumnLayout {
                                                                        id: groupedToolContent
                                                                        anchors.left: parent.left
                                                                        anchors.right: parent.right
                                                                        anchors.top: parent.top
                                                                        anchors.margins: 6
                                                                        spacing: 5
                                                                        RowLayout {
                                                                            Layout.fillWidth: true
                                                                            UI.FlatIcon {
                                                                                name: groupedToolEntry.isThinkingChild ? "flow" : (groupedToolEntry.isEdaChild || groupedToolEntry.isShellChild ? "terminal" : "skills")
                                                                                color: groupedToolEntry.childData.status === "failed" ? "#bd4540" : "#397fbd"
                                                                                width: 14
                                                                                height: 14
                                                                            }
                                                                            Text {
                                                                                Layout.fillWidth: true
                                                                                text: groupedToolEntry.isThinkingChild
                                                                                    ? (groupedToolEntry.thinkingExpanded ? window.localizedUiText("思考过程") : window.localizedUiText("思考中…"))
                                                                                    : window.chatActivityTitle(groupedToolEntry.childData)
                                                                                color: window.chatText
                                                                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                                                elide: Text.ElideRight
                                                                            }
                                                                            Text {
                                                                                text: groupedToolEntry.isThinkingChild ? window.localizedUiText("思考") : (groupedToolEntry.childData.status === "running" ? window.localizedUiText("执行中") : groupedToolEntry.childData.status === "failed" ? window.localizedUiText("失败") : window.localizedUiText("完成"))
                                                                                color: groupedToolEntry.childData.status === "running" ? window.accent : groupedToolEntry.childData.status === "failed" ? "#bd4540" : "#24845a"
                                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                            }
                                                                            UI.ToolbarButton {
                                                                                id: groupedEdaOutput
                                                                                visible: groupedToolEntry.isEdaChild
                                                                                text: window.localizedUiText("输出")
                                                                                iconName: "terminal"
                                                                                tooltipText: window.localizedUiText("查看 EDA 终端输出")
                                                                                onClicked: window.openEdaOutput(groupedToolEntry.childData, Number(groupedToolEntry.childData.sourceIndex))
                                                                            }
                                                                            UI.FlatIcon {
                                                                                name: "chevron"
                                                                                color: "#7d8b99"
                                                                                width: 13
                                                                                height: 13
                                                                                rotation: (groupedToolEntry.isThinkingChild
                                                                                    ? groupedToolEntry.thinkingExpanded
                                                                                    : groupedToolEntry.childExpanded) ? 90 : 0
                                                                            }
                                                                        }
                                                                        Text {
                                                                            Layout.fillWidth: true
                                                                            visible: groupedToolEntry.thinkingExpanded
                                                                            text: String(groupedToolEntry.childData.text || "")
                                                                            color: "#566879"
                                                                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                            wrapMode: Text.Wrap
                                                                        }
                                                                        Text {
                                                                            Layout.fillWidth: true
                                                                            visible: groupedToolEntry.childExpanded && !groupedToolEntry.isShellChild
                                                                                && !groupedToolEntry.isThinkingChild
                                                                                && String(window.chatToolResult(groupedToolEntry.childData) || "").length > 0
                                                                            text: window.chatToolResult(groupedToolEntry.childData)
                                                                            color: groupedToolEntry.childData.status === "failed" ? "#bd4540" : window.chatMuted
                                                                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                            wrapMode: Text.WrapAnywhere
                                                                        }
                                                                        Rectangle {
                                                                            Layout.fillWidth: true
                                                                            visible: groupedToolEntry.childExpanded && groupedToolEntry.isShellChild
                                                                            implicitHeight: groupedShellColumn.implicitHeight + 14
                                                                            color: window.darkMode ? "#202a32" : "#eef6fb"
                                                                            radius: 6
                                                                            border.color: window.darkMode ? "#3d5365" : "#cfe3f1"
                                                                            ColumnLayout {
                                                                                id: groupedShellColumn
                                                                                anchors.fill: parent
                                                                                anchors.margins: 7
                                                                                spacing: 4
                                                                                RowLayout {
                                                                                    Layout.fillWidth: true
                                                                                    Text {
                                                                                        text: "Shell"
                                                                                        color: window.darkMode ? "#79b9df" : "#397fbd"
                                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                                        font.weight: Font.DemiBold
                                                                                    }
                                                                                    Item { Layout.fillWidth: true }
                                                                                }
                                                                                Text {
                                                                                    Layout.fillWidth: true
                                                                                    text: "$ " + window.shellDisplayCommand(groupedToolEntry.childData)
                                                                                    color: window.darkMode ? "#83bddd" : "#365d78"
                                                                                    font.family: "Monospace"
                                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                                    wrapMode: Text.WrapAnywhere
                                                                                }
                                                                                UI.SmoothScrollView {
                                                                                    id: groupedShellScroll
                                                                                    Layout.fillWidth: true
                                                                                    visible: String(groupedToolEntry.childData.shellOutput || "").length > 0
                                                                                    Layout.preferredHeight: 150
                                                                                    clip: true
                                                                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                                                                    TextEdit {
                                                                                        width: Math.max(1, groupedShellScroll.availableWidth)
                                                                                        height: Math.max(groupedShellScroll.availableHeight, implicitHeight)
                                                                                        readOnly: true
                                                                                        selectByMouse: true
                                                                                        persistentSelection: true
                                                                                        text: window.terminalOutputText(groupedToolEntry.childData.shellOutput)
                                                                                        color: window.darkMode ? "#c3d2dc" : "#40586b"
                                                                                        font.family: "Monospace"
                                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                                        textFormat: TextEdit.PlainText
                                                                                        wrapMode: TextEdit.WrapAnywhere
                                                                                        cursorVisible: false
                                                                                    }
                                                                                }
                                                                                ColumnLayout {
                                                                                    Layout.fillWidth: true
                                                                                    visible: groupedToolEntry.childData.fileChanges !== undefined
                                                                                        && groupedToolEntry.childData.fileChanges.length > 0
                                                                                    spacing: 3
                                                                                    Text {
                                                                                        Layout.fillWidth: true
                                                                                        text: window.localizedUiText("已编辑的文件")
                                                                                        color: window.darkMode ? "#83bddd" : "#365d78"
                                                                                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                                        font.weight: Font.DemiBold
                                                                                    }
                                                                                    Repeater {
                                                                                        model: groupedToolEntry.childData.fileChanges || []
                                                                                        delegate: RowLayout {
                                                                                            Layout.fillWidth: true
                                                                                            spacing: 5
                                                                                            Text {
                                                                                                Layout.fillWidth: true
                                                                                                text: String(modelData.relative_path || modelData.path || "")
                                                                                                color: window.darkMode ? "#c3d2dc" : "#40586b"
                                                                                                font.family: "Monospace"
                                                                                                font.pixelSize: Math.round(9 * languageSettings.uiScale)
                                                                                                elide: Text.ElideMiddle
                                                                                            }
                                                                                            Text {
                                                                                                text: "+" + String((modelData.line_stats || {}).added || 0)
                                                                                                    + "  -" + String((modelData.line_stats || {}).removed || 0)
                                                                                                color: window.darkMode ? "#79b9df" : "#397fbd"
                                                                                                font.pixelSize: Math.round(9 * languageSettings.uiScale)
                                                                                            }
                                                                                            UI.ToolbarButton {
                                                                                                text: "Diff"
                                                                                                iconName: "code"
                                                                                                visible: String(modelData.patch_file || modelData.diff_file || "").length > 0
                                                                                                tooltipText: window.localizedUiText("查看文件差异")
                                                                                                onClicked: {
                                                                                                    patchDiffDialog.patchFile = String(modelData.patch_file || modelData.diff_file || "")
                                                                                                    patchDiffDialog.patchTitle = String(modelData.relative_path || modelData.path || "")
                                                                                                    patchDiffDialog.open()
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                            Item {
                                                                                anchors.fill: parent
                                                                                z: 3
                                                                                UI.ToolbarButton {
                                                                                    anchors.top: parent.top
                                                                                    anchors.right: parent.right
                                                                                    anchors.topMargin: 2
                                                                                    anchors.rightMargin: 2
                                                                                    visible: groupedToolHover.hovered
                                                                                    text: ""
                                                                                    iconName: "copy"
                                                                                    tooltipText: window.localizedUiText("复制命令")
                                                                                    onClicked: agentController.copyText(window.shellDisplayCommand(groupedToolEntry.childData))
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                            }
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.alignment: modelData.kind === "user" ? Qt.AlignRight : Qt.AlignLeft
                                                    Layout.preferredWidth: codexChatActivity.isReconnect
                                                        ? Math.min(parent.width, Math.max(180, chatAgentText.implicitWidth + 26))
                                                        : modelData.kind === "agent" ? Math.min(parent.width, Math.max(260, chatAgentText.implicitWidth + 26))
                                                        : modelData.kind === "user" ? Math.min(parent.width * 0.82, Math.max(220, chatAgentText.implicitWidth + 26))
                                                        : parent.width
                                                    Layout.maximumWidth: parent.width
                                                    Layout.preferredHeight: chatAgentText.implicitHeight + 22
                                                    visible: !codexChatActivity.isTool
                                                        && !codexChatActivity.isToolGroup
                                                        && !codexChatActivity.isRedundantAgentStatus
                                                    radius: 9
                                                    color: window.darkMode
                                                        ? modelData.kind === "user" ? "#20384d" : codexChatActivity.isAgentStatus ? "#20282f" : window.chatPanel
                                                        : modelData.kind === "user" ? "#e6f2fc" : codexChatActivity.isAgentStatus ? "#f3f7fa" : window.chatPanel
                                                    border.color: window.darkMode
                                                        ? modelData.kind === "user" ? "#315675" : codexChatActivity.isAgentStatus ? "#3d4a56" : window.chatBorder
                                                        : modelData.kind === "user" ? "#b9d8ef" : codexChatActivity.isAgentStatus ? "#c9dbe7" : window.chatBorder
                                                    ColumnLayout {
                                                        id: chatAgentText
                                                        anchors.fill: parent
                                                        anchors.margins: 11
                                                        spacing: 5
                                                        TapHandler {
                                                            enabled: codexChatActivity.modelData.kind === "agent"
                                                                && String(codexChatActivity.modelData.role || "") === "thinking"
                                                            onTapped: window.toggleThinkingEntry(codexChatActivity.modelData)
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.isAgentStatus
                                                            spacing: 7
                                                            Rectangle {
                                                                id: subagentCapsule
                                                                visible: String(codexChatActivity.modelData.subagentId || "").length > 0
                                                                Layout.preferredWidth: visible ? subagentCapsuleContent.implicitWidth + 18 : 0
                                                                Layout.preferredHeight: visible ? 39 : 0
                                                                radius: 15
                                                                color: window.darkMode ? "#20282f" : "#f3f7fa"
                                                                border.color: window.subagentColor(codexChatActivity.modelData)
                                                                border.width: 1
                                                                RowLayout {
                                                                    id: subagentCapsuleContent
                                                                    anchors.centerIn: parent
                                                                    spacing: 6
                                                                    Rectangle {
                                                                        Layout.preferredWidth: 21
                                                                        Layout.preferredHeight: 21
                                                                        radius: 11
                                                                        color: window.subagentColor(codexChatActivity.modelData)
                                                                        Text {
                                                                            anchors.centerIn: parent
                                                                            text: window.subagentInitials(codexChatActivity.modelData)
                                                                            color: "white"
                                                                            font.pixelSize: Math.round(9 * languageSettings.uiScale)
                                                                            font.weight: Font.DemiBold
                                                                        }
                                                                    }
                                                                    ColumnLayout {
                                                                        spacing: 1
                                                                        Text {
                                                                            Layout.maximumWidth: 140
                                                                            text: window.subagentDisplayName(codexChatActivity.modelData)
                                                                            color: window.subagentColor(codexChatActivity.modelData)
                                                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                            font.weight: Font.DemiBold
                                                                            elide: Text.ElideRight
                                                                        }
                                                                        Text {
                                                                            Layout.maximumWidth: 140
                                                                            text: window.subagentSpecialty(codexChatActivity.modelData)
                                                                            color: "#62788a"
                                                                            font.pixelSize: Math.round(9 * languageSettings.uiScale)
                                                                            elide: Text.ElideRight
                                                                        }
                                                                    }
                                                                    Text {
                                                                        text: window.subagentStatusText(codexChatActivity.modelData)
                                                                        color: "#63798a"
                                                                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                    }
                                                                }
                                                                TapHandler {
                                                                    onTapped: window.openCodexSubagentSession(codexChatActivity.modelData)
                                                                }
                                                                HoverHandler { id: subagentCapsuleHover }
                                                                UI.ThemedToolTip {
                                                                    target: subagentCapsule
                                                                    active: subagentCapsuleHover.hovered
                                                                    message: window.localizedUiText("打开 %1 会话").arg(window.subagentDisplayName(codexChatActivity.modelData))
                                                                }
                                                            }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                visible: String(codexChatActivity.modelData.subagentId || "").length === 0
                                                                text: String(codexChatActivity.modelData.role || "") === "thinking"
                                                                    ? (window.codexExpandedThinkingKey === window.thinkingEntryKey(codexChatActivity.modelData)
                                                                        ? window.localizedUiText("思考过程（点击收起）") : window.localizedUiText("思考中…（点击展开）"))
                                                                    : String(codexChatActivity.modelData.text || "")
                                                                color: "#526b7d"
                                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                wrapMode: Text.Wrap
                                                            }
                                                            Item { Layout.fillWidth: String(codexChatActivity.modelData.subagentId || "").length > 0 }
                                                        }
                                                        Text {
                                                            id: queuedPromptPreviewText
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.isAgentStatus
                                                                && String(codexChatActivity.modelData.subagentId || "").length > 0
                                                            text: window.subagentDetail(codexChatActivity.modelData)
                                                            color: "#526b7d"
                                                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                            wrapMode: Text.Wrap
                                                        }
                                                        TextEdit {
                                                            Layout.fillWidth: true
                                                            Layout.preferredHeight: visible ? Math.max(1, implicitHeight) : 0
                                                            visible: codexChatActivity.isAgentStatus
                                                                && String(codexChatActivity.modelData.role || "") === "thinking"
                                                                && window.codexExpandedThinkingKey === window.thinkingEntryKey(codexChatActivity.modelData)
                                                            readOnly: true
                                                            selectByMouse: true
                                                            selectByKeyboard: true
                                                            persistentSelection: true
                                                            text: String(codexChatActivity.modelData.text || "")
                                                            color: "#526b7d"
                                                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                            wrapMode: TextEdit.Wrap
                                                            textFormat: TextEdit.PlainText
                                                            cursorVisible: false
                                                        }
                                                        TextEdit {
                                                            Layout.fillWidth: true
                                                            Layout.preferredHeight: visible ? Math.max(1, implicitHeight) : 0
                                                            visible: !codexChatActivity.isAgentStatus
                                                            readOnly: true
                                                            selectByMouse: true
                                                            selectByKeyboard: true
                                                            persistentSelection: true
                                                            text: window.isFullHtmlDocument(codexChatActivity.modelData.text)
                                                                ? window.renderActivityText(codexChatActivity.modelData)
                                                                : window.activityUsesRichText(codexChatActivity.modelData)
                                                                    ? window.renderActivityText(codexChatActivity.modelData)
                                                                    : String(codexChatActivity.modelData.text || "")
                                                            color: window.chatText
                                                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                            wrapMode: TextEdit.Wrap
                                                            textFormat: window.activityUsesRichText(codexChatActivity.modelData)
                                                                ? TextEdit.RichText : TextEdit.PlainText
                                                            cursorVisible: false
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.modelData.kind === "agent" && !codexChatActivity.isAgentStatus
                                                            Item { Layout.fillWidth: true }
                                                            UI.ToolbarButton {
                                                                text: window.localizedUiText("复制")
                                                                iconName: "copy"
                                                                tooltipText: window.localizedUiText("复制回复")
                                                                onClicked: agentController.copyText(String(codexChatActivity.modelData.text || ""))
                                                            }
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: chatToolContent.implicitHeight + 18
                                                    visible: codexChatActivity.isTool && !codexChatActivity.isShell
                                                    radius: 8
                                                    color: modelData.status === "failed" ? (window.darkMode ? "#3c2929" : "#fff3f2") : window.chatPanel
                                                    border.color: modelData.status === "failed" ? (window.darkMode ? "#684242" : "#efc7c4") : window.chatBorder
                                                    UI.ActivityShimmer {
                                                        anchors.fill: parent
                                                        running: String(modelData.status || "") === "running"
                                                            && !window.currentChatSessionPaused()
                                                    }
                                                    HoverHandler { id: chatToolHover }
                                                    TapHandler {
                                                        enabled: codexChatActivity.hasDetails
                                                            && (!codexChatActivity.isEda || !edaOutputButton.pointerHovered)
                                                        onTapped: codexChatActivity.expanded = !codexChatActivity.expanded
                                                    }

                                                    ColumnLayout {
                                                        id: chatToolContent
                                                        anchors.left: parent.left
                                                        anchors.right: parent.right
                                                        anchors.top: parent.top
                                                        anchors.margins: 9
                                                        spacing: 7
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            UI.FlatIcon {
                                                                name: modelData.kind === "patch" ? "code" : codexChatActivity.isEda ? "terminal" : modelData.shell === true ? "terminal" : modelData.name === "terminal" ? "terminal" : "skills"
                                                                color: modelData.status === "failed" ? "#bd4540" : "#397fbd"
                                                                width: 16
                                                                height: 16
                                                            }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: window.chatActivityTitle(codexChatActivity.modelData)
                                                                color: window.chatText
                                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                font.weight: Font.DemiBold
                                                                elide: Text.ElideRight
                                                            }
                                                            Text {
                                                                text: modelData.status === "running" ? window.localizedUiText("执行中") : modelData.status === "failed" ? window.localizedUiText("失败") : modelData.kind === "approval" ? window.localizedUiText("待批准") : window.localizedUiText("完成")
                                                                color: modelData.status === "running" ? window.accent : modelData.status === "failed" ? "#bd4540" : "#24845a"
                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                            }
                                                            UI.ToolbarButton {
                                                                id: edaOutputButton
                                                                z: 2
                                                                Layout.preferredWidth: implicitWidth
                                                                visible: codexChatActivity.isEda
                                                                text: window.localizedUiText("输出")
                                                                iconName: "terminal"
                                                                tooltipText: window.localizedUiText("查看 EDA 终端输出")
                                                                onClicked: window.openEdaOutput(
                                                                    codexChatActivity.modelData,
                                                                    Number(codexChatActivity.modelData.sourceIndex !== undefined
                                                                        ? codexChatActivity.modelData.sourceIndex : index))
                                                            }
                                                            UI.FlatIcon {
                                                                visible: codexChatActivity.hasDetails
                                                                name: "chevron"
                                                                color: "#7d8b99"
                                                                width: 14
                                                                height: 14
                                                                rotation: codexChatActivity.expanded ? 90 : 0
                                                            }
                                                        }
                                                        Text {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.expanded
                                                                && !codexChatActivity.isShell
                                                                && !codexChatActivity.isEda
                                                                && modelData.kind !== "patch"
                                                                && !(modelData.kind === "approval" && String(modelData.patchText || "").length > 0)
                                                                && String(window.chatToolResult(modelData) || "").length > 0
                                                            text: window.chatToolResult(modelData)
                                                            color: modelData.status === "failed" ? "#bd4540" : window.chatMuted
                                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                            wrapMode: Text.Wrap
                                                        }
                                                        Rectangle {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.expanded && modelData.shell === true
                                                            implicitHeight: shellOutputColumn.implicitHeight + 16
                                                            color: window.darkMode ? "#202a32" : "#eef6fb"
                                                            radius: 7
                                                            border.color: window.darkMode ? "#3d5365" : "#cfe3f1"
                                                            ColumnLayout {
                                                                id: shellOutputColumn
                                                                anchors.fill: parent
                                                                anchors.margins: 8
                                                                spacing: 5
                                                                RowLayout {
                                                                    Layout.fillWidth: true
                                                                    Text {
                                                                        text: "Shell"
                                                                        color: window.darkMode ? "#79b9df" : "#397fbd"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        font.weight: Font.DemiBold
                                                                    }
                                                                    Item { Layout.fillWidth: true }
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: "$ " + window.shellDisplayCommand(modelData)
                                                                    color: window.darkMode ? "#83bddd" : "#365d78"
                                                                    font.family: "Monospace"
                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                    wrapMode: Text.WrapAnywhere
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.workingDirectory || "").length > 0
                                                                    text: window.localizedUiText("工作目录：") + String(modelData.workingDirectory)
                                                                    color: window.darkMode ? "#a0acb8" : "#718296"
                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                    elide: Text.ElideMiddle
                                                                }
                                                                UI.SmoothScrollView {
                                                                    id: shellOutputScroll
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.shellOutput || "").length > 0
                                                                    Layout.preferredHeight: 210
                                                                    clip: true
                                                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                                                    TextEdit {
                                                                        width: Math.max(1, shellOutputScroll.availableWidth)
                                                                        height: Math.max(shellOutputScroll.availableHeight, implicitHeight)
                                                                        readOnly: true
                                                                        selectByMouse: true
                                                                        persistentSelection: true
                                                                        text: window.terminalOutputText(modelData.shellOutput)
                                                                        color: window.darkMode ? "#c3d2dc" : "#40586b"
                                                                        font.family: "Monospace"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        textFormat: TextEdit.PlainText
                                                                        wrapMode: TextEdit.WrapAnywhere
                                                                        cursorVisible: false
                                                                    }
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    visible: modelData.exitCode !== undefined
                                                                    text: window.localizedUiText("退出码：") + String(modelData.exitCode)
                                                                    color: modelData.exitCode === 0 ? "#24845a" : "#bd4540"
                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                }
                                                                ColumnLayout {
                                                                    Layout.fillWidth: true
                                                                    visible: modelData.fileChanges !== undefined && modelData.fileChanges.length > 0
                                                                    spacing: 4
                                                                    Text {
                                                                        Layout.fillWidth: true
                                                                        text: window.localizedUiText("已编辑的文件")
                                                                        color: window.darkMode ? "#83bddd" : "#365d78"
                                                                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                                        font.weight: Font.DemiBold
                                                                    }
                                                                    Repeater {
                                                                        model: modelData.fileChanges || []
                                                                        delegate: RowLayout {
                                                                            Layout.fillWidth: true
                                                                            spacing: 6
                                                                            Text {
                                                                                Layout.fillWidth: true
                                                                                text: String(modelData.relative_path || modelData.path || "")
                                                                                color: window.darkMode ? "#c3d2dc" : "#40586b"
                                                                                font.family: "Monospace"
                                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                                elide: Text.ElideMiddle
                                                                            }
                                                                            Text {
                                                                                text: "+" + String((modelData.line_stats || {}).added || 0)
                                                                                    + "  -" + String((modelData.line_stats || {}).removed || 0)
                                                                                color: window.darkMode ? "#79b9df" : "#397fbd"
                                                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                            }
                                                                            UI.ToolbarButton {
                                                                                text: "Diff"
                                                                                iconName: "code"
                                                                                visible: String(modelData.patch_file || modelData.diff_file || "").length > 0
                                                                                tooltipText: window.localizedUiText("查看文件差异")
                                                                                onClicked: {
                                                                                    patchDiffDialog.patchFile = String(modelData.patch_file || modelData.diff_file || "")
                                                                                    patchDiffDialog.patchTitle = String(modelData.relative_path || modelData.path || "")
                                                                                    patchDiffDialog.open()
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                            Item {
                                                                anchors.fill: parent
                                                                z: 3
                                                                UI.ToolbarButton {
                                                                    anchors.top: parent.top
                                                                    anchors.right: parent.right
                                                                    anchors.topMargin: 2
                                                                    anchors.rightMargin: 2
                                                                    visible: chatToolHover.hovered
                                                                    text: ""
                                                                    iconName: "copy"
                                                                    tooltipText: window.localizedUiText("复制命令")
                                                                    onClicked: agentController.copyText(window.shellDisplayCommand(modelData))
                                                                }
                                                            }
                                                        }
                                                        Rectangle {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.expanded
                                                                && (modelData.kind === "patch"
                                                                    || (modelData.kind === "approval"
                                                                        && String(modelData.patchText || "").length > 0))
                                                                && (String(modelData.patchFile || "").length > 0
                                                                    || String(modelData.patchText || "").length > 0
                                                                    || window.patchErrorText(modelData).length > 0)
                                                            implicitHeight: patchPreviewColumn.implicitHeight + 16
                                                            color: window.patchErrorText(modelData).length > 0
                                                                ? (window.darkMode ? "#3c2929" : "#fff3f2")
                                                                : (window.darkMode ? "#202a32" : "#f4f8fb")
                                                            radius: 7
                                                            border.color: window.patchErrorText(modelData).length > 0
                                                                ? (window.darkMode ? "#684242" : "#efc7c4")
                                                                : (window.darkMode ? "#3d5365" : "#cfe3f1")
                                                            ColumnLayout {
                                                                id: patchPreviewColumn
                                                                anchors.fill: parent
                                                                anchors.margins: 8
                                                                spacing: 5
                                                                RowLayout {
                                                                    Layout.fillWidth: true
                                                                    spacing: 7
                                                                    Text {
                                                                        text: window.patchFilePath(modelData)
                                                                        color: window.chatText
                                                                        font.family: "Monospace"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        font.weight: Font.DemiBold
                                                                        elide: Text.ElideMiddle
                                                                        Layout.fillWidth: true
                                                                    }
                                                                    Text {
                                                                        readonly property var stats: window.patchLineStats(modelData)
                                                                        text: "+" + String(stats.added || 0)
                                                                        color: "#16834b"
                                                                        font.family: "Monospace"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        font.weight: Font.DemiBold
                                                                    }
                                                                    Text {
                                                                        readonly property var stats: window.patchLineStats(modelData)
                                                                        text: "-" + String(stats.removed || 0)
                                                                        color: "#c43d32"
                                                                        font.family: "Monospace"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        font.weight: Font.DemiBold
                                                                    }
                                                                    UI.ToolbarButton {
                                                                        text: ""
                                                                        iconName: "copy"
                                                                        tooltipText: window.localizedUiText("复制文件名")
                                                                        visible: window.patchFilePath(modelData).length > 0
                                                                        onClicked: agentController.copyText(window.patchFilePath(modelData))
                                                                    }
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    visible: window.patchErrorText(modelData).length > 0
                                                                    text: window.localizedUiText("错误：") + window.patchErrorText(modelData)
                                                                    color: "#b42318"
                                                                    font.family: "Monospace"
                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                    wrapMode: Text.WrapAnywhere
                                                                }
                                                                UI.SmoothScrollView {
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.patchFile || "").length > 0
                                                                        || String(modelData.patchText || "").length > 0
                                                                    Layout.preferredHeight: visible ? 220 : 0
                                                                    clip: true
                                                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                                                    TextEdit {
                                                                        id: patchPreviewText
                                                                        width: Math.max(1, patchPreviewColumn.width - 2)
                                                                        height: Math.max(1, implicitHeight)
                                                                        readOnly: true
                                                                        selectByMouse: true
                                                                        persistentSelection: true
                                                                        textFormat: TextEdit.RichText
                                                                        visible: String(modelData.patchFile || "").length > 0
                                                                            || String(modelData.patchText || "").length > 0
                                                                        text: String(modelData.patchFile || "").length > 0
                                                                            ? agentController.readPatchDiffHtml(String(modelData.patchFile), window.darkMode)
                                                                            : agentController.renderPatchDiffHtml(String(modelData.patchText || ""), window.darkMode)
                                                                        font.family: "Monospace"
                                                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                        wrapMode: TextEdit.NoWrap
                                                                        cursorVisible: false
                                                                    }
                                                                }
                                                            }
                                                        }
                                                        Rectangle {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.expanded
                                                                && String(modelData.permissionPath || "").length > 0
                                                            implicitHeight: permissionRequestColumn.implicitHeight + 16
                                                            color: window.darkMode ? "#393225" : "#fffaf1"
                                                            radius: 7
                                                            border.color: window.darkMode ? "#64543a" : "#ebd9b6"
                                                            ColumnLayout {
                                                                id: permissionRequestColumn
                                                                anchors.fill: parent
                                                                anchors.margins: 9
                                                                spacing: 7
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: (languageSettings.language === "en" ? "Requested path: " : "请求访问：")
                                                                        + String(modelData.permissionPath || "")
                                                                    color: window.chatText
                                                                    font.family: "Monospace"
                                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                                    wrapMode: Text.WrapAnywhere
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: (languageSettings.language === "en" ? "Operation: " : "操作：")
                                                                        + String(modelData.permissionOperation || (languageSettings.language === "en" ? "read file" : "读取文件"))
                                                                        + (modelData.permissionStatus === "pending"
                                                                           ? (languageSettings.language === "en" ? ". Waiting up to 60 seconds; the Agent continues after timeout and keeps this request." : "。等待 60 秒；超时后 Agent 会继续，但此请求仍保留。")
                                                                           : modelData.permissionStatus === "approved"
                                                                               ? (languageSettings.language === "en" ? ". Access granted." : "。已授权。")
                                                                               : modelData.permissionStatus === "denied"
                                                                                   ? (languageSettings.language === "en" ? ". Denied." : "。已拒绝。")
                                                                                   : (languageSettings.language === "en" ? ". The Agent continued; grant access to resume." : "。Agent 已继续；可授权后让它续跑。"))
                                                                    color: window.darkMode ? "#d7c7a6" : "#6f5a39"
                                                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                                    wrapMode: Text.Wrap
                                                                }
                                                                RowLayout {
                                                                    Layout.fillWidth: true
                                                                    spacing: 7
                                                                    UI.ToolbarButton {
                                                                        visible: ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                                        text: languageSettings.language === "en" ? "Allow once" : "允许一次"
                                                                        iconName: "check"
                                                                        emphasized: true
                                                                        onClicked: window.decidePathPermission(modelData, "once")
                                                                    }
                                                                    UI.ToolbarButton {
                                                                        visible: ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                                        text: languageSettings.language === "en" ? "Allow directory" : "授权此目录"
                                                                        iconName: "folder"
                                                                        onClicked: window.decidePathPermission(modelData, "session")
                                                                    }
                                                                    UI.ToolbarButton {
                                                                        visible: ["pending", "expired"].indexOf(String(modelData.permissionStatus || "pending")) >= 0
                                                                        text: languageSettings.language === "en" ? "Deny" : "拒绝"
                                                                        iconName: "close"
                                                                        accentColor: "#bd4540"
                                                                        onClicked: window.decidePathPermission(modelData, "deny")
                                                                    }
                                                                }
                                                            }
                                                        }
                                                        Rectangle {
                                                            Layout.fillWidth: true
                                                            visible: codexChatActivity.expanded && codexChatActivity.isEda
                                                            implicitHeight: edaSummaryColumn.implicitHeight + 14
                                                            color: window.darkMode ? "#202a32" : "#f2f7fb"
                                                            radius: 6
                                                            border.color: window.darkMode ? "#3d5365" : "#d7e3ed"
                                                            ColumnLayout {
                                                                id: edaSummaryColumn
                                                                anchors.fill: parent
                                                                anchors.margins: 7
                                                                spacing: 4
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: window.localizedUiText("EDA 作业") + (String(modelData.jobId || "").length > 0 ? " · " + String(modelData.jobId) : "")
                                                                    color: window.darkMode ? "#8cc8ec" : "#2f6e9c"
                                                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                                    font.weight: Font.DemiBold
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.edaState || "").length > 0 || String(modelData.edaOperation || "").length > 0
                                                                    text: (String(modelData.edaOperation || "").length > 0 ? window.localizedUiText("操作：") + String(modelData.edaOperation) + "  " : "")
                                                                        + (String(modelData.edaState || "").length > 0 ? window.localizedUiText("状态：") + String(modelData.edaState) : "")
                                                                    color: window.darkMode ? "#b2c0cb" : "#566879"
                                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                }
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.edaWorkspace || "").length > 0 || String(modelData.edaLog || "").length > 0
                                                                    text: (String(modelData.edaWorkspace || "").length > 0 ? window.localizedUiText("工作目录：") + String(modelData.edaWorkspace) + "\n" : "")
                                                                        + (String(modelData.edaLog || "").length > 0 ? window.localizedUiText("日志：") + String(modelData.edaLog) : "")
                                                                    color: window.darkMode ? "#a7b5c2" : "#718296"
                                                                    font.family: "Monospace"
                                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                    wrapMode: Text.WrapAnywhere
                                                                }
                                                                TextEdit {
                                                                    Layout.fillWidth: true
                                                                    visible: String(modelData.edaOutput || "").length > 0 || String(modelData.edaError || "").length > 0
                                                                    readOnly: true
                                                                    selectByMouse: true
                                                                    persistentSelection: true
                                                                    text: String(modelData.edaOutput || modelData.edaError || "")
                                                                    color: window.darkMode ? "#d0dbe4" : "#33495c"
                                                                    font.family: "Monospace"
                                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                                    wrapMode: TextEdit.WrapAnywhere
                                                                    cursorVisible: false
                                                                    Layout.preferredHeight: Math.min(180, Math.max(24, implicitHeight))
                                                                }
                                                            }
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: modelData.kind === "patch"
                                                                && String(modelData.editId || "").length > 0
                                                                && modelData.status === "applied"
                                                            UI.ToolbarButton {
                                                                text: window.localizedUiText("撤销")
                                                                iconName: "undo"
                                                                onClicked: {
                                                                    const result = agentController.rollbackFileEdit(
                                                                        String(modelData.editId),
                                                                        String(window.currentProject.id || ""),
                                                                        String(window.currentProject.root || "")
                                                                    )
                                                                    if (!result.ok)
                                                                        window.codexWorkspaceMessage = String(result.message || "回滚失败")
                                                                }
                                                            }
                                                            Item { Layout.fillWidth: true }
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: modelData.kind === "patch" && modelData.status === "awaiting_approval"
                                                            UI.ToolbarButton { text: window.localizedUiText("批准"); iconName: "check"; emphasized: true; onClicked: agentController.decidePatch(String(modelData.proposalId), true) }
                                                            UI.ToolbarButton { text: window.localizedUiText("拒绝"); iconName: "close"; accentColor: "#b84d55"; onClicked: agentController.decidePatch(String(modelData.proposalId), false) }
                                                            Item { Layout.fillWidth: true }
                                                        }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            visible: modelData.kind === "patch" && modelData.status === "approved"
                                                            UI.ToolbarButton {
                                                                text: window.localizedUiText("验证")
                                                                iconName: "play"
                                                                emphasized: true
                                                                onClicked: window.verifyApprovedPatch(String(modelData.proposalId), String(modelData.purpose || ""))
                                                            }
                                                            Item { Layout.fillWidth: true }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Keep chat content selectable and non-draggable while
                                    // handling wheel scrolling explicitly.
                                    MouseArea {
                                        anchors.fill: parent
                                        z: 20
                                        acceptedButtons: Qt.NoButton
                                        hoverEnabled: false
                                        onWheel: function(wheel) {
                                            window.revealCodexChatScrollbar()
                                            if (window.codexActivityScrollLocked) {
                                                wheel.accepted = true
                                                return
                                            }
                                            const delta = wheel.pixelDelta.y !== 0
                                                ? wheel.pixelDelta.y * 1.35
                                                : wheel.angleDelta.y * 0.55
                                            if (delta === 0)
                                                return
                                            // ListView may use a non-zero/negative originY
                                            // when delegates are variable-height.  Clamping
                                            // against zero makes the thumb move while the
                                            // wheel can no longer reach the true top edge.
                                            const minimum = Number(codexChatActivityList.originY || 0)
                                            const maximum = Math.max(minimum, minimum
                                                + codexChatActivityList.contentHeight
                                                - codexChatActivityList.height)
                                            const base = chatWheelAnimator.scrolling
                                                ? Number(chatWheelAnimator.destinationPosition)
                                                : Number(codexChatActivityList.contentY)
                                            const target = Math.max(minimum, Math.min(maximum, base - delta))
                                            if (target <= minimum + 0.5) {
                                                if (chatWheelAnimator.scrolling)
                                                    chatWheelAnimator.cancel()
                                                codexChatActivityList.positionViewAtBeginning()
                                                // contentY may already equal the
                                                // true beginning, so no change
                                                // signal is guaranteed here.
                                                window.scheduleOlderCodexActivityLoad()
                                                wheel.accepted = true
                                                return
                                            }
                                            if (target >= maximum - 0.5) {
                                                if (chatWheelAnimator.scrolling)
                                                    chatWheelAnimator.cancel()
                                                codexChatActivityList.positionViewAtEnd()
                                                wheel.accepted = true
                                                return
                                            }
                                            chatWheelAnimator.scrollTo(target)
                                            wheel.accepted = true
                                        }
                                    }
                                }

                                Rectangle {
                                    id: chatComposerFrame
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: chatComposer.implicitHeight + 18
                                    radius: 10
                                    color: window.chatSurface
                                    border.color: window.chatBorder

                                    ColumnLayout {
                                        id: chatComposer
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 9
                                        spacing: 6
                                        TextArea {
                                            id: agentPromptEditor
                                            property bool syncingPrompt: false
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: Math.max(54, Math.min(130, contentHeight + 12))
                                            placeholderText: window.localizedUiText("描述下一步工作，或插入一个引导…")
                                            placeholderTextColor: "#98a5b2"
                                            color: window.chatText
                                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                            selectByMouse: true
                                            wrapMode: TextEdit.Wrap
                                            background: Item {}
                                            Component.onCompleted: text = window.pendingChatPrompt
                                            onTextChanged: {
                                                if (!syncingPrompt && window.pendingChatPrompt !== text)
                                                    window.pendingChatPrompt = text
                                            }
                                            Connections {
                                                target: window
                                                function onPendingChatPromptChanged() {
                                                    if (agentPromptEditor.text === window.pendingChatPrompt)
                                                        return
                                                    agentPromptEditor.syncingPrompt = true
                                                    agentPromptEditor.text = window.pendingChatPrompt
                                                    agentPromptEditor.syncingPrompt = false
                                                }
                                            }
                                            Keys.onPressed: function(event) {
                                                event.accepted = window.handleChatPromptKey(event)
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 5
                                            visible: agentController.queuedPromptCount > 0
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Text {
                                                    text: languageSettings.language === "en" ? "Message queue" : "消息队列"
                                                    color: window.chatMuted
                                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                    font.weight: Font.DemiBold
                                                }
                                                Rectangle {
                                                    implicitWidth: queueCount.implicitWidth + 12
                                                    implicitHeight: 20
                                                    radius: 10
                                                    color: window.darkMode ? "#443823" : "#fff1dc"
                                                    Text {
                                                        id: queueCount
                                                        anchors.centerIn: parent
                                                        text: agentController.queuedPromptCount
                                                        color: window.darkMode ? "#dec17f" : "#a66819"
                                                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                        font.weight: Font.DemiBold
                                                    }
                                                }
                                                Item { Layout.fillWidth: true }
                                                Text {
                                                    text: languageSettings.language === "en"
                                                        ? "Enter steers now · Ctrl+Enter queues"
                                                        : "Enter 立即引导 · Ctrl+Enter 排队"
                                                    color: window.chatMuted
                                                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                }
                                            }
                                            UI.SmoothListView {
                                                id: queuedPromptList
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: Math.min(3, agentController.queuedPromptCount) * 46
                                                model: agentController.queuedPrompts
                                                clip: true
                                                interactive: agentController.queuedPromptCount > 3
                                                spacing: 3
                                                boundsBehavior: Flickable.StopAtBounds
                                                ScrollBar.vertical: ScrollBar {
                                                    policy: agentController.queuedPromptCount > 3
                                                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                                                }
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    width: queuedPromptList.width
                                                    height: 43
                                                    radius: 6
                                                    color: window.darkMode ? "#20262d" : "#f1f6fa"
                                                    border.color: window.darkMode ? "#39434e" : "#d5e1e9"
                                                    Behavior on color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                                    Behavior on border.color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 9
                                                        anchors.rightMargin: 4
                                                        spacing: 4
                                                        UI.FlatIcon {
                                                            name: modelData.status === "steering" ? "bolt" : "history"
                                                            color: modelData.status === "steering" ? "#d9822b" : "#6c8999"
                                                            width: 15
                                                            height: 15
                                                        }
                                                        Text {
                                                            text: modelData.status === "steering"
                                                                ? (languageSettings.language === "en" ? "Next" : "优先下一条")
                                                                : (languageSettings.language === "en" ? "Queued" : "排队中")
                                                            color: modelData.status === "steering" ? "#b96920" : "#687a89"
                                                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                            Layout.preferredWidth: implicitWidth
                                                        }
                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: String(modelData.prompt || "").replace(/\s+/g, " ")
                                                            color: window.chatText
                                                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                            elide: Text.ElideRight
                                                            maximumLineCount: 1
                                                            HoverHandler { id: queuedPromptPreviewHover }
                                                            UI.ThemedToolTip {
                                                                target: queuedPromptPreviewText
                                                                active: queuedPromptPreviewHover.hovered && queuedPromptPreviewText.truncated
                                                                message: String(modelData.prompt || "")
                                                            }
                                                        }
                                                        UI.ToolbarButton {
                                                            text: ""
                                                            iconName: "editor"
                                                            tooltipText: languageSettings.language === "en" ? "Edit queued message" : "编辑排队消息"
                                                            onClicked: {
                                                                queuedPromptEditDialog.queueId = String(modelData.id || "")
                                                                queuedPromptEditDialog.promptText = String(modelData.prompt || "")
                                                                queuedPromptEditDialog.open()
                                                            }
                                                        }
                                                        UI.ToolbarButton {
                                                            text: ""
                                                            iconName: "bolt"
                                                            iconColor: "#d9822b"
                                                            enabled: modelData.status !== "steering"
                                                            tooltipText: languageSettings.language === "en" ? "Move to front and steer now" : "移至队首并立即引导"
                                                            onClicked: agentController.steerQueuedPrompt(String(modelData.id || ""))
                                                        }
                                                        UI.ToolbarButton {
                                                            text: ""
                                                            iconName: "trash"
                                                            iconColor: "#a45a5a"
                                                            tooltipText: languageSettings.language === "en" ? "Remove from queue" : "移出队列"
                                                            onClicked: agentController.removeQueuedPrompt(String(modelData.id || ""))
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        RowLayout {
                                            id: chatComposerActions
                                            Layout.fillWidth: true
                                            spacing: 7
                                            readonly property bool hasPrompt: window.pendingChatPrompt.trim().length > 0
                                            readonly property bool sessionRunning: window.currentChatSessionRunning()
                                            readonly property bool sessionPaused: window.currentChatSessionPaused()
                                            readonly property bool showingPause: sessionRunning && !hasPrompt
                                            readonly property bool showingSteer: sessionRunning && hasPrompt
                                            readonly property bool showingRecovery: window.codexSessionRecoveryPending
                                                && !sessionRunning && !hasPrompt
                                            UI.ToolbarButton {
                                                id: chatInsertButton
                                                text: ""
                                                iconName: "plus"
                                                iconColor: "#3a4655"
                                                tooltipText: window.localizedUiText("Goal 与 Plan")
                                                onClicked: {
                                                    chatPermissionMenu.close()
                                                    chatModelMenu.close()
                                                    chatInsertMenu.popup(chatInsertButton, 0, -chatInsertMenu.implicitHeight - 8)
                                                }
                                            }
                                            Button {
                                                id: chatMultiAgentToggle
                                                text: languageSettings.language === "en" ? "Multi-agent" : "多智能体"
                                                checkable: true
                                                checked: languageSettings.chatMultiAgentEnabled
                                                implicitWidth: multiAgentToggleLabel.implicitWidth + 26
                                                implicitHeight: 28
                                                Layout.preferredWidth: implicitWidth
                                                Layout.minimumWidth: implicitWidth
                                                Layout.maximumWidth: implicitWidth
                                                leftPadding: 0
                                                rightPadding: 0
                                                topPadding: 0
                                                bottomPadding: 0
                                                hoverEnabled: false
                                                HoverHandler { id: multiAgentToggleHover }
                                                background: Rectangle {
                                                    radius: 14
                                                    color: chatMultiAgentToggle.checked
                                                        ? (multiAgentToggleHover.hovered ? "#61399f" : "#7046b5")
                                                        : (window.darkMode
                                                            ? (multiAgentToggleHover.hovered ? "#37424d" : "#2b333c")
                                                            : (multiAgentToggleHover.hovered ? "#e2edf7" : "#edf2f7"))
                                                    border.width: 1
                                                    border.color: chatMultiAgentToggle.checked
                                                        ? (window.darkMode ? "#9471ca" : "#603b9d")
                                                        : (window.darkMode ? "#566370" : "#bdcbd8")
                                                    Behavior on color { ColorAnimation { duration: 130 } }
                                                    Behavior on border.color { ColorAnimation { duration: 130 } }
                                                }
                                                contentItem: Text {
                                                    id: multiAgentToggleLabel
                                                    text: chatMultiAgentToggle.text
                                                    color: chatMultiAgentToggle.checked ? "#ffffff" : (window.darkMode ? "#e1e8ef" : "#465666")
                                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                                    font.weight: Font.Medium
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                    elide: Text.ElideRight
                                                }
                                                onToggled: languageSettings.chatMultiAgentEnabled = checked
                                                UI.ThemedToolTip {
                                                    target: chatMultiAgentToggle
                                                    active: multiAgentToggleHover.hovered
                                                    message: languageSettings.language === "en"
                                                        ? "Let the agent delegate independent work to parallel agents"
                                                        : "允许主智能体将独立工作并行委托给多个子智能体"
                                                }
                                            }
                                            UI.ToolbarButton {
                                                id: chatPermissionButton
                                                // Keep the access label tightly wrapped around its icon and text.
                                                // A fixed width left a visible blank strip beside the Chinese label.
                                                Layout.preferredWidth: implicitWidth
                                                Layout.minimumWidth: implicitWidth
                                                Layout.maximumWidth: implicitWidth
                                                text: window.agentPermissionMode === "full_access"
                                                    ? (languageSettings.language === "en" ? "Full access" : "完全访问")
                                                    : window.agentPermissionMode === "autonomous"
                                                        ? (languageSettings.language === "en" ? "Autonomous iteration" : "自主迭代")
                                                        : (languageSettings.language === "en" ? "Ask approval" : "请求批准")
                                                iconName: window.agentPermissionMode === "full_access"
                                                    ? "shield"
                                                    : window.agentPermissionMode === "autonomous" ? "rocket" : "hand"
                                                iconColor: window.agentPermissionMode === "full_access"
                                                    ? (window.darkMode ? "#ff8585" : "#d14343")
                                                    : window.agentPermissionMode === "autonomous"
                                                        ? (window.darkMode ? "#ffc16b" : "#d97706")
                                                        : (window.darkMode ? "#d6e5f0" : "#55788c")
                                                idleSurfaceColor: "transparent"
                                                hoverSurfaceColor: window.darkMode ? "#293b4b" : "#eef5fb"
                                                useLabelColor: true
                                                labelColor: window.agentPermissionMode === "full_access"
                                                    ? (window.darkMode ? "#ff9292" : "#b83232")
                                                    : window.agentPermissionMode === "autonomous"
                                                        ? (window.darkMode ? "#ffc16b" : "#a45d08")
                                                        : (window.darkMode ? "#d6e5f0" : "#55788c")
                                                onClicked: {
                                                    chatInsertMenu.close()
                                                    chatModelMenu.close()
                                                    if (chatPermissionMenu.visible)
                                                        chatPermissionMenu.close()
                                                    else
                                                        chatPermissionMenu.openAtAnchor()
                                                }
                                            }
                            Item { Layout.fillWidth: true }
                                            UI.ToolbarButton {
                                                id: chatModelButton
                                                Layout.preferredWidth: implicitWidth
                                                Layout.maximumWidth: 196
                                                visible: !chatModelMenu.visible || chatModelMenu.pickerPage === 1
                                                text: window.codexChatModelLabel() + "  " + window.codexChatReasoningLabel()
                                                iconName: "bolt"
                                                iconColor: "#4db5e6"
                                                idleSurfaceColor: "transparent"
                                                hoverSurfaceColor: "transparent"
                                                surfaceRadius: 7
                                                useLabelColor: true
                                                labelColor: window.darkMode ? "#70c8f1" : "#2589ba"
                                                tooltipText: languageSettings.language === "en" ? "Switch model and reasoning depth" : "切换模型与推理深度"
                                                onClicked: {
                                                    chatInsertMenu.close()
                                                    chatPermissionMenu.close()
                                                    if (chatModelMenu.visible)
                                                        chatModelMenu.close()
                                                    else
                                                        chatModelMenu.openAtAnchor()
                                                }
                                            }
                                            UI.ToolbarButton {
                                                id: chatReasoningComposerButton
                                                Layout.preferredWidth: implicitWidth
                                                visible: chatModelMenu.visible && chatModelMenu.pickerPage === 0
                                                text: languageSettings.language === "en" ? "Select effort" : "选择强度"
                                                iconName: "chevron"
                                                iconOnRight: true
                                                iconItem.rotation: 90
                                                iconColor: "#6daec9"
                                                idleSurfaceColor: "transparent"
                                                hoverSurfaceColor: "transparent"
                                                surfaceRadius: 7
                                                useLabelColor: true
                                                labelColor: window.darkMode ? "#78c2df" : "#247d9f"
                                                tooltipText: languageSettings.language === "en" ? "Choose reasoning depth" : "选择思考深度"
                                                onClicked: {
                                                    chatInsertMenu.close()
                                                    chatPermissionMenu.close()
                                                    if (chatModelMenu.visible)
                                                        chatModelMenu.openReasoningPicker()
                                                    else
                                                        chatModelMenu.openAtAnchor(0, chatReasoningComposerButton)
                                                }
                                            }
                                            UI.ToolbarButton {
                                                id: chatModelSettingsButton
                                                text: ""
                                                iconName: "settings"
                                                visible: false
                                                tooltipText: languageSettings.language === "en" ? "Model settings" : "模型设置"
                                                onClicked: {
                                                    window.syncModelSettingsEditor()
                                                    window.selectedPage = 8
                                                }
                                            }
                                            Text {
                                                visible: false
                                                text: agentController.queuedPromptCount + window.localizedUiText(" 条待发送")
                                                color: "#a66819"
                                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                            }
                                            Text {
                                                text: chatComposerActions.sessionRunning ? window.localizedUiText("Enter 立即引导 · Ctrl+Enter 排队") : window.localizedUiText("Enter 发送")
                                                color: "#7c8996"
                                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                            }
                                            UI.ToolbarButton {
                                                id: chatActionButton
                                                Layout.preferredWidth: (chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? 38 : implicitWidth
                                                Layout.minimumWidth: (chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? 38 : 0
                                                Layout.preferredHeight: (chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? 38 : 32
                                                Layout.minimumHeight: (chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? 38 : 32
                                                text: chatComposerActions.showingSteer
                                                    ? "立即引导"
                                                    : ((chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? "" : "发送")
                                                iconName: chatComposerActions.showingRecovery ? "play"
                                                    : (chatComposerActions.showingSteer ? "bolt"
                                                       : (chatComposerActions.showingPause ? (chatComposerActions.sessionPaused ? "play" : "stop") : "play"))
                                                emphasized: !chatComposerActions.showingSteer
                                                    && !chatComposerActions.showingPause
                                                    && !chatComposerActions.showingRecovery
                                                enabled: chatComposerActions.showingPause
                                                    || chatComposerActions.showingRecovery
                                                    || chatComposerActions.hasPrompt
                                                iconColor: chatComposerActions.showingRecovery ? (window.darkMode ? "#b4ccdf" : "#526b7d")
                                                    : chatComposerActions.showingSteer ? (window.darkMode ? "#f1ad60" : "#d9822b")
                                                    : chatComposerActions.sessionPaused ? (window.darkMode ? "#78c797" : "#3a8a63")
                                                    : (window.darkMode ? "#b4ccdf" : "#526b7d")
                                                useLabelColor: chatComposerActions.showingSteer
                                                labelColor: window.darkMode ? "#f1b56f" : "#a95d15"
                                                idleSurfaceColor: (chatComposerActions.showingPause || chatComposerActions.showingRecovery)
                                                    ? (window.darkMode ? (chatComposerActions.sessionPaused ? "#263b31" : "#293846")
                                                        : (chatComposerActions.showingRecovery ? "#dce8f0" : (chatComposerActions.sessionPaused ? "#e3f2e9" : "#dce8f0")))
                                                    : "transparent"
                                                hoverSurfaceColor: (chatComposerActions.showingPause || chatComposerActions.showingRecovery)
                                                    ? (window.darkMode ? (chatComposerActions.sessionPaused ? "#314b3d" : "#364c5e")
                                                        : (chatComposerActions.showingRecovery ? "#cddde8" : (chatComposerActions.sessionPaused ? "#d5ebdf" : "#cddde8")))
                                                    : (window.darkMode ? "#293b4b" : "#eef5fb")
                                                surfaceRadius: (chatComposerActions.showingPause || chatComposerActions.showingRecovery) ? 18 : 7
                                                tooltipText: chatComposerActions.showingSteer
                                                    ? "立即发送引导；当前工具不中断"
                                                    : (chatComposerActions.showingRecovery
                                                       ? "从最近检查点恢复中断的 Agent 回合"
                                                       : (chatComposerActions.showingPause
                                                          ? (chatComposerActions.sessionPaused ? "继续运行" : "暂停当前回合")
                                                          : "Enter 立即引导；Ctrl+Enter 排队；Shift+Enter 换行"))
                                                onClicked: {
                                                    if (chatComposerActions.showingRecovery) {
                                                        window.resumeCodexSession()
                                                    } else if (chatComposerActions.showingPause) {
                                                        agentController.setFlowDisplaySession(window.codexSessionId)
                                                        if (chatComposerActions.sessionPaused)
                                                            agentController.resume()
                                                        else
                                                            agentController.pause()
                                                    } else if (chatComposerActions.showingSteer) {
                                                        window.submitAgentPrompt(true)
                                                    } else {
                                                        window.submitAgentPrompt()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Menu {
                        id: chatInsertMenu
                        width: 286
                        padding: 5
                        background: Rectangle {
                            radius: 10
                            color: window.chatSurface
                            border.color: window.chatBorder
                            border.width: 1
                        }
                        enter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 150; easing.type: Easing.OutBack }
                            }
                        }
                        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90; easing.type: Easing.InCubic } }
                        UI.ChatMenuItem {
                            text: window.codexSessionGoal.length > 0 ? window.localizedUiText("编辑 Goal") : window.localizedUiText("创建 Goal")
                            menuIcon: "projects"
                            subtitle: window.localizedUiText("设置当前会话持续追踪的工程目标")
                            onTriggered: window.openCodexGoalEditor()
                        }
                        UI.ChatMenuItem {
                            text: window.codexPlanText().length > 0 ? window.localizedUiText("更新 Plan") : window.localizedUiText("创建 Plan")
                            menuIcon: "flow"
            subtitle: window.localizedUiText("生成执行计划")
                            enabled: !agentController.running
                            onTriggered: window.requestCodexPlan()
                        }
                    }

                    Popup {
                        id: chatModelMenu
                        parent: Overlay.overlay
                        width: 280
                        height: pickerHeight
                        // Keep the picker above the composer and other page
                        // content when a tall model list is open.
                        z: 500
                        padding: 12
                        modal: false
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        focus: true
                        transformOrigin: Item.BottomRight
                        property int pickerPage: 0
                        property int renderedPickerPage: 0
                        property int pickerPageBeforeTransition: 0
                        property int pickerHeight: pageHeight(0)
                        property Item pickerAnchor: chatModelButton
                        function pageHeight(page) {
                            if (page === 0)
                                return 112
                            const count = window.codexChatModelOptions().length
                            return Math.min(360, 84 + Math.max(1, count) * 30)
                        }
                        Behavior on height {
                            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                        }
                        function switchPickerPage(page) {
                            if (page === pickerPage && page === renderedPickerPage)
                                return
                            pickerPageBeforeTransition = pickerPage
                            pickerPage = page
                            pickerHeight = pageHeight(page)
                            pickerPageTransition.restart()
                        }
                        function openModelPicker() { if (!visible) openAtAnchor(1); else switchPickerPage(1) }
                        function openReasoningPicker() { if (!visible) openAtAnchor(0); else switchPickerPage(0) }
                        function updateAnchor() {
                            if (!Overlay.overlay)
                                return
                            const source = pickerAnchor && pickerAnchor.visible ? pickerAnchor : chatModelButton
                            const anchor = source.mapToItem(Overlay.overlay, 0, 0)
                            const centeredX = anchor.x + (source.width - width) / 2
                            x = Math.max(8, Math.min(centeredX, Overlay.overlay.width - width - 8))
                            const above = anchor.y - height - 8
                            const below = anchor.y + chatModelButton.height + 8
                            y = above >= 8 && above + height <= Overlay.overlay.height - 8
                                ? above
                                : below + height <= Overlay.overlay.height - 8
                                    ? below
                                    : Math.max(8, Overlay.overlay.height - height - 8)
                        }
                        function openAtAnchor(page, anchorItem) {
                            pickerPage = page === undefined ? 0 : page
                            pickerAnchor = anchorItem || chatModelButton
                            renderedPickerPage = pickerPage
                            pickerHeight = pageHeight(pickerPage)
                            chatModelPages.opacity = 1
                            chatModelPages.x = 0
                            updateAnchor()
                            open()
                            forceActiveFocus()
                        }
                        onHeightChanged: if (visible) Qt.callLater(updateAnchor)
                        Connections {
                            target: agentController
                            function onCodexModelsChanged() {
                                if (chatModelMenu.visible && chatModelMenu.pickerPage === 1) {
                                    chatModelMenu.pickerHeight = chatModelMenu.pageHeight(1)
                                    Qt.callLater(chatModelMenu.updateAnchor)
                                }
                            }
                        }
                        onClosed: {
                            pickerPage = 0
                            renderedPickerPage = 0
                            pickerHeight = pageHeight(0)
                            chatModelPages.currentIndex = 0
                            chatModelPages.opacity = 1
                            chatModelPages.x = 0
                        }
                        background: Rectangle {
                            radius: 16
                            color: window.chatSurface
                            border.color: window.chatBorder
                            border.width: 1
                        }
                        enter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: 160; easing.type: Easing.OutCubic }
                            }
                        }
                        exit: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InCubic }
                                NumberAnimation { property: "scale"; from: 1; to: 0.96; duration: 100; easing.type: Easing.InCubic }
                            }
                        }
                        SequentialAnimation {
                            id: pickerPageTransition
                            ParallelAnimation {
                                NumberAnimation { target: chatModelPages; property: "opacity"; to: 0.12; duration: 75; easing.type: Easing.InCubic }
                                NumberAnimation { target: chatModelPages; property: "x"; to: chatModelMenu.pickerPage > chatModelMenu.pickerPageBeforeTransition ? -14 : 14; duration: 75; easing.type: Easing.InCubic }
                            }
                            ScriptAction { script: { chatModelPages.currentIndex = chatModelMenu.pickerPage; chatModelMenu.renderedPickerPage = chatModelMenu.pickerPage } }
                            ParallelAnimation {
                                NumberAnimation { target: chatModelPages; property: "opacity"; to: 1; duration: 180; easing.type: Easing.OutCubic }
                                NumberAnimation { target: chatModelPages; property: "x"; to: 0; duration: 180; easing.type: Easing.OutCubic }
                            }
                        }
                        contentItem: StackLayout {
                            id: chatModelPages
                            anchors.fill: parent
                            anchors.margins: chatModelMenu.padding
                            currentIndex: chatModelMenu.renderedPickerPage
                            opacity: 1
                            x: 0
                            clip: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                    UI.FlatIcon { name: "bolt"; color: "#45afe1"; width: 18; height: 18 }
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 1
                                            Row {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                spacing: 5
                                                Text {
                                                    text: window.codexChatReasoningLabel()
                                                    color: "#3c9ed1"
                                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                                    font.weight: Font.DemiBold
                                                }
                                                UI.FlatIcon { name: "chevron"; color: "#3c9ed1"; width: 12; height: 12; rotation: 90 }
                                            }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: window.codexChatModelLabel()
                                                color: "#738898"
                                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                            }
                                        }
                                        MouseArea { anchors.fill: parent; onClicked: chatModelMenu.openModelPicker() }
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "refresh"
                                        implicitWidth: 28
                                        darkMode: false
                                        iconColor: "#5e7b8d"
                                        enabled: !agentController.codexModelsLoading
                                        tooltipText: languageSettings.language === "en" ? "Refresh available models" : "刷新可用模型"
                                        onClicked: window.refreshCodexModels()
                                    }
                                }
                                Slider {
                                    id: chatReasoningSlider
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    from: 0
                                    to: Math.max(0, window.codexChatReasoningLevels().length - 1)
                                    stepSize: 1
                                    snapMode: Slider.SnapAlways
                                    value: window.codexChatReasoningIndex()
                                    onMoved: {
                                        const levels = window.codexChatReasoningLevels()
                                        if (levels.length > 0)
                                            window.codexChatReasoningEffort = String(levels[Math.max(0, Math.min(levels.length - 1, Math.round(value)))].effort || "medium")
                                    }
                                    background: Rectangle {
                                        x: chatReasoningSlider.leftPadding
                                        y: chatReasoningSlider.topPadding + chatReasoningSlider.availableHeight / 2 - height / 2
                                        width: chatReasoningSlider.availableWidth
                                        height: 12
                                        radius: 6
                                        color: "#d9e8f1"
                                        Rectangle {
                                            width: chatReasoningSlider.visualPosition * parent.width
                                            height: parent.height
                                            radius: 6
                                            color: "#4aaee0"
                                        }
                                    }
                                    handle: Rectangle {
                                        x: chatReasoningSlider.leftPadding + chatReasoningSlider.visualPosition * (chatReasoningSlider.availableWidth - width)
                                        y: chatReasoningSlider.topPadding + chatReasoningSlider.availableHeight / 2 - height / 2
                                        width: 30
                                        height: 30
                                        radius: 15
                                        color: window.darkMode ? "#dce4eb" : "#ffffff"
                                        border.color: window.darkMode ? "#5a7184" : "#cae1ee"
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: languageSettings.language === "en" ? "Select model" : "选择模型"
                                        color: "#4e9bc4"
                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                    }
                                    UI.ToolbarButton {
                                        text: ""
                                        iconName: "refresh"
                                        implicitWidth: 28
                                        iconColor: "#5e7b8d"
                                        enabled: !agentController.codexModelsLoading
                                        onClicked: window.refreshCodexModels()
                                    }
                                }
                                UI.SmoothListView {
                                    id: codexChatModelList
                                    objectName: "qaChatModelList"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: window.codexChatModelOptions()
                                    delegate: Item {
                                        id: modelRow
                                        required property var modelData
                                        width: ListView.view ? ListView.view.width : 0
                                        height: 30
                                        property bool hovered: false
                                        readonly property bool isDefault: Boolean(modelData.isDefault)
                                        readonly property bool isSelected: !isDefault
                                            && String(modelData.id || "") === (window.codexChatUsesDefaultModel
                                                ? window.codexDefaultModelId()
                                                : String(window.codexChatModel || ""))
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8
                                            color: window.darkMode
                                                ? modelRow.isSelected ? "#20384d" : modelRow.hovered ? "#29333d" : "transparent"
                                                : modelRow.isSelected ? "#e5f2fb" : modelRow.hovered ? "#f1f7fb" : "transparent"
                                        }
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: String(modelData.label || modelData.id || "")
                                            color: window.darkMode ? (modelRow.isSelected ? "#9bc8ef" : "#d0d9e1")
                                                : modelRow.isSelected ? "#287ca9" : "#3a4b58"
                                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                            elide: Text.ElideRight
                                        }
                                        UI.FlatIcon {
                                            anchors.right: parent.right
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: modelRow.isSelected
                                            name: "check"
                                            color: "#5a7180"
                                            width: 16
                                            height: 16
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onEntered: modelRow.hovered = true
                                            onExited: modelRow.hovered = false
                                            onClicked: {
                                                window.selectCodexChatModel(
                                                    modelRow.isDefault ? window.codexDefaultModelId() : String(modelData.id || ""),
                                                    modelRow.isDefault ? window.codexApiModel() : modelData,
                                                    modelRow.isDefault
                                                )
                                                chatModelMenu.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Popup {
                        id: chatPermissionMenu
                        parent: Overlay.overlay
                        width: 326
                        padding: 5
                        modal: false
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        focus: true
                        function openAtAnchor() {
                            const anchor = chatPermissionButton.mapToItem(Overlay.overlay, 0, 0)
                            x = Math.max(8, Math.min(anchor.x, Overlay.overlay.width - width - 8))
                            y = Math.max(8, anchor.y - height - 8)
                            open()
                            forceActiveFocus()
                        }
                        background: Rectangle {
                            radius: 10
                            color: window.chatSurface
                            border.color: window.chatBorder
                            border.width: 1
                        }
                        enter: Transition {
                            ParallelAnimation {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
                                NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 150; easing.type: Easing.OutBack }
                            }
                        }
                        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90; easing.type: Easing.InCubic } }
                        contentItem: ColumnLayout {
                            spacing: 1
                            Text {
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 5
                                text: languageSettings.language === "en" ? "File and tool permissions" : "文件修改与工具执行权限"
                                color: "#627486"
                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                            }
                            UI.ChatMenuItem {
                                Layout.fillWidth: true
                                text: languageSettings.language === "en" ? "Ask for approval" : "请求批准"
                                subtitle: languageSettings.language === "en" ? "Create a patch and wait for review" : "修改先生成补丁，人工批准后再验证"
                                menuIcon: "hand"
                                showCheckbox: true
                                checkable: true
                                checked: window.agentPermissionMode === "approval"
                                onTriggered: window.setAgentPermissionMode("approval")
                            }
                            UI.ChatMenuItem {
                                Layout.fillWidth: true
                                text: languageSettings.language === "en" ? "Autonomous iteration" : "自主迭代"
                                subtitle: languageSettings.language === "en" ? "Apply project edits with rollback snapshots" : "允许 Agent 直接修改项目文件并保留回滚快照"
                                menuIcon: "rocket"
                                iconColor: "#f39a3d"
                                accentColor: "#d97706"
                                textColor: "#d97706"
                                subtitleColor: "#b45309"
                                showCheckbox: true
                                checkable: true
                                checked: window.agentPermissionMode === "autonomous"
                                onTriggered: window.setAgentPermissionMode("autonomous")
                            }
                            UI.ChatMenuItem {
                                Layout.fillWidth: true
                                text: languageSettings.language === "en" ? "Full access" : "完全访问"
                                subtitle: languageSettings.language === "en"
                                    ? "Allow shell and file tools to use any path"
                                    : "允许 shell 与文件工具访问任意路径"
                                menuIcon: "shield"
                                iconColor: "#d14343"
                                accentColor: "#d14343"
                                textColor: "#c43d3d"
                                subtitleColor: "#a63a3a"
                                showCheckbox: true
                                checkable: true
                                checked: window.agentPermissionMode === "full_access"
                                onTriggered: window.setAgentPermissionMode("full_access")
                            }
                        }
                    }

                    Menu {
                        id: chatSessionMenu
                        width: 226
                        padding: 5
                        property string sessionId: ""
                        property int sessionTurns: 0
                        property bool sessionArchived: false
                        background: Rectangle {
                            radius: 10
                            color: window.chatSurface
                            border.color: window.chatBorder
                            border.width: 1
                        }
                        UI.ChatMenuItem {
                            text: window.localizedUiText("重命名会话")
                            menuIcon: "editor"
                            enabled: !chatSessionMenu.sessionArchived
                        onTriggered: {
                                const matching = window.findCodexSession(chatSessionMenu.sessionId)
                                window.renameCodexChatSession(
                                    chatSessionMenu.sessionId,
                                    matching ? String(matching.name || "") : ""
                                )
                            }
                        }
                        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#e2e8ee" } }
                        UI.ChatMenuItem {
                            text: window.localizedUiText("从此处创建分支")
                            menuIcon: "copy"
                            enabled: !chatSessionMenu.sessionArchived
                            onTriggered: window.manageCodexChatSession("session_fork", chatSessionMenu.sessionId, false)
                        }
                        UI.ChatMenuItem {
                            text: window.localizedUiText("回退最近一轮")
                            menuIcon: "undo"
                            enabled: !chatSessionMenu.sessionArchived && chatSessionMenu.sessionTurns > 0
                            onTriggered: window.manageCodexChatSession("session_rollback", chatSessionMenu.sessionId, false)
                        }
                        UI.ChatMenuItem {
                            text: window.localizedUiText("压缩较早上下文")
                            menuIcon: "compress"
                            enabled: !chatSessionMenu.sessionArchived && chatSessionMenu.sessionTurns > 4
                            onTriggered: window.manageCodexChatSession("session_compact", chatSessionMenu.sessionId, false)
                        }
                        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: "#e2e8ee" } }
                        UI.ChatMenuItem {
                            text: chatSessionMenu.sessionArchived ? window.localizedUiText("恢复会话") : window.localizedUiText("归档会话")
                            menuIcon: chatSessionMenu.sessionArchived ? "undo" : "archive"
                            onTriggered: window.manageCodexChatSession(
                                chatSessionMenu.sessionArchived ? "session_restore" : "session_archive",
                                chatSessionMenu.sessionId,
                                chatSessionMenu.sessionArchived
                            )
                        }
                        UI.ChatMenuItem {
                            text: window.localizedUiText("删除会话")
                            menuIcon: "trash"
                            accentColor: "#b84d55"
                            subtitle: window.localizedUiText("永久删除本地会话记录和工具事件")
                            onTriggered: {
                                chatSessionDeleteDialog.sessionId = chatSessionMenu.sessionId
                                chatSessionDeleteDialog.sessionName = ""
                                const session = window.findCodexSession(chatSessionMenu.sessionId)
                                chatSessionDeleteDialog.sessionName = session ? String(session.name || "会话") : "会话"
                                chatSessionDeleteDialog.open()
                            }
                        }
                    }

                    UI.ChatDialog {
                        id: chatSessionRenameDialog
                        property string sessionId: ""
                        width: Math.min(420, window.width - 48)
                        fitContent: true
                        title: window.localizedUiText("重命名会话")
                        iconName: "editor"
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            UI.StyledTextField {
                                id: chatSessionRenameField
                                Layout.fillWidth: true
                                placeholderText: window.localizedUiText("会话名称")
                                maximumLength: 120
                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                        chatSessionRenameDialog.accept()
                                        event.accepted = true
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton {
                                    text: window.localizedUiText("取消")
                                    onClicked: chatSessionRenameDialog.close()
                                }
                                UI.ToolbarButton {
                                    text: window.localizedUiText("保存")
                                    iconName: "save"
                                    emphasized: true
                                    enabled: chatSessionRenameField.text.trim().length > 0
                                    onClicked: chatSessionRenameDialog.accept()
                                }
                            }
                        }
                        onAccepted: {
                            const response = window.codexWorkspaceAction("session_rename", ({
                                thread_id: sessionId,
                                name: chatSessionRenameField.text.trim()
                            }), window.projectForCodexSession(sessionId))
                            if (response.ok) {
                                window.refreshFlowSessionList()
                                window.refreshCodexWorkspace()
                            }
                        }
                    }

                    UI.ChatDialog {
                        id: chatSessionDeleteDialog
                        property string sessionId: ""
                        property string sessionName: ""
                        title: window.localizedUiText("删除会话")
                        description: window.localizedUiText("此操作只删除本机保存的会话文本、Plan 和工具事件，无法撤销。")
                        iconName: "trash"
                        width: Math.min(470, window.width - 48)
                        fitContent: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 14
                            Text {
                                Layout.fillWidth: true
                                text: window.localizedUiText("确定删除“") + chatSessionDeleteDialog.sessionName + window.localizedUiText("”吗？")
                                color: window.graphite
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton { text: window.localizedUiText("取消"); onClicked: chatSessionDeleteDialog.close() }
                                UI.ToolbarButton {
                                    text: window.localizedUiText("删除")
                                    iconName: "trash"
                                    accentColor: "#b84d55"
                                    emphasized: true
                                    enabled: chatSessionDeleteDialog.sessionId.length > 0
                                    onClicked: {
                                        window.manageCodexChatSession(
                                            "session_delete",
                                            chatSessionDeleteDialog.sessionId,
                                            false
                                        )
                                        chatSessionDeleteDialog.close()
                                    }
                                }
                            }
                        }
                    }

                    UI.ChatDialog {
                        id: codexSessionBatchDeleteDialog
                        property int selectedCount: 0
                        title: window.localizedUiText("批量删除会话")
                        description: window.localizedUiText("将永久删除所选会话的文本、Plan 和工具事件，且无法撤销。")
                        iconName: "trash"
                        width: Math.min(480, window.width - 48)
                        fitContent: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 14
                            Text {
                                Layout.fillWidth: true
                                text: window.localizedUiText("确定永久删除选中的 ") + codexSessionBatchDeleteDialog.selectedCount + window.localizedUiText(" 个会话吗？")
                                color: window.graphite
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton { text: window.localizedUiText("取消"); onClicked: codexSessionBatchDeleteDialog.close() }
                                UI.ToolbarButton {
                                    text: window.localizedUiText("删除")
                                    iconName: "trash"
                                    accentColor: "#b84d55"
                                    emphasized: true
                                    enabled: codexSessionBatchDeleteDialog.selectedCount > 0
                                    onClicked: {
                                        window.performCodexChatSessionBatch("session_delete_many")
                                        codexSessionBatchDeleteDialog.close()
                                    }
                                }
                            }
                        }
                    }

                    UI.ChatDialog {
                        id: chatEdaOutputDialog
                        property int activityIndex: -1
                        property string jobId: ""
                        property string operation: ""
                        property string state: ""
                        property string workspace: ""
                        property string logPath: ""
                        property string commandText: ""
                        property string outputText: ""
                        property string fallbackOutputText: ""
                        property string errorText: ""
                        property string liveOutputText: ""
                        property string loadedLogPath: ""
                        property string loadedLogWorkspace: ""
                        property int logStartOffset: 0
                        property int logEndOffset: 0
                        property int logFileSize: 0
                        property bool logHasMore: false
                        property bool followTail: true
                        property bool pagingOlder: false
                        property bool appendingLog: false
                        readonly property int logPageBytes: 65536
                        readonly property int maximumLoadedLogCharacters: 524288
                        width: Math.min(900, window.width - 48)
                        fitContent: false
                        title: window.localizedUiText("EDA 工具输出")
                        description: (operation.length > 0 ? operation + window.localizedUiText(" · 终端日志") : window.localizedUiText("dc_shell / TestMAX 实时终端日志"))
                            + " · 向上滚动加载历史，贴底时自动跟随"
                        iconName: "terminal"

                        function syncFromEntry(entry) {
                            jobId = String(entry.jobId || "")
                            operation = String(entry.edaOperation || entry.name || "")
                            state = String(entry.edaState || entry.status || "")
                            workspace = String(entry.edaWorkspace || "")
                            logPath = String(entry.edaLog || "")
                            commandText = String(entry.edaCommand || "")
                            fallbackOutputText = String(entry.edaOutput || "")
                            liveOutputText = Boolean(entry.edaLive) ? fallbackOutputText : ""
                            errorText = String(entry.edaError || "")
                            const pathChanged = logPath.length > 0 && loadedLogPath.length > 0
                                && loadedLogPath !== logPath
                            const workspaceChanged = loadedLogWorkspace.length > 0
                                && loadedLogWorkspace !== workspace
                            if (pathChanged || workspaceChanged) {
                                loadedLogPath = ""
                                loadedLogWorkspace = ""
                                logStartOffset = 0
                                logEndOffset = 0
                                logFileSize = 0
                                logHasMore = false
                                followTail = true
                                outputText = ""
                            }
                            refreshTerminalLog()
                        }

                        function terminalLogCandidates() {
                            if (workspace.length === 0)
                                return []
                            let root = workspace.trim()
                            while (root.endsWith("/"))
                                root = root.slice(0, -1)
                            const flow = root.endsWith("/flow") ? root : root + "/flow"
                            const candidates = [
                                flow + "/command.log",
                                flow + "/filenames.log",
                                flow + "/logs/agent_insert_dft.log",
                                flow + "/logs/agent_dc_shell.log",
                                flow + "/logs/dc_shell.log",
                                flow + "/logs/dc.log",
                                flow + "/logs/agent_atpg.stdout.log",
                                flow + "/logs/agent_atpg.log",
                                flow + "/reports/agent_mbist_simulation.log"
                            ]
                            const discovered = agentController.edaLogFiles(root)
                            for (let index = 0; index < discovered.length; ++index) {
                                const candidate = String(discovered[index] || "")
                                if (candidate.length > 0 && candidates.indexOf(candidate) < 0)
                                    candidates.push(candidate)
                            }
                            return candidates
                        }

                        function refreshTerminalLog() {
                            if (!followTail)
                                return

                            let path = logPath.trim()
                            let firstPage = path.length > 0 && path === loadedLogPath
                                ? ({ available: true })
                                : path.length > 0 ? agentController.readReportPage(path, -1, logPageBytes)
                                                  : ({ available: false })
                            if (!firstPage.available) {
                                const candidates = terminalLogCandidates()
                                for (let index = 0; index < candidates.length; ++index) {
                                    const candidate = String(candidates[index] || "")
                                    if (candidate === loadedLogPath) {
                                        path = candidate
                                        firstPage = ({ available: true })
                                        break
                                    }
                                    const candidatePage = agentController.readReportPage(candidate, -1, logPageBytes)
                                    if (candidatePage.available) {
                                        path = candidate
                                        firstPage = candidatePage
                                        break
                                    }
                                }
                            }

                            if (path.length === 0 || !firstPage.available) {
                                const live = liveOutputText.slice(-maximumLoadedLogCharacters)
                                if (followTail && live.length > 0 && live !== outputText) {
                                    appendingLog = true
                                    outputText = live
                                    Qt.callLater(scrollLogToBottom)
                                }
                                return
                            }

                            if (loadedLogPath !== path) {
                                const page = firstPage
                                if (!page.available)
                                    return
                                loadedLogPath = path
                                loadedLogWorkspace = workspace
                                appendingLog = true
                                outputText = String(page.text || "")
                                logStartOffset = Number(page.start_offset || 0)
                                logEndOffset = Number(page.end_offset || 0)
                                logFileSize = Number(page.file_size || 0)
                                logHasMore = Boolean(page.has_more)
                                Qt.callLater(scrollLogToBottom)
                                return
                            }

                            const delta = agentController.readReportRange(path, logEndOffset, logPageBytes)
                            const currentSize = Number(delta.file_size || 0)
                            if (currentSize < logEndOffset) {
                                loadedLogPath = ""
                                refreshTerminalLog()
                                return
                            }
                            logFileSize = currentSize
                            if (currentSize <= logEndOffset)
                                return

                            const added = String(delta.text || "")
                            if (added.length === 0) {
                                logEndOffset = Number(delta.end_offset || logEndOffset)
                                return
                            }
                            logEndOffset = Number(delta.end_offset || logEndOffset)
                            appendingLog = true
                            if (outputText.length + added.length > maximumLoadedLogCharacters) {
                                const tail = agentController.readReportPage(path, -1, maximumLoadedLogCharacters / 2)
                                outputText = String(tail.text || "")
                                logStartOffset = Number(tail.start_offset || 0)
                                logEndOffset = Number(tail.end_offset || logEndOffset)
                                logHasMore = Boolean(tail.has_more)
                            } else {
                                appendingLog = true
                                outputText += added
                            }
                            Qt.callLater(scrollLogToBottom)
                        }

                        function scrollLogToBottom() {
                            if (!edaTerminalOutput)
                                return
                            if (followTail) {
                                appendingLog = true
                                edaTerminalOutput.contentY = Math.max(edaTerminalOutput.originY,
                                    edaTerminalOutput.contentHeight - edaTerminalOutput.height + edaTerminalOutput.originY)
                            }
                            Qt.callLater(function() { appendingLog = false })
                        }

                        function loadOlderLogPage() {
                            if (pagingOlder || !logHasMore || !loadedLogPath
                                    || outputText.length >= maximumLoadedLogCharacters)
                                return
                            const oldY = edaTerminalOutput.contentY
                            const oldHeight = edaTerminalOutput.contentHeight
                            const page = agentController.readReportPage(loadedLogPath, logStartOffset, logPageBytes)
                            const older = String(page.text || "")
                            if (older.length === 0) {
                                logHasMore = false
                                return
                            }
                            pagingOlder = true
                            followTail = false
                            outputText = older + outputText
                            logStartOffset = Number(page.start_offset || 0)
                            logHasMore = Boolean(page.has_more)
                            Qt.callLater(function() {
                                edaTerminalOutput.contentY = oldY + (edaTerminalOutput.contentHeight - oldHeight)
                                Qt.callLater(function() { pagingOlder = false })
                            })
                        }

                        function updateLogScrollIntent() {
                            if (pagingOlder || appendingLog)
                                return
                            const maximumY = Math.max(edaTerminalOutput.originY,
                                edaTerminalOutput.contentHeight - edaTerminalOutput.height + edaTerminalOutput.originY)
                            followTail = edaTerminalOutput.contentY >= maximumY - 20
                            if (edaTerminalOutput.contentY <= edaTerminalOutput.originY + 18 && logHasMore)
                                loadOlderLogPage()
                        }

                        function terminalText() {
                            const output = outputText.length > 0 ? outputText
                                : fallbackOutputText.length > 0 ? fallbackOutputText : errorText
                            if (commandText.length > 0 && output.indexOf("$ " + commandText) < 0)
                                return "$ " + commandText + "\n\n" + output
                            return output
                        }

                        Connections {
                            target: agentController
                            function onActivityEntriesChanged() {
                                if (!chatEdaOutputDialog.visible || chatEdaOutputDialog.activityIndex < 0)
                                    return
                                const entries = agentController.activityEntries
                                if (chatEdaOutputDialog.activityIndex < entries.length)
                                    chatEdaOutputDialog.syncFromEntry(entries[chatEdaOutputDialog.activityIndex])
                            }
                        }

                        Timer {
                            interval: 450
                            repeat: true
                            running: chatEdaOutputDialog.visible
                                && (chatEdaOutputDialog.liveOutputText.length > 0
                                    || ["queued", "running", "waiting_for_eda"].indexOf(chatEdaOutputDialog.state.toLowerCase()) >= 0)
                            onTriggered: chatEdaOutputDialog.refreshTerminalLog()
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: window.localizedUiText("作业"); color: "#68798a"; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                                Text { Layout.fillWidth: true; text: chatEdaOutputDialog.jobId.length > 0 ? chatEdaOutputDialog.jobId : window.localizedUiText("未返回作业 ID"); color: window.chatText; font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideMiddle }
                                Text { text: chatEdaOutputDialog.state; color: chatEdaOutputDialog.state === "completed" ? "#24845a" : chatEdaOutputDialog.state === "failed" ? "#bd4540" : window.accent; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: chatEdaOutputDialog.workspace.length > 0 || chatEdaOutputDialog.logPath.length > 0
                                text: (chatEdaOutputDialog.workspace.length > 0 ? window.localizedUiText("工作目录：") + chatEdaOutputDialog.workspace + "\n" : "")
                                    + (chatEdaOutputDialog.logPath.length > 0 ? window.localizedUiText("日志：") + chatEdaOutputDialog.logPath : "")
                                color: "#718296"
                                font.family: "Monospace"
                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                wrapMode: Text.WrapAnywhere
                            }
                            UI.ScrollableTextArea {
                                id: edaTerminalOutput
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(220, Math.min(520, window.height - 330))
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                                text: chatEdaOutputDialog.terminalText().length > 0
                                    ? chatEdaOutputDialog.terminalText()
                                    : (["completed", "failed"].indexOf(chatEdaOutputDialog.state.toLowerCase()) >= 0
                                        ? "EDA 工具未生成可读终端日志。"
                                        : "等待 EDA 工具输出…")
                                color: "#33495c"
                                font.family: "Monospace"
                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                wrapMode: TextEdit.NoWrap
                                onContentYChanged: chatEdaOutputDialog.updateLogScrollIntent()
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton {
                                    text: window.localizedUiText("复制输出")
                                    iconName: "copy"
                                    enabled: chatEdaOutputDialog.terminalText().length > 0
                                    onClicked: agentController.copyText(chatEdaOutputDialog.terminalText())
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: runReportPage
                Component.onCompleted: Qt.callLater(window.refreshRunReports)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        Column {
                            Text {
                                text: window.localizedText("manualDebug")
                                color: window.graphite
                                font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.localizedText("operatorOnly")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    UI.SectionCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 16

                            RowLayout {
                                Layout.fillWidth: true

                                UI.FlatIcon {
                                    name: "terminal"
                                    color: "#30a46c"
                                    width: 19
                                    height: 19
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: window.localizedText("projectTerminal")
                                    color: window.graphite
                                    font.pixelSize: Math.round(15 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 7
                                color: "#20262e"
                                border.color: "#343d47"
                                clip: true

                                KonsoleTerminal {
                                    id: embeddedTerminal
                                    objectName: "embeddedTerminal"
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    active: window.displayedPage === 5
                                    workingDirectory: window.currentProject.root || ""
                                    fontPointSize: Math.max(1, Math.round(languageSettings.terminalFontSize * languageSettings.uiScale))
                                    visible: ready
                                }

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 48, 680)
                                    spacing: 12
                                    visible: !embeddedTerminal.ready

                                    UI.FlatIcon {
                                        Layout.alignment: Qt.AlignHCenter
                                        name: "terminal"
                                        color: "#9eabb8"
                                        width: 30
                                        height: 30
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: embeddedTerminal.terminalError.length > 0
                                            ? embeddedTerminal.terminalError
                                            : window.localizedText("selectProjectTerminal")
                                        color: "#d9e1ea"
                                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                    }
                                    UI.ToolbarButton {
                                        Layout.alignment: Qt.AlignHCenter
                                        visible: embeddedTerminal.terminalError.length > 0
                                            && Boolean(window.currentProject.root)
                                        text: window.localizedText("openProjectTerminal")
                                        iconName: "terminal"
                                        emphasized: true
                                        onClicked: {
                                            const launched = agentController.openProjectTerminal(window.currentProject.root)
                                            window.terminalLaunchStatus = window.localizedText(
                                                launched ? "terminalOpened" : "terminalUnavailable")
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: window.terminalLaunchStatus.length > 0
                                        text: window.terminalLaunchStatus
                                        color: text === window.localizedText("terminalOpened") ? "#69c493" : "#e3837f"
                                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true

                        UI.FlatIcon {
                            name: "report"
                            color: "#5b83b4"
                            width: 28
                            height: 28
                        }
                        Column {
                            Text {
                                text: window.localizedText("evidence")
                                color: window.graphite
                                font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: window.localizedText("evidenceRule")
                                color: window.muted
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        UI.StatusPill {
                            status: agentController.hasError ? "failed" : (agentController.report.length > 0 ? "verified" : "ready")
                            label: agentController.hasError ? window.localizedText("errorsFound") : (agentController.report.length > 0 ? window.localizedText("reportReady") : window.localizedText("idle"))
                        }
                        UI.ToolbarButton {
                            text: ""
                            iconName: "refresh"
                            implicitWidth: 32
                            tooltipText: languageSettings.language === "en" ? "Refresh reports" : "刷新报告列表"
                            onClicked: window.refreshRunReports()
                        }
                    }

                    SplitView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Horizontal

                        handle: Rectangle {
                            implicitWidth: 12
                            color: "transparent"
                        }

                        UI.SectionCard {
                            SplitView.preferredWidth: 286
                            SplitView.minimumWidth: 230

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    UI.FlatIcon { name: "report"; color: "#397fbd"; width: 17; height: 17 }
                                    Text { Layout.fillWidth: true; text: languageSettings.language === "en" ? "Reports" : "报告列表"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                                    Text { text: String(window.filteredRunReports().length); color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                                }

                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: window.reportProjectOptions()
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        const options = window.reportProjectOptions()
                                        const index = options.findIndex(function(item) { return String(item.value) === window.reportProjectFilter })
                                        return Math.max(0, index)
                                    }
                                    onActivated: function(index) {
                                        window.reportProjectFilter = String(currentValue || "*")
                                        window.reportSessionFilter = "*"
                                        window.selectFirstFilteredReport()
                                    }
                                }

                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: window.reportSessionOptions()
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        const options = window.reportSessionOptions()
                                        const index = options.findIndex(function(item) { return String(item.value) === window.reportSessionFilter })
                                        return Math.max(0, index)
                                    }
                                    onActivated: function(index) {
                                        window.reportSessionFilter = String(currentValue || "*")
                                        window.selectFirstFilteredReport()
                                    }
                                }

                                UI.StyledComboBox {
                                    Layout.fillWidth: true
                                    model: [
                                        { value: "all", label: languageSettings.language === "en" ? "All report types" : "全部报告类型" },
                                        { value: "run_report", label: languageSettings.language === "en" ? "Run reports" : "运行报告" },
                                        { value: "design_summary", label: languageSettings.language === "en" ? "Design summaries" : "设计总结" },
                                        { value: "evidence", label: languageSettings.language === "en" ? "EDA evidence" : "EDA 原始报告" }
                                    ]
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        const options = ["all", "run_report", "design_summary", "evidence"]
                                        return Math.max(0, options.indexOf(window.reportCategoryFilter))
                                    }
                                    onActivated: function(index) {
                                        window.reportCategoryFilter = String(currentValue || "all")
                                        window.selectFirstFilteredReport()
                                    }
                                }
                                UI.SmoothListView {
                                    id: runReportList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 4
                                    model: window.filteredRunReports()
                                    delegate: Rectangle {
                                        required property var modelData
                                        property bool hovered: reportHover.hovered
                                        width: ListView.view.width
                                        height: 82
                                        radius: 7
                                        color: window.darkMode
                                            ? String(modelData.path || "") === window.selectedReportPath
                                                ? (hovered ? "#294a68" : "#203e59")
                                                : hovered ? "#29333d" : "#20262d"
                                            : String(modelData.path || "") === window.selectedReportPath
                                                ? (hovered ? "#e2f1ff" : "#eaf3fc") : hovered ? "#eaf5ff" : "#f8fafc"
                                        border.color: window.darkMode
                                            ? String(modelData.path || "") === window.selectedReportPath ? "#315675"
                                                : hovered ? "#465666" : "#39434e"
                                            : String(modelData.path || "") === window.selectedReportPath ? "#b8d8f5"
                                                : hovered ? "#c5def2" : "#e1e8ef"
                                        border.width: 1
                                        Behavior on color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }
                                        Behavior on border.color { ColorAnimation { duration: 230; easing.type: Easing.OutCubic } }
                                        HoverHandler { id: reportHover; cursorShape: Qt.PointingHandCursor }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: window.selectedReportPath = String(modelData.path || "")
                                        }
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            anchors.topMargin: 7
                                            anchors.bottomMargin: 7
                                            spacing: 3
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.title || "")
                                                color: window.graphite
                                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                                font.weight: Font.Medium
                                                elide: Text.ElideMiddle
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.project_name || modelData.project_id || (languageSettings.language === "en" ? "Unlinked project" : "未关联项目"))
                                                    + " · " + String(modelData.session_title || modelData.session_id || (languageSettings.language === "en" ? "Unlinked session" : "未关联会话"))
                                                color: window.muted
                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                elide: Text.ElideMiddle
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: {
                                                    const category = String(modelData.category || "evidence")
                                                    const label = category === "run_report"
                                                        ? (languageSettings.language === "en" ? "RUN REPORT" : "运行报告")
                                                        : category === "design_summary"
                                                            ? (languageSettings.language === "en" ? "DESIGN SUMMARY" : "设计总结")
                                                            : (languageSettings.language === "en" ? "EDA EVIDENCE" : "EDA 证据")
                                                    return label + " · " + String(modelData.modified || "")
                                                }
                                                color: window.muted
                                                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: window.filteredRunReports().length === 0
                                    text: languageSettings.language === "en" ? "No reports match these filters." : "当前筛选下没有报告。"
                                    color: window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        UI.SectionCard {
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: 420

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                    UI.FlatIcon { name: "file"; color: "#397fbd"; width: 17; height: 17 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(window.selectedReportEntry().title || (languageSettings.language === "en" ? "Report" : "报告内容"))
                                        color: window.graphite
                                        font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: String(window.selectedReportEntry().modified || "")
                                        color: window.muted
                                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: window.selectedReportPath.length > 0
                                    text: {
                                        const entry = window.selectedReportEntry()
                                        const project = String(entry.project_name || entry.project_id || "")
                                        const session = String(entry.session_title || entry.session_id || "")
                                        return [project, session].filter(function(value) { return value.length > 0 }).join(" · ")
                                    }
                                    color: window.muted
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                    elide: Text.ElideMiddle
                                }

                                UI.ReportMarkdownView {
                                    id: reportContentScroll
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    darkMode: window.darkMode
                                    html: agentController.renderMarkdown(window.selectedReportPath.length > 0 ? agentController.readReport(window.selectedReportPath) : window.localizedText("noEvidence"))
                                    fontFamily: window.font.family
                                    textPixelSize: Math.round(13 * languageSettings.uiScale)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                SplitView {
                    anchors.fill: parent
                    anchors.margins: 20
                    orientation: Qt.Horizontal

                    handle: Rectangle {
                        implicitWidth: 20
                        color: "transparent"

                        HoverHandler {
                            id: editorSplitHover
                            cursorShape: Qt.SizeHorCursor
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: parent.height
                            color: editorSplitHover.hovered ? "#8cbef4" : "#dfe4ea"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                    }

                    UI.SectionCard {
                        SplitView.preferredWidth: 320
                        SplitView.minimumWidth: 240
                        color: "#202225"
                        border.color: "#35383d"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                UI.FlatIcon {
                                    name: "editor"
                                    color: "#83b8ee"
                                    width: 18
                                    height: 18
                                }
                                Text {
                                    text: languageSettings.language === "en" ? "EXPLORER" : "资源管理器"
                                    color: "#e7e9ec"
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                UI.ToolbarButton {
                                    text: ""
                                    iconName: "plus"
                                    darkMode: true
                                    implicitWidth: 30
                                    enabled: String(window.currentProject.id || "").length > 0
                                    tooltipText: languageSettings.language === "en"
                                        ? "Add folder to workspace" : "添加文件夹到工作区"
                                    onClicked: editorWorkspaceFolderDialog.open()
                                }
                                UI.ToolbarButton {
                                    text: ""
                                    iconName: "refresh"
                                    darkMode: true
                                    implicitWidth: 30
                                    tooltipText: window.localizedText("refreshFiles")
                                    onClicked: fileEditor.refreshFiles()
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: window.currentProject.name || window.localizedText("noProjectSelected")
                                color: "#9ca3ad"
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                elide: Text.ElideRight
                            }

                            UI.StyledTextField {
                                id: editorFileSearch
                                Layout.fillWidth: true
                                implicitHeight: 32
                                text: window.fileFilter
                                placeholderText: languageSettings.language === "en" ? "Filter files" : "筛选文件"
                                placeholderTextColor: "#89909a"
                                color: "#d4d4d4"
                                onTextChanged: window.fileFilter = text
                                background: Rectangle {
                                    radius: 6
                                    color: "#2b2d30"
                                    border.width: editorFileSearch.activeFocus ? 2 : 1
                                    border.color: editorFileSearch.activeFocus ? "#3d7fb2" : "#3b3e43"
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#34373b"
                            }

                            UI.SmoothListView {
                                id: fileSearchResults
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: window.fileFilter.trim().length > 0
                                property var displayedFiles: {
                                    const query = window.fileFilter.trim().toLowerCase()
                                    const files = []
                                    for (let index = 0; index < fileEditor.editableFiles.length; ++index) {
                                        const path = fileEditor.editableFiles[index]
                                        const name = fileEditor.fileName(path)
                                        const relative = fileEditor.relativePath(path)
                                        if (query.length === 0 || name.toLowerCase().indexOf(query) >= 0 || relative.toLowerCase().indexOf(query) >= 0)
                                            files.push(path)
                                    }
                                    return files
                                }
                                model: displayedFiles
                                clip: true
                                spacing: 1

                                delegate: UI.FileExplorerItem {
                                    required property string modelData
                                    filePath: modelData
                                    displayName: fileEditor.fileName(modelData)
                                    relativeFilePath: fileEditor.relativePath(modelData)
                                    language: fileEditor.languageForFile(modelData)
                                    currentFile: modelData === fileEditor.filePath
                                    onClicked: window.openEditorFile(modelData)
                                }
                            }

                            TreeView {
                                id: fileTree
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: window.fileFilter.trim().length === 0
                                model: fileEditor.treeModel
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                selectionBehavior: TableView.SelectionDisabled
                                columnWidthProvider: function(column) { return width }

                                delegate: UI.FileTreeItem {
                                    currentFile: entryPath === fileEditor.filePath
                                    removeToolTip: languageSettings.language === "en"
                                        ? "Remove from Explorer" : "从资源管理器移除"
                                    nonEditableToolTip: languageSettings.language === "en"
                                        ? "This file is not loaded as text" : "该文件不作为文本载入"
                                    onFileActivated: function(path) { window.openEditorFile(path) }
                                    onRemoveRootRequested: function(path) { window.removeEditorWorkspaceFolder(path) }
                                }

                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }

                                Component.onCompleted: Qt.callLater(function() {
                                    fileTree.expandRecursively(-1, 1)
                                })

                                Connections {
                                    target: fileEditor
                                    function onTreeRootsChanged() {
                                        Qt.callLater(function() { fileTree.expandRecursively(-1, 1) })
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: window.fileFilter.trim().length > 0
                                    ? fileSearchResults.displayedFiles.length === 0
                                    : fileEditor.treeRootCount === 0
                                text: window.fileFilter.trim().length > 0
                                    ? (languageSettings.language === "en" ? "No matching files" : "没有匹配的文件")
                                    : (languageSettings.language === "en"
                                        ? "Configure or add a project folder" : "请配置或添加项目目录")
                                color: "#9ca3ad"
                                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    ColumnLayout {
                        SplitView.fillWidth: true
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 2
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 0
                                text: fileEditor.filePath.length > 0 ? fileEditor.fileName(fileEditor.filePath) : window.localizedText("openFile")
                                color: window.graphite
                                font.pixelSize: Math.round(16 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            UI.StyledCheckBox {
                                visible: !languageSettings.editorUseVim
                                compact: true
                                text: languageSettings.language === "en" ? "Code" : "代码"
                                checked: window.localCompletionEnabled
                                onToggled: {
                                    window.localCompletionEnabled = checked
                                    if (!checked) {
                                        window.localEditorSuggestions = []
                                        window.localEditorSuggestionIndex = 0
                                    }
                                }
                            }
                            UI.StyledCheckBox {
                                visible: !languageSettings.editorUseVim
                                compact: true
                                text: "AI"
                                checked: window.aiCompletionEnabled
                                onToggled: {
                                    window.aiCompletionEnabled = checked
                                    if (!checked) {
                                        window.editorSuggestion = ""
                                        fileEditor.cancelAgentCompletion()
                                    } else {
                                        aiCompletionTimer.restart()
                                    }
                                }
                            }
                            UI.StatusPill {
                                visible: window.width >= 1240
                                status: languageSettings.editorUseVim
                                    ? (embeddedVim.ready ? "ready" : "running")
                                    : (fileEditor.loading || fileEditor.dirty ? "running" : "ready")
                                label: languageSettings.editorUseVim
                                    ? "Vim"
                                    : fileEditor.loading
                                    ? (languageSettings.language === "en" ? "Loading" : "载入中")
                                    : fileEditor.limitedPreview
                                        ? (languageSettings.language === "en" ? "Preview" : "受限预览")
                                        : fileEditor.dirty
                                            ? (languageSettings.language === "en" ? "Modified" : "已修改")
                                            : (languageSettings.language === "en" ? "Saved" : "已保存")
                            }
                            UI.ToolbarButton {
                                text: ""
                                iconName: "settings"
                                implicitWidth: 32
                                tooltipText: languageSettings.language === "en" ? "Editor settings" : "编辑器设置"
                                onClicked: window.openStudioSettingsSubpage(4)
                            }
                            UI.ToolbarButton {
                                visible: !languageSettings.editorUseVim
                                text: window.width < 1180 ? "" : (fileEditor.agentCompletionRunning
                                    ? (languageSettings.language === "en" ? "Cancel" : "取消")
                                    : window.localizedText("agentComplete"))
                                iconName: "agent"
                                enabled: window.aiCompletionEnabled && fileEditor.filePath.length > 0
                                    && !fileEditor.loading && !fileEditor.limitedPreview
                                tooltipText: fileEditor.agentCompletionRunning
                                    ? (languageSettings.language === "en" ? "Cancel AI completion" : "取消 AI 补全")
                                    : window.localizedText("agentComplete")
                                onClicked: {
                                    if (fileEditor.agentCompletionRunning)
                                        fileEditor.cancelAgentCompletion()
                                    else {
                                        window.requestEditorCompletion(true)
                                        editorText.focusEditor()
                                    }
                                }
                            }
                            UI.ToolbarButton {
                                text: window.width < 1180 ? "" : window.localizedText("saveFile")
                                iconName: "save"
                                enabled: fileEditor.filePath.length > 0
                                    && (languageSettings.editorUseVim ? embeddedVim.ready : fileEditor.dirty)
                                tooltipText: window.localizedText("saveFile")
                                onClicked: {
                                    if (languageSettings.editorUseVim)
                                        embeddedVim.sendText("\u001b:write\n")
                                    else
                                        fileEditor.save()
                                }
                            }
                        }

                        UI.SectionCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#1e1e1e"
                            border.color: "#35383d"

                            UI.CodeEditor {
                                id: editorText
                                visible: !languageSettings.editorUseVim
                                anchors.fill: parent
                                anchors.margins: 8
                                text: fileEditor.content
                                readOnly: fileEditor.filePath.length === 0 || fileEditor.loading || fileEditor.limitedPreview
                                selectByMouse: true
                                persistentSelection: true
                                font.family: languageSettings.editorFontFamily
                                font.pixelSize: Math.round(languageSettings.editorFontSize * languageSettings.uiScale)
                                color: "#d4d4d4"
                                language: fileEditor.language
                                lineHeight: Math.round(languageSettings.editorLineHeight * languageSettings.uiScale)
                                tabSize: languageSettings.editorTabSize
                                insertSpaces: languageSettings.editorInsertSpaces
                                wordWrap: languageSettings.editorWordWrap
                                smoothScrolling: languageSettings.editorSmoothScrolling
                                showLineNumbers: languageSettings.editorShowLineNumbers
                                reducedHighlighting: fileEditor.limitedPreview
                                completionText: window.activeEditorSuggestion()
                                completionItems: window.activeEditorSuggestion().length > 0
                                    ? [] : window.localEditorSuggestions
                                completionIndex: window.localEditorSuggestionIndex
                                onTextChanged: {
                                    if (text !== fileEditor.content) {
                                        fileEditor.setContent(text)
                                        window.editorSuggestion = ""
                                        window.localEditorSuggestions = []
                                        window.localEditorSuggestionIndex = 0
                                        localCompletionTimer.restart()
                                        aiCompletionTimer.restart()
                                    }
                                }
                                onCursorMoved: {
                                    window.editorSuggestion = ""
                                    window.localEditorSuggestions = []
                                    window.localEditorSuggestionIndex = 0
                                    fileEditor.cancelAgentCompletion()
                                }
                                onLocalCompletionRequested: window.updateLocalEditorSuggestions(true)
                                onAgentCompletionRequested: window.requestEditorCompletion(true)
                                onCompletionAccepted: {
                                    fileEditor.setContent(text)
                                    window.editorSuggestion = ""
                                    window.localEditorSuggestions = []
                                    window.localEditorSuggestionIndex = 0
                                }
                                onCompletionItemAccepted: function(item) {
                                    fileEditor.setContent(text)
                                    window.editorSuggestion = ""
                                    window.localEditorSuggestions = []
                                    window.localEditorSuggestionIndex = 0
                                }
                                onCompletionIndexRequested: function(index) {
                                    window.localEditorSuggestionIndex = index
                                }
                                onCompletionDismissed: window.clearEditorCompletions(true)
                                onSaveRequested: fileEditor.save()
                            }

                            KonsoleTerminal {
                                id: embeddedVim
                                objectName: "embeddedVim"
                                anchors.fill: parent
                                anchors.margins: 8
                                visible: languageSettings.editorUseVim && fileEditor.filePath.length > 0
                                active: visible && window.displayedPage === 7
                                workingDirectory: window.editorDirectory(fileEditor.filePath)
                                program: "vim"
                                arguments: ["-c", "set mouse=a", "--", fileEditor.filePath]
                                fontFamily: languageSettings.editorFontFamily
                                fontPointSize: Math.max(1, Math.round(languageSettings.editorFontSize * languageSettings.uiScale))
                            }

                            Column {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 420)
                                spacing: 10
                                visible: languageSettings.editorUseVim
                                    && (fileEditor.filePath.length === 0 || !embeddedVim.ready)

                                UI.FlatIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: fileEditor.filePath.length === 0 ? "file" : "terminal"
                                    width: 28
                                    height: 28
                                    color: "#f4f6f8"
                                }
                                Text {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: fileEditor.filePath.length === 0
                                        ? (languageSettings.language === "en"
                                            ? "Select a file in the Explorer" : "请从资源管理器选择文件")
                                        : embeddedVim.terminalError.length > 0
                                            ? embeddedVim.terminalError
                                            : (languageSettings.language === "en" ? "Starting Vim..." : "正在启动 Vim...")
                                    color: embeddedVim.terminalError.length > 0 ? "#ff8b86" : "#d4d4d4"
                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                }
                            }
                        }

                        UI.SectionCard {
                            id: relatedFilesCard
                            Layout.fillWidth: true
                            readonly property bool hasMessage: fileEditor.loading || fileEditor.loadNotice.length > 0
                                || fileEditor.error.length > 0 || fileEditor.agentCompletionError.length > 0
                            implicitHeight: visible ? (window.relatedFilesExpanded ? 58 : 42) : 0
                            Layout.preferredHeight: implicitHeight
                            visible: fileEditor.relatedFiles.length > 0 || hasMessage

                            Behavior on implicitHeight {
                                NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: window.relatedFilesExpanded ? 10 : 6
                                spacing: 8
                                Text {
                                    text: window.localizedText("relatedFiles")
                                    color: window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                }
                                UI.SmoothHorizontalListView {
                                    id: relatedFilesList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: window.relatedFilesExpanded && !relatedFilesCard.hasMessage
                                    model: fileEditor.relatedFiles
                                    spacing: 6
                                    delegate: UI.ToolbarButton {
                                        required property string modelData
                                        height: 32
                                        text: modelData.split("/").pop()
                                        iconName: "file"
                                        onClicked: window.openEditorFile(modelData)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: window.relatedFilesExpanded && relatedFilesCard.hasMessage
                                    text: fileEditor.loading
                                        ? (languageSettings.language === "en" ? "Loading file in the background..." : "正在后台载入文件...")
                                        : fileEditor.error.length > 0
                                            ? window.localizedStoredMessage(fileEditor.error)
                                            : fileEditor.agentCompletionError.length > 0
                                                ? window.localizedStoredMessage(fileEditor.agentCompletionError)
                                                : window.localizedStoredMessage(fileEditor.loadNotice)
                                    color: fileEditor.error.length > 0 || fileEditor.agentCompletionError.length > 0
                                        ? "#ba3a35" : window.muted
                                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    elide: Text.ElideMiddle
                                }
                                Item {
                                    Layout.fillWidth: true
                                    visible: !window.relatedFilesExpanded
                                }
                                UI.ToolbarButton {
                                    text: ""
                                    iconName: "chevron"
                                    implicitWidth: 28
                                    implicitHeight: 28
                                    iconItem.rotation: window.relatedFilesExpanded ? 90 : 0
                                    tooltipText: window.relatedFilesExpanded
                                        ? (languageSettings.language === "en" ? "Hide related files" : "收起关联文件")
                                        : (languageSettings.language === "en" ? "Show related files" : "展开关联文件")
                                    onClicked: window.relatedFilesExpanded = !window.relatedFilesExpanded
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: modelSettingsPage

                UI.SmoothScrollView {
                    anchors.fill: parent
                    anchors.margins: 26
                    contentWidth: availableWidth

                    GridLayout {
                        id: modelSettingsSections
                        width: Math.max(0, modelSettingsPage.width - 52)
                        columns: width >= 1120 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 16

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: modelSettingsSections.columns
                            UI.FlatIcon { name: "agent"; color: "#8a67c7"; width: 28; height: 28 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: window.localizedText("modelsContextSettings"); color: window.graphite; font.pixelSize: Math.round(24 * languageSettings.uiScale); font.weight: Font.DemiBold }
                                Text {
                                    Layout.fillWidth: true
                                    text: window.localizedText("modelsContextSettingsDescription")
                                    color: window.muted; font.pixelSize: Math.round(13 * languageSettings.uiScale); wrapMode: Text.Wrap
                                }
                            }
                            UI.ToolbarButton {
                                text: window.localizedText("studioSettings")
                                iconName: "chevron"
                                iconItem.rotation: 180
                                onClicked: window.openStudioSettings()
                            }
                            UI.ToolbarButton {
                                text: languageSettings.language === "en" ? "Add cloud" : "添加云端"
                                iconName: "plus"
                                onClicked: window.addModel("api")
                            }
                            UI.ToolbarButton {
                                text: languageSettings.language === "en" ? "Add local" : "添加本地"
                                iconName: "plus"
                                onClicked: window.addModel("legacy")
                            }
                        }

                        UI.SectionCard {
                            Layout.fillWidth: true
                            Layout.row: 1
                            Layout.column: 0
                            Layout.columnSpan: modelSettingsSections.columns
                            implicitHeight: modelSelectorLayout.implicitHeight + 32
                            ColumnLayout {
                                id: modelSelectorLayout
                                anchors.fill: parent; anchors.margins: 16; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: languageSettings.language === "en" ? "Default model" : "默认模型"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledComboBox {
                                        id: modelSettingsSelector
                                        Layout.fillWidth: true
                                        textRole: "label"
                                        valueRole: "modelId"
                                        model: modelCatalog
                                        currentIndex: modelCatalog.activeModelIndex
                                        onActivated: {
                                            if (modelCatalog.setActiveModelIndex(index))
                                                Qt.callLater(window.syncModelSettingsEditor)
                                        }
                                    }
                                    UI.StatusPill {
                                        status: window.activeModel().provider === "legacy" ? "ready" : "active"
                                        label: window.activeModel().provider === "legacy"
                                            ? (languageSettings.language === "en" ? "Local" : "本地")
                                            : (languageSettings.language === "en" ? "Cloud/API" : "云端/API")
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (languageSettings.language === "en"
                                        ? "Used by new Chat turns and as the initial model selection."
                                        : "用于新建 Chat 回合，并作为启动时的初始模型选择。")
                                    color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); elide: Text.ElideMiddle
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelCatalog.storagePath
                                    color: window.muted; opacity: 0.72
                                    font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideMiddle
                                }
                            }
                        }

                        UI.SectionCard {
                            Layout.fillWidth: true
                            Layout.row: 2
                            Layout.column: 0
                            implicitHeight: modelEditorLayout.implicitHeight + 32
                            ColumnLayout {
                                id: modelEditorLayout
                                readonly property bool localModelConfiguration: modelSettingsProviderBox.currentValue === "legacy"
                                anchors.fill: parent; anchors.margins: 16; spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: languageSettings.language === "en" ? "Model endpoint and runtime" : "模型接口与运行时"; color: window.graphite; font.pixelSize: Math.round(15 * languageSettings.uiScale); font.weight: Font.DemiBold }
                                    UI.StyledCheckBox { id: modelSettingsEnabledCheck; text: languageSettings.language === "en" ? "Enabled" : "启用"; compact: true }
                                }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 900 ? 4 : 2
                                    columnSpacing: 12; rowSpacing: 10
                                    Text { text: "ID"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsIdField; Layout.fillWidth: true }
                                    Text { text: languageSettings.language === "en" ? "Label" : "名称"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsLabelField; Layout.fillWidth: true }
                                    Text { text: languageSettings.language === "en" ? "Inference" : "推理位置"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledComboBox {
                                        id: modelSettingsProviderBox
                                        Layout.fillWidth: true; textRole: "label"; valueRole: "value"
                                        model: [{label: languageSettings.language === "en" ? "Local worker" : "本地模型（llama.cpp）", value: "legacy"}, {label: languageSettings.language === "en" ? "Cloud / compatible API" : "云端 / 兼容 API", value: "api"}]
                                    }
                                    Text { text: languageSettings.language === "en" ? "API address" : "API 地址"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsApiBaseField; Layout.fillWidth: true; placeholderText: "https://api.openai.com/v1" }
                                    Text { visible: !modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "API key file" : "API 密钥文件"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsApiKeyField; Layout.fillWidth: true; visible: !modelEditorLayout.localModelConfiguration; placeholderText: window.localizedUiText("可留空；也可在下一项直接输入密钥") }
                                    Text { visible: !modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "API key value" : "API 密钥内容"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: modelSettingsApiKeyValueField
                                        Layout.fillWidth: true
                                        visible: !modelEditorLayout.localModelConfiguration
                                        echoMode: TextInput.Password
                                        placeholderText: languageSettings.language === "en" ? "Optional; sent to the worker via environment" : "可直接输入密钥，仅通过环境变量传给推理进程"
                                    }
                                    Text { visible: !modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "Thinking depth" : "思考深度"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledComboBox {
                                        id: modelSettingsReasoningBox
                                        Layout.fillWidth: true
                                        visible: !modelEditorLayout.localModelConfiguration
                                        textRole: "label"
                                        valueRole: "value"
                                        model: [
                                            {label: languageSettings.language === "en" ? "Low" : "低", value: "low"},
                                            {label: languageSettings.language === "en" ? "Medium" : "中", value: "medium"},
                                            {label: languageSettings.language === "en" ? "High" : "高", value: "high"},
                                            {label: languageSettings.language === "en" ? "Extra high" : "极高", value: "xhigh"},
                                            {label: languageSettings.language === "en" ? "Max" : "最大", value: "max"},
                                            {label: languageSettings.language === "en" ? "Ultra" : "极限", value: "ultra"}
                                        ]
                                    }
                                    Text { visible: !modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "Reconnect attempts" : "断联重试次数"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: modelSettingsReconnectAttemptsField
                                        Layout.fillWidth: true
                                        visible: !modelEditorLayout.localModelConfiguration
                                        validator: IntValidator { bottom: 1; top: 20 }
                                        placeholderText: languageSettings.language === "en" ? "1-20" : "1-20 次"
                                    }
                                    Text { visible: !modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "Reconnect interval (s)" : "断联重试间隔（秒）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField {
                                        id: modelSettingsReconnectDelayField
                                        Layout.fillWidth: true
                                        visible: !modelEditorLayout.localModelConfiguration
                                        validator: DoubleValidator { bottom: 0.1; top: 60; decimals: 1 }
                                        placeholderText: languageSettings.language === "en" ? "0.1-60" : "0.1-60 秒"
                                    }
                                    Text { visible: modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "Runtime" : "运行时"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    Text { visible: modelEditorLayout.localModelConfiguration; text: "llama.cpp · OpenAI-compatible local API"; color: window.graphite; font.pixelSize: Math.round(13 * languageSettings.uiScale) }
                                    Text { visible: modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "GGUF base model path" : "GGUF 基础模型路径"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsBasePathField; Layout.fillWidth: true; visible: modelEditorLayout.localModelConfiguration; placeholderText: window.localizedUiText("本地模型或 GGUF 路径") }
                                    Text { visible: modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "GGUF LoRA adapter path" : "GGUF LoRA 适配器路径"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsAdapterPathField; Layout.fillWidth: true; visible: modelEditorLayout.localModelConfiguration; placeholderText: languageSettings.language === "en" ? "Optional GGUF adapter" : "可选 GGUF 适配器" }
                                    Text { visible: modelEditorLayout.localModelConfiguration; text: languageSettings.language === "en" ? "llama-server executable" : "llama-server 程序路径"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsLlamaServerPathField; Layout.fillWidth: true; visible: modelEditorLayout.localModelConfiguration; placeholderText: languageSettings.language === "en" ? "Auto-detect bundled or system llama-server" : "留空时自动查找内置或系统 llama-server" }
                                }
                            }
                        }

                        UI.SectionCard {
                            Layout.fillWidth: true
                            Layout.row: modelSettingsSections.columns > 1 ? 2 : 3
                            Layout.column: modelSettingsSections.columns > 1 ? 1 : 0
                            implicitHeight: modelBudgetLayout.implicitHeight + 32
                            ColumnLayout {
                                id: modelBudgetLayout
                                anchors.fill: parent; anchors.margins: 16; spacing: 12
                                Text { text: languageSettings.language === "en" ? "Context and inference budget" : "上下文与推理预算"; color: window.graphite; font.pixelSize: Math.round(15 * languageSettings.uiScale); font.weight: Font.DemiBold }
                                GridLayout {
                                    Layout.fillWidth: true; columns: width >= 900 ? 8 : 2; columnSpacing: 12; rowSpacing: 10
                                Text { text: languageSettings.language === "en" ? "Context" : "上下文"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsContextField; Layout.fillWidth: true; validator: IntValidator { bottom: 2048; top: 262144 } }
                                Text { text: languageSettings.language === "en" ? "Effective window (%)" : "有效窗口（%）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsEffectivePercentField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 100 } }
                                Text { text: languageSettings.language === "en" ? "Auto-compact at" : "自动压缩阈值"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsAutoCompactField; Layout.fillWidth: true; validator: IntValidator { bottom: 1024; top: 262144 } }
                                Text { text: languageSettings.language === "en" ? "History" : "历史记录"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsHistoryField; Layout.fillWidth: true; validator: IntValidator { bottom: 1024; top: 131071 } }
                                Text { text: languageSettings.language === "en" ? "Output" : "回答上限"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsOutputField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 262144 } }
                                Text { visible: modelEditorLayout.localModelConfiguration; text: window.localizedText("inferenceMode"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledComboBox { id: modelSettingsModeBox; visible: modelEditorLayout.localModelConfiguration; Layout.fillWidth: true; model: [{label: window.localizedText("gpuOnly"), value: "gpu"}, {label: window.localizedText("cpuGpu"), value: "cpu_gpu"}, {label: window.localizedText("cpuOnly"), value: "cpu"}]; textRole: "label"; valueRole: "value" }
                                Text { visible: modelEditorLayout.localModelConfiguration; text: window.localizedText("gpuMemory"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsGpuField; visible: modelEditorLayout.localModelConfiguration; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 128; decimals: 2 } }
                                Text { visible: modelEditorLayout.localModelConfiguration; text: window.localizedText("cpuMemory"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                UI.StyledTextField { id: modelSettingsCpuField; visible: modelEditorLayout.localModelConfiguration; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1024; decimals: 2 } }
                                }
                                GridLayout {
                                    id: modelBudgetActionButtons
                                    Layout.fillWidth: true
                                    columns: width >= 480 ? 2 : 1
                                    columnSpacing: 8
                                    rowSpacing: 7
                                    UI.ToolbarButton {
                                        objectName: "modelMaximumResponseButton"
                                        Layout.fillWidth: false
                                        Layout.columnSpan: modelEditorLayout.localModelConfiguration ? modelBudgetActionButtons.columns : 1
                                        emphasized: true
                                        surfaceRadius: 6
                                        text: window.localizedText("recommendedAllocation")
                                        iconName: ""
                                        onClicked: window.recommendedContextAllocation()
                                    }
                                    UI.ToolbarButton {
                                        objectName: "modelContextRefreshButton"
                                        Layout.fillWidth: false
                                        emphasized: true
                                        surfaceRadius: 6
                                        text: modelContextRefreshPending
                                            ? (languageSettings.language === "en" ? "Fetching..." : "正在获取…")
                                            : (languageSettings.language === "en" ? "Update context from API" : "从 API 更新上下文")
                                        iconName: ""
                                        visible: !modelEditorLayout.localModelConfiguration
                                        enabled: !modelEditorLayout.localModelConfiguration
                                            && !modelContextRefreshPending && !agentController.codexModelsLoading
                                        onClicked: window.refreshModelContextFromApi()
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: window.localizedStoredMessage(modelSettingsMessage); color: window.modelCatalogError.length > 0 || modelSettingsMessage.indexOf("失败") >= 0 || modelSettingsMessage.toLowerCase().indexOf("failed") >= 0 ? "#b43a34" : "#24845a"; font.pixelSize: Math.round(12 * languageSettings.uiScale); wrapMode: Text.Wrap }
                                    UI.ToolbarButton { text: window.localizedText("save"); iconName: "save"; emphasized: true; onClicked: window.saveModelSettings() }
                                    UI.ToolbarButton { text: languageSettings.language === "en" ? "Remove" : "删除"; iconName: "trash"; enabled: modelCatalog.count > 1; onClicked: { if (modelCatalog.removeModel(modelCatalog.activeModelIndex)) Qt.callLater(window.syncModelSettingsEditor) } }
                                }
                            }
                        }

                        UI.SectionCard {
                            Layout.fillWidth: true
                            Layout.row: modelSettingsSections.columns > 1 ? 3 : 4
                            Layout.column: modelSettingsSections.columns > 1 ? 1 : 0
                            implicitHeight: modelSamplingLayout.implicitHeight + 32
                            ColumnLayout {
                                id: modelSamplingLayout
                                anchors.fill: parent; anchors.margins: 16; spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: languageSettings.language === "en" ? "Sampling and repetition control" : "采样与防复读"
                                        color: window.graphite; font.pixelSize: Math.round(15 * languageSettings.uiScale); font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: languageSettings.language === "en" ? "Applied per model" : "按模型保存"
                                        color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                    }
                                }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 900 ? 4 : 2
                                    columnSpacing: 12; rowSpacing: 10
                                    Text { text: "Temperature"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsTemperatureField; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 2; decimals: 3 } }
                                    Text { text: "Top-K"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsTopKField; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 100000 } }
                                    Text { text: "Top-P"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsTopPField; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; decimals: 3 } }
                                    Text { text: "Min-P"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsMinPField; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; decimals: 3 } }
                                    Text { text: languageSettings.language === "en" ? "Repeat penalty" : "重复惩罚"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsRepeatPenaltyField; Layout.fillWidth: true; validator: DoubleValidator { bottom: 1; top: 2; decimals: 3 } }
                                    Text { text: languageSettings.language === "en" ? "Repeat window" : "重复窗口"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsRepeatLastNField; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 4096 } }
                                    Text { text: "DRY multiplier"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsDryMultiplierField; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 5; decimals: 3 } }
                                    Text { text: "Presence penalty"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsPresencePenaltyField; Layout.fillWidth: true; validator: DoubleValidator { bottom: -2; top: 2; decimals: 3 } }
                                    Text { text: "Frequency penalty"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                                    UI.StyledTextField { id: modelSettingsFrequencyPenaltyField; Layout.fillWidth: true; validator: DoubleValidator { bottom: -2; top: 2; decimals: 3 } }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: languageSettings.language === "en"
                                        ? "Recommended for long-thinking models: temperature 0.6, top_p 0.9, min_p 0.05, repeat penalty 1.1, window 256, DRY 0.5."
                                        : "长思考模型建议：temperature 0.6、top_p 0.9、min_p 0.05、重复惩罚 1.1、窗口 256、DRY 0.5。"
                                    color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: studioSettingsHomePage

                UI.SmoothScrollView {
                    anchors.fill: parent
                    anchors.margins: 28
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: parent.availableWidth
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: window.localizedText("studioSettings")
                                    color: window.graphite
                                    font.pixelSize: Math.round(24 * languageSettings.uiScale)
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: window.localizedText("settingsHomeDescription")
                                    color: window.muted
                                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        Repeater {
                            model: [
                                { page: 4, title: "appearanceSettings", description: "appearanceSettingsDescription", icon: "editor", color: "#3478b8" },
                                { page: 10, title: "skillsSettings", description: "skillsSettingsDescription", icon: "skills", color: "#21866a" },
                                { page: 11, title: "agentToolsSettings", description: "agentToolsSettingsDescription", icon: "terminal", color: "#8a67c7" },
                                { page: 8, title: "modelsContextSettings", description: "modelsContextSettingsDescription", icon: "agent", color: "#b0742d" }
                            ]

                            delegate: ItemDelegate {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 76
                                hoverEnabled: true
                                leftPadding: 12
                                rightPadding: 12

                                background: Rectangle {
                                    radius: 7
                                    color: window.darkMode
                                        ? (parent.hovered ? "#29333d" : window.chatSurface)
                                        : (parent.hovered ? "#eaf3fb" : window.chatSurface)
                                    border.width: parent.hovered ? 1 : 0
                                    border.color: window.darkMode ? "#465666" : "#c8dff2"
                                    Behavior on color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                    Behavior on border.color { ColorAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                }

                                contentItem: RowLayout {
                                    spacing: 12
                                    UI.FlatIcon {
                                        name: modelData.icon
                                        color: modelData.color
                                        width: 21
                                        height: 21
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: window.localizedText(modelData.title)
                                            color: window.graphite
                                            font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: window.localizedText(modelData.description)
                                            color: window.muted
                                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                    UI.FlatIcon { name: "chevron"; color: "#8693a1"; width: 16; height: 16 }
                                }

                                onClicked: window.openStudioSettingsSubpage(modelData.page)
                            }
                        }
                    }
                }
            }

            Item {
                id: aboutPage
            }
        }
    }

    Popup {
        id: aboutDialog
        parent: Overlay.overlay
        width: Math.min(488, parent ? parent.width - 32 : window.width - 32)
        height: Math.min(390, parent ? parent.height - 32 : window.height - 32)
        x: parent ? Math.round((parent.width - width) / 2) : Math.round((window.width - width) / 2)
        y: parent ? Math.round((parent.height - height) / 2) : Math.round((window.height - height) / 2)
        padding: 0
        modal: true
        focus: true
        dim: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Overlay.modal: Rectangle {
            color: "#730f1720"
            opacity: aboutDialog.visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 180; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110; easing.type: Easing.InCubic }
        }

        background: Rectangle {
            radius: 14
            color: window.chatSurface
            border.color: window.chatBorder
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                UI.ToolbarButton {
                    text: ""
                    iconName: "chevron"
                    iconItem.rotation: 180
                    tooltipText: languageSettings.language === "en" ? "Back" : "返回"
                    onClicked: aboutDialog.close()
                }
                Text {
                    Layout.fillWidth: true
                    text: languageSettings.language === "en" ? "About" : "关于"
                    color: window.chatText
                    font.pixelSize: Math.round(17 * languageSettings.uiScale)
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }
                Item { width: 32; height: 32 }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: window.chatBorder
            }

            UI.SmoothScrollView {
                id: aboutDialogScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 10
                Layout.bottomMargin: 14
                contentWidth: availableWidth

                ColumnLayout {
                    width: aboutDialogScroll.availableWidth
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Image {
                            Layout.alignment: Qt.AlignHCenter
                            source: "qrc:/icons/dft-agent-studio.png"
                            sourceSize.width: 128
                            sourceSize.height: 128
                            Layout.preferredWidth: 96
                            Layout.preferredHeight: 96
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "DFT Agent Studio"
                            color: window.chatText
                            font.pixelSize: Math.round(25 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: (languageSettings.language === "en" ? "Version " : "版本 ")
                                  + (Qt.application.version.length > 0 ? Qt.application.version : "0.1.0")
                            color: window.chatMuted
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                        }
                        Button {
                            id: aboutAuthorButton
                            Layout.alignment: Qt.AlignHCenter
                            text: window.localizedText("applicationAuthorName")
                            leftPadding: 24
                            rightPadding: 24
                            topPadding: 5
                            bottomPadding: 5
                            hoverEnabled: false
                            onClicked: Qt.openUrlExternally("https://github.com/Little-W")
                            HoverHandler { id: aboutAuthorPointer }
                            background: Item {
                                clip: true
                                Rectangle {
                                    id: aboutAuthorSurface
                                    readonly property bool expanded: aboutAuthorPointer.hovered || aboutAuthorButton.down
                                    x: expanded ? 0 : 6
                                    y: 0
                                    width: expanded ? aboutAuthorButton.width : Math.max(0, aboutAuthorButton.width - 12)
                                    height: aboutAuthorButton.height
                                    radius: height / 2
                                    color: expanded
                                        ? (window.darkMode ? "#343b43" : "#dce6f0")
                                        : "transparent"
                                    border.width: 1
                                    border.color: expanded
                                        ? (window.darkMode ? "#59636e" : "#b3c4d2")
                                        : "transparent"
                                    Behavior on color { ColorAnimation { duration: 135 } }
                                    Behavior on border.color { ColorAnimation { duration: 145 } }
                                    Behavior on x { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
                                    Behavior on width { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
                                }
                            }
                            contentItem: Text {
                                text: aboutAuthorButton.text
                                color: window.darkMode ? "#e8eaf0" : "#60616d"
                                font.pixelSize: Math.round(14 * languageSettings.uiScale)
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: languageSettings.language === "en"
                                ? "Native DFT engineering workspace"
                                : "面向 DFT 工程的原生智能工作台"
                            color: window.chatText
                            font.pixelSize: Math.round(16 * languageSettings.uiScale)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.localizedText("aboutDescription")
                            color: window.chatMuted
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.localizedText("licenseNotDeclaredDetail")
                            color: window.chatMuted
                            font.pixelSize: Math.round(12 * languageSettings.uiScale)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                    }

                }
            }
        }
    }

    Component.onCompleted: {
        width = Math.max(minimumWidth, languageSettings.windowWidth)
        height = Math.max(minimumHeight, languageSettings.windowHeight)
        rememberedWindowWidth = Math.round(width)
        rememberedWindowHeight = Math.round(height)
        windowSizeRestored = true
        restoreSelectedProject()
        refreshEditorRoots()
        rebuildCodexChatDisplayEntries()
        // Populate the sidebar catalog before the user first opens Chat. This
        // matters when a supervisor created or resumed a thread externally.
        Qt.callLater(function() {
            if (window.selectedPage === 3)
                window.refreshCodexWorkspace()
            else
                window.refreshFlowSessionList()
        })
        Qt.callLater(window.syncModelSettingsEditor)
        Qt.callLater(window.syncCodexChatPreferences)
        Qt.callLater(window.refreshCodexModels)
    }

    Connections {
        target: projectConfigImporter

        function onCompleted(successful) {
            if (!successful)
                return
            if (projectConfigImporter.stage === "synthesis")
                window.applyImportedSynthesisConfiguration(projectConfigImporter.result)
            else if (projectConfigImporter.stage === "dft")
                window.applyImportedDftConfiguration(projectConfigImporter.result)
        }
    }

    UI.SettingsDialog {
        id: projectGeneralDialog
        objectName: "qaDialog_projectGeneralDialog"
        title: languageSettings.language === "en" ? "Project and flow" : "项目与流程"
        description: languageSettings.language === "en" ? "Configure the project source, workspace and enabled stages" : "配置项目输入、工作区与启用阶段"
        iconName: "projects"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"

        onOpened: {
            projectRelatedDocumentsModel.clear()
            for (const path of (window.currentProject.relatedDocuments || []))
                projectRelatedDocumentsModel.append({path: String(path || "")})
            window.projectRelatedDocumentsValue = window.currentProject.relatedDocuments || []
            projectGeneralTypeBox.currentIndex = window.projectTypeIndex(window.currentProject.kind || "rtl")
            projectGeneralNotesEditor.text = window.currentProject.notes || ""
            projectGeneralWorkspaceField.text = String(window.executionValue("workspace_path", "")) || window.defaultWorkspacePath
            projectGeneralWorkspaceSuffixCheck.checked = Boolean(window.executionValue("workspace_suffix_enabled", true))
            projectGeneralRootField.text = window.currentProject.root || ""
            projectGeneralRtlField.text = window.currentProject.rtlRoot || ""
            projectGeneralTopField.text = window.currentProject.top || ""
            projectGeneralSourceFilesEditor.text = window.sourceFilesText()
            projectGeneralFileListField.text = String(window.executionValue("filelist", ""))
            projectGeneralLanguageBox.currentIndex = String(window.executionValue("language", "sverilog")) === "verilog" ? 1 : 0
            projectGeneralSynthesisCheck.checked = window.flowModuleValue("synthesis", true)
            projectGeneralDftCheck.checked = window.flowModuleValue("dft", true)
            projectGeneralScanCheck.checked = window.flowModuleValue("scan", true)
            projectGeneralMbistCheck.checked = window.flowModuleValue("mbist", false)
            projectGeneralAtpgCheck.checked = window.flowModuleValue("atpg", false)
            projectGeneralLbistCheck.checked = window.flowModuleValue("lbist", false)
        }
        onAccepted: {
            window.projectRelatedDocumentsValue = window.projectRelatedDocumentPaths()
            projectTypeBox.currentIndex = projectGeneralTypeBox.currentIndex
            projectNotesEditor.text = projectGeneralNotesEditor.text
            workspacePathField.text = projectGeneralWorkspaceField.text
            workspaceSuffixCheck.checked = projectGeneralWorkspaceSuffixCheck.checked
            rootField.text = projectGeneralRootField.text
            rtlField.text = projectGeneralRtlField.text
            topField.text = projectGeneralTopField.text
            sourceFilesEditor.text = projectGeneralSourceFilesEditor.text
            fileListField.text = projectGeneralFileListField.text
            sourceLanguageBox.currentIndex = projectGeneralLanguageBox.currentIndex
            synthesisModuleCheck.checked = projectGeneralSynthesisCheck.checked
            dftModuleCheck.checked = projectGeneralDftCheck.checked
            scanModuleCheck.checked = projectGeneralDftCheck.checked && projectGeneralScanCheck.checked
            mbistModuleCheck.checked = projectGeneralDftCheck.checked && projectGeneralMbistCheck.checked
            atpgModuleCheck.checked = projectGeneralDftCheck.checked && projectGeneralAtpgCheck.checked
            lbistModuleCheck.checked = projectGeneralDftCheck.checked && projectGeneralLbistCheck.checked
            window.persistEditor()
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: projectIdentityLayout.implicitHeight + 32

            GridLayout {
                id: projectIdentityLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10

                Text { text: window.localizedText("projectType"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledComboBox {
                    id: projectGeneralTypeBox
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "value"
                    model: window.projectTypeOptions()
                }
                Text { text: languageSettings.language === "en" ? "Description" : "项目说明"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextArea {
                    id: projectGeneralNotesEditor
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: relatedDocumentsLayout.implicitHeight + 28
            ColumnLayout {
                id: relatedDocumentsLayout
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: languageSettings.language === "en" ? "Project-related documents" : "项目相关文档"
                            color: window.graphite
                            font.pixelSize: Math.round(13 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: languageSettings.language === "en"
                                ? "The Agent reads these files at the start of each new session."
                                : "每次启动新会话时，Agent 会先阅读这些文件了解项目背景与设计。"
                            color: window.muted
                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                            wrapMode: Text.Wrap
                        }
                    }
                    UI.ToolbarButton {
                        text: languageSettings.language === "en" ? "Add document" : "添加文档"
                        iconName: "file"
                        onClicked: {
                            projectRelatedDocumentsModel.append({path: ""})
                            relatedDocumentDialogRowId = String(projectRelatedDocumentsModel.count - 1)
                            relatedDocumentDialog.open()
                        }
                    }
                }
                Repeater {
                    model: projectRelatedDocumentsModel
                    delegate: RowLayout {
                        required property int index
                        required property string path
                        Layout.fillWidth: true
                        spacing: 6
                        UI.StyledTextField {
                            Layout.fillWidth: true
                            text: path
                            placeholderText: languageSettings.language === "en" ? "Document path" : "文档路径"
                            selectByMouse: true
                            onTextEdited: projectRelatedDocumentsModel.setProperty(index, "path", text)
                        }
                        UI.ToolbarButton {
                            text: languageSettings.language === "en" ? "Browse" : "浏览"
                            iconName: "folder"
                            onClicked: {
                                relatedDocumentDialogRowId = String(index)
                                relatedDocumentDialog.open()
                            }
                        }
                        UI.ToolbarButton {
                            text: languageSettings.language === "en" ? "Remove" : "移除"
                            iconName: "trash"
                            onClicked: projectRelatedDocumentsModel.remove(index)
                        }
                    }
                }
                Text {
                    visible: projectRelatedDocumentsModel.count === 0
                    text: languageSettings.language === "en" ? "No project documents added." : "尚未添加项目文档。"
                    color: window.muted
                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: projectWorkspaceLayout.implicitHeight + 32

            GridLayout {
                id: projectWorkspaceLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10

                Text { text: window.localizedText("workspacePath"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField {
                    id: projectGeneralWorkspaceField
                    Layout.fillWidth: true
                    folderMode: true
                    onBrowseRequested: window.chooseFolderForField(projectGeneralWorkspaceField)
                }
                Text { text: window.localizedText("workspaceSuffix"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox {
                    id: projectGeneralWorkspaceSuffixCheck
                    compact: true
                    text: window.localizedText("createWorkspacePerRun")
                }
                Text { text: window.localizedText("projectFolder"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField {
                    id: projectGeneralRootField
                    Layout.fillWidth: true
                    folderMode: true
                    onBrowseRequested: window.chooseFolderForField(projectGeneralRootField)
                }
                Text { text: window.localizedText("rtlFolder"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField {
                    id: projectGeneralRtlField
                    Layout.fillWidth: true
                    folderMode: true
                    onBrowseRequested: window.chooseFolderForField(projectGeneralRtlField)
                }
                Text { text: window.localizedText("topModule"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: projectGeneralTopField; Layout.fillWidth: true; selectByMouse: true }
                Text { text: window.localizedText("sourceLanguage"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledComboBox {
                    id: projectGeneralLanguageBox
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "value"
                    model: [{label: "SystemVerilog", value: "sverilog"}, {label: "Verilog", value: "verilog"}]
                }
                Text { text: window.localizedText("rtlFilelist"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField {
                    id: projectGeneralFileListField
                    Layout.fillWidth: true
                    editorAvailable: true
                    onBrowseRequested: window.chooseFileForField(projectGeneralFileListField, projectGeneralRootField.text, ["File lists (*.f)", "All files (*)"])
                    onEditRequested: window.openConfiguredFile(projectGeneralFileListField.text, projectGeneralRootField.text)
                }
                RowLayout {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Text { text: window.localizedText("rtlSources"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                    Item { Layout.fillWidth: true }
                    UI.ToolbarButton {
                        text: languageSettings.language === "en" ? "Expand editor" : "放大编辑"
                        iconName: "expand"
                        onClicked: sourceEditorDialog.open()
                    }
                    UI.ToolbarButton {
                        text: languageSettings.language === "en" ? "Add files" : "添加文件"
                        iconName: "file"
                        onClicked: sourceFilesDialog.open()
                    }
                }
                UI.ScrollableTextArea {
                    id: projectGeneralSourceFilesEditor
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    wrapMode: TextEdit.NoWrap
                    font.family: "Monospace"
                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                    selectByMouse: true
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: 126

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text {
                    text: languageSettings.language === "en" ? "Flow stages" : "流程阶段"
                    color: window.graphite
                    font.pixelSize: Math.round(14 * languageSettings.uiScale)
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 6
                    columnSpacing: 10
                    rowSpacing: 8
                    UI.StyledCheckBox { id: projectGeneralSynthesisCheck; text: languageSettings.language === "en" ? "Synthesis" : "综合"; compact: true }
                    UI.StyledCheckBox {
                        id: projectGeneralDftCheck
                        text: "DFT"
                        compact: true
                        onToggled: {
                            if (!checked) {
                                projectGeneralScanCheck.checked = false
                                projectGeneralMbistCheck.checked = false
                                projectGeneralAtpgCheck.checked = false
                                projectGeneralLbistCheck.checked = false
                            }
                        }
                    }
                    UI.StyledCheckBox { id: projectGeneralScanCheck; text: languageSettings.language === "en" ? "Scan" : "扫描链"; compact: true; enabled: projectGeneralDftCheck.checked }
                    UI.StyledCheckBox { id: projectGeneralMbistCheck; text: "MBIST"; compact: true; enabled: projectGeneralDftCheck.checked }
                    UI.StyledCheckBox { id: projectGeneralAtpgCheck; text: "ATPG"; compact: true; enabled: projectGeneralDftCheck.checked }
                    UI.StyledCheckBox { id: projectGeneralLbistCheck; text: "LBIST"; compact: true; enabled: projectGeneralDftCheck.checked }
                }
            }
        }
    }

    UI.SettingsDialog {
        id: synthesisSettingsDialog
        objectName: "qaDialog_synthesisSettingsDialog"
        title: languageSettings.language === "en" ? "Synthesis settings" : "综合设置"
        description: languageSettings.language === "en" ? "Design Compiler inputs, constraints, compile options and outputs" : "Design Compiler 输入、约束、编译选项与输出"
        iconName: "synthesis"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"

        onOpened: {
            window.syncSynthesisModels()
            synthesisEnabledCheck.checked = synthesisModuleCheck.checked
            synthesisLibraryDirField.text = libraryDirField.text
            synthesisLibraryFileField.text = libraryFileField.text
            synthesisLibraryProfileField.text = libraryProfileField.text
            synthesisConstraintField.text = constraintFileField.text
            synthesisLoadConstraintCheck.checked = loadConstraintCheck.checked
            synthesisConstraintFilesEditor.text = (window.synthesisValue("constraint_files", []) || []).join("\n")
            synthesisPreScriptsEditor.text = (window.synthesisValue("pre_scripts", []) || []).join("\n")
            synthesisPostScriptsEditor.text = (window.synthesisValue("post_scripts", []) || []).join("\n")
            synthesisAdditionalTclEditor.text = (window.synthesisValue("additional_tcl_commands", []) || []).join("\n")
            clockGroupsEditor.text = String(window.synthesisValue("clock_groups_tcl", ""))
            operatingConditionField.text = String(window.synthesisValue("operating_condition", ""))
            minimumLibraryField.text = String(window.synthesisValue("min_library", ""))
            maxCoresField.text = String(window.synthesisValue("max_cores", 4))
            compileCommandBox.currentIndex = String(window.synthesisValue("compile_command", "compile")) === "compile_ultra" ? 1 : 0
            incrementalCompileCheck.checked = Boolean(window.synthesisValue("incremental", false))
            retimeCompileCheck.checked = Boolean(window.synthesisValue("retime", false))
            gateClockCompileCheck.checked = Boolean(window.synthesisValue("gate_clock", false))
            scanReadyCompileCheck.checked = Boolean(window.synthesisValue("scan_ready", true))
            boundaryOptimizationCheck.checked = Boolean(window.synthesisValue("boundary_optimization", true))
            const ungroup = String(window.synthesisValue("auto_ungroup", "none"))
            autoUngroupBox.currentIndex = ["none", "area", "delay", "all"].indexOf(ungroup)
            maxTransitionField.text = String(window.synthesisValue("max_transition", ""))
            maxFanoutField.text = String(window.synthesisValue("max_fanout", ""))
            maxCapacitanceField.text = String(window.synthesisValue("max_capacitance", ""))
            drivingCellField.text = String(window.synthesisValue("driving_cell", ""))
            outputLoadField.text = String(window.synthesisValue("output_load", ""))
            const reports = window.synthesisValue("reports", ["qor", "timing", "area", "power", "constraints"])
            reportQorCheck.checked = reports.indexOf("qor") >= 0
            reportTimingCheck.checked = reports.indexOf("timing") >= 0
            reportAreaCheck.checked = reports.indexOf("area") >= 0
            reportPowerCheck.checked = reports.indexOf("power") >= 0
            reportConstraintsCheck.checked = reports.indexOf("constraints") >= 0
            reportResourcesCheck.checked = reports.indexOf("resources") >= 0
            synthesisOutputDialogField.text = synthesisOutputField.text
        }
        onAccepted: {
            synthesisModuleCheck.checked = synthesisEnabledCheck.checked
            libraryDirField.text = synthesisLibraryDirField.text
            libraryFileField.text = synthesisLibraryFileField.text
            libraryProfileField.text = synthesisLibraryProfileField.text
            constraintFileField.text = synthesisConstraintField.text
            loadConstraintCheck.checked = synthesisLoadConstraintCheck.checked
            mapEffortBox.currentIndex = synthesisMapEffortBox.currentIndex
            areaEffortBox.currentIndex = synthesisAreaEffortBox.currentIndex
            powerEffortBox.currentIndex = synthesisPowerEffortBox.currentIndex
            synthesisOutputField.text = synthesisOutputDialogField.text
            window.persistEditor()
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: synthesisImportLayout.implicitHeight + 28

            RowLayout {
                id: synthesisImportLayout
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                UI.FlatIcon { name: "agent"; color: "#397fbd"; width: 20; height: 20 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: languageSettings.language === "en" ? "AI project settings import" : "AI 导入项目设置"
                        color: window.graphite
                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: languageSettings.language === "en"
                            ? "Starts a background session to inspect project scripts and documents, then update settings."
                            : "新建后台会话检查项目脚本和文档，并自动完善设置。完成后会自动归档。"
                        color: window.muted
                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                        elide: Text.ElideRight
                    }
                }
                UI.ToolbarButton {
                    text: languageSettings.language === "en" ? "AI import" : "AI 一键导入"
                    iconName: "agent"
                    emphasized: true
                    enabled: !window.projectImportRunning
                    onClicked: window.startProjectSettingsImport()
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: synthesisLibraryLayout.implicitHeight + 32
            GridLayout {
                id: synthesisLibraryLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { text: languageSettings.language === "en" ? "Enable synthesis" : "启用综合"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox { id: synthesisEnabledCheck; compact: true; text: languageSettings.language === "en" ? "Run synthesis stage" : "执行综合阶段" }
                Text { text: languageSettings.language === "en" ? "Technology library directory" : "工艺库目录"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField { id: synthesisLibraryDirField; Layout.fillWidth: true; folderMode: true; onBrowseRequested: window.chooseFolderForField(synthesisLibraryDirField) }
                Text { text: languageSettings.language === "en" ? "Target library (.db)" : "目标标准单元库 (.db)"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField { id: synthesisLibraryFileField; Layout.fillWidth: true; onBrowseRequested: window.chooseFileForField(synthesisLibraryFileField, synthesisLibraryDirField.text, ["Library database (*.db)", "All files (*)"]) }
                Text { text: languageSettings.language === "en" ? "Library profile" : "工艺库 profile"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: synthesisLibraryProfileField; Layout.fillWidth: true; selectByMouse: true }
                Text { text: languageSettings.language === "en" ? "Operating condition (Agent checked)" : "工作条件（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: operatingConditionField; Layout.fillWidth: true; placeholderText: "WCCOM"; selectByMouse: true }
                Text { text: languageSettings.language === "en" ? "Minimum-delay library (Agent checked)" : "最小延迟库（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField { id: minimumLibraryField; Layout.fillWidth: true; onBrowseRequested: window.chooseFileForField(minimumLibraryField, synthesisLibraryDirField.text, ["Library database (*.db)", "All files (*)"]) }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: synthesisConstraintLayout.implicitHeight + 32
            GridLayout {
                id: synthesisConstraintLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { text: window.localizedText("constraintFile"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField {
                    id: synthesisConstraintField
                    Layout.fillWidth: true
                    editorAvailable: true
                    onBrowseRequested: window.chooseFileForField(synthesisConstraintField, projectGeneralRootField.text || window.currentProject.root, ["Timing constraints (*.sdc)", "Tcl scripts (*.tcl)", "All files (*)"])
                    onEditRequested: window.openConfiguredFile(synthesisConstraintField.text, projectGeneralRootField.text || window.currentProject.root)
                }
                Text { text: window.localizedText("loadConstraintFile"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox { id: synthesisLoadConstraintCheck; compact: true; text: languageSettings.language === "en" ? "Load configured SDC files" : "载入已配置的 SDC 文件" }
                Text { text: languageSettings.language === "en" ? "Additional SDC files (Agent checked)" : "附加 SDC 文件（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea { id: synthesisConstraintFilesEditor; Layout.fillWidth: true; Layout.preferredHeight: 92; wrapMode: TextEdit.NoWrap; font.family: "Monospace"; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                Text { text: languageSettings.language === "en" ? "Pre-compile Tcl (Agent checked)" : "编译前 Tcl（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea { id: synthesisPreScriptsEditor; Layout.fillWidth: true; Layout.preferredHeight: 76; wrapMode: TextEdit.NoWrap; font.family: "Monospace"; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                Text { text: languageSettings.language === "en" ? "Post-compile Tcl (Agent checked)" : "编译后 Tcl（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea { id: synthesisPostScriptsEditor; Layout.fillWidth: true; Layout.preferredHeight: 76; wrapMode: TextEdit.NoWrap; font.family: "Monospace"; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                Text { text: languageSettings.language === "en" ? "Additional synthesis Tcl" : "附加综合 Tcl"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea {
                    id: synthesisAdditionalTclEditor
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    wrapMode: TextEdit.NoWrap
                    font.family: "Monospace"
                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                    placeholderText: languageSettings.language === "en"
                        ? "Verified Tcl commands not covered by dedicated controls"
                        : "尚无专用控件、且已经检查的 Tcl 指令"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: languageSettings.language === "en" ? "Primary clocks" : "主时钟"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            UI.ToolbarButton {
                text: languageSettings.language === "en" ? "Add clock" : "添加时钟"
                iconName: "plus"
                onClicked: synthesisClockModel.append({name: "clk" + (synthesisClockModel.count + 1), source: "", period: 10, rise: 0, fall: 5, setup_uncertainty: 0, hold_uncertainty: 0, transition: 0, source_latency: 0, network_latency: 0})
            }
        }

        Repeater {
            model: synthesisClockModel
            delegate: UI.SectionCard {
                id: clockCard
                required property int index
                required property string name
                required property string source
                required property var period
                required property var rise
                required property var fall
                required property var setup_uncertainty
                required property var hold_uncertainty
                required property var transition
                required property var source_latency
                required property var network_latency
                Layout.fillWidth: true
                implicitHeight: clockEditorLayout.implicitHeight + 28
                GridLayout {
                    id: clockEditorLayout
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: width >= 720 ? 6 : 4
                    columnSpacing: 8
                    rowSpacing: 8
                    Text { text: languageSettings.language === "en" ? "Name" : "名称"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: clockCard.name || ""; onEditingFinished: synthesisClockModel.setProperty(index, "name", text) }
                    Text { text: languageSettings.language === "en" ? "Port/pin" : "端口/引脚"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: clockCard.source || ""; onEditingFinished: synthesisClockModel.setProperty(index, "source", text) }
                    Text { text: languageSettings.language === "en" ? "Period" : "周期"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.period === undefined ? 10 : clockCard.period); onEditingFinished: synthesisClockModel.setProperty(index, "period", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Rise" : "上升沿"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.rise || 0); onEditingFinished: synthesisClockModel.setProperty(index, "rise", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Fall" : "下降沿"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.fall === undefined ? Number(clockCard.period || 10) / 2 : clockCard.fall); onEditingFinished: synthesisClockModel.setProperty(index, "fall", Number(text)) }
                    Text { text: "Setup unc."; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.setup_uncertainty || 0); onEditingFinished: synthesisClockModel.setProperty(index, "setup_uncertainty", Number(text)) }
                    Text { text: "Hold unc."; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.hold_uncertainty || 0); onEditingFinished: synthesisClockModel.setProperty(index, "hold_uncertainty", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Transition" : "时钟跳变"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.transition || 0); onEditingFinished: synthesisClockModel.setProperty(index, "transition", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Source latency" : "源端延迟"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.source_latency || 0); onEditingFinished: synthesisClockModel.setProperty(index, "source_latency", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Network latency" : "网络延迟"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(clockCard.network_latency || 0); onEditingFinished: synthesisClockModel.setProperty(index, "network_latency", Number(text)) }
                    Item { Layout.fillWidth: true }
                    UI.ToolbarButton { text: languageSettings.language === "en" ? "Remove" : "删除"; iconName: "trash"; enabled: synthesisClockModel.count > 1; onClicked: synthesisClockModel.remove(index) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: languageSettings.language === "en" ? "Generated clocks" : "派生时钟"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            UI.ToolbarButton {
                text: languageSettings.language === "en" ? "Add" : "添加"
                iconName: "plus"
                onClicked: generatedClockModel.append({name: "gclk" + (generatedClockModel.count + 1), target: "", source: "", master: "", divide_by: 1, multiply_by: 1, duty_cycle: 50, invert: false})
            }
        }
        Repeater {
            model: generatedClockModel
            delegate: UI.SectionCard {
                id: generatedClockCard
                required property int index
                required property string name
                required property string target
                required property string source
                required property string master
                required property var divide_by
                required property var multiply_by
                required property var duty_cycle
                required property bool invert
                Layout.fillWidth: true
                implicitHeight: generatedClockLayout.implicitHeight + 28
                GridLayout {
                    id: generatedClockLayout
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: width >= 720 ? 6 : 4
                    columnSpacing: 8
                    rowSpacing: 8
                    Text { text: languageSettings.language === "en" ? "Name" : "名称"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: generatedClockCard.name || ""; onEditingFinished: generatedClockModel.setProperty(index, "name", text) }
                    Text { text: languageSettings.language === "en" ? "Target" : "目标引脚"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: generatedClockCard.target || ""; onEditingFinished: generatedClockModel.setProperty(index, "target", text) }
                    Text { text: languageSettings.language === "en" ? "Source" : "源引脚"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: generatedClockCard.source || ""; onEditingFinished: generatedClockModel.setProperty(index, "source", text) }
                    Text { text: languageSettings.language === "en" ? "Master" : "主时钟"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: generatedClockCard.master || ""; onEditingFinished: generatedClockModel.setProperty(index, "master", text) }
                    Text { text: languageSettings.language === "en" ? "Divide" : "分频"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(generatedClockCard.divide_by || 1); onEditingFinished: generatedClockModel.setProperty(index, "divide_by", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Multiply" : "倍频"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(generatedClockCard.multiply_by || 1); onEditingFinished: generatedClockModel.setProperty(index, "multiply_by", Number(text)) }
                    Text { text: languageSettings.language === "en" ? "Duty (%)" : "占空比 (%)"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: String(generatedClockCard.duty_cycle || 50); onEditingFinished: generatedClockModel.setProperty(index, "duty_cycle", Number(text)) }
                    UI.StyledCheckBox { text: languageSettings.language === "en" ? "Invert" : "反相"; compact: true; checked: generatedClockCard.invert; onToggled: generatedClockModel.setProperty(index, "invert", checked) }
                    Item { Layout.fillWidth: true }
                    UI.ToolbarButton { text: languageSettings.language === "en" ? "Remove" : "删除"; iconName: "trash"; onClicked: generatedClockModel.remove(index) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: languageSettings.language === "en" ? "I/O timing" : "I/O 时序"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            UI.ToolbarButton {
                text: languageSettings.language === "en" ? "Add group" : "添加端口组"
                iconName: "plus"
                onClicked: ioDelayModel.append({direction: "input", ports: "", clock: "", max: 0, min: 0, edge: "both"})
            }
        }
        Repeater {
            model: ioDelayModel
            delegate: UI.SectionCard {
                id: ioDelayCard
                required property int index
                required property string direction
                required property string ports
                required property string clock
                required property var max
                required property var min
                Layout.fillWidth: true
                implicitHeight: ioDelayLayout.implicitHeight + 28
                GridLayout {
                    id: ioDelayLayout
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: width >= 720 ? 8 : 4
                    columnSpacing: 8
                    rowSpacing: 8
                    UI.StyledComboBox { Layout.preferredWidth: 110; model: ["input", "output"]; currentIndex: ioDelayCard.direction === "output" ? 1 : 0; onActivated: ioDelayModel.setProperty(index, "direction", currentText) }
                    Text { text: languageSettings.language === "en" ? "Ports" : "端口"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: ioDelayCard.ports || ""; onEditingFinished: ioDelayModel.setProperty(index, "ports", text) }
                    Text { text: languageSettings.language === "en" ? "Clock" : "时钟"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: ioDelayCard.clock || ""; onEditingFinished: ioDelayModel.setProperty(index, "clock", text) }
                    Text { text: "Max"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.preferredWidth: 70; text: String(ioDelayCard.max || 0); onEditingFinished: ioDelayModel.setProperty(index, "max", Number(text)) }
                    Text { text: "Min"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.preferredWidth: 70; text: String(ioDelayCard.min || 0); onEditingFinished: ioDelayModel.setProperty(index, "min", Number(text)) }
                    Item { Layout.fillWidth: true }
                    UI.ToolbarButton { text: languageSettings.language === "en" ? "Remove" : "删除"; iconName: "trash"; onClicked: ioDelayModel.remove(index) }
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: timingRulesLayout.implicitHeight + 32
            GridLayout {
                id: timingRulesLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { text: languageSettings.language === "en" ? "Clock groups Tcl (Agent checked)" : "时钟组 Tcl（由 Agent 检查）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea { id: clockGroupsEditor; Layout.fillWidth: true; Layout.preferredHeight: 90; wrapMode: TextEdit.NoWrap; font.family: "Monospace"; font.pixelSize: Math.round(12 * languageSettings.uiScale); placeholderText: "set_clock_groups -asynchronous -group ..." }
                Text { text: languageSettings.language === "en" ? "Maximum transition" : "最大跳变"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: maxTransitionField; Layout.fillWidth: true; placeholderText: languageSettings.language === "en" ? "Leave empty to use library value" : "留空则使用库值" }
                Text { text: languageSettings.language === "en" ? "Maximum fanout" : "最大扇出"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: maxFanoutField; Layout.fillWidth: true }
                Text { text: languageSettings.language === "en" ? "Maximum capacitance" : "最大电容"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: maxCapacitanceField; Layout.fillWidth: true }
                Text { text: languageSettings.language === "en" ? "Input driving cell" : "输入驱动单元"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: drivingCellField; Layout.fillWidth: true; placeholderText: "BUFX4" }
                Text { text: languageSettings.language === "en" ? "Output load" : "输出电容"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: outputLoadField; Layout.fillWidth: true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: languageSettings.language === "en" ? "Timing exceptions (Agent checked)" : "时序例外（由 Agent 检查）"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            UI.ToolbarButton {
                text: languageSettings.language === "en" ? "Add" : "添加"
                iconName: "plus"
                onClicked: timingExceptionModel.append({type: "false_path", from: "", through: "", to: "", value: 1, check: "setup"})
            }
        }
        Repeater {
            model: timingExceptionModel
            delegate: UI.SectionCard {
                id: timingExceptionCard
                required property int index
                required property string type
                required property string from
                required property string through
                required property string to
                required property var value
                Layout.fillWidth: true
                implicitHeight: timingExceptionLayout.implicitHeight + 28
                GridLayout {
                    id: timingExceptionLayout
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: width >= 720 ? 8 : 4
                    columnSpacing: 8
                    rowSpacing: 8
                    UI.StyledComboBox { Layout.preferredWidth: 150; model: ["false_path", "multicycle", "max_delay", "min_delay"]; currentIndex: Math.max(0, model.indexOf(timingExceptionCard.type || "false_path")); onActivated: timingExceptionModel.setProperty(index, "type", currentText) }
                    Text { text: "From"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: timingExceptionCard.from || ""; onEditingFinished: timingExceptionModel.setProperty(index, "from", text) }
                    Text { text: "Through"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: timingExceptionCard.through || ""; onEditingFinished: timingExceptionModel.setProperty(index, "through", text) }
                    Text { text: "To"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.fillWidth: true; text: timingExceptionCard.to || ""; onEditingFinished: timingExceptionModel.setProperty(index, "to", text) }
                    Text { text: languageSettings.language === "en" ? "Value" : "数值"; color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale) }
                    UI.StyledTextField { Layout.preferredWidth: 70; text: String(timingExceptionCard.value || 1); onEditingFinished: timingExceptionModel.setProperty(index, "value", Number(text)) }
                    Item { Layout.fillWidth: true }
                    UI.ToolbarButton { text: languageSettings.language === "en" ? "Remove" : "删除"; iconName: "trash"; onClicked: timingExceptionModel.remove(index) }
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: compileSettingsLayout.implicitHeight + 32
            GridLayout {
                id: compileSettingsLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 4
                columnSpacing: 10
                rowSpacing: 10
                Text { text: languageSettings.language === "en" ? "Command" : "编译命令"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledComboBox { id: compileCommandBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: "compile", value: "compile"}, {label: "compile_ultra", value: "compile_ultra"}] }
                Text { text: languageSettings.language === "en" ? "CPU cores" : "CPU 核数"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: maxCoresField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 256 } }
                Text { text: window.localizedText("compileEffort"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                RowLayout {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    spacing: 8
                    UI.StyledComboBox { id: synthesisMapEffortBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: "Map: low", value: "low"}, {label: "Map: medium", value: "medium"}, {label: "Map: high", value: "high"}]; currentIndex: ["low", "medium", "high"].indexOf(String(window.executionValue("map_effort", "low"))) }
                    UI.StyledComboBox { id: synthesisAreaEffortBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: "Area: none", value: "none"}, {label: "Area: low", value: "low"}, {label: "Area: medium", value: "medium"}, {label: "Area: high", value: "high"}]; currentIndex: ["none", "low", "medium", "high"].indexOf(String(window.executionValue("area_effort", "low"))) }
                    UI.StyledComboBox { id: synthesisPowerEffortBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: "Power: none", value: "none"}, {label: "Power: low", value: "low"}, {label: "Power: medium", value: "medium"}, {label: "Power: high", value: "high"}]; currentIndex: ["none", "low", "medium", "high"].indexOf(String(window.executionValue("power_effort", "none"))) }
                }
                UI.StyledCheckBox { id: incrementalCompileCheck; text: languageSettings.language === "en" ? "Incremental" : "增量编译"; compact: true }
                UI.StyledCheckBox { id: retimeCompileCheck; text: "Retime"; compact: true; enabled: compileCommandBox.currentValue === "compile_ultra" }
                UI.StyledCheckBox { id: gateClockCompileCheck; text: languageSettings.language === "en" ? "Clock gating" : "门控时钟"; compact: true }
                UI.StyledCheckBox { id: scanReadyCompileCheck; text: languageSettings.language === "en" ? "Scan-ready" : "扫描链就绪"; compact: true }
                UI.StyledCheckBox { id: boundaryOptimizationCheck; text: languageSettings.language === "en" ? "Boundary optimization" : "模块接口优化"; compact: true }
                UI.StyledComboBox { id: autoUngroupBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: languageSettings.language === "en" ? "Keep hierarchy" : "保留层次", value: "none"}, {label: "Auto ungroup: area", value: "area"}, {label: "Auto ungroup: delay", value: "delay"}, {label: "Ungroup all", value: "all"}] }
                Text { text: window.localizedText("synthesisOutput"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField { id: synthesisOutputDialogField; Layout.fillWidth: true; onBrowseRequested: window.chooseFolderForField(synthesisOutputDialogField) }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: 100
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Text { text: languageSettings.language === "en" ? "Reports" : "报告"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    UI.StyledCheckBox { id: reportQorCheck; text: "QoR"; compact: true }
                    UI.StyledCheckBox { id: reportTimingCheck; text: languageSettings.language === "en" ? "Timing" : "时序"; compact: true }
                    UI.StyledCheckBox { id: reportAreaCheck; text: languageSettings.language === "en" ? "Area" : "面积"; compact: true }
                    UI.StyledCheckBox { id: reportPowerCheck; text: languageSettings.language === "en" ? "Power" : "功耗"; compact: true }
                    UI.StyledCheckBox { id: reportConstraintsCheck; text: languageSettings.language === "en" ? "Constraints" : "约束"; compact: true }
                    UI.StyledCheckBox { id: reportResourcesCheck; text: languageSettings.language === "en" ? "Resources" : "资源"; compact: true }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    UI.SettingsDialog {
        id: dftSettingsDialog
        objectName: "qaDialog_dftSettingsDialog"
        title: languageSettings.language === "en" ? "DFT settings" : "DFT 设置"
        description: languageSettings.language === "en" ? "Configure Scan, MBIST, ATPG, LBIST and DFT DRC handling" : "配置扫描链、MBIST、ATPG、LBIST 与 DFT DRC 处理"
        iconName: "scan"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"

        onOpened: {
            dftEnabledDialogCheck.checked = dftModuleCheck.checked
            scanEnabledDialogCheck.checked = scanModuleCheck.checked
            mbistEnabledDialogCheck.checked = mbistModuleCheck.checked
            atpgEnabledDialogCheck.checked = atpgModuleCheck.checked
            lbistEnabledDialogCheck.checked = lbistModuleCheck.checked
            dftClockNameField.text = String(window.executionValue("clock", ""))
            dftResetNameField.text = String(window.executionValue("reset", ""))
            dftResetActiveBox.currentIndex = Number(window.executionValue("reset_active_state", 0)) === 1 ? 1 : 0
            dftScanChainCountField.text = String(window.executionValue("scan_chain_count", 1))
            dftMaxChainLengthField.text = String(window.executionValue("max_chain_length", 1000))
            dftAutofixDialogCheck.checked = String(window.drcAutofixValue("mode", "off")) === "clock_reset_set"
            dftAutofixPortField.text = String(window.drcAutofixValue("test_mode_port", ""))
            dftCellModelEditor.text = window.atpgCellModelFilesText()
            dftAtpgTimeoutField.text = String(window.executionValue("atpg_timeout_seconds", 1800))
            dftIterationField.text = String(window.executionValue("iteration_limit", 1))
            dftOutputDialogField.text = String(window.executionValue("dft_output_dir", ""))
            dftToolDialogBox.currentIndex = window.selectedDftTool === "tessent" ? 1 : 0
            dftTessentDofileField.text = window.tessentDofile
        }
        onAccepted: {
            dftModuleCheck.checked = dftEnabledDialogCheck.checked
            scanModuleCheck.checked = dftEnabledDialogCheck.checked && scanEnabledDialogCheck.checked
            mbistModuleCheck.checked = dftEnabledDialogCheck.checked && mbistEnabledDialogCheck.checked
            atpgModuleCheck.checked = dftEnabledDialogCheck.checked && atpgEnabledDialogCheck.checked
            lbistModuleCheck.checked = dftEnabledDialogCheck.checked && lbistEnabledDialogCheck.checked
            clockNameField.text = dftClockNameField.text
            resetNameField.text = dftResetNameField.text
            resetActiveStateBox.currentIndex = dftResetActiveBox.currentIndex
            scanChainCountField.text = dftScanChainCountField.text
            maxChainLengthField.text = dftMaxChainLengthField.text
            drcAutofixCheck.checked = dftAutofixDialogCheck.checked
            drcAutofixPortField.text = dftAutofixPortField.text
            atpgCellModelFilesEditor.text = dftCellModelEditor.text
            atpgTimeoutSecondsField.text = dftAtpgTimeoutField.text
            iterationLimitField.text = dftIterationField.text
            dftOutputField.text = dftOutputDialogField.text
            window.selectedDftTool = String(dftToolDialogBox.currentValue)
            window.tessentDofile = dftTessentDofileField.text
            window.persistEditor()
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: dftImportLayout.implicitHeight + 28

            RowLayout {
                id: dftImportLayout
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                UI.FlatIcon { name: "agent"; color: "#2e9a77"; width: 20; height: 20 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: languageSettings.language === "en" ? "AI project settings import" : "AI 导入项目设置"
                        color: window.graphite
                        font.pixelSize: Math.round(13 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: languageSettings.language === "en"
                            ? "Starts a background session to inspect project scripts and documents, then update settings."
                            : "新建后台会话检查项目脚本和文档，并自动完善设置。完成后会自动归档。"
                        color: window.muted
                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                        elide: Text.ElideRight
                    }
                }
                UI.ToolbarButton {
                    text: languageSettings.language === "en" ? "AI import" : "AI 一键导入"
                    iconName: "agent"
                    emphasized: true
                    enabled: !window.projectImportRunning
                    onClicked: window.startProjectSettingsImport()
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: dftToolLayout.implicitHeight + 32

            GridLayout {
                id: dftToolLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10

                Text {
                    text: languageSettings.language === "en" ? "DFT / ATPG tool" : "DFT / ATPG 工具"
                    color: window.muted
                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                }
                UI.StyledComboBox {
                    id: dftToolDialogBox
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "value"
                    model: [
                        { label: "TestMAX", value: "testmax" },
                        { label: "Tessent", value: "tessent" }
                    ]
                }
                Text {
                    visible: dftToolDialogBox.currentValue === "tessent"
                    text: languageSettings.language === "en" ? "Custom Tessent dofile (optional)" : "自定义 Tessent dofile（可选）"
                    color: window.muted
                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                }
                UI.PathField {
                    id: dftTessentDofileField
                    visible: dftToolDialogBox.currentValue === "tessent"
                    Layout.fillWidth: true
                    placeholderText: "flow/tessent_atpg.tcl"
                    editorAvailable: text.trim().length > 0
                    onBrowseRequested: window.chooseFileForField(
                        dftTessentDofileField,
                        String(window.currentProject.root || ""),
                        ["Tessent dofile (*.tcl *.do)", "All files (*)"]
                    )
                    onEditRequested: window.openConfiguredFile(text, String(window.currentProject.root || ""))
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: 112
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text { text: languageSettings.language === "en" ? "DFT stages" : "DFT 阶段"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    UI.StyledCheckBox {
                        id: dftEnabledDialogCheck
                        text: "DFT"
                        compact: true
                        onToggled: {
                            if (!checked) {
                                scanEnabledDialogCheck.checked = false
                                mbistEnabledDialogCheck.checked = false
                                atpgEnabledDialogCheck.checked = false
                                lbistEnabledDialogCheck.checked = false
                            }
                        }
                    }
                    UI.StyledCheckBox { id: scanEnabledDialogCheck; text: languageSettings.language === "en" ? "Scan" : "扫描链"; compact: true; enabled: dftEnabledDialogCheck.checked }
                    UI.StyledCheckBox { id: mbistEnabledDialogCheck; text: "MBIST"; compact: true; enabled: dftEnabledDialogCheck.checked }
                    UI.StyledCheckBox { id: atpgEnabledDialogCheck; text: "ATPG"; compact: true; enabled: dftEnabledDialogCheck.checked }
                    UI.StyledCheckBox { id: lbistEnabledDialogCheck; text: "LBIST"; compact: true; enabled: dftEnabledDialogCheck.checked }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            visible: scanEnabledDialogCheck.checked
            implicitHeight: visible ? dftScanLayout.implicitHeight + 32 : 0
            GridLayout {
                id: dftScanLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { text: languageSettings.language === "en" ? "Scan clock" : "扫描时钟"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftClockNameField; Layout.fillWidth: true; placeholderText: "clk"; selectByMouse: true }
                Text { text: window.localizedText("resetName"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftResetNameField; Layout.fillWidth: true; placeholderText: "reset_n"; selectByMouse: true }
                Text { text: languageSettings.language === "en" ? "Reset active state" : "复位有效状态"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledComboBox { id: dftResetActiveBox; Layout.fillWidth: true; textRole: "label"; valueRole: "value"; model: [{label: languageSettings.language === "en" ? "Active low (0)" : "低有效 (0)", value: 0}, {label: languageSettings.language === "en" ? "Active high (1)" : "高有效 (1)", value: 1}] }
                Text { text: languageSettings.language === "en" ? "Scan chain count" : "扫描链数量"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftScanChainCountField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 100000 } }
                Text { text: languageSettings.language === "en" ? "Maximum scan-chain length" : "最大扫描链长度"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftMaxChainLengthField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 10000000 } }
                Text { text: languageSettings.language === "en" ? "DRC AutoFix trial" : "DFT DRC 修复尝试"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox { id: dftAutofixDialogCheck; compact: true; text: languageSettings.language === "en" ? "Allow generated test control logic" : "允许生成测试控制逻辑" }
                Text { visible: dftAutofixDialogCheck.checked; text: languageSettings.language === "en" ? "Existing test-mode port" : "已有测试模式端口"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftAutofixPortField; visible: dftAutofixDialogCheck.checked; Layout.fillWidth: true; placeholderText: "test_mode" }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            visible: atpgEnabledDialogCheck.checked
            implicitHeight: visible ? dftAtpgLayout.implicitHeight + 32 : 0
            GridLayout {
                id: dftAtpgLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { visible: dftToolDialogBox.currentValue === "testmax"; text: languageSettings.language === "en" ? "Cell model files" : "标准单元 Verilog 文件"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.ScrollableTextArea { id: dftCellModelEditor; visible: dftToolDialogBox.currentValue === "testmax"; Layout.fillWidth: true; Layout.preferredHeight: 130; wrapMode: TextEdit.NoWrap; font.family: "Monospace"; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                Text { text: languageSettings.language === "en" ? "ATPG timeout (seconds)" : "ATPG 超时（秒）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftAtpgTimeoutField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 86400 } }
                Text { text: languageSettings.language === "en" ? "Maximum iteration rounds" : "最大迭代轮数"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: dftIterationField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 20 } }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: 72
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14
                Text { text: window.localizedText("dftOutput"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.PathField { id: dftOutputDialogField; Layout.fillWidth: true; folderMode: true; onBrowseRequested: window.chooseFolderForField(dftOutputDialogField) }
            }
        }
    }

    UI.SettingsDialog {
        id: agentProjectSettingsDialog
        objectName: "qaDialog_agentProjectSettingsDialog"
        title: languageSettings.language === "en" ? "Agent target" : "Agent 目标"
        description: languageSettings.language === "en" ? "Set the task target, acceptance limits and isolated terminal" : "设置任务目标、验收要求与隔离终端"
        iconName: "agent"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"

        onOpened: {
            agentProjectGoalEditor.text = window.currentProject.goal || ""
            agentMinimumCoverageField.text = window.currentProject.minimumCoverage === undefined || window.currentProject.minimumCoverage === null ? "" : String(window.currentProject.minimumCoverage)
            agentMaximumDrcField.text = window.currentProject.maximumDftDrcViolations === undefined || window.currentProject.maximumDftDrcViolations === null ? "" : String(window.currentProject.maximumDftDrcViolations)
            agentRunTimeoutField.text = String(window.executionValue("timeout_seconds", 1800))
            agentPatchReviewDialogCheck.checked = Boolean(window.executionValue("patch_review_enabled", false))
            agentTerminalDialogCheck.checked = Boolean(window.agentTerminalValue("enabled", true))
            agentCommandTimeoutField.text = String(window.agentTerminalValue("maximum_command_seconds", 300))
        }
        onAccepted: {
            goalEditor.text = agentProjectGoalEditor.text
            minimumCoverageField.text = agentMinimumCoverageField.text
            maximumDrcField.text = agentMaximumDrcField.text
            timeoutSecondsField.text = agentRunTimeoutField.text
            agentTerminalCheck.checked = agentTerminalDialogCheck.checked
            agentTerminalMaximumSecondsField.text = agentCommandTimeoutField.text
            window.persistEditor(agentPatchReviewDialogCheck.checked)
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: agentGoalLayout.implicitHeight + 32
            ColumnLayout {
                id: agentGoalLayout
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text { text: window.localizedText("agentGoal"); color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                UI.GoalDictionary {
                    Layout.fillWidth: true
                    language: languageSettings.language
                    onInsertRequested: function(fragment) {
                        const separator = languageSettings.language === "en" ? "; " : "；"
                        agentProjectGoalEditor.insert(agentProjectGoalEditor.cursorPosition, (agentProjectGoalEditor.text.length > 0 ? separator : "") + fragment)
                    }
                }
                UI.StyledTextArea { id: agentProjectGoalEditor; Layout.fillWidth: true; Layout.preferredHeight: 150; wrapMode: TextEdit.Wrap; selectByMouse: true }
            }
        }

        UI.SectionCard {
            Layout.fillWidth: true
            implicitHeight: agentRuntimeLayout.implicitHeight + 32
            GridLayout {
                id: agentRuntimeLayout
                anchors.fill: parent
                anchors.margins: 16
                columns: 2
                columnSpacing: 14
                rowSpacing: 10
                Text { text: languageSettings.language === "en" ? "Minimum coverage (%)" : "最低覆盖率 (%)"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: agentMinimumCoverageField; Layout.fillWidth: true; placeholderText: languageSettings.language === "en" ? "Optional" : "可选" }
                Text { text: languageSettings.language === "en" ? "Maximum DFT DRC" : "最大 DFT DRC 违规数"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: agentMaximumDrcField; Layout.fillWidth: true; placeholderText: languageSettings.language === "en" ? "Optional" : "可选" }
                Text { text: window.localizedText("runTimeout"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: agentRunTimeoutField; Layout.fillWidth: true; validator: IntValidator { bottom: 1; top: 86400 } }
                Text { text: languageSettings.language === "en" ? "Static patch review" : "静态补丁审核"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox { id: agentPatchReviewDialogCheck; compact: true; text: languageSettings.language === "en" ? "Enable reviewer subagent" : "启用子智能体审核" }
                Text {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    text: languageSettings.language === "en"
                        ? "Off by default. Static review is not a substitute for simulation; DFT flow verification remains available independently."
                        : "默认关闭。静态审核不能替代仿真验证；DFT 流程验证仍独立可用。"
                    color: window.muted
                    font.pixelSize: Math.round(11 * languageSettings.uiScale)
                    wrapMode: Text.Wrap
                }
                Text { text: languageSettings.language === "en" ? "Agent isolated terminal" : "Agent 隔离终端"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledCheckBox { id: agentTerminalDialogCheck; compact: true; text: languageSettings.language === "en" ? "Enable for this project" : "为当前项目启用" }
                Text { visible: agentTerminalDialogCheck.checked; text: languageSettings.language === "en" ? "Command wait limit (seconds)" : "命令等待上限（秒）"; color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                UI.StyledTextField { id: agentCommandTimeoutField; visible: agentTerminalDialogCheck.checked; Layout.fillWidth: true; validator: IntValidator { bottom: 10; top: 900 } }
            }
        }
    }

    UI.SettingsDialog {
        id: addProjectDialog
        objectName: "qaDialog_addProjectDialog"
        title: window.localizedText("addProject")
        description: languageSettings.language === "en" ? "Register an existing project folder" : "登记一个已有的项目目录"
        iconName: "projects"
        acceptText: window.localizedText("addProject")
        acceptIcon: "plus"
        cancelText: languageSettings.language === "en" ? "Cancel" : "取消"
        width: Math.min(520, window.width - 48)
        height: Math.min(560, window.height - 48)

        onOpened: {
            addProjectFolderField.text = ""
            addProjectNameField.text = ""
            addProjectNotesEditor.text = ""
            addProjectTypeBox.currentIndex = 0
        }
        onAccepted: {
            const folder = addProjectFolderField.text.trim()
            if (folder.length === 0)
                return
            if (projectModel.addProject(folder, addProjectTypeBox.currentValue)) {
                const updates = ({})
                if (addProjectNameField.text.trim().length > 0)
                    updates.name = addProjectNameField.text.trim()
                if (addProjectNotesEditor.text.trim().length > 0)
                    updates.notes = addProjectNotesEditor.text.trim()
                if (Object.keys(updates).length > 0)
                    projectModel.updateProject(projectModel.currentIndex, updates)
                window.refreshProject()
                window.selectedPage = 1
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: window.localizedText("projectFolder")
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                UI.ScrollableTextField {
                    id: addProjectFolderField
                    Layout.fillWidth: true
                    placeholderText: "/path/to/project"
                    selectByMouse: true
                }
                UI.ToolbarButton {
                    text: window.localizedText("browse")
                    iconName: "projects"
                    onClicked: folderDialog.open()
                }
            }
            Text {
                text: window.localizedText("projectType")
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
            }
            UI.StyledComboBox {
                id: addProjectTypeBox
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: window.projectTypeOptions()
                currentIndex: 0
            }
            Text {
                text: languageSettings.language === "en" ? "Project name" : "项目名称"
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
            }
            UI.StyledTextField {
                id: addProjectNameField
                Layout.fillWidth: true
                placeholderText: languageSettings.language === "en" ? "Defaults to the folder name" : "默认使用文件夹名称"
                selectByMouse: true
            }
            Text {
                text: languageSettings.language === "en" ? "Project description" : "项目说明"
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
            }
            UI.StyledTextArea {
                id: addProjectNotesEditor
                Layout.fillWidth: true
                Layout.preferredHeight: 76
                placeholderText: languageSettings.language === "en" ? "Optional project notes" : "可选的项目说明"
                wrapMode: TextEdit.Wrap
                selectByMouse: true
            }
        }

    }

    UI.SettingsDialog {
        id: sourceEditorDialog
        objectName: "qaDialog_sourceEditorDialog"
        title: languageSettings.language === "en" ? "RTL source files" : "RTL 源文件"
        description: languageSettings.language === "en" ? "One source path per line" : "每行填写一个源码路径"
        iconName: "file"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"
        width: Math.min(1180, window.width - 72)
        height: Math.min(760, window.height - 72)

        onOpened: {
            expandedSourceEditor.text = projectGeneralDialog.visible
                ? projectGeneralSourceFilesEditor.text : sourceFilesEditor.text
            Qt.callLater(function() {
                expandedSourceEditor.contentX = expandedSourceEditor.originX
                expandedSourceEditor.contentY = expandedSourceEditor.originY
            })
        }
        onAccepted: {
            if (projectGeneralDialog.visible)
                projectGeneralSourceFilesEditor.text = expandedSourceEditor.text
            else
                sourceFilesEditor.text = expandedSourceEditor.text
        }

        UI.ScrollableTextArea {
            id: expandedSourceEditor
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.max(420, sourceEditorDialog.height - 180)
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.NoWrap
            font.family: "Monospace"
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
        }
    }

    UI.SettingsDialog {
        id: atpgCellModelEditorDialog
        objectName: "qaDialog_atpgCellModelEditorDialog"
        title: languageSettings.language === "en" ? "Cell model files" : "标准单元 Verilog 文件"
        description: languageSettings.language === "en" ? "One Verilog model path per line" : "每行填写一个 Verilog 模型路径"
        iconName: "file"
        acceptText: window.localizedText("save")
        cancelText: languageSettings.language === "en" ? "Close" : "关闭"
        width: Math.min(1180, window.width - 72)
        height: Math.min(620, window.height - 72)

        onOpened: {
            expandedAtpgCellModelEditor.text = atpgCellModelFilesEditor.text
            Qt.callLater(function() {
                expandedAtpgCellModelEditor.contentX = expandedAtpgCellModelEditor.originX
                expandedAtpgCellModelEditor.contentY = expandedAtpgCellModelEditor.originY
            })
        }
        onAccepted: atpgCellModelFilesEditor.text = expandedAtpgCellModelEditor.text

        UI.ScrollableTextArea {
            id: expandedAtpgCellModelEditor
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.max(320, atpgCellModelEditorDialog.height - 180)
            enabled: true
            readOnly: false
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.NoWrap
            font.family: "Monospace"
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
        }
    }

    FolderDialog {
        id: folderDialog
        title: window.localizedText("chooseProjectFolder")
        onAccepted: {
            addProjectFolderField.text = window.localPathFromUrl(selectedFolder)
            if (addProjectNameField.text.trim().length === 0) {
                const parts = addProjectFolderField.text.replace(/\/$/, "").split("/")
                addProjectNameField.text = parts.length > 0 ? parts[parts.length - 1] : ""
            }
        }
    }

    FileDialog {
        id: configuredFileDialog
        property var targetField: null
        property string basePath: ""
        title: languageSettings.language === "en" ? "Choose file" : "选择文件"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Supported text (*.sv *.v *.vh *.tcl *.sdc *.f *.cfg)", "All files (*)"]
        onAccepted: {
            if (targetField)
                targetField.text = window.projectRelativePath(window.localPathFromUrl(selectedFile), basePath)
        }
    }

    FileDialog {
        id: relatedDocumentDialog
        title: languageSettings.language === "en" ? "Choose project document" : "选择项目文档"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Project documents (*.md *.txt *.rst *.html *.tcl *.sdc *.f *.cfg *.json *.yaml *.yml)", "All files (*)"]
        onAccepted: {
            const row = Number(window.relatedDocumentDialogRowId)
            if (row >= 0 && row < projectRelatedDocumentsModel.count)
                projectRelatedDocumentsModel.setProperty(row, "path", window.localPathFromUrl(selectedFile))
        }
    }

    FileDialog {
        id: sourceFilesDialog
        title: languageSettings.language === "en" ? "Choose RTL source files" : "选择 RTL 源文件"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["RTL sources (*.sv *.v *.vh)", "All files (*)"]
        onAccepted: window.appendSelectedSourceFiles(selectedFiles)
    }

    FileDialog {
        id: tclImportFileDialog
        property string importStage: "synthesis"
        title: importStage === "synthesis"
            ? (languageSettings.language === "en" ? "Choose synthesis Tcl" : "选择综合 Tcl")
            : (languageSettings.language === "en" ? "Choose DFT Tcl" : "选择 DFT Tcl")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Tcl and SDC (*.tcl *.sdc)", "All files (*)"]
        onAccepted: {
            const model = window.activeModel()
            projectConfigImporter.start(
                importStage,
                window.localPathFromUrl(selectedFile),
                model.modelId || ""
            )
        }
    }

    UI.ChatDialog {
        id: queuedPromptEditDialog
        property string queueId: ""
        property string promptText: ""
        width: Math.min(620, window.width - 48)
        fitContent: true
        title: window.localizedUiText("编辑排队消息")
        description: window.localizedUiText("保存后会更新队列中的原消息，不会额外创建一条。")
        iconName: "editor"
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            UI.StyledTextArea {
                id: queuedPromptEditor
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                text: queuedPromptEditDialog.promptText
                onTextChanged: queuedPromptEditDialog.promptText = text
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                UI.ToolbarButton { text: window.localizedUiText("取消"); onClicked: queuedPromptEditDialog.close() }
                UI.ToolbarButton {
                    text: window.localizedUiText("保存")
                    iconName: "save"
                    emphasized: true
                    enabled: queuedPromptEditDialog.promptText.trim().length > 0
                    onClicked: {
                        agentController.editQueuedPrompt(queuedPromptEditDialog.queueId, queuedPromptEditDialog.promptText)
                        queuedPromptEditDialog.close()
                    }
                }
            }
        }
    }

    UI.ChatDialog {
        id: codexSessionImportDialog
        property string selectedPath: ""
        title: window.localizedUiText("导入会话")
        description: window.localizedUiText("选择一个 JSON 会话副本，导入后会作为独立会话保存在本地。")
        iconName: "history"
        width: Math.min(620, window.width - 56)
        fitContent: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: window.localizedUiText("会话文件")
                color: window.chatText
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                font.weight: Font.DemiBold
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                UI.StyledTextField {
                    id: codexSessionImportPathField
                    Layout.fillWidth: true
                    placeholderText: window.localizedUiText("选择 .json 会话文件")
                    text: codexSessionImportDialog.selectedPath
                    readOnly: true
                    selectByMouse: true
                }
                UI.ToolbarButton {
                    text: window.localizedUiText("选择文件")
                    iconName: "file"
                    onClicked: codexSessionImportFileDialog.open()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: codexSessionImportDialog.selectedPath.length === 0
                text: window.localizedUiText("支持 DFT Agent 会话 JSON；导入不会覆盖当前会话。")
                color: window.chatMuted
                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                UI.ToolbarButton {
                    text: window.localizedUiText("取消")
                    iconName: "close"
                    onClicked: codexSessionImportDialog.close()
                }
                UI.ToolbarButton {
                    text: window.localizedUiText("导入")
                    iconName: "file"
                    emphasized: true
                    enabled: codexSessionImportDialog.selectedPath.length > 0
                    onClicked: {
                        window.importCodexSession(codexSessionImportDialog.selectedPath)
                        codexSessionImportDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: codexSessionImportFileDialog
        title: window.localizedUiText("选择会话 JSON")
        fileMode: FileDialog.OpenFile
        nameFilters: ["DFT Agent 会话 (*.json)", "JSON files (*.json)", "All files (*)"]
        onAccepted: codexSessionImportDialog.selectedPath = window.localPathFromUrl(selectedFile)
    }

    FileDialog {
        id: codexSessionExportDialog
        property string threadId: ""
        title: window.localizedUiText("导出会话")
        fileMode: FileDialog.SaveFile
        nameFilters: ["DFT Agent 会话 (*.json)", "JSON files (*.json)"]
        onAccepted: window.exportCodexSession(threadId, window.localPathFromUrl(selectedFile))
    }

    FolderDialog {
        id: configuredFolderDialog
        property var targetField: null
        title: languageSettings.language === "en" ? "Choose folder" : "选择目录"
        onAccepted: {
            if (targetField)
                targetField.text = window.localPathFromUrl(selectedFolder)
        }
    }

    FolderDialog {
        id: editorWorkspaceFolderDialog
        title: languageSettings.language === "en"
            ? "Add folder to workspace" : "添加文件夹到工作区"
        onAccepted: window.addEditorWorkspaceFolder(window.localPathFromUrl(selectedFolder))
    }

    UI.ChatDialog {
        id: codexGoalDialog
        title: window.codexSessionGoal.length > 0 ? window.localizedUiText("编辑会话 Goal") : window.localizedUiText("创建会话 Goal")
        description: window.localizedUiText("Goal 会持续进入后续回合上下文，不会作为临时文字插入输入框。")
        iconName: "projects"
        width: Math.min(680, window.width - 56)
        height: 390
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: window.localizedUiText("描述可验收的最终目标、约束和停止条件。")
                color: window.chatMuted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
            UI.StyledTextArea {
                id: codexGoalEditor
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                placeholderText: window.localizedUiText("例如：修复当前工程的 DFT DRC，完成扫描链插入，并在真实 ATPG 证据下达到覆盖率目标。")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
            }
            RowLayout {
                Layout.fillWidth: true
                UI.ToolbarButton {
                    text: window.localizedUiText("清除 Goal")
                    iconName: "trash"
                    enabled: window.codexSessionGoal.length > 0
                    onClicked: {
                        const response = window.codexWorkspaceAction("session_goal_set", ({
                            thread_id: window.codexSessionId,
                            goal: ""
                        }))
                        if (response.ok) {
                            window.hydrateCodexSessionRead(response.result || ({}), false)
                            codexGoalDialog.close()
                            window.refreshCodexWorkspace()
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                UI.ToolbarButton { text: window.localizedUiText("取消"); onClicked: codexGoalDialog.close() }
                UI.ToolbarButton {
                    text: window.localizedUiText("保存 Goal")
                    iconName: "save"
                    emphasized: true
                    enabled: codexGoalEditor.text.trim().length > 0
                    onClicked: codexGoalDialog.accept()
                }
            }
        }
        onAccepted: {
            const response = window.codexWorkspaceAction("session_goal_set", ({
                thread_id: window.codexSessionId,
                goal: codexGoalEditor.text.trim()
            }))
            if (response.ok) {
                window.hydrateCodexSessionRead(response.result || ({}), false)
                window.refreshCodexWorkspace()
                const savedGoal = codexGoalEditor.text.trim()
                codexGoalDialog.close()
                // A Goal is an execution request in this workspace. Start the
                // autonomous turn after the workspace write has settled so the
                // new session state and its goal are visible immediately.
                Qt.callLater(function() { window.launchCodexGoal(savedGoal) })
            }
        }
    }

    UI.ChatDialog {
        id: codexPlanDialog
        title: window.localizedUiText("当前 Plan")
        description: window.codexSessionPlanUpdatedAt.length > 0
            ? "最后更新：" + window.codexSessionPlanUpdatedAt : "当前会话尚未生成 Plan。"
        iconName: "flow"
        width: Math.min(820, window.width - 64)
        readonly property real planBodyHeight: Math.min(420, Math.max(86, planTextMetrics.implicitHeight + 14))
        height: Math.min(window.height - 64, planBodyHeight + 128)
        Text {
            id: planTextMetrics
            visible: false
            width: codexPlanDialog.width - 40
            text: window.codexPlanText().length > 0 ? window.codexPlanText() : window.localizedUiText("尚未生成 Plan。")
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
            wrapMode: Text.Wrap
        }
        UI.ScrollableTextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: codexPlanDialog.planBodyHeight
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.Wrap
            text: window.codexPlanText().length > 0 ? window.codexPlanText() : window.localizedUiText("尚未生成 Plan。")
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
        }
    }

    UI.ChatDialog {
        id: supervisorWarningDialog
        title: window.localizedText("supervisorWarning")
        description: window.localizedText("supervisorWarningDetail")
        iconName: "report"
        width: Math.min(760, window.width - 64)
        height: Math.min(520, window.height - 64)
        UI.ScrollableTextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: agentController.report
        }
    }

    UI.ChatDialog {
        id: patchDecisionDialog
        title: languageSettings.language === "en" ? "Patch decision" : "补丁确认"
        iconName: "check"
        property string messageText: ""
        width: Math.min(700, window.width - 64)
        height: Math.min(360, window.height - 64)
        UI.ScrollableTextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: patchDecisionDialog.messageText
        }
    }

    UI.ChatDialog {
        id: patchDiffDialog
        property string patchFile: ""
        property string patchTitle: ""
        title: patchTitle.length > 0
            ? (languageSettings.language === "en" ? "Patch proposal: " : "补丁候选：") + patchTitle
            : (languageSettings.language === "en" ? "Patch proposal" : "补丁候选")
        iconName: "code"
        width: Math.min(1120, window.width - 64)
        height: Math.min(720, window.height - 64)
        UI.ScrollableTextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.NoWrap
            textFormat: TextEdit.RichText
            font.family: "Monospace"
            font.pixelSize: Math.round(12 * languageSettings.uiScale)
            text: agentController.readPatchDiffHtml(patchDiffDialog.patchFile, window.darkMode)
        }
    }

    UI.ChatDialog {
        id: projectLaunchSessionDialog
        title: window.localizedUiText("会话")
        iconName: "play"
        width: Math.min(760, window.width - 56)
        height: Math.min(610, window.height - 72)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: window.localizedUiText("选择会话")
                    color: window.graphite
                    font.pixelSize: Math.round(14 * languageSettings.uiScale)
                    font.weight: Font.DemiBold
                }
            }
            Text {
                Layout.fillWidth: true
                text: window.localizedUiText("选择会话可切换到所属项目并查看流程进度；选择新建会话会清空当前对话，运行时再创建会话。")
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
            UI.SmoothListView {
                id: projectLaunchSessionList
                objectName: "qaProjectLaunchSessionList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
                Layout.preferredHeight: Math.max(220, Math.min(420, projectLaunchSessionDialog.height - 190))
                clip: true
                spacing: 7
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AlwaysOn
                    interactive: true
                }
                model: [{ id: "__new_session__", name: "新建会话", project_id: window.currentProject.id, is_new: true }]
                    .concat(flowSessionSummaries || [])
                delegate: UI.HoverSurface {
                    id: sessionDialogDelegate
                    required property var modelData
                    width: projectLaunchSessionList.width
                    height: launchSessionRow.implicitHeight + 18
                    cornerRadius: 6
                    hovered: launchSessionHover.hovered
                    idleColor: window.darkMode
                        ? modelData.is_new ? "#202a32" : "#20262d"
                        : modelData.is_new ? "#f3f8fc" : "#fbfcfd"
                    hoverColor: window.darkMode ? "#293b4b" : "#eaf5ff"
                    hoverBorderColor: window.darkMode ? "#496a86" : "#c5def2"
                    idleBorderWidth: 1
                    idleBorderColor: window.darkMode
                        ? modelData.is_new ? "#3d5365" : "#39434e"
                        : modelData.is_new ? "#bdd6eb" : "#d9e2ea"
                    transitionDuration: 180
                    HoverHandler { id: launchSessionHover; cursorShape: Qt.PointingHandCursor }
                    RowLayout {
                        id: launchSessionRow
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10
                        UI.FlatIcon {
                            name: modelData.is_new ? "plus" : "history"
                            color: modelData.is_new ? window.accent : window.muted
                            width: 17
                            height: 17
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: modelData.is_new ? window.localizedUiText("新建会话") : String(modelData.name || window.localizedUiText("DFT 会话"))
                                color: window.graphite
                                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.is_new
                                    ? "清空当前会话；点击运行时创建新的根会话"
                                    : String(modelData.project_id || "") + " · " + String(modelData.updated_at || "")
                                        + " · " + (modelData.has_turn_count ? String(modelData.turn_count || 0) + " 轮" : "历史会话")
                                        + " · " + String(modelData.progress || 0) + "% · " + String(modelData.phase || "Ready")
                                        + (Boolean(modelData.archived) ? " · 已归档" : "")
                                color: window.muted
                                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                elide: Text.ElideRight
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: modelData.is_new
                            ? window.selectNewFlowSession()
                            : window.selectFlowProgressSession(String(modelData.id))
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: flowSessionSummaries.length === 0
                    text: window.localizedUiText("目前没有历史根会话。")
                    color: window.muted
                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                    z: -1
                }
            }
        }
    }

    UI.ChatDialog {
        id: subagentSessionDialog
        title: window.localizedUiText("历史子智能体会话")
        iconName: "agent"
        width: Math.min(720, window.width - 56)
        height: Math.min(560, window.height - 72)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: window.localizedUiText("按最近更新排序。打开后可查看该子智能体自己的完整对话和工具轨迹。")
                color: window.muted
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
            UI.SmoothListView {
                id: subagentSessionList
                objectName: "qaSubagentSessionList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: codexSubagentSessions
                spacing: 7
                clip: true
                delegate: UI.HoverSurface {
                    required property var modelData
                    width: subagentSessionList.width
                    height: subagentSessionRow.implicitHeight + 18
                    cornerRadius: 6
                    hovered: subagentSessionHover.hovered
                    idleColor: window.darkMode ? "#20262d" : "#f6f9fc"
                    hoverColor: window.darkMode ? "#293b4b" : "#eaf5ff"
                    idleBorderWidth: 1
                    idleBorderColor: window.darkMode ? "#39434e" : "#d9e2ea"
                    hoverBorderColor: window.darkMode ? "#496a86" : "#c5def2"
                    transitionDuration: 180
                    HoverHandler { id: subagentSessionHover; cursorShape: Qt.PointingHandCursor }
                    RowLayout {
                        id: subagentSessionRow
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 9
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 14
                            color: ["#dceeff", "#e6f5e9", "#fff0df", "#f1e8fb"][index % 4]
                            Text { anchors.centerIn: parent; text: String(modelData.name || "Agent").slice(0, 1); color: window.graphite; font.pixelSize: 13; font.weight: Font.DemiBold }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { Layout.fillWidth: true; text: String(modelData.name || window.localizedUiText("子智能体")); color: window.graphite; font.pixelSize: Math.round(13 * languageSettings.uiScale); font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: String(modelData.preview || modelData.updated_at || ""); color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideRight }
                        }
                        UI.ToolbarButton {
                            text: window.localizedUiText("打开")
                            iconName: "agent"
                            onClicked: {
                                window.selectCodexChatSession(
                                    String(modelData.id),
                                    String(modelData.project_id || ""))
                                subagentSessionDialog.close()
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: codexSubagentSessions.length === 0
                    text: window.localizedUiText("当前会话还没有子智能体会话。")
                    color: window.muted
                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                }
            }
        }
    }

    UI.ChatDialog {
        id: codexSessionDialog
        title: window.localizedUiText("Agent 会话")
        iconName: "history"
        width: Math.min(820, window.width - 72)
        height: Math.min(620, window.height - 72)
        onOpened: window.refreshFlowSessionList()
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    visible: !codexSessionMultiSelect
                    text: codexSessionId.length > 0 ? window.localizedUiText("当前会话已选定") : window.localizedUiText("将继续最近的未归档会话")
                    color: window.muted
                    font.pixelSize: Math.round(12 * languageSettings.uiScale)
                }
                UI.ToolbarButton {
                    visible: codexSessionMultiSelect
                    text: window.localizedUiText("取消")
                    iconName: "close"
                    onClicked: window.toggleCodexChatSessionMultiSelect()
                }
                UI.ToolbarButton {
                    visible: !codexSessionMultiSelect
                    text: codexSessionMultiSelect ? window.localizedUiText("取消多选") : window.localizedUiText("多选")
                    iconName: codexSessionMultiSelect ? "close" : "check"
                    tooltipText: window.localizedUiText("批量选择会话")
                    onClicked: window.toggleCodexChatSessionMultiSelect()
                }
                UI.ToolbarButton {
                    visible: codexSessionMultiSelect
                    text: window.localizedUiText("全选")
                    iconName: "check"
                    onClicked: {
                        const selected = ({})
                        for (const session of flowSessionSummaries || []) {
                            if (String(session.project_id || "") && String(session.workspace || "")
                                    && !agentController.sessionRunning(String(session.id || "")))
                                selected[String(session.id || "")] = true
                        }
                        window.codexSelectedSessionIds = selected
                    }
                }
                UI.ToolbarButton {
                    visible: codexSessionMultiSelect
                    text: window.localizedUiText("归档 (") + window.selectedCodexChatSessionIds().length + ")"
                    iconName: "archive"
                    enabled: window.selectedCodexChatSessionIds().length > 0
                    onClicked: window.performCodexChatSessionBatch("session_archive_many")
                }
                UI.ToolbarButton {
                    visible: codexSessionMultiSelect
                    text: window.localizedUiText("删除 (") + window.selectedCodexChatSessionIds().length + ")"
                    iconName: "trash"
                    accentColor: "#b84d55"
                    enabled: window.selectedCodexChatSessionIds().length > 0
                    onClicked: window.requestCodexChatSessionBatchDelete()
                }
                UI.ToolbarButton {
                    visible: !codexSessionMultiSelect
                    text: window.localizedUiText("导入")
                    iconName: "file"
                    tooltipText: window.localizedUiText("从 JSON 文件导入会话副本")
                    onClicked: codexSessionImportDialog.open()
                }
                UI.ToolbarButton {
                    visible: !codexSessionMultiSelect
                    text: window.localizedUiText("导出")
                    iconName: "save"
                    enabled: codexSessionId.length > 0
                    tooltipText: window.localizedUiText("导出当前会话及工具事件")
                    onClicked: {
                        codexSessionExportDialog.threadId = codexSessionId
                        codexSessionExportDialog.open()
                    }
                }
                UI.ToolbarButton {
                    visible: !codexSessionMultiSelect
                    text: window.localizedUiText("新建")
                    iconName: "plus"
                    onClicked: {
                        window.codexWorkspaceActionAsync("session_new", ({ name: "新会话" }), function(response) {
                            if (response && response.ok)
                                window.refreshCodexWorkspace()
                        })
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: codexWorkspaceMessage.length > 0
                text: window.localizedStoredMessage(codexWorkspaceMessage)
                color: "#b43a34"
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
            UI.SmoothListView {
                id: codexSessionList
                objectName: "qaCodexSessionList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 240
                Layout.preferredHeight: Math.max(240, Math.min(460, codexSessionDialog.height - 185))
                model: flowSessionSummaries
                spacing: 7
                clip: true
                delegate: UI.HoverSurface {
                    required property var modelData
                    width: codexSessionList.width
                    height: sessionRow.implicitHeight + 20
                    cornerRadius: 7
                    hovered: codexSessionHover.hovered
                    idleColor: window.darkMode ? "#20262d" : "#f8fafc"
                    selected: codexSessionMultiSelect
                        ? Boolean(codexSelectedSessionIds[String(modelData.id || "")])
                        : String(modelData.id) === codexSessionId
                    hoverColor: window.darkMode ? "#29333d" : "#eaf5ff"
                    selectedColor: window.darkMode
                        ? String(modelData.id) === codexSessionId ? "#20384d" : "#203e59"
                        : String(modelData.id) === codexSessionId ? "#eaf4ff" : "#e7f2fc"
                    selectedHoverColor: window.darkMode ? "#294a68" : "#dceeff"
                    selectedBorderColor: window.darkMode
                        ? Boolean(codexSelectedSessionIds[String(modelData.id || "")]) ? "#3975aa" : "#315675"
                        : Boolean(codexSelectedSessionIds[String(modelData.id || "")]) ? "#82b8e5" : "#a9cdf1"
                    hoverBorderColor: window.darkMode ? "#465666" : "#c5def2"
                    idleBorderWidth: 1
                    idleBorderColor: window.darkMode ? "#39434e" : "#dbe3eb"
                    opacity: modelData.archived ? 0.58 : 1
                    transitionDuration: 180
                    HoverHandler { id: codexSessionHover }
                    TapHandler {
                        enabled: codexSessionMultiSelect
                        onTapped: function(eventPoint) {
                            if (eventPoint.position.x >= 54)
                                window.setCodexChatSessionSelected(
                                    String(sessionDialogDelegate.modelData.id || ""),
                                    !Boolean(window.codexSelectedSessionIds[String(sessionDialogDelegate.modelData.id || "")]))
                        }
                    }
                    RowLayout {
                        id: sessionRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 9
                        UI.StyledCheckBox {
                            visible: codexSessionMultiSelect
                            compact: true
                            text: ""
                            checked: Boolean(codexSelectedSessionIds[String(modelData.id || "")])
                            enabled: !agentController.sessionRunning(String(modelData.id || ""))
                            onToggled: window.setCodexChatSessionSelected(String(modelData.id || ""), checked)
                        }
                        UI.FlatIcon { name: modelData.archived ? "archive" : "history"; color: modelData.archived ? "#7c8793" : window.accent; width: 17; height: 17 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { Layout.fillWidth: true; text: String(modelData.name); color: window.graphite; font.pixelSize: Math.round(13 * languageSettings.uiScale); font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: window.projectNameForCodexSession(modelData) + " · " + String(modelData.updated_at || "") + " · " + (modelData.has_turn_count ? String(modelData.turn_count || 0) + window.localizedUiText(" 轮") : window.localizedUiText("历史会话")); color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideRight }
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect && !modelData.archived
                            text: window.localizedUiText("继续")
                            iconName: "play"
                            onClicked: {
                                window.selectCodexChatSession(String(modelData.id), String(modelData.project_id || ""))
                                codexSessionDialog.close()
                            }
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect && !modelData.archived
                            text: window.localizedUiText("导出")
                            iconName: "save"
                            onClicked: {
                                codexSessionExportDialog.threadId = String(modelData.id)
                                codexSessionExportDialog.open()
                            }
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect && !modelData.archived
                            text: window.localizedUiText("分支")
                            iconName: "copy"
                            onClicked: window.manageCodexChatSession("session_fork", String(modelData.id), false)
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect && !modelData.archived && Number(modelData.turn_count) > 0
                            text: window.localizedUiText("回退")
                            iconName: "undo"
                            tooltipText: window.localizedUiText("撤回最近一轮会话状态，不修改工程文件")
                            onClicked: {
                                window.manageCodexChatSession("session_rollback", String(modelData.id), false)
                            }
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect && !modelData.archived && Number(modelData.turn_count) > 4
                            text: window.localizedUiText("压缩")
                            iconName: "compress"
                            tooltipText: window.localizedUiText("把较早的会话记录转入可管理的记忆")
                            onClicked: {
                                window.manageCodexChatSession("session_compact", String(modelData.id), false)
                            }
                        }
                        UI.ToolbarButton {
                            visible: !codexSessionMultiSelect
                            text: modelData.archived ? window.localizedUiText("恢复") : window.localizedUiText("归档")
                            iconName: modelData.archived ? "undo" : "archive"
                            onClicked: {
                                window.manageCodexChatSession(modelData.archived ? "session_restore" : "session_archive", String(modelData.id), Boolean(modelData.archived))
                                if (String(modelData.id) === codexSessionId && !modelData.archived)
                                    codexSessionId = ""
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: flowSessionSummaries.length === 0
                    text: window.localizedUiText("尚未建立会话。首次发送消息后会自动创建。")
                    color: window.muted
                    font.pixelSize: Math.round(13 * languageSettings.uiScale)
                }
            }
        }
    }

    UI.ChatDialog {
        id: codexWorkspaceDialog
        title: window.localizedUiText("Agent 工作区")
        iconName: "settings"
        width: Math.min(980, window.width - 72)
        height: Math.min(700, window.height - 72)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Text { text: window.localizedUiText("Memories、Skills、Rules、Hooks"); color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: window.localizedUiText("Rules 和 Skills 仅在启用后加入下一轮上下文；外部 Hooks 仅供审查，不会由独立 Runtime 自动执行。")
                color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); wrapMode: Text.Wrap
            }
                UI.SectionCard {
                    Layout.fillWidth: true
                    implicitHeight: memoryLayout.implicitHeight + 26
                    ColumnLayout {
                        id: memoryLayout; anchors.fill: parent; anchors.margins: 13; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon { name: "history"; color: "#397fbd"; width: 17; height: 17 }
                            Text { Layout.fillWidth: true; text: "Memories"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                            UI.ToolbarButton { text: window.localizedUiText("添加"); iconName: "plus"; onClicked: {
                                codexMemoryEditor.memoryId = ""
                                codexMemoryTitle.text = ""
                                codexMemoryContent.text = ""
                                codexMemoryEditor.visible = !codexMemoryEditor.visible
                            } }
                        }
                        ColumnLayout {
                            id: codexMemoryEditor
                            property string memoryId: ""
                            Layout.fillWidth: true; visible: false; spacing: 6
                            UI.StyledTextField { id: codexMemoryTitle; Layout.fillWidth: true; placeholderText: window.localizedUiText("记忆标题") }
                            UI.StyledTextArea { id: codexMemoryContent; Layout.fillWidth: true; Layout.preferredHeight: 76; placeholderText: window.localizedUiText("只记录会在后续工程任务中复用的事实或偏好。") }
                            RowLayout {
                                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                                UI.ToolbarButton { text: window.localizedUiText("保存记忆"); iconName: "save"; emphasized: true; enabled: codexMemoryTitle.text.trim().length > 0 && codexMemoryContent.text.trim().length > 0; onClicked: {
                                    const response = window.codexWorkspaceAction("memory_save", ({ thread_id: codexSessionId, memory_id: codexMemoryEditor.memoryId, title: codexMemoryTitle.text, content: codexMemoryContent.text }))
                                    if (response.ok) { codexMemoryEditor.memoryId = ""; codexMemoryTitle.text = ""; codexMemoryContent.text = ""; codexMemoryEditor.visible = false; window.refreshCodexWorkspace() }
                                } }
                            }
                        }
                        Repeater {
                            model: codexWorkspaceCatalog.memories || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 42
                                radius: 7
                                color: window.darkMode ? "#242b32" : "#f9fbfd"
                                border.color: window.darkMode ? "#3a4651" : "#e2e9ef"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 6
                                    spacing: 7
                                    UI.FlatIcon { name: "history"; color: modelData.enabled ? "#397fbd" : "#9aa8b4"; width: 15; height: 15 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Text { Layout.fillWidth: true; text: String(modelData.title); color: modelData.enabled ? window.chatText : window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); font.weight: Font.Medium; elide: Text.ElideRight }
                                        Text { Layout.fillWidth: true; text: String(modelData.content); color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale); elide: Text.ElideRight }
                                    }
                                    UI.ToolbarButton { text: ""; iconName: "editor"; tooltipText: window.localizedUiText("编辑记忆"); onClicked: { codexMemoryEditor.memoryId = String(modelData.memory_id); codexMemoryTitle.text = String(modelData.title); codexMemoryContent.text = String(modelData.content); codexMemoryEditor.visible = true } }
                                    UI.ToolbarButton { text: ""; iconName: modelData.enabled ? "stop" : "play"; tooltipText: modelData.enabled ? window.localizedUiText("停用记忆") : window.localizedUiText("启用记忆"); onClicked: { window.codexWorkspaceAction("memory_toggle", ({ memory_id: modelData.memory_id, enabled: !Boolean(modelData.enabled) })); window.refreshCodexWorkspace() } }
                                    UI.ToolbarButton { text: ""; iconName: "trash"; tooltipText: window.localizedUiText("删除记忆"); onClicked: { window.codexWorkspaceAction("memory_delete", ({ memory_id: modelData.memory_id })); window.refreshCodexWorkspace() } }
                                }
                            }
                        }
                    }
                }
                UI.SectionCard {
                    Layout.fillWidth: true
                    implicitHeight: checkpointLayout.implicitHeight + 26
                    ColumnLayout {
                        id: checkpointLayout; anchors.fill: parent; anchors.margins: 13; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon { name: "compress"; color: "#6e8ca0"; width: 17; height: 17 }
                            Text { Layout.fillWidth: true; text: window.localizedUiText("上下文压缩检查点"); color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                            Text {
                                text: String((codexWorkspaceCatalog.compaction_checkpoints || []).length) + window.localizedUiText(" 项")
                                color: window.muted; font.pixelSize: Math.round(11 * languageSettings.uiScale)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.localizedUiText("检查点会随下一次会话恢复一并注入；完整工具证据仍保存在会话事件记录中。")
                            color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: codexWorkspaceCatalog.compaction_checkpoints || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: checkpointSummary.implicitHeight + checkpointHeader.implicitHeight + 19
                                radius: 6
                                color: window.darkMode ? "#242b32" : "#f7fafc"
                                border.color: window.darkMode ? "#3a4651" : "#d8e3eb"
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 9; spacing: 4
                                    RowLayout {
                                        id: checkpointHeader
                                        Layout.fillWidth: true
                                        UI.FlatIcon { name: "compress"; color: "#6e8ca0"; width: 14; height: 14 }
                                        Text {
                                            Layout.fillWidth: true
                                            text: window.localizedUiText("已压缩 ") + String(modelData.message_count || 0) + window.localizedUiText(" 条消息")
                                            color: window.graphite; font.pixelSize: Math.round(12 * languageSettings.uiScale); font.weight: Font.DemiBold
                                        }
                                        Text { text: String(modelData.created_at || ""); color: window.muted; font.pixelSize: Math.round(10 * languageSettings.uiScale) }
                                    }
                                    Text {
                                        id: checkpointSummary
                                        Layout.fillWidth: true
                                        text: String(modelData.summary || "")
                                        color: window.chatMuted; font.pixelSize: Math.round(11 * languageSettings.uiScale)
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 4
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                        Text {
                            visible: (codexWorkspaceCatalog.compaction_checkpoints || []).length === 0
                            text: codexSessionId.length === 0 ? window.localizedUiText("选择会话后可查看其压缩检查点。") : window.localizedUiText("当前会话尚未压缩历史。")
                            color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale)
                        }
                    }
                }
                UI.SectionCard {
                    Layout.fillWidth: true
                    implicitHeight: rulesLayout.implicitHeight + 26
                    ColumnLayout {
                        id: rulesLayout; anchors.fill: parent; anchors.margins: 13; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon { name: "editor"; color: "#397fbd"; width: 17; height: 17 }
                            Text { Layout.fillWidth: true; text: "Rules"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                        }
                        Repeater {
                            model: codexWorkspaceCatalog.rules || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 40
                                radius: 7
                                color: window.darkMode ? "#242b32" : "#f9fbfd"
                                border.color: window.darkMode ? "#3a4651" : "#e2e9ef"
                                UI.StyledCheckBox {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    text: String(modelData.name) + " · " + String(modelData.scope)
                                    checked: window.workspaceSelectionContains("rule", String(modelData.id))
                                    onToggled: window.setWorkspaceSourceEnabled("rule", String(modelData.id), checked)
                                }
                            }
                        }
                        Text { visible: (codexWorkspaceCatalog.rules || []).length === 0; text: window.localizedUiText("未发现 AGENTS.md 或规则文件。"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                    }
                }
                UI.SectionCard {
                    Layout.fillWidth: true
                    implicitHeight: skillsLayout.implicitHeight + 26
                    ColumnLayout {
                        id: skillsLayout; anchors.fill: parent; anchors.margins: 13; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon { name: "skills"; color: "#397fbd"; width: 17; height: 17 }
                            Text { Layout.fillWidth: true; text: "Skills"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                        }
                        Repeater {
                            model: codexWorkspaceCatalog.skills || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 40
                                radius: 7
                                color: window.darkMode ? "#242b32" : "#f9fbfd"
                                border.color: window.darkMode ? "#3a4651" : "#e2e9ef"
                                UI.StyledCheckBox {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    text: String(modelData.name) + (String(modelData.description).length > 0 ? " · " + String(modelData.description) : "")
                                    checked: window.workspaceSkillEnabled(modelData)
                                    enabled: Boolean(currentProject.id)
                                    onToggled: window.setWorkspaceSourceEnabled("skill", String(modelData.id), checked)
                                }
                            }
                        }
                        Text { visible: (codexWorkspaceCatalog.skills || []).length === 0; text: window.localizedUiText("未发现可用 SKILL.md。"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                    }
                }
                UI.SectionCard {
                    Layout.fillWidth: true
                    implicitHeight: hooksLayout.implicitHeight + 26
                    ColumnLayout {
                        id: hooksLayout; anchors.fill: parent; anchors.margins: 13; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            UI.FlatIcon { name: "terminal"; color: "#397fbd"; width: 17; height: 17 }
                            Text { Layout.fillWidth: true; text: "Hooks"; color: window.graphite; font.pixelSize: Math.round(14 * languageSettings.uiScale); font.weight: Font.DemiBold }
                            Switch {
                                id: hookSwitch
                                checked: false
                                enabled: false
                                UI.ThemedToolTip {
                                    target: hookSwitch
                                    message: window.localizedUiText("独立 Agent Runtime 不会自动执行外部 Hook")
                                    active: hookSwitch.hovered
                                }
                            }
                        }
                        Repeater {
                            model: codexWorkspaceCatalog.hooks || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 36
                                radius: 7
                                color: window.darkMode ? "#242b32" : "#f9fbfd"
                                border.color: window.darkMode ? "#3a4651" : "#e2e9ef"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    UI.FlatIcon { name: "terminal"; color: "#718596"; width: 15; height: 15 }
                                    Text { Layout.fillWidth: true; text: String(modelData.name) + " · " + String(modelData.scope); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale); elide: Text.ElideRight }
                                }
                            }
                        }
                        Text { visible: (codexWorkspaceCatalog.hooks || []).length === 0; text: window.localizedUiText("未发现 hooks.json。"); color: window.muted; font.pixelSize: Math.round(12 * languageSettings.uiScale) }
                    }
                }
        }
    }

    Rectangle {
        z: 10000
        visible: window.projectImportToast.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(parent.width - 40, Math.max(300, importToastRow.implicitWidth + 28))
        height: importToastRow.implicitHeight + 22
        radius: 8
        color: window.chatSurface
        border.color: window.chatBorder
        RowLayout {
            id: importToastRow
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            spacing: 9
            UI.FlatIcon { name: "agent"; color: "#397fbd"; width: 18; height: 18 }
            Text {
                Layout.fillWidth: true
                text: window.localizedStoredMessage(window.projectImportToast)
                color: window.graphite
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                wrapMode: Text.Wrap
            }
        }
    }

    Rectangle {
        id: qaFrameProbe
        visible: window.qaFrameProfile
        width: 2
        height: 2
        color: "#000000"
        opacity: 0.01
        FrameAnimation {
            running: window.qaFrameProfile
            onTriggered: {
                qaFrameProbe.x = currentFrame % 2
                window.qaAnimationTickCount += 1
            }
        }
    }
}
