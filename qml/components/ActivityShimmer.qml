import QtQuick

Item {
    id: root

    property bool running: false
    property color tint: "#4e9df0"

    visible: running
    clip: true

    Rectangle {
        id: beam
        objectName: "activityShimmerBeam"
        width: Math.max(48, root.width * 0.16)
        height: root.height * 1.7
        y: (root.height - height) / 2
        rotation: -12
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0) }
            GradientStop { position: 0.5; color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.16) }
            GradientStop { position: 1.0; color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0) }
        }

        SequentialAnimation on x {
            running: root.running
            loops: Animation.Infinite
            NumberAnimation {
                from: -beam.width
                to: root.width + beam.width
                duration: 1450
                easing.type: Easing.Linear
            }
        }
    }
}
