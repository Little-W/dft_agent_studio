import QtQuick
import QtQuick.Controls
import DftAgentStudio.Native 1.0
import "WheelScroll.js" as WheelScroll

ListView {
    id: control

    property real mouseWheelStep: 64

    clip: true
    pixelAligned: false
    boundsBehavior: Flickable.StopAtBounds
    maximumFlickVelocity: 2800
    flickDeceleration: 2500
    cacheBuffer: Math.max(240, height * 0.8)
    reuseItems: true

    function scrollBy(delta) {
        return wheelAnimator.scrollBy(-delta)
    }

    function wheelDelta(event) {
        return WheelScroll.delta(event, false, mouseWheelStep)
    }

    onDraggingChanged: if (dragging) wheelAnimator.cancel()

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

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
        interactive: true
        active: hovered || pressed || wheelAnimator.scrolling || control.moving
    }
}
