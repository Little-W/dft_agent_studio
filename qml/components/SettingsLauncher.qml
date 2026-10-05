import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: ""
    property string description: ""
    property string iconName: "settings"
    property color accentColor: "#397fbd"
    property string statusText: ""
    property bool hovered: false
    property bool pressed: false
    signal clicked()

    implicitHeight: 76
    radius: 8
    color: languageSettings.darkModeEnabled
        ? (root.pressed ? "#34414d" : root.hovered ? "#29333d" : "#20262d")
        : (root.pressed ? "#dbe9f5" : root.hovered ? "#eaf1f7" : "#ffffff")
    border.width: 1
    border.color: languageSettings.darkModeEnabled
        ? (root.hovered ? "#53687b" : "#39434e")
        : (root.hovered ? "#b9cfe1" : "#dce3ea")

    Behavior on color { ColorAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on border.color { ColorAnimation { duration: 220; easing.type: Easing.OutCubic } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 8
            color: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.12)

            FlatIcon {
                anchors.centerIn: parent
                name: root.iconName
                color: root.accentColor
                width: 19
                height: 19
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.title
                color: languageSettings.darkModeEnabled ? "#e5eaf0" : "#252a31"
                font.pixelSize: Math.round(14 * languageSettings.uiScale)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.description
                color: languageSettings.darkModeEnabled ? "#a0acb8" : "#667384"
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                elide: Text.ElideRight
            }
        }

        Text {
            visible: root.statusText.length > 0
            text: root.statusText
            color: languageSettings.darkModeEnabled ? "#a0acb8" : "#748190"
            font.pixelSize: Math.round(11 * languageSettings.uiScale)
        }

        FlatIcon {
            name: "chevron"
            color: languageSettings.darkModeEnabled ? "#a0acb8" : "#667384"
            width: 16
            height: 16
        }
    }

    HoverHandler { onHoveredChanged: root.hovered = hovered }
    TapHandler {
        onPressedChanged: root.pressed = pressed
        onTapped: root.clicked()
    }
}
