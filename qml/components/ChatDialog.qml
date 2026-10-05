import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shared dialog shell for Chat session/workspace and review surfaces. Keeping the
// chrome here prevents a native Dialog style from changing between platforms.
Popup {
    id: root

    property string title: ""
    property string description: ""
    property string iconName: "agent"
    property string closeText: languageSettings.language === "en" ? "Close" : "关闭"
    property bool showFooter: false
    property bool fitContent: false
    default property alias bodyData: bodyColumn.data

    signal accepted()

    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(980, parent ? parent.width - 72 : 980)
    readonly property real naturalHeight: (root.description.length > 0 ? 76 : 62)
        + bodyColumn.implicitHeight + bodyScroll.topPadding + bodyScroll.bottomPadding
        + (root.showFooter ? 62 : 0)
    height: root.fitContent
        ? Math.min(parent ? parent.height - 72 : 700, Math.max(132, root.naturalHeight))
        : Math.min(700, parent ? parent.height - 72 : 700)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    function accept() {
        root.accepted()
        root.close()
    }

    Overlay.modal: Rectangle {
        color: "#730f1720"
        opacity: root.visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }

    background: Rectangle {
        radius: 10
        color: languageSettings.darkModeEnabled ? "#20262d" : "#fbfcfd"
        border.color: languageSettings.darkModeEnabled ? "#39434e" : "#cbd7e2"
        border.width: 1
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 190; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.985; duration: 110; easing.type: Easing.InCubic }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.description.length > 0 ? 76 : 62
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                spacing: 12
                FlatIcon { name: root.iconName; color: "#397fbd"; width: 22; height: 22 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { Layout.fillWidth: true; text: root.title; color: languageSettings.darkModeEnabled ? "#e5eaf0" : "#252a31"; font.pixelSize: Math.round(17 * languageSettings.uiScale); font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; visible: root.description.length > 0; text: root.description; color: languageSettings.darkModeEnabled ? "#a0acb8" : "#667384"; font.pixelSize: Math.round(12 * languageSettings.uiScale); elide: Text.ElideRight }
                }
                ToolbarButton {
                    text: ""
                    iconName: "close"
                    implicitWidth: 32
                    implicitHeight: 32
                    tooltipText: root.closeText
                    onClicked: root.close()
                }
            }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: languageSettings.darkModeEnabled ? "#39434e" : "#e4e9ef" }
        }

        SmoothScrollView {
            id: bodyScroll
            Layout.fillWidth: true
            Layout.fillHeight: !root.fitContent
            Layout.preferredHeight: root.fitContent
                ? bodyColumn.implicitHeight + topPadding + bottomPadding : 0
            Layout.minimumHeight: 0
            leftPadding: 20
            rightPadding: 20
            topPadding: 18
            bottomPadding: 18
            contentWidth: availableWidth
            ColumnLayout {
                id: bodyColumn
                width: bodyScroll.availableWidth
                spacing: 12
            }
        }

        Item {
            visible: root.showFooter
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 62 : 0
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: languageSettings.darkModeEnabled ? "#39434e" : "#e4e9ef" }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                ToolbarButton { Layout.alignment: Qt.AlignRight; text: root.closeText; onClicked: root.close() }
            }
        }
    }
}
