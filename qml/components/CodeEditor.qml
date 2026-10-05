import QtQuick
import QtQuick.Controls
import "WheelScroll.js" as WheelScroll

Item {
    id: control

    property alias text: textArea.text
    property alias cursorPosition: textArea.cursorPosition
    property alias readOnly: textArea.readOnly
    property alias selectByMouse: textArea.selectByMouse
    property alias persistentSelection: textArea.persistentSelection
    property alias font: textArea.font
    property alias color: textArea.color
    property string language: "config"
    property string completionText: ""
    property var completionItems: []
    property int completionIndex: 0
    property int lineHeight: 19
    property int tabSize: 4
    property bool insertSpaces: true
    property bool wordWrap: false
    property bool smoothScrolling: true
    property bool showLineNumbers: true
    property bool reducedHighlighting: false
    readonly property bool editorHasFocus: textArea.activeFocus
    readonly property int gutterWidth: showLineNumbers ? 52 : 0
    readonly property int lineCount: Math.max(1, textArea.lineCount)
    readonly property real textPadding: 10
    readonly property rect completionCursorRect: textArea.positionToRectangle(textArea.cursorPosition)
    readonly property real completionAnchorX: editorScroll.x + textArea.x + completionCursorRect.x - editorScroll.contentX
    readonly property real completionAnchorY: editorScroll.y + textArea.y + completionCursorRect.y - editorScroll.contentY
    readonly property string completionFirstLine: completionText.split("\n")[0]
    readonly property bool completionMenuVisible: completionItems && completionItems.length > 0 && editorHasFocus

    signal localCompletionRequested()
    signal agentCompletionRequested()
    signal completionAccepted()
    signal completionItemAccepted(var item)
    signal completionIndexRequested(int index)
    signal completionDismissed()
    signal saveRequested()
    signal cursorMoved()

    function focusEditor() {
        textArea.forceActiveFocus()
    }

    function ensureCursorVisible() {
        const rect = textArea.positionToRectangle(textArea.cursorPosition)
        const left = textArea.x + rect.x
        const right = left + Math.max(2, rect.width)
        const top = textArea.y + rect.y
        const bottom = top + Math.max(control.lineHeight, rect.height)
        if (left < editorScroll.contentX + 4)
            editorScroll.contentX = Math.max(editorScroll.originX, left - 4)
        else if (right > editorScroll.contentX + editorScroll.width - 8)
            editorScroll.contentX = Math.min(editorScroll.contentWidth - editorScroll.width, right - editorScroll.width + 8)
        if (top < editorScroll.contentY + 4)
            editorScroll.contentY = Math.max(editorScroll.originY, top - 4)
        else if (bottom > editorScroll.contentY + editorScroll.height - 8)
            editorScroll.contentY = Math.min(editorScroll.contentHeight - editorScroll.height, bottom - editorScroll.height + 8)
    }

    function insert(position, value) {
        textArea.insert(position, value)
    }

    function acceptCompletion() {
        if (completionText.length === 0)
            return
        const cursor = textArea.cursorPosition
        textArea.insert(cursor, completionText)
        textArea.cursorPosition = cursor + completionText.length
        completionAccepted()
    }

    function acceptCompletionItem(index) {
        if (!completionItems || index < 0 || index >= completionItems.length)
            return
        const item = completionItems[index]
        const value = String(item.insertText || "")
        if (value.length === 0)
            return
        const cursor = textArea.cursorPosition
        textArea.insert(cursor, value)
        textArea.cursorPosition = cursor + value.length
        completionItemAccepted(item)
    }

    function dismissCompletion() {
        completionDismissed()
    }

    function lineNumbers() {
        const rows = []
        for (let line = 1; line <= lineCount; ++line)
            rows.push(line)
        return rows.join("\n")
    }

    function tabText() {
        if (!insertSpaces)
            return "\t"
        const beforeCursor = textArea.text.slice(0, textArea.cursorPosition)
        const lineStart = beforeCursor.lastIndexOf("\n") + 1
        const column = beforeCursor.length - lineStart
        const count = Math.max(1, tabSize - column % tabSize)
        return " ".repeat(count)
    }

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: "#1e1e1e"
        border.width: control.editorHasFocus ? 2 : 1
        border.color: control.editorHasFocus ? "#3d7fb2" : "#35383d"
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    Rectangle {
        visible: control.showLineNumbers
        x: 1
        y: 1
        width: control.gutterWidth
        height: Math.max(0, control.height - 2)
        color: "#181818"
        radius: 6
    }

    Flickable {
        id: editorScroll
        anchors.fill: parent
        anchors.leftMargin: control.gutterWidth
        anchors.margins: 1
        clip: true
        interactive: false
        pixelAligned: false
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: 2800
        flickDeceleration: 2500
        contentWidth: control.wordWrap ? width : Math.max(width, textArea.width + control.textPadding * 2)
        contentHeight: Math.max(height, textArea.contentHeight + control.textPadding * 2)
        function scrollBy(delta) {
            if (control.smoothScrolling)
                return wheelAnimator.scrollBy(-delta)
            const minimum = originY
            const maximum = Math.max(minimum, contentHeight - height + minimum)
            if (maximum <= minimum)
                return false
            const next = Math.max(minimum, Math.min(maximum, contentY - delta))
            if (Math.abs(next - contentY) < 0.1)
                return false
            wheelAnimator.cancel()
            contentY = next
            return true
        }

        function wheelDelta(event) {
            return WheelScroll.delta(event, false, 64)
        }

        onDraggingChanged: if (dragging) wheelAnimator.cancel()

        TextEdit {
            id: textArea
            x: control.textPadding
            y: control.textPadding
            width: control.wordWrap
                ? Math.max(1, editorScroll.width - control.textPadding * 2)
                : Math.max(editorScroll.width - control.textPadding * 2, implicitWidth)
            height: Math.max(1, contentHeight)
            wrapMode: control.wordWrap ? TextEdit.Wrap : TextEdit.NoWrap
            selectByMouse: true
            persistentSelection: true
            font.family: "Monospace"
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
            color: "#d4d4d4"
            onCursorPositionChanged: {
                control.cursorMoved()
                Qt.callLater(control.ensureCursorVisible)
            }

            Keys.onTabPressed: function(event) {
                if (control.completionText.length > 0) {
                    control.acceptCompletion()
                    event.accepted = true
                } else if (control.completionMenuVisible) {
                    control.acceptCompletionItem(control.completionIndex)
                    event.accepted = true
                } else if (!textArea.readOnly) {
                    const cursor = textArea.cursorPosition
                    const value = control.tabText()
                    textArea.insert(cursor, value)
                    textArea.cursorPosition = cursor + value.length
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) {
                if (control.completionMenuVisible) {
                    control.acceptCompletionItem(control.completionIndex)
                    event.accepted = true
                }
            }
            Keys.onEscapePressed: function(event) {
                if (control.completionText.length > 0 || control.completionMenuVisible) {
                    control.dismissCompletion()
                    event.accepted = true
                }
            }
            Keys.onPressed: function(event) {
                if (control.completionMenuVisible && event.key === Qt.Key_Down) {
                    control.completionIndexRequested((control.completionIndex + 1) % control.completionItems.length)
                    event.accepted = true
                } else if (control.completionMenuVisible && event.key === Qt.Key_Up) {
                    control.completionIndexRequested((control.completionIndex - 1 + control.completionItems.length) % control.completionItems.length)
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) {
                    control.saveRequested()
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Space) {
                    if (event.modifiers & Qt.AltModifier)
                        control.agentCompletionRequested()
                    else
                        control.localCompletionRequested()
                    event.accepted = true
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            width: 11
            policy: editorScroll.contentHeight > editorScroll.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 4
                color: parent.pressed ? "#6c7179" : parent.active ? "#535861" : "#41454d"
            }
        }
        ScrollBar.horizontal: ScrollBar {
            height: 11
            policy: editorScroll.contentWidth > editorScroll.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            contentItem: Rectangle {
                implicitHeight: 6
                radius: 4
                color: parent.pressed ? "#6c7179" : parent.active ? "#535861" : "#41454d"
            }
        }

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: true
            onWheel: function(event) {
                if (editorScroll.scrollBy(editorScroll.wheelDelta(event)))
                    event.accepted = true
            }
        }

        WheelScrollAnimator {
            id: wheelAnimator
            flickable: editorScroll
        }

        FrameAnimation {
            running: wheelAnimator.scrolling
            onTriggered: wheelAnimator.advance(frameTime)
        }
    }

    Item {
        visible: control.showLineNumbers
        x: 1
        y: 1
        width: control.gutterWidth - 1
        height: Math.max(0, control.height - 2)
        clip: true

        Text {
            x: 0
            y: 9 - editorScroll.contentY
            width: parent.width - 10
            text: control.lineNumbers()
            color: "#858585"
            font.family: "Monospace"
            font.pixelSize: Math.round(12 * languageSettings.uiScale)
            lineHeightMode: Text.FixedHeight
            lineHeight: control.lineHeight
            horizontalAlignment: Text.AlignRight
        }
    }

    SyntaxHighlighter {
        textDocument: textArea.textDocument
        language: control.reducedHighlighting ? "plain" : control.language
        lineHeight: control.lineHeight
    }

    Text {
        id: inlineCompletion
        visible: control.completionText.length > 0 && control.completionFirstLine.length > 0 && control.editorHasFocus
        z: 4
        x: Math.max(control.gutterWidth + control.textPadding, control.completionAnchorX)
        y: control.completionAnchorY
        width: Math.max(10, control.width - x - 16)
        text: control.completionFirstLine
        color: "#74808d"
        opacity: 0.96
        font: textArea.font
        elide: Text.ElideRight
        Behavior on opacity { NumberAnimation { duration: 100 } }
    }

    Rectangle {
        id: completionMenu
        visible: control.completionMenuVisible
        z: 5
        readonly property real rowHeight: 40
        readonly property real desiredHeight: Math.min(248, suggestionList.contentHeight + 8)
        width: Math.min(440, Math.max(260, control.width - control.gutterWidth - 28))
        height: desiredHeight
        x: Math.min(control.width - width - 10, Math.max(control.gutterWidth + 8, control.completionAnchorX))
        y: {
            const below = control.completionAnchorY + control.completionCursorRect.height + 4
            const above = control.completionAnchorY - height - 4
            return below + height <= control.height - 6 ? below : Math.max(6, above)
        }
        radius: 7
        color: "#252526"
        border.color: "#4a5260"
        border.width: 1
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 100 } }

        SmoothListView {
            id: suggestionList
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: control.completionItems
            currentIndex: control.completionIndex
            spacing: 1
            ScrollBar.vertical: ScrollBar { policy: suggestionList.contentHeight > suggestionList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            delegate: Item {
                required property var modelData
                required property int index
                width: suggestionList.width
                height: completionMenu.rowHeight

                Rectangle {
                    anchors.fill: parent
                    radius: 5
                    color: index === control.completionIndex ? "#094771" : itemPointer.containsMouse ? "#2a2d2e" : "transparent"
                    Behavior on color { ColorAnimation { duration: 80 } }
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 9
                    FlatIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.kind === "text" ? "search" : "code"
                        width: 15
                        height: 15
                        color: index === control.completionIndex ? "#8fc9ff" : "#59b8f5"
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 34
                        spacing: 1
                        Text {
                            width: parent.width
                            text: modelData.label || ""
                            color: "#f0f3f6"
                            font.family: textArea.font.family
                            font.pixelSize: Math.max(Math.round(11 * languageSettings.uiScale), textArea.font.pixelSize - 1)
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.detail || ""
                            color: "#9ca7b4"
                            font.pixelSize: Math.round(10 * languageSettings.uiScale)
                            elide: Text.ElideRight
                        }
                    }
                }
                MouseArea {
                    id: itemPointer
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: control.completionIndexRequested(index)
                    onClicked: control.acceptCompletionItem(index)
                }
            }
        }
    }

    onCompletionIndexChanged: {
        if (completionIndex >= 0 && completionIndex < completionItems.length)
            suggestionList.positionViewAtIndex(completionIndex, ListView.Contain)
    }

}
