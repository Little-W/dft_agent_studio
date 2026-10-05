import QtQuick
import "WheelScroll.js" as WheelScroll

ListView {
    id: control

    property real mouseWheelStep: 72
    property real wheelTargetX: contentX
    property real wheelVelocityX: 0
    property bool wheelScrolling: false

    orientation: ListView.Horizontal
    clip: true
    pixelAligned: false
    boundsBehavior: Flickable.StopAtBounds
    maximumFlickVelocity: 2800
    flickDeceleration: 2500
    cacheBuffer: Math.max(240, width * 0.8)
    reuseItems: true

    function scrollBy(delta) {
        const minimum = originX - leftMargin
        const maximum = Math.max(minimum, contentWidth + originX - width + rightMargin)
        const next = Math.max(minimum, Math.min(maximum, wheelTargetX - delta))
        if (Math.abs(next - wheelTargetX) < 0.1)
            return false
        wheelTargetX = next
        if (!wheelScrolling) {
            wheelVelocityX = 0
            wheelScrolling = true
        }
        cancelFlick()
        return true
    }

    function advanceWheel(frameTime) {
        const dt = Math.max(0.001, Math.min(0.025, frameTime))
        const omega = 40
        const error = contentX - wheelTargetX
        const c = wheelVelocityX + omega * error
        const decay = Math.exp(-omega * dt)
        const nextError = (error + c * dt) * decay
        wheelVelocityX = (wheelVelocityX - omega * c * dt) * decay
        contentX = Math.max(originX - leftMargin,
            Math.min(contentWidth + originX - width + rightMargin, wheelTargetX + nextError))
        if (Math.abs(nextError) < 0.04 && Math.abs(wheelVelocityX) < 0.8) {
            contentX = wheelTargetX
            wheelVelocityX = 0
            wheelScrolling = false
        }
    }

    function wheelDelta(event) {
        return WheelScroll.delta(event, true, mouseWheelStep)
    }

    onContentXChanged: if (!wheelScrolling && !dragging) wheelTargetX = contentX
    onDraggingChanged: {
        if (dragging) {
            wheelScrolling = false
            wheelTargetX = contentX
            wheelVelocityX = 0
        }
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

    FrameAnimation {
        running: control.wheelScrolling
        onTriggered: control.advanceWheel(frameTime)
    }
}
