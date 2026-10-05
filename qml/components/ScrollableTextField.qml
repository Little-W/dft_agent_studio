import QtQuick
import QtQuick.Controls
import "WheelScroll.js" as WheelScroll

Control {
    id: control

    property alias text: pathInput.text
    property alias readOnly: pathInput.readOnly
    property alias selectByMouse: pathInput.selectByMouse
    property alias visualBackground: fieldFrame
    property string placeholderText: ""
    readonly property bool editorHasFocus: pathInput.activeFocus
    readonly property bool pointerHovered: fieldHover.hovered
    readonly property real horizontalOffset: viewport.contentX <= 0.5 ? 0 : viewport.contentX
    readonly property real horizontalExtent: Math.max(0, viewport.maximumContentX)
    property real mouseWheelStep: 72
    property color editableTextColor: languageSettings.darkModeEnabled ? "#e0e7ee" : "#26313d"
    property color inactiveTextColor: languageSettings.darkModeEnabled ? "#8794a1" : "#8b96a4"

    signal editingFinished()

    implicitHeight: 36
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: false
    focusPolicy: Qt.NoFocus

    function keepCursorVisible() {
        if (!pathInput.activeFocus) {
            viewport.contentX = viewport.originX
            return
        }
        if (pathInput.cursorPosition <= 0) {
            viewport.contentX = viewport.originX
            return
        }
        const cursorLeft = pathInput.x + pathInput.cursorRectangle.x
        const cursorRight = cursorLeft + Math.max(2, pathInput.cursorRectangle.width)
        const leftLimit = viewport.contentX + 8
        const rightLimit = viewport.contentX + viewport.width - 8
        if (cursorLeft < leftLimit)
            viewport.contentX = Math.max(viewport.originX, cursorLeft - 8)
        else if (cursorRight > rightLimit)
            viewport.contentX = Math.min(viewport.maximumContentX, cursorRight - viewport.width + 8)
    }

    HoverHandler {
        id: fieldHover
    }

    background: Rectangle {
        id: fieldFrame
        radius: 7
        color: languageSettings.darkModeEnabled
            ? (!control.enabled ? "#252b32" : control.editorHasFocus ? "#171b20" : control.pointerHovered ? "#242c34" : "#1c2228")
            : (!control.enabled ? "#f0f2f5" : control.editorHasFocus ? "#ffffff" : control.pointerHovered ? "#f6f9fc" : "#ffffff")
        border.width: control.editorHasFocus ? 2 : 1
        border.color: control.editorHasFocus ? "#78afe8"
            : languageSettings.darkModeEnabled ? (control.pointerHovered ? "#566c80" : "#3b4651")
            : control.pointerHovered ? "#b4cbe2" : "#d5dde6"

        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Behavior on border.width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    contentItem: Flickable {
        id: viewport

        readonly property real maximumContentX: Math.max(originX, contentWidth - width + originX)

        clip: true
        interactive: false
        pixelAligned: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, textMetrics.advanceWidth + 24)
        contentHeight: height

        onWidthChanged: Qt.callLater(control.keepCursorVisible)

        TextMetrics {
            id: textMetrics
            font: control.font
            text: pathInput.text.length > 0 ? pathInput.text : " "
        }

        TextInput {
            id: pathInput
            x: 11
            width: Math.max(viewport.width - 22, textMetrics.advanceWidth + 2)
            height: viewport.height - 3
            verticalAlignment: TextInput.AlignVCenter
            color: control.enabled && !pathInput.readOnly
                ? control.editableTextColor : control.inactiveTextColor
            selectionColor: "#b8dafe"
            selectedTextColor: "#17202a"
            clip: false
            selectByMouse: true
            font: control.font
            Keys.priority: Keys.BeforeItem

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home) {
                    cursorPosition = 0
                    viewport.contentX = viewport.originX
                    event.accepted = true
                    Qt.callLater(control.keepCursorVisible)
                }
            }

            onCursorRectangleChanged: Qt.callLater(control.keepCursorVisible)
            onCursorPositionChanged: Qt.callLater(control.keepCursorVisible)
            onEditingFinished: control.editingFinished()
        }

        Text {
            x: 11
            width: Math.max(0, viewport.width - 22)
            anchors.verticalCenter: parent.verticalCenter
            visible: pathInput.text.length === 0 && !pathInput.activeFocus
            text: control.placeholderText
            color: "#8b96a4"
            font: control.font
            elide: Text.ElideRight
        }

        ScrollBar.horizontal: ScrollBar {
            policy: viewport.contentWidth > viewport.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: true
            onWheel: function(event) {
                const rawDelta = WheelScroll.delta(event, true, control.mouseWheelStep)
                if (rawDelta === 0 || viewport.maximumContentX <= viewport.originX)
                    return
                if (wheelAnimator.scrollBy(-rawDelta))
                    event.accepted = true
            }
        }

        WheelScrollAnimator {
            id: wheelAnimator
            flickable: viewport
            horizontal: true
        }

        FrameAnimation {
            running: wheelAnimator.scrolling
            onTriggered: wheelAnimator.advance(frameTime)
        }
    }
}
