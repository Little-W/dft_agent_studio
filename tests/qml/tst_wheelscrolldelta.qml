import QtQuick
import QtTest
import "../../qml/components/WheelScroll.js" as WheelScroll

TestCase {
    name: "WheelScrollDelta"

    function wheel(pixelX, pixelY, angleX, angleY) {
        return {
            pixelDelta: { x: pixelX, y: pixelY },
            angleDelta: { x: angleX, y: angleY }
        }
    }

    function test_angleResolutionKeepsSameTravel() {
        const coarse = WheelScroll.delta(wheel(0, 0, 0, -120), false, 64)
        let fine = 0
        for (let i = 0; i < 8; ++i)
            fine += WheelScroll.delta(wheel(0, 0, 0, -15), false, 64)
        verify(Math.abs(coarse - fine) < 0.001)
        compare(coarse, -64)
    }

    function test_pixelResolutionUsesAccumulatedDistance() {
        let total = 0
        for (let i = 0; i < 16; ++i)
            total += WheelScroll.delta(wheel(0, -4, 0, 0), false, 64)
        compare(total, -64)
    }

    function test_horizontalWheelUsesHorizontalAxisOrVerticalFallback() {
        compare(WheelScroll.delta(wheel(-12, 0, -30, 0), true, 64), -12)
        compare(WheelScroll.delta(wheel(0, -12, 0, -30), true, 64), -12)
    }
}
