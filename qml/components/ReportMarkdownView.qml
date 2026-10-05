import QtQuick
import QtQuick.Controls
import "WheelScroll.js" as WheelScroll

Flickable {
    id: root

    property string html: ""
    property string fontFamily: ""
    property int textPixelSize: 13
    property bool darkMode: false
    property real mouseWheelStep: 64
    property real wheelTargetY: contentY
    property real wheelVelocityY: 0
    property bool wheelScrolling: false

    function scrollWheelBy(delta) {
        const minimum = originY - topMargin
        const maximum = Math.max(minimum, contentHeight + originY - height + bottomMargin)
        const next = Math.max(minimum, Math.min(maximum, wheelTargetY + delta))
        if (Math.abs(next - wheelTargetY) < 0.1)
            return false
        wheelTargetY = next
        if (!wheelScrolling) {
            wheelVelocityY = 0
            wheelScrolling = true
        }
        cancelFlick()
        return true
    }

    function advanceWheel(frameTime) {
        const dt = Math.max(0.001, Math.min(0.025, frameTime))
        const omega = 40
        const error = contentY - wheelTargetY
        const c = wheelVelocityY + omega * error
        const decay = Math.exp(-omega * dt)
        const nextError = (error + c * dt) * decay
        wheelVelocityY = (wheelVelocityY - omega * c * dt) * decay
        contentY = Math.max(originY - topMargin,
            Math.min(contentHeight + originY - height + bottomMargin, wheelTargetY + nextError))
        if (Math.abs(nextError) < 0.04 && Math.abs(wheelVelocityY) < 0.8) {
            contentY = wheelTargetY
            wheelVelocityY = 0
            wheelScrolling = false
        }
    }

    onContentYChanged: {
        if (!wheelScrolling && !dragging)
            wheelTargetY = contentY
    }
    onDraggingChanged: {
        if (dragging) {
            wheelScrolling = false
            wheelTargetY = contentY
            wheelVelocityY = 0
        }
    }

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    pixelAligned: false
    contentWidth: width
    contentHeight: Math.max(reportText.implicitHeight + 28, height)

    WheelHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: true
        onWheel: function(event) {
            const delta = WheelScroll.delta(event, false, root.mouseWheelStep)
            if (delta !== 0 && root.scrollWheelBy(-delta))
                event.accepted = true
        }
    }

    FrameAnimation {
        running: root.wheelScrolling
        onTriggered: root.advanceWheel(frameTime)
    }

    Rectangle {
        width: root.width
        height: root.contentHeight
        radius: 7
        color: root.darkMode ? "#20262d" : "#ffffff"
        border.color: root.darkMode ? "#39434e" : "#e1e8ef"
    }

    Text {
        id: reportText
        objectName: "reportMarkdownText"
        x: 14
        y: 14
        width: Math.max(0, root.width - 28)
        height: implicitHeight
        text: root.html
        textFormat: Text.RichText
        wrapMode: Text.Wrap
        renderType: Text.NativeRendering
        color: root.darkMode ? "#e0e7ee" : "#34404d"
        font.pixelSize: root.textPixelSize
        font.family: root.fontFamily
        font.weight: Font.Normal
    }

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }
}
