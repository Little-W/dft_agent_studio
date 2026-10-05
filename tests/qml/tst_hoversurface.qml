import QtQuick
import QtTest
import "../../qml/components" as UI

Item {
    width: 120
    height: 44

    UI.HoverSurface {
        id: surface
        anchors.fill: parent
        idleColor: "transparent"
        hoverColor: "#eaf3fb"
        transitionDuration: 220
    }

    TestCase {
        name: "HoverSurface"
        when: windowShown

        function test_hoverFadesInAndOut() {
            compare(surface.hoverOpacity, 0)
            compare(surface.color, Qt.rgba(0, 0, 0, 0))
            surface.hovered = true
            wait(70)
            verify(surface.hoverOpacity > 0 && surface.hoverOpacity < 1)
            compare(surface.color, Qt.rgba(0, 0, 0, 0))
            tryVerify(function() { return surface.hoverOpacity > 0.99 }, 400)

            surface.hovered = false
            wait(70)
            verify(surface.hoverOpacity > 0 && surface.hoverOpacity < 1)
            compare(surface.color, Qt.rgba(0, 0, 0, 0))
            tryVerify(function() { return surface.hoverOpacity === 0 }, 400)
        }
    }
}
