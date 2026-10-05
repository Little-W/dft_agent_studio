import QtQuick
import QtTest
import "../../qml/components" as UI

TestCase {
    name: "ContextUsageIndicator"
    when: windowShown
    width: 480
    height: 360

    UI.ContextUsageIndicator {
        id: usage
        totalCapacity: 65536
        inputCapacity: 16384
        systemUsed: 2045
        historyUsed: 12645
        outputUsed: 1045
    }

    function test_unusedUsesEffectiveContextWindow() {
        compare(usage.totalUsed, 15735)
        compare(usage.availableUnused, 46524)
        compare(usage.sections[3].used, 1045)
        compare(usage.sections[4].used, 46524)
        compare(usage.sections[4].capacity, usage.effectiveCapacity)
    }
}
