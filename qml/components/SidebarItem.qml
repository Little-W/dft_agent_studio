import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: control
    property string iconName: "grid"
    property bool selected: false
    property bool isCurrentProject: false
    property int badge: 0
    property color tint: "#0a84ff"
    property real labelProgress: 1.0
    property string tooltipText: ""
    readonly property bool pointerHovered: itemPointer.containsMouse
    readonly property bool pointerPressed: itemPointer.pressed
    property alias iconItem: itemIcon
    property alias labelItem: itemLabel
    property alias visualBackground: itemHighlight
    property alias currentMarker: projectMarker
    implicitHeight: 36
    width: ListView.view ? ListView.view.width : (parent ? parent.width : implicitWidth)
    // A full-row MouseArea is the sole pointer state source. This keeps the
    // state stable while the pointer crosses the icon and text children.
    hoverEnabled: false
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    leftPadding: 10
    rightPadding: 10 + (isCurrentProject ? 16 * labelProgress : 0)
    topPadding: 0
    bottomPadding: 0
    background: HoverSurface {
        id: itemHighlight
        inactiveInset: 2
        hovered: control.pointerHovered
        selected: control.selected
        pressed: control.pointerPressed
        idleColor: languageSettings.darkModeEnabled ? "transparent" : "#f0f2f5"
        hoverColor: languageSettings.darkModeEnabled ? "#29333d" : "#eaf3fb"
        selectedColor: languageSettings.darkModeEnabled ? "#203e59" : "#d8eaff"
        selectedHoverColor: languageSettings.darkModeEnabled ? "#294a68" : "#cce3ff"
        pressedColor: languageSettings.darkModeEnabled ? "#34516c" : "#bddbfa"
        hoverBorderColor: languageSettings.darkModeEnabled ? "#465666" : "#c9d1da"
        selectedBorderColor: languageSettings.darkModeEnabled ? "#3975aa" : "#92c3f5"
    }
    contentItem: RowLayout {
        spacing: 9 * control.labelProgress
        FlatIcon {
            id: itemIcon
            name: control.iconName
            color: control.selected ? "#0a84ff" : (languageSettings.darkModeEnabled ? "#9eabb8" : "#667384")
            scale: 0.94 + 0.06 * control.labelProgress
            Layout.preferredWidth: 17
            Layout.preferredHeight: 17
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            id: itemLabel
            visible: control.labelProgress > 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: control.text
            opacity: control.labelProgress
            transform: Translate { x: -5 * (1 - control.labelProgress) }
            color: languageSettings.darkModeEnabled
                ? (control.selected ? "#c5e2ff" : "#d4dce4")
                : (control.selected ? "#083d75" : "#303b48")
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        RowLayout {
            visible: control.badge > 0 && control.labelProgress > 0.45
            opacity: Math.min(1, control.labelProgress / 0.45)
            Layout.alignment: Qt.AlignVCenter
            spacing: 5
            Rectangle {
                visible: control.badge > 0
                Layout.preferredWidth: Math.max(18, badgeText.width + 10)
                Layout.preferredHeight: 18
                radius: 9
                color: languageSettings.darkModeEnabled ? (control.selected ? "#294a68" : "#303941") : (control.selected ? "#b6d7ff" : "#e5e9ee")
                Text { id: badgeText; anchors.centerIn: parent; text: control.badge; color: languageSettings.darkModeEnabled ? "#c3ced8" : "#516070"; font.pixelSize: Math.round(11 * languageSettings.uiScale); font.weight: Font.Medium }
            }
        }
    }
    Rectangle {
        id: projectMarker
        visible: control.isCurrentProject
        anchors.right: parent.right
        anchors.rightMargin: 4 + 6 * control.labelProgress
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: control.selected ? "#0a72e8" : "#0a84ff"
    }

    MouseArea {
        id: itemPointer
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            control.tooltipDismissed = true
            control.click()
        }
    }

    property bool tooltipDismissed: false
    ThemedToolTip {
        id: sidebarTooltip
        target: control
        message: control.tooltipText
        active: control.pointerHovered && !control.tooltipDismissed && control.labelProgress < 0.4
    }

    Connections {
        target: control
        function onPointerHoveredChanged() {
            if (!control.pointerHovered)
                control.tooltipDismissed = false
        }
    }
}
