import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: control

    required property var treeView
    required property bool isTreeNode
    required property bool expanded
    required property bool hasChildren
    required property int depth
    required property int row
    required property int column
    required property string displayName
    required property string entryPath
    required property string entryType
    required property string rootKind
    required property bool editable
    required property bool available

    property bool currentFile: false
    property string removeToolTip: languageSettings.language === "en" ? "Remove from explorer" : "从资源管理器移除"
    property string nonEditableToolTip: languageSettings.language === "en" ? "This file is not loaded as text" : "该文件不作为文本载入"
    readonly property bool isRoot: entryType === "root"
    readonly property bool isDirectory: entryType === "directory" || isRoot
    readonly property bool pointerHovered: pointerHover.hovered
    property bool tooltipDismissed: false

    signal fileActivated(string path)
    signal removeRootRequested(string path)

    implicitHeight: isRoot ? 34 : 28
    implicitWidth: treeView ? treeView.width : 260

    function entryColor() {
        if (!available)
            return "#68717c"
        if (isDirectory)
            return isRoot ? "#b9c7d7" : "#d5aa62"
        if (!editable)
            return "#6f7883"
        const lower = displayName.toLowerCase()
        if (/\.(sv|v|vh)$/.test(lower))
            return "#5ab4f5"
        if (/\.(tcl|sdc|do)$/.test(lower))
            return "#d5a65a"
        if (/\.(f|list)$/.test(lower))
            return "#9a8fea"
        if (/\.(lib|lef|tf)$/.test(lower))
            return "#59b995"
        return "#91a1b1"
    }

    HoverSurface {
        anchors.fill: parent
        inactiveInset: 2
        cornerRadius: 5
        hovered: control.pointerHovered
        selected: control.currentFile
        pressed: pointer.pressed
        idleColor: control.isRoot ? "#25272b" : "transparent"
        hoverColor: control.isRoot ? "#30343a" : "#2a2d31"
        selectedColor: "#17344e"
        selectedHoverColor: "#1b405d"
        pressedColor: "#173c59"
        hoverBorderColor: "#3b4149"
        selectedBorderColor: "#285d84"
        transitionDuration: 95
    }

    HoverHandler {
        id: pointerHover
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 7 + Math.max(0, control.depth) * 15
        anchors.rightMargin: 6
        spacing: 5

        Item {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 18

            FlatIcon {
                anchors.centerIn: parent
                visible: control.isDirectory && control.hasChildren
                name: "chevron"
                color: control.available ? "#9ba5b1" : "#656e78"
                width: 11
                height: 11
                rotation: control.expanded ? 90 : 0
            }
        }

        FlatIcon {
            name: control.isDirectory ? "folder" : "file"
            color: control.entryColor()
            width: control.isRoot ? 17 : 15
            height: control.isRoot ? 17 : 15
        }

        Text {
            Layout.fillWidth: true
            text: control.displayName
            color: !control.available ? "#737c86"
                : control.currentFile ? "#ffffff"
                : control.isRoot ? "#e2e6ea" : "#cdd1d6"
            font.pixelSize: control.isRoot ? Math.round(12 * languageSettings.uiScale) : Math.round(11 * languageSettings.uiScale)
            font.weight: control.isRoot || control.currentFile ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        ToolbarButton {
            visible: control.isRoot && control.rootKind === "additional" && control.pointerHovered
            text: ""
            iconName: "trash"
            darkMode: true
            implicitWidth: 25
            implicitHeight: 24
            tooltipText: control.removeToolTip
            onClicked: control.removeRootRequested(control.entryPath)
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        anchors.rightMargin: control.isRoot && control.rootKind === "additional" && control.pointerHovered ? 29 : 0
        hoverEnabled: false
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            control.tooltipDismissed = true
            if (control.isDirectory)
                control.treeView.toggleExpanded(control.row)
            else
                control.fileActivated(control.entryPath)
        }
    }

    ThemedToolTip {
        id: pathTooltip
        target: control
        delay: 700
        message: control.entryPath + (!control.editable && !control.isDirectory
            ? "\n" + control.nonEditableToolTip : "")
        active: control.pointerHovered && !control.isRoot && !control.tooltipDismissed
    }

    Connections {
        target: control
        function onPointerHoveredChanged() {
            if (!control.pointerHovered)
                control.tooltipDismissed = false
        }
    }
}
