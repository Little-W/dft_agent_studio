import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property alias visualBackground: fieldFrame
    readonly property bool pointerHovered: fieldHover.hovered

    implicitHeight: 36
    leftPadding: 11
    rightPadding: 11
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: enabled ? (languageSettings.darkModeEnabled ? "#e0e7ee" : "#303b48") : "#9aa4af"
    placeholderTextColor: languageSettings.darkModeEnabled ? "#8794a1" : "#8b96a4"
    selectByMouse: true
    hoverEnabled: false
    font.pixelSize: Math.round(13 * languageSettings.uiScale)

    HoverHandler {
        id: fieldHover
    }

    background: Rectangle {
        id: fieldFrame
        radius: 7
        color: languageSettings.darkModeEnabled
            ? (!control.enabled ? "#252b32" : control.activeFocus ? "#171b20" : control.pointerHovered ? "#242c34" : "#1c2228")
            : (!control.enabled ? "#f0f2f5" : control.activeFocus ? "#ffffff" : control.pointerHovered ? "#f6f9fc" : "#ffffff")
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? "#78afe8"
            : languageSettings.darkModeEnabled ? (control.pointerHovered ? "#566c80" : "#3b4651")
            : control.pointerHovered ? "#b4cbe2" : "#d5dde6"

        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Behavior on border.width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
}
