import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    property alias visualBackground: comboFrame
    property alias indicatorIcon: chevronIcon
    property alias popupSurface: popupFrame
    readonly property bool pointerHovered: comboHover.hovered

    implicitHeight: 36
    hoverEnabled: false
    leftPadding: 12
    rightPadding: 35
    topPadding: 0
    bottomPadding: 0

    HoverHandler {
        id: comboHover
    }

    // Popups live in the application overlay. Close them when their page or
    // dialog is hidden so a settings menu cannot remain above another page.
    onVisibleChanged: {
        if (!visible && popup.visible)
            popup.close()
    }

    function commitIndex(index) {
        if (index < 0 || index >= count)
            return
        currentIndex = index
        activated(index)
        popup.close()
    }

    contentItem: Text {
        verticalAlignment: Text.AlignVCenter
        text: control.displayText
        color: control.enabled ? (languageSettings.darkModeEnabled ? "#e0e7ee" : "#303b48") : "#9aa4af"
        font.pixelSize: Math.round(13 * languageSettings.uiScale)
        elide: Text.ElideRight
    }

    indicator: Item {
        width: 32
        height: control.height
        x: control.width - width

        FlatIcon {
            id: chevronIcon
            anchors.centerIn: parent
            name: "chevron"
            width: 14
            height: 14
            rotation: control.popup.visible ? 270 : 90
            color: control.enabled ? (languageSettings.darkModeEnabled ? "#a0acb8" : "#627184") : "#a9b2bd"
            Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }
    }

    background: Rectangle {
        id: comboFrame
        radius: 7
        color: languageSettings.darkModeEnabled
            ? (!control.enabled ? "#252b32" : control.popup.visible ? "#171b20" : control.pointerHovered ? "#242c34" : "#1c2228")
            : (!control.enabled ? "#f0f2f5" : control.popup.visible ? "#ffffff" : control.pointerHovered ? "#f3f7fc" : "#ffffff")
        border.width: control.activeFocus || control.popup.visible ? 2 : 1
        border.color: control.activeFocus || control.popup.visible ? "#78afe8"
            : languageSettings.darkModeEnabled ? (control.pointerHovered ? "#566c80" : "#3b4651")
            : control.pointerHovered ? "#aac5df" : "#d5dde6"
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Behavior on border.width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    delegate: Item {
        id: optionDelegate
        required property int index
        width: ListView.view.width
        height: 34
        readonly property bool selected: control.currentIndex === index
        readonly property bool pointerHovered: optionPointer.containsMouse
        property alias visualBackground: optionFrame

        Rectangle {
            id: optionFrame
            x: 3
            width: parent.width - 6
            height: parent.height
            radius: 7
            color: optionPointer.pressed ? (languageSettings.darkModeEnabled ? "#34516c" : "#bddbfa")
                : optionDelegate.selected && optionDelegate.pointerHovered ? (languageSettings.darkModeEnabled ? "#34516c" : "#c9e3ff")
                : optionDelegate.selected ? (languageSettings.darkModeEnabled ? "#293e52" : "#d8eaff")
                : optionDelegate.pointerHovered ? (languageSettings.darkModeEnabled ? "#29333d" : "#e7eef5")
                : "transparent"
        }
        Text {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 30
            verticalAlignment: Text.AlignVCenter
            text: control.textAt(index)
            color: languageSettings.darkModeEnabled
                ? (optionDelegate.selected ? "#b9dcff" : "#e0e7ee")
                : (optionDelegate.selected ? "#073f78" : "#303b48")
            font.pixelSize: Math.round(13 * languageSettings.uiScale)
            font.weight: optionDelegate.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        FlatIcon {
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            visible: optionDelegate.selected
            name: "check"
            width: 14
            height: 14
            color: "#0a84ff"
        }
        MouseArea {
            id: optionPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: control.commitIndex(optionDelegate.index)
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 5
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        implicitHeight: Math.min(listView.implicitHeight + topPadding + bottomPadding, 198)

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90; easing.type: Easing.OutCubic }
        }

        contentItem: SmoothListView {
            id: listView
            clip: true
            pixelAligned: false
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            highlight: null
            focus: true
            Keys.onReturnPressed: control.commitIndex(control.highlightedIndex)
            Keys.onEnterPressed: control.commitIndex(control.highlightedIndex)
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            id: popupFrame
            radius: 10
            color: languageSettings.darkModeEnabled ? "#20262d" : "#ffffff"
            border.color: languageSettings.darkModeEnabled ? "#39434e" : "#cbd6e1"
            border.width: 1
        }
    }
}
