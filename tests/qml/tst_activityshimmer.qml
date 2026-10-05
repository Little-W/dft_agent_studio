import QtQuick
import QtTest
import "../../qml/components" as UI

Item {
    width: 320
    height: 72

    UI.ActivityShimmer {
        id: shimmer
        objectName: "activityShimmer"
        anchors.fill: parent
        running: true
    }

    TestCase {
        name: "ActivityShimmer"
        when: windowShown

        function test_beamMovesOnlyWhileRunning() {
            const beam = findChild(shimmer, "activityShimmerBeam")
            verify(beam !== null)
            const initialX = beam.x
            wait(180)
            verify(beam.x > initialX)

            shimmer.running = false
            const stoppedX = beam.x
            wait(120)
            compare(beam.x, stoppedX)

            shimmer.running = true
            wait(180)
            verify(beam.x > -beam.width)
        }
    }
}
