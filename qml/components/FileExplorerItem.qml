import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: control

    property string filePath: ""
    property string displayName: ""
    property string relativeFilePath: ""
    property string language: "config"
    property bool currentFile: false
    readonly property bool pointerHovered: filePointer.containsMouse
    readonly property bool pointerPressed: filePointer.pressed

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: 50
    hoverEnabled: false
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    function fileColor() {
        if (language === "systemverilog" || language === "verilog")
            return "#5ab4f5"
        if (language === "tcl" || language === "sdc")
            return "#d5a65a"
        if (language === "filelist")
            return "#9a8fea"
        return "#7f95a8"
    }

    background: HoverSurface {
        inactiveInset: 2
        cornerRadius: 6
        hovered: control.pointerHovered
        selected: control.currentFile
        pressed: control.pointerPressed
        idleColor: "#202225"
        hoverColor: "#2a2d2e"
        selectedColor: "#17344e"
        selectedHoverColor: "#1b405d"
        pressedColor: "#173c59"
        hoverBorderColor: "#35383d"
        selectedBorderColor: "#285d84"
        transitionDuration: 110
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 9

        Rectangle {
            Layout.preferredWidth: 3
            Layout.preferredHeight: 26
            radius: 2
            color: control.currentFile ? "#0a84ff" : "transparent"
        }
        FlatIcon {
            name: "file"
            width: 16
            height: 16
            color: control.fileColor()
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: control.displayName
                color: control.currentFile ? "#ffffff" : "#d4d4d4"
                font.pixelSize: Math.round(12 * languageSettings.uiScale)
                font.weight: control.currentFile ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: control.relativeFilePath
                color: control.currentFile ? "#9bc7eb" : "#8c929a"
                font.pixelSize: Math.round(10 * languageSettings.uiScale)
                elide: Text.ElideMiddle
            }
        }
    }

    MouseArea {
        id: filePointer
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: control.click()
    }
}
