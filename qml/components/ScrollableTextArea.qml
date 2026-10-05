import QtQuick
import QtQuick.Controls
import "WheelScroll.js" as WheelScroll

Flickable {
    id: control

    property alias text: editor.text
    property alias readOnly: editor.readOnly
    property alias selectByMouse: editor.selectByMouse
    property alias persistentSelection: editor.persistentSelection
    property alias wrapMode: editor.wrapMode
    property alias textFormat: editor.textFormat
    property alias font: editor.font
    property alias color: editor.color
    property alias placeholderText: editor.placeholderText
    readonly property bool editorHasFocus: editor.activeFocus
    property real mouseWheelStep: 64

    clip: true
    interactive: false
    pixelAligned: false
    boundsBehavior: Flickable.StopAtBounds
    maximumFlickVelocity: 2800
    flickDeceleration: 2500
    contentWidth: Math.max(width, textMetrics.implicitWidth + editor.leftPadding + editor.rightPadding)
    contentHeight: Math.max(height, textMetrics.implicitHeight + editor.topPadding + editor.bottomPadding)

    function focusEditor() {
        editor.forceActiveFocus()
    }

    function scrollBy(delta) {
        return wheelAnimator.scrollBy(-delta)
    }

    function wheelDelta(event) {
        return WheelScroll.delta(event, false, mouseWheelStep)
    }

    onDraggingChanged: if (dragging) wheelAnimator.cancel()

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: languageSettings.darkModeEnabled
            ? (control.editorHasFocus ? "#171b20" : "#1c2228")
            : (control.editorHasFocus ? "#ffffff" : "#f8fafc")
        border.width: control.editorHasFocus ? 2 : 1
        border.color: control.editorHasFocus ? "#78afe8" : (languageSettings.darkModeEnabled ? "#3b4651" : "#d5dde6")
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    Text {
        id: textMetrics
        visible: false
        text: editor.text.length > 0 ? editor.text : editor.placeholderText
        font: editor.font
        wrapMode: Text.NoWrap
    }

    StyledTextArea {
        id: editor
        width: control.contentWidth
        height: control.contentHeight
        background: Item { }
    }

    ScrollBar.vertical: ScrollBar {
        policy: control.contentHeight > control.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }
    ScrollBar.horizontal: ScrollBar {
        policy: control.contentWidth > control.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }

    WheelHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        onWheel: function(event) {
            if (control.scrollBy(control.wheelDelta(event)))
                event.accepted = true
        }
    }

    WheelScrollAnimator {
        id: wheelAnimator
        flickable: control
    }

    FrameAnimation {
        running: wheelAnimator.scrolling
        onTriggered: wheelAnimator.advance(frameTime)
    }
}
