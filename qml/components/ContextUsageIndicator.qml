import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string language: "zh-CN"
    property int totalCapacity: 65536
    property int effectiveCapacity: 62259
    property int autoCompactLimit: 58982
    property int inputUsed: 0
    property int inputCapacity: 16384
    property int systemUsed: 0
    property int historyUsed: 0
    property int toolsUsed: 0
    property int toolsCapacity: 8192
    property int outputUsed: 0
    property int outputCapacity: 4096
    property bool compacted: false
    property bool providerMeasured: false
    readonly property bool darkMode: languageSettings.darkModeEnabled

    readonly property int effectiveHistoryCapacity: Math.min(effectiveCapacity, Math.max(0, inputCapacity))
    readonly property bool hasMeasuredUsage: inputUsed > 0 || systemUsed > 0 || historyUsed > 0 || outputUsed > 0
    readonly property int systemPromptUsed: Math.min(totalCapacity, Math.max(0, systemUsed))
    readonly property int displayedHistoryUsed: Math.max(0, historyUsed)
    readonly property int systemPromptCapacity: Math.max(1, effectiveCapacity)
    readonly property int historyCapacity: effectiveHistoryCapacity
    readonly property int currentSessionCapacity: Math.max(
        0,
        effectiveCapacity - systemPromptUsed - Math.max(0, toolsUsed) - displayedHistoryUsed
    )
    readonly property int availableUnused: Math.max(
        0,
        effectiveCapacity - totalUsed
    )
    readonly property int totalUsed: Math.min(
        effectiveCapacity,
        Math.max(0, providerMeasured
            ? inputUsed
            : systemPromptUsed + Math.max(0, toolsUsed) + displayedHistoryUsed + outputUsed)
    )
    readonly property real usedFraction: effectiveCapacity > 0 ? Math.min(1, totalUsed / effectiveCapacity) : 0
    readonly property var sections: [
        { key: "system", labelZh: "系统提示", labelEn: "System prompt", used: systemPromptUsed, capacity: systemPromptCapacity, showCapacity: false, color: "#258f82" },
        { key: "tools", labelZh: "工具定义", labelEn: "Tool definitions", used: Math.max(0, toolsUsed), capacity: Math.max(1, effectiveCapacity), showCapacity: false, color: "#347fb1" },
        { key: "history", labelZh: "历史记录", labelEn: "History", used: displayedHistoryUsed, capacity: historyCapacity, showCapacity: true, color: "#8068ad" },
        { key: "session", labelZh: "当前工作会话", labelEn: "Current session", used: outputUsed, capacity: currentSessionCapacity, showCapacity: true, color: "#bd7b2e" },
        { key: "reserve", labelZh: "未使用", labelEn: "Unused", used: availableUnused, capacity: effectiveCapacity, showCapacity: false, color: "#7b8996" }
    ]

    implicitWidth: 34
    implicitHeight: 32

    function tokenText(value) {
        const safeValue = Math.max(0, Number(value) || 0)
        if (safeValue < 1024)
            return String(Math.round(safeValue))
        const scaled = safeValue / 1024
        return (Math.abs(scaled - Math.round(scaled)) < 0.05 ? String(Math.round(scaled)) : scaled.toFixed(1)) + "K"
    }

    function exactTokenText(value) {
        const safeValue = Math.max(0, Math.round(Number(value) || 0))
        return String(safeValue).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    }

    function ringColor() {
        if (usedFraction >= 0.88)
            return "#d0524c"
        if (usedFraction >= 0.72)
            return "#c18432"
        return "#397fbd"
    }

    function openDetails() {
        usagePopup.open()
    }

    onUsedFractionChanged: usageRing.requestPaint()

    ToolButton {
        id: contextButton
        readonly property bool pointerHovered: contextHover.hovered
        anchors.fill: parent
        hoverEnabled: false
        padding: 0
        Accessible.name: root.language === "en" ? "Context usage" : "上下文用量"
        onClicked: usagePopup.visible ? usagePopup.close() : usagePopup.open()

        HoverHandler {
            id: contextHover
        }

        background: Item {
            clip: true

            Rectangle {
                readonly property bool expanded: usagePopup.visible || contextButton.pointerHovered || contextButton.down
                x: expanded ? 0 : 5
                width: expanded ? parent.width : Math.max(0, parent.width - 10)
                height: parent.height
                radius: 7
                color: usagePopup.visible ? (root.darkMode ? "#263849" : "#d5e5f3")
                    : contextButton.down ? (root.darkMode ? "#31495e" : "#c9dbea")
                    : contextButton.pointerHovered ? (root.darkMode ? "#29333d" : "#dce6f0") : "transparent"
                border.width: expanded ? 1 : 0
                border.color: usagePopup.visible ? (root.darkMode ? "#46617a" : "#a9c4dc")
                    : contextButton.pointerHovered ? (root.darkMode ? "#53687b" : "#bcc9d7") : "transparent"

                Behavior on x { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
                Behavior on width { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 130 } }
                Behavior on border.color { ColorAnimation { duration: 130 } }
            }
        }

        contentItem: Item {
            Canvas {
                id: usageRing
                anchors.centerIn: parent
                width: 26
                height: 26
                antialiasing: true

                onPaint: {
                    const context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    context.lineWidth = 3
                    context.lineCap = "round"
                    context.strokeStyle = root.darkMode ? "#414c57" : "#d6dde5"
                    context.beginPath()
                    context.arc(width / 2, height / 2, 10, 0, Math.PI * 2)
                    context.stroke()
                    if (root.usedFraction > 0) {
                        context.strokeStyle = root.ringColor()
                        context.beginPath()
                        context.arc(width / 2, height / 2, 10, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * root.usedFraction)
                        context.stroke()
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: Math.round(root.usedFraction * 100)
                color: root.darkMode ? "#c5ced7" : "#52606d"
                font.pixelSize: Math.round(8 * languageSettings.uiScale)
                font.weight: Font.DemiBold
            }
        }

        ThemedToolTip {
            id: contextTooltip
            target: contextButton
            message: root.hasMeasuredUsage
                ? (root.language === "en" ? "Context " : "上下文 ")
                    + root.tokenText(root.totalUsed) + " / " + root.tokenText(root.totalCapacity)
                : (root.language === "en" ? "Waiting for the first model request" : "等待首次模型请求")
            active: contextButton.pointerHovered && !usagePopup.visible
        }

        Connections {
            target: contextButton
            function onClicked() { contextTooltip.dismiss() }
        }
    }

    Popup {
        id: usagePopup
        parent: root
        x: root.width - width
        y: root.height + 7
        width: 364
        height: 334
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 130; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; from: 1; to: 0.98; duration: 90; easing.type: Easing.InCubic }
            }
        }

        background: Rectangle {
            radius: 8
            color: root.darkMode ? "#20262d" : "#fbfcfd"
            border.color: root.darkMode ? "#39434e" : "#d5dde6"
            border.width: 1
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    spacing: 1
                    Text {
                        text: root.language === "en" ? "Context window" : "上下文窗口"
                        color: root.darkMode ? "#e0e7ee" : "#2f3a45"
                        font.pixelSize: Math.round(14 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.tokenText(root.totalUsed) + " / " + root.tokenText(root.effectiveCapacity)
                            + "  (" + Math.round(root.usedFraction * 100) + "%)"
                            + (root.providerMeasured ? (root.language === "en" ? " · measured" : " · API 实测") : "")
                        color: root.darkMode ? "#a0acb8" : "#6b7784"
                        font.pixelSize: Math.round(11 * languageSettings.uiScale)
                    }
                }
                Text {
                    text: (root.language === "en" ? "Model " : "模型 ")
                        + root.tokenText(root.totalCapacity) + " · "
                        + (root.language === "en" ? "usable " : "有效 ")
                        + root.tokenText(root.effectiveCapacity) + " · "
                        + (root.language === "en" ? "compact " : "压缩 ")
                        + root.tokenText(root.autoCompactLimit)
                    color: root.darkMode ? "#a0acb8" : "#6b7784"
                    font.pixelSize: Math.round(10 * languageSettings.uiScale)
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    visible: root.compacted
                    implicitWidth: compactedText.implicitWidth + 14
                    implicitHeight: 22
                    radius: 6
                    color: root.darkMode ? "#29333d" : "#e7eff6"
                    Text {
                        id: compactedText
                        anchors.centerIn: parent
                        text: root.language === "en" ? "Compacted" : "已压缩"
                        color: root.darkMode ? "#b4c8d9" : "#526d84"
                        font.pixelSize: Math.round(10 * languageSettings.uiScale)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.darkMode ? "#39434e" : "#e4e8ed"
            }

            Repeater {
                model: root.sections

                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: modelData.color
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.language === "en" ? modelData.labelEn : modelData.labelZh
                            color: root.darkMode ? "#c0cad4" : "#4d5965"
                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                        }
                        Text {
                            text: !root.hasMeasuredUsage && modelData.key === "system"
                                ? (root.language === "en" ? "Waiting for request" : "等待模型请求")
                                : modelData.showCapacity
                                    ? root.exactTokenText(modelData.used) + " / " + root.exactTokenText(modelData.capacity)
                                    : root.exactTokenText(modelData.used)
                            color: root.darkMode ? "#e0e7ee" : "#3e4b57"
                            font.pixelSize: Math.round(11 * languageSettings.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        radius: 4
                        color: root.darkMode ? "#343d46" : "#e4e8ed"
                        clip: true

                        Rectangle {
                            width: parent.width * Math.min(1, modelData.capacity > 0 ? modelData.used / modelData.capacity : 0)
                            height: parent.height
                            radius: 4
                            color: modelData.color

                            Behavior on width {
                                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }
            }
        }
    }
}
