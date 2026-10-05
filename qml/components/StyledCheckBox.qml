import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    property color accentColor: "#0a84ff"
    property bool compact: false
    property alias visualIndicator: checkFrame
    readonly property bool pointerHovered: checkHover.hovered
    readonly property int indicatorSize: compact ? 16 : 18

    implicitWidth: contentLabel.implicitWidth + indicatorSize + spacing
    implicitHeight: compact ? 26 : 30
    spacing: 9
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: false

    HoverHandler {
        id: checkHover
    }

    indicator: Rectangle {
        id: checkFrame
        x: control.leftPadding
        y: Math.round((control.height - height) / 2)
        width: control.indicatorSize
        height: control.indicatorSize
        radius: compact ? 4 : 5
        color: !control.enabled ? (languageSettings.darkModeEnabled ? "#29313a" : "#edf0f3")
              : control.down ? (control.checked ? "#006edc" : (languageSettings.darkModeEnabled ? "#313c48" : "#e4edf7"))
              : control.checked ? control.accentColor
              : control.pointerHovered ? (languageSettings.darkModeEnabled ? "#29333d" : "#f2f7fd")
              : languageSettings.darkModeEnabled ? "#1c2228" : "#ffffff"
        border.width: control.checked ? 0 : control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#78afe8"
            : languageSettings.darkModeEnabled ? (control.pointerHovered ? "#566c80" : "#4a5661")
            : control.pointerHovered ? "#9bb8d4" : "#aab5c0"

        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on border.color { ColorAnimation { duration: 110 } }
        Behavior on border.width { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

        Canvas {
            id: checkMark
            anchors.centerIn: parent
            width: parent.width
            height: parent.height
            visible: control.checked || control.checkState === Qt.PartiallyChecked
            antialiasing: true

            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#ffffff"
                ctx.lineWidth = Math.max(1.7, width * 0.14)
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                if (control.checkState === Qt.PartiallyChecked) {
                    ctx.moveTo(width * 0.28, height * 0.5)
                    ctx.lineTo(width * 0.72, height * 0.5)
                } else {
                    ctx.moveTo(width * 0.23, height * 0.53)
                    ctx.lineTo(width * 0.42, height * 0.72)
                    ctx.lineTo(width * 0.78, height * 0.3)
                }
                ctx.stroke()
            }

            Connections {
                target: control
                function onCheckedChanged() { checkMark.requestPaint() }
                function onCheckStateChanged() { checkMark.requestPaint() }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
    }

    contentItem: Text {
        id: contentLabel
        leftPadding: control.indicatorSize + control.spacing
        rightPadding: 0
        text: control.text
        color: !control.enabled ? "#9aa4af"
            : languageSettings.darkModeEnabled ? (control.checked ? "#e1e8ef" : "#bcc6d0")
            : control.checked ? "#243242" : "#445365"
        font.pixelSize: control.compact ? Math.round(12 * languageSettings.uiScale) : Math.round(13 * languageSettings.uiScale)
        font.weight: control.checked ? Font.Medium : Font.Normal
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
