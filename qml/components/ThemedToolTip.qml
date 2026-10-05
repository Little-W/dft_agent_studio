import QtQuick
import QtQuick.Controls

// A small, platform-independent tooltip used by the application controls.
// Keeping it as a Popup avoids the native grey ToolTip style leaking into the
// light and dark surfaces used by the studio.
Popup {
    id: root

    property Item target: null
    property string message: ""
    property bool active: false
    property int delay: 430
    property bool suppressed: false

    // Keep a stable reference. Popup may reparent its visual item while it is
    // opened; mapping against `parent` directly can otherwise send the tooltip
    // off-screen on some Qt/Wayland combinations.
    readonly property Item overlayItem: Overlay.overlay
    parent: overlayItem
    modal: false
    focus: false
    padding: 0
    closePolicy: Popup.NoAutoClose
    z: 3000

    width: Math.min(320, Math.max(72, tooltipLabel.implicitWidth + 20))
    height: Math.max(30, tooltipLabel.implicitHeight + 14)

    function dismiss() {
        suppressed = true
        tooltipTimer.stop()
        close()
    }

    function reset() {
        suppressed = false
        if (active)
            tooltipTimer.restart()
    }

    function reposition() {
        if (!target || !overlayItem)
            return
        const point = target.mapToItem(overlayItem, 0, 0)
        x = Math.max(8, Math.min(overlayItem.width - width - 8,
            point.x + (target.width - width) / 2))
        const below = point.y + target.height + 7
        y = below + height <= overlayItem.height - 8
            ? below
            : Math.max(8, point.y - height - 7)
    }

    onActiveChanged: {
        if (!active) {
            tooltipTimer.stop()
            close()
        } else if (!suppressed && message.length > 0) {
            tooltipTimer.restart()
        }
    }
    onMessageChanged: {
        if (!message.length)
            close()
    }
    onOpened: Qt.callLater(reposition)
    onWidthChanged: if (visible) Qt.callLater(reposition)
    onHeightChanged: if (visible) Qt.callLater(reposition)

    Timer {
        id: tooltipTimer
        interval: root.delay
        repeat: false
        onTriggered: {
            if (root.active && !root.suppressed && root.message.length > 0) {
                root.reposition()
                root.open()
            }
        }
    }

    background: Rectangle {
        radius: 7
        color: languageSettings.darkModeEnabled ? "#252c33" : "#ffffff"
        border.width: 1
        border.color: languageSettings.darkModeEnabled ? "#46515c" : "#cbd9e6"
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            z: -1
            radius: 9
            color: "#1a64748b"
            opacity: 0.18
        }
    }

    contentItem: Text {
        id: tooltipLabel
        text: root.message
        color: languageSettings.darkModeEnabled ? "#e0e7ee" : "#273746"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        font.pixelSize: Math.round(12 * languageSettings.uiScale)
        font.weight: Font.Medium
        leftPadding: 10
        rightPadding: 10
        topPadding: 6
        bottomPadding: 6
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 135; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.98; duration: 80; easing.type: Easing.InCubic }
        }
    }
}
