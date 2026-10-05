import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string iconName: "grid"
    property color accentColor: "#0a84ff"
    property bool emphasized: false
    property bool darkMode: languageSettings.darkModeEnabled
    property bool useLabelColor: false
    property color labelColor: "#27313d"
    property color iconColor: "transparent"
    property bool outlined: false
    property color outlineColor: "#cbd6e1"
    property bool iconOnRight: false
    property color idleSurfaceColor: "transparent"
    property color hoverSurfaceColor: "transparent"
    property real surfaceRadius: 7
    // Set this instead of using the platform ToolTip attached property.  The
    // custom popup is theme-aware and can be dismissed on the first action.
    property string tooltipText: ""
    readonly property bool pointerHovered: pointerHover.hovered
    readonly property bool hasIcon: iconName.length > 0
    readonly property int horizontalInset: text.length > 0 ? 12 : 0
    readonly property int iconLabelSpacing: 6
    property alias iconItem: icon
    property alias labelItem: textLabel
    property alias visualBackground: buttonSurface
    implicitWidth: text.length > 0
        ? Math.ceil(textLabel.implicitWidth)
            + (hasIcon ? icon.width + iconLabelSpacing : 0) + horizontalInset * 2
        : 32
    implicitHeight: 32
    // Use the explicit handler below so custom and native hover states cannot
    // briefly compete while the pointer crosses child items.
    hoverEnabled: false
    leftPadding: horizontalInset
    rightPadding: horizontalInset
    topPadding: 0
    bottomPadding: 0
    HoverHandler {
        id: pointerHover
        onHoveredChanged: {
            if (!hovered)
                control.tooltipDismissed = false
        }
    }
    property bool tooltipDismissed: false

    ThemedToolTip {
        id: themedTooltip
        target: control
        message: control.tooltipText
        active: control.pointerHovered && !control.tooltipDismissed && !control.down
    }

    Connections {
        target: control
        function onPressedChanged() {
            if (control.pressed) {
                control.tooltipDismissed = true
                themedTooltip.dismiss()
            }
        }
        function onClicked() {
            control.tooltipDismissed = true
            themedTooltip.dismiss()
        }
    }
    background: Item {
        clip: true

        Rectangle {
            id: buttonSurface
            // Disabled actions remain visible so the available command layout
            // stays stable while their prerequisite input is incomplete.
            readonly property bool expanded: !control.enabled || control.emphasized || control.pointerHovered || control.down
            readonly property color surfaceColor: !control.enabled
                ? (control.darkMode ? "#2b333c" : "#e7ecf1")
                : control.down ? (control.emphasized ? "#0878dd" : (control.darkMode ? "#46505b" : "#c9d7e3"))
                : control.pointerHovered ? (control.emphasized ? "#238fee" : (control.hoverSurfaceColor.a > 0 ? control.hoverSurfaceColor : (control.darkMode ? "#343b43" : "#dce6f0")))
                : control.emphasized ? control.accentColor : control.idleSurfaceColor

            x: expanded ? 0 : 6
            y: 0
            width: expanded ? control.width : Math.max(0, control.width - 12)
            height: control.height
            radius: control.surfaceRadius
            color: surfaceColor
            border.width: control.emphasized && control.enabled && !control.outlined ? 0 : 1
            border.color: control.outlined
                ? (control.pointerHovered ? (control.darkMode ? "#718397" : "#91a9bd") : (control.darkMode ? "#566370" : control.outlineColor))
                : !control.enabled ? (control.darkMode ? "#47515c" : "#d2dae3")
                : control.down ? (control.darkMode ? "#74808d" : "#9eb3c4")
                : control.pointerHovered ? (control.darkMode ? "#59636e" : "#b3c4d2") : "#00b3c4d2"
            Behavior on color { ColorAnimation { duration: 135 } }
            Behavior on border.color { ColorAnimation { duration: 145 } }
            Behavior on x { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 105; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {
        FlatIcon {
            id: icon
            visible: control.hasIcon
            name: control.iconName
            color: !control.enabled ? (control.darkMode ? "#7e8995" : "#8b97a4")
                : control.emphasized ? "white"
                : control.iconColor.a > 0 ? control.iconColor
                : control.darkMode ? (control.enabled ? "#f4f6f8" : "#747d87")
                : (control.enabled ? "#3a4655" : "#9aa4af")
            width: 16
            height: 16
            x: control.iconOnRight
                ? Math.max(0, parent.width - width)
                : control.text.length > 0 ? 0 : Math.round((parent.width - width) / 2)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: textLabel
            visible: control.text.length > 0
            text: control.text
            color: !control.enabled ? (control.darkMode ? "#8b96a2" : "#7f8b98")
                : control.emphasized ? "white"
                : control.useLabelColor ? control.labelColor
                : control.darkMode ? (control.enabled ? "#f4f6f8" : "#747d87")
                : (control.enabled ? "#27313d" : "#9aa4af")
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
            font.weight: Font.Medium
            x: control.hasIcon && !control.iconOnRight ? icon.x + icon.width + control.iconLabelSpacing : 0
            width: Math.max(0, parent.width - x - (control.iconOnRight ? icon.width + control.iconLabelSpacing : 0))
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
}
