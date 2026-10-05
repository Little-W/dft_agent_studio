import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string language: "zh-CN"
    property int categoryIndex: 0
    readonly property bool useEnglish: language === "en"
    readonly property var activeCategory: categories[categoryIndex] || categories[0]
    readonly property int entryCount: activeCategory ? activeCategory.entries.length : 0
    property alias termRepeater: termRepeater
    signal insertRequested(string fragment)

    readonly property var categories: [
        {
            zh: "扫描架构", en: "Scan architecture",
            entries: [
                { zh: "扫描链插入", en: "Scan insertion", insertZh: "插入可控的扫描链结构", insertEn: "insert a controllable scan DFT architecture" },
                { zh: "扫描使能", en: "Scan enable", insertZh: "定义并验证扫描使能信号", insertEn: "define and verify scan_enable test control" },
                { zh: "测试时钟", en: "Test clock", insertZh: "约束并验证扫描测试时钟", insertEn: "constrain and verify the scan test clock" },
                { zh: "扫描链数量", en: "Chain count", insertZh: "配置扫描链数量", insertEn: "configure the scan chain count" },
                { zh: "最大扫描链长度 <= 1000", en: "Max chain length <= 1000", insertZh: "限制最大扫描链长度为 1000", insertEn: "limit the maximum scan chain length to 1000" },
                { zh: "Lockup 锁存器", en: "Lockup latch", insertZh: "评估跨时钟扫描链，并按需要插入 Lockup 锁存器", insertEn: "evaluate and insert lockup latches for cross-clock scan chains" },
                { zh: "测试点", en: "Test points", insertZh: "评估 test point 插入以改善可控性与可观测性", insertEn: "evaluate test-point insertion to improve controllability and observability" },
                { zh: "异步复位", en: "Asynchronous reset", insertZh: "检查 asynchronous reset 的测试模式约束", insertEn: "check test-mode constraints for asynchronous resets" },
                { zh: "测试协议", en: "Test protocol", insertZh: "生成并验证 test protocol", insertEn: "generate and verify the test protocol" }
            ]
        },
        {
            zh: "ATPG", en: "ATPG",
            entries: [
                { zh: "Stuck-at", en: "Stuck-at", insertZh: "执行 stuck-at ATPG", insertEn: "run stuck-at ATPG" },
                { zh: "Transition", en: "Transition", insertZh: "执行 transition ATPG", insertEn: "run transition ATPG" },
                { zh: "At-speed", en: "At-speed", insertZh: "配置并验证 at-speed 测试", insertEn: "configure and verify at-speed testing" },
                { zh: "LOC", en: "Launch-on-capture", insertZh: "使用 launch-on-capture 模式生成模式", insertEn: "generate patterns with launch-on-capture" },
                { zh: "LOS", en: "Launch-on-shift", insertZh: "评估 launch-on-shift 模式的适用性", insertEn: "evaluate launch-on-shift applicability" },
                { zh: "模式压缩", en: "Pattern compaction", insertZh: "启用 ATPG pattern compaction", insertEn: "enable ATPG pattern compaction" },
                { zh: "X-fill", en: "X-fill", insertZh: "优化 X-fill 以降低切换活动", insertEn: "optimize X-fill to reduce switching activity" },
                { zh: "Abort limit", en: "Abort limit", insertZh: "审查 ATPG abort limit 和未完成故障", insertEn: "review the ATPG abort limit and aborted faults" },
                { zh: "诊断模式", en: "Diagnostic patterns", insertZh: "生成用于故障诊断的 ATPG 模式", insertEn: "generate ATPG patterns for fault diagnosis" }
            ]
        },
        {
            zh: "覆盖率", en: "Coverage metrics",
            entries: [
                { zh: "Test coverage >= 99%", en: "Test coverage >= 99%", insertZh: "test coverage 至少达到 99.00%", insertEn: "achieve test coverage of at least 99.00%" },
                { zh: "Fault coverage >= 99%", en: "Fault coverage >= 99%", insertZh: "fault coverage 至少达到 99.00%", insertEn: "achieve fault coverage of at least 99.00%" },
                { zh: "模式数 <= 10000", en: "Patterns <= 10000", insertZh: "将 ATPG pattern count 控制在 10000 以内", insertEn: "keep ATPG pattern count within 10000" },
                { zh: "运行时间 <= 30 min", en: "Runtime <= 30 min", insertZh: "将 ATPG runtime 控制在 30 分钟以内", insertEn: "keep ATPG runtime within 30 minutes" },
                { zh: "不可测故障", en: "Untestable faults", insertZh: "分类并审查 untestable faults", insertEn: "classify and review untestable faults" },
                { zh: "覆盖率豁免", en: "Coverage exclusions", insertZh: "记录并审查 coverage exclusions", insertEn: "record and review coverage exclusions" },
                { zh: "中止故障", en: "Aborted faults", insertZh: "分析 aborted faults 并给出改进建议", insertEn: "analyze aborted faults and provide improvement actions" },
                { zh: "可控性 / 可观测性", en: "Controllability / observability", insertZh: "分析 controllability 和 observability 瓶颈", insertEn: "analyze controllability and observability bottlenecks" }
            ]
        },
        {
            zh: "压缩与存储", en: "Compression and memory",
            entries: [
                { zh: "扫描压缩", en: "Scan compression", insertZh: "评估扫描压缩架构", insertEn: "evaluate the scan-compression architecture" },
                { zh: "EDT 解压器", en: "EDT decompressor", insertZh: "配置 EDT decompressor", insertEn: "configure the EDT decompressor" },
                { zh: "响应压缩器", en: "Response compactor", insertZh: "配置 response compactor 并检查 X 传播", insertEn: "configure the response compactor and check X propagation" },
                { zh: "压缩比", en: "Compression ratio", insertZh: "报告扫描压缩比", insertEn: "report the scan-compression ratio" },
                { zh: "通道数", en: "Channel count", insertZh: "优化扫描通道数量", insertEn: "optimize scan channel count" },
                { zh: "Wrapper chain", en: "Wrapper chain", insertZh: "配置并验证 wrapper chain", insertEn: "configure and verify wrapper chains" },
                { zh: "MBIST", en: "MBIST", insertZh: "规划并验证 MBIST", insertEn: "plan and verify MBIST" },
                { zh: "BISR", en: "BISR", insertZh: "评估 BISR 与修复信息管理", insertEn: "evaluate BISR and repair-information management" },
                { zh: "IJTAG", en: "IJTAG", insertZh: "评估 IJTAG 测试访问需求", insertEn: "evaluate IJTAG test-access requirements" }
            ]
        },
        {
            zh: "审核交付", en: "Review and handoff",
            entries: [
                { zh: "DFT DRC = 0", en: "DFT DRC = 0", insertZh: "完成 DFT DRC，确保 error count 为 0", insertEn: "complete DFT DRC with an error count of 0" },
                { zh: "扫描链追踪", en: "Scan-chain tracing", insertZh: "追踪并验证每条扫描链的输入和输出端点", insertEn: "trace and verify every scan-chain endpoint" },
                { zh: "门级网表", en: "Gate-level netlist", insertZh: "导出 DFT 插入后的 gate-level netlist", insertEn: "export the DFT-inserted gate-level netlist" },
                { zh: "SPF", en: "SPF", insertZh: "导出并检查 SPF test protocol 文件", insertEn: "export and check the SPF test-protocol file" },
                { zh: "STIL", en: "STIL", insertZh: "导出 STIL pattern 交付物", insertEn: "export STIL pattern deliverables" },
                { zh: "SDF", en: "SDF", insertZh: "准备时序验证所需的 SDF", insertEn: "prepare SDF required for timing validation" },
                { zh: "报告包", en: "Report package", insertZh: "交付 DFT DRC、coverage、ATPG 和 chain 报告", insertEn: "deliver DFT DRC, coverage, ATPG, and chain reports" },
                { zh: "执行证据", en: "Execution evidence", insertZh: "记录可追溯的命令、日志、报告和交叉验证证据", insertEn: "record traceable commands, logs, reports, and cross-validation evidence" }
            ]
        }
    ]

    implicitHeight: dictionaryColumn.implicitHeight

    Column {
        id: dictionaryColumn
        width: parent.width
        spacing: 7

        Text {
            text: root.useEnglish ? "DFT goal dictionary" : "DFT 目标词典"
            color: "#667384"
            font.pixelSize: Math.round(12 * languageSettings.uiScale)
            font.weight: Font.DemiBold
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.categories

                delegate: Button {
                    required property int index
                    required property var modelData
                    readonly property bool active: index === root.categoryIndex
                    implicitWidth: categoryLabel.implicitWidth + 18
                    implicitHeight: 26
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.categoryIndex = index

                    background: Rectangle {
                        radius: 6
                        color: languageSettings.darkModeEnabled
                            ? parent.down ? "#294a68" : parent.active ? "#203e59" : parent.hovered ? "#29333d" : "#20262d"
                            : parent.down ? "#b8d9fb" : parent.active ? "#dcecff" : parent.hovered ? "#edf3fa" : "#f5f7fa"
                        border.width: parent.active ? 1 : 0
                        border.color: languageSettings.darkModeEnabled ? "#3975aa" : "#aed0f3"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    contentItem: Text {
                        id: categoryLabel
                        text: root.useEnglish ? modelData.en : modelData.zh
                        color: languageSettings.darkModeEnabled
                            ? parent.active ? "#b9dcf7" : "#c1cbd4"
                            : parent.active ? "#075bac" : "#576575"
                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                        font.weight: parent.active ? Font.DemiBold : Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                id: termRepeater
                model: root.activeCategory ? root.activeCategory.entries : []

                delegate: Button {
                    required property var modelData
                    implicitWidth: Math.min(root.width, termLabel.implicitWidth + 18)
                    implicitHeight: 27
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.insertRequested(root.useEnglish ? modelData.insertEn : modelData.insertZh)

                    background: Rectangle {
                        radius: 6
                        color: languageSettings.darkModeEnabled
                            ? parent.down ? "#294a68" : parent.hovered ? "#29333d" : "#20262d"
                            : parent.down ? "#c5e0fb" : parent.hovered ? "#e4f0fc" : "#f4f7fb"
                        border.width: 1
                        border.color: languageSettings.darkModeEnabled
                            ? parent.hovered ? "#496a86" : "#39434e"
                            : parent.hovered ? "#aacded" : "#dde5ed"
                        Behavior on color { ColorAnimation { duration: 110 } }
                        Behavior on border.color { ColorAnimation { duration: 110 } }
                    }
                    contentItem: Text {
                        id: termLabel
                        text: root.useEnglish ? modelData.en : modelData.zh
                        color: languageSettings.darkModeEnabled ? "#d0d9e1" : "#3c4a59"
                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
