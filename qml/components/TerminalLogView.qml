import QtQuick
import QtQuick.Controls
import DftAgentStudio.Native 1.0
import "WheelScroll.js" as WheelScroll

Rectangle {
    id: control

    property string outputText: ""
    property string emptyText: ""
    property int fontPixelSize: 11
    property bool followTail: true
    property bool adjustingOutput: false
    signal fontPixelSizeRequested(int pixelSize)

    radius: 7
    color: "#20252c"
    border.color: "#323a44"
    clip: true

    ScrollView {
        id: outputScroll
        anchors.fill: parent
        anchors.margins: 10
        clip: true

        ScrollBar.vertical: ScrollBar {
            id: outputScrollBar
            onPositionChanged: {
                if (!control.adjustingOutput)
                    control.followTail = position + size >= 0.985
            }
            onPressedChanged: {
                if (pressed)
                    control.followTail = position + size >= 0.985
            }
        }

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: true
            onWheel: function(event) {
                const delta = WheelScroll.delta(event, false, 64)
                if (delta !== 0 && outputWheelAnimator.scrollBy(-delta))
                    event.accepted = true
            }
        }

        WheelScrollAnimator {
            id: outputWheelAnimator
            flickable: outputScroll.contentItem
        }

        FrameAnimation {
            running: outputWheelAnimator.scrolling
            onTriggered: outputWheelAnimator.advance(frameTime)
        }

        TextArea {
            id: outputArea
            width: Math.max(outputScroll.availableWidth, implicitWidth)
            text: control.outputText.length > 0 ? control.outputText : control.emptyText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            color: control.outputText.length > 0 ? "#d7e1eb" : "#8996a3"
            selectionColor: "#315f88"
            selectedTextColor: "#ffffff"
            font.family: "Monospace"
            font.pixelSize: Math.round(control.fontPixelSize * languageSettings.uiScale)
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            background: null
        }

    }

    Component.onCompleted: {
        if (outputScroll.contentItem)
            outputScroll.contentItem.pixelAligned = false
    }

    WheelZoomItem {
        anchors.fill: parent
        z: 2
        fontPixelSize: control.fontPixelSize
        onFontPixelSizeRequested: function(pixelSize) {
            control.fontPixelSizeRequested(pixelSize)
        }
    }

    onOutputTextChanged: {
        if (!followTail)
            return
        adjustingOutput = true
        Qt.callLater(function() {
            if (control.followTail) {
                outputArea.cursorPosition = outputArea.length
                outputScrollBar.position = 1 - outputScrollBar.size
            }
            adjustingOutput = false
        })
    }
}
