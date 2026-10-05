import QtQuick

Rectangle {
    id: control

    property bool hovered: false
    property bool selected: false
    property bool pressed: false
    property real inactiveInset: 0
    property real cornerRadius: 7
    property color idleColor: "transparent"
    property color hoverColor: "#eaf3fb"
    property color selectedColor: "#d8eaff"
    property color selectedHoverColor: "#cce3ff"
    property color pressedColor: "#d7e9f8"
    property color idleBorderColor: "transparent"
    property real idleBorderWidth: 0
    property color hoverBorderColor: "transparent"
    property color selectedBorderColor: "transparent"
    property int transitionDuration: 220
    readonly property bool active: control.hovered || control.selected || control.pressed
    readonly property real hoverOpacity: stateWash.opacity

    x: control.active ? 0 : control.inactiveInset
    width: parent ? Math.max(0, parent.width - (control.active ? 0 : control.inactiveInset * 2)) : 0
    height: parent ? parent.height : 0
    radius: control.cornerRadius
    color: control.idleColor
    border.width: 0

    Rectangle {
        id: stateWash
        anchors.fill: parent
        radius: control.cornerRadius
        color: control.pressed ? control.pressedColor
             : control.selected && control.hovered ? control.selectedHoverColor
             : control.selected ? control.selectedColor
             : control.hoverColor
        opacity: control.active ? 1 : 0
        border.width: control.hovered || control.selected ? 1 : control.idleBorderWidth
        border.color: control.selected ? control.selectedBorderColor
                    : control.hovered ? control.hoverBorderColor : control.idleBorderColor

        Behavior on opacity {
            NumberAnimation { duration: control.transitionDuration; easing.type: Easing.OutCubic }
        }
        Behavior on color {
            ColorAnimation { duration: control.transitionDuration; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            ColorAnimation { duration: control.transitionDuration; easing.type: Easing.OutCubic }
        }
    }
}
