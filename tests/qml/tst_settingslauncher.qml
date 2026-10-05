import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import "../../qml/components" as UI

Item {
    id: root
    width: 640
    height: 240

    QtObject {
        id: languageSettings
        property real uiScale: 1
    }

    GridLayout {
        id: settingsGrid
        x: 20
        y: 20
        width: 600
        columns: 2
        columnSpacing: 12
        rowSpacing: 12

        UI.SettingsLauncher { id: projectFlow; Layout.fillWidth: true; title: "Project and flow" }
        UI.SettingsLauncher { id: synthesis; Layout.fillWidth: true; title: "Synthesis" }
        UI.SettingsLauncher { id: dft; Layout.fillWidth: true; title: "DFT" }
        UI.SettingsLauncher { id: agentTarget; Layout.fillWidth: true; title: "Agent target" }
    }

    TestCase {
        name: "SettingsLauncherLayout"
        when: true

        function test_hoverDoesNotChangeGridGeometry() {
            wait(80)
            compare(settingsGrid.children.length, 4)
            const widths = [projectFlow.width, synthesis.width, dft.width, agentTarget.width]
            verify(widths.every(function(width) { return width > 0 && width < settingsGrid.width }))

            mouseMove(agentTarget, agentTarget.width / 2, agentTarget.height / 2)
            tryVerify(function() { return agentTarget.hovered })
            wait(80)

            compare(settingsGrid.children.length, 4)
            compare(projectFlow.width, widths[0])
            compare(synthesis.width, widths[1])
            compare(dft.width, widths[2])
            compare(agentTarget.width, widths[3])
            verify([projectFlow, synthesis, dft, agentTarget].every(function(item) { return item.visible }))
        }
    }
}
