import QtQuick
import QtQuick.Controls

TextArea {
    id: control

    property alias visualBackground: areaFrame
    readonly property bool pointerHovered: areaHover.hovered

    leftPadding: 11
    rightPadding: 11
    topPadding: 9
    bottomPadding: 9
    color: enabled ? (languageSettings.darkModeEnabled ? "#e0e7ee" : "#303b48") : "#9aa4af"
    placeholderTextColor: languageSettings.darkModeEnabled ? "#8794a1" : "#8b96a4"
    selectByMouse: true
    hoverEnabled: false

    HoverHandler {
        id: areaHover
    }

    background: Rectangle {
        id: areaFrame
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
