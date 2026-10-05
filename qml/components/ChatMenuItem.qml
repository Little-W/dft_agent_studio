import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// Compact menu row shared by the Chat insert and permission menus.
// Keeping the visual treatment here prevents native menu styles from
// diverging between desktop environments.
T.MenuItem {
    id: root

    property string menuIcon: "agent"
    property string subtitle: ""
    property color accentColor: "#0a84ff"
    property color iconColor: "transparent"
    property color textColor: languageSettings.darkModeEnabled ? "#e0e7ee" : "#273746"
    property color subtitleColor: languageSettings.darkModeEnabled ? "#a0acb8" : "#748394"
    property bool showCheckbox: false

    implicitHeight: subtitle.length > 0 ? 54 : 42
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4
    hoverEnabled: false

    HoverHandler { id: pointerHover }

    // The menu uses the themed checkbox below, so the template indicator
    // deliberately stays empty.
    indicator: Item {
        implicitWidth: 0
        implicitHeight: 0
        visible: false
    }

    background: HoverSurface {
        // Match SidebarItem/FileExplorerItem: one themed surface owns all
        // hover and press visuals, while MenuItem contributes only behavior.
        inactiveInset: 0
        cornerRadius: 7
        hovered: pointerHover.hovered
        selected: root.checked && root.showCheckbox
        pressed: root.down
        // Fade a themed state overlay over the stable menu surface.
        idleColor: "transparent"
        hoverColor: languageSettings.darkModeEnabled ? "#29333d" : "#e8f2fb"
        selectedColor: languageSettings.darkModeEnabled ? "#203e59" : "#e5f2fc"
        selectedHoverColor: languageSettings.darkModeEnabled ? "#294a68" : "#d8e8f7"
        pressedColor: languageSettings.darkModeEnabled ? "#31516b" : "#d8e8f7"
        idleBorderColor: "transparent"
        hoverBorderColor: languageSettings.darkModeEnabled ? "#465666" : "#c8dff2"
        selectedBorderColor: languageSettings.darkModeEnabled ? "#3975aa" : "transparent"
        transitionDuration: 120
    }

    contentItem: RowLayout {
        spacing: 10

        FlatIcon {
            // Keep every permission icon in the same fixed slot. Centering the
            // slot against the complete title/subtitle block prevents the
            // shield and rocket from appearing vertically offset.
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            visible: root.menuIcon.length > 0
            name: root.menuIcon
            color: !root.enabled ? "#aab5c0"
                : root.iconColor.a > 0 ? root.iconColor
                : root.checked ? root.accentColor : "#536779"
            width: 20
            height: 20
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: root.text
                color: !root.enabled ? "#9aa4af" : root.textColor
                font.pixelSize: Math.round(13 * languageSettings.uiScale)
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: !root.enabled ? "#b0bac3" : root.subtitleColor
                font.pixelSize: Math.round(11 * languageSettings.uiScale)
                elide: Text.ElideRight
            }
        }

        FlatIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: !root.showCheckbox && root.checkable && root.checked
            name: "check"
            color: root.accentColor
            width: 16
            height: 16
        }

        CheckBox {
            id: stateCheck
            Layout.alignment: Qt.AlignVCenter
            visible: root.showCheckbox && root.checkable
            checked: root.checked
            enabled: false
            width: 19
            height: 19
            focusPolicy: Qt.NoFocus
            indicator: Rectangle {
                width: 18
                height: 18
                radius: 5
                color: stateCheck.checked ? root.accentColor : "#ffffff"
                border.width: stateCheck.checked ? 0 : 1
                border.color: "#aab5c0"
                Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
                FlatIcon {
                    anchors.centerIn: parent
                    visible: stateCheck.checked
                    name: "check"
                    color: "#ffffff"
                    width: 13
                    height: 13
                }
            }
            contentItem: Item {}
        }
    }
}
