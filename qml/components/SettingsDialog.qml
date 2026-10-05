import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string title: ""
    property string description: ""
    property string iconName: "settings"
    property string acceptText: languageSettings.language === "en" ? "Save" : "保存"
    property string acceptIcon: "save"
    property string cancelText: languageSettings.language === "en" ? "Cancel" : "取消"
    property bool showFooter: true
    property bool closeAfterAccept: true
    default property alias bodyData: bodyColumn.data

    signal accepted()

    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(960, parent ? parent.width - 48 : 960)
    height: Math.min(760, parent ? parent.height - 48 : 760)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    function accept() {
        root.accepted()
        if (root.closeAfterAccept)
            root.close()
    }

    Overlay.modal: Rectangle {
        color: "#730f1720"
        opacity: root.visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }

    background: Rectangle {
        radius: 8
        color: languageSettings.darkModeEnabled ? "#20262d" : "#fbfcfd"
        border.color: languageSettings.darkModeEnabled ? "#39434e" : "#d6dee7"
        border.width: 1
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 190; easing.type: Easing.OutCubic }
            NumberAnimation { property: "y"; from: root.y + 10; to: root.y; duration: 190; easing.type: Easing.OutCubic }
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

                FlatIcon {
                    name: root.iconName
                    color: "#397fbd"
                    width: 22
                    height: 22
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: root.title
                        color: languageSettings.darkModeEnabled ? "#e5eaf0" : "#252a31"
                        font.pixelSize: Math.round(17 * languageSettings.uiScale)
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.description.length > 0
                        text: root.description
                        color: languageSettings.darkModeEnabled ? "#a0acb8" : "#667384"
                        font.pixelSize: Math.round(12 * languageSettings.uiScale)
                        elide: Text.ElideRight
                    }
                }

                ToolbarButton {
                    text: ""
                    iconName: "close"
                    implicitWidth: 32
                    implicitHeight: 32
                    tooltipText: root.cancelText
                    onClicked: root.close()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#e4e9ef"
            }
        }

        SmoothScrollView {
            id: bodyScroll
            objectName: "qaScroll_" + root.objectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftPadding: 20
            rightPadding: 20
            topPadding: 18
            bottomPadding: 18
            contentWidth: availableWidth

            ColumnLayout {
                id: bodyColumn
                width: bodyScroll.availableWidth
                spacing: 14
            }
        }

        Item {
            visible: root.showFooter
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 62 : 0

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: "#e4e9ef"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                Item { Layout.fillWidth: true }
                ToolbarButton {
                    text: root.cancelText
                    onClicked: root.close()
                }
                ToolbarButton {
                    text: root.acceptText
                    iconName: root.acceptIcon
                    emphasized: true
                    onClicked: root.accept()
                }
            }
        }
    }
}
