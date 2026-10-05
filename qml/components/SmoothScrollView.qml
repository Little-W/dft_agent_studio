import QtQuick
import QtQuick.Controls
import DftAgentStudio.Native 1.0
import "WheelScroll.js" as WheelScroll

ScrollView {
    id: control

    property real mouseWheelStep: 64
    readonly property var scrollFlickable: contentItem

    function configureFlickable() {
        if (!scrollFlickable)
            return
        if (control.objectName.length > 0)
            scrollFlickable.objectName = control.objectName + "_flickable"
        scrollFlickable.pixelAligned = false
        scrollFlickable.maximumFlickVelocity = 2800
        scrollFlickable.flickDeceleration = 2500
        scrollFlickable.boundsBehavior = Flickable.StopAtBounds
    }

    function scrollBy(delta) {
        if (!scrollFlickable)
            return false

        return wheelAnimator.scrollBy(-delta)
    }

    function wheelDelta(event) {
        return WheelScroll.delta(event, false, mouseWheelStep)
    }

    onContentItemChanged: configureFlickable()
    Component.onCompleted: configureFlickable()

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
        flickable: control.scrollFlickable
    }

    FrameAnimation {
        running: wheelAnimator.scrolling
        onTriggered: wheelAnimator.advance(frameTime)
    }
}
