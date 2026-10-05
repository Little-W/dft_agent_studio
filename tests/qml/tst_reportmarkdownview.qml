import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/components" as UI

TestCase {
    name: "ReportMarkdownView"
    when: windowShown
    width: 420
    height: 300

    Window {
        id: reportWindow
        width: 420
        height: 300
        visible: true

        UI.ReportMarkdownView {
            id: report
            anchors.fill: parent
            html: "<p><strong>Report heading</strong></p>" + Array(160).join("<p>Evidence row with <strong>bold result</strong> and details.</p>")
            fontFamily: "MiSans"
        }
    }

    function test_longReportKeepsTopVisibleAcrossRepeatedRoundTrips() {
        tryVerify(function() { return report.contentHeight > report.height })
        const initialHeight = report.contentHeight
        wait(100)
        const textItem = findChild(report, "reportMarkdownText")
        verify(textItem.visible)
        verify(textItem.parent.visible)

        for (let round = 0; round < 2; ++round) {
            report.contentY = report.contentHeight - report.height
            tryVerify(function() { return report.contentY > 0 })
            wait(100)
            report.contentY = report.originY
            tryVerify(function() { return report.contentY === report.originY })
            wait(100)
            const image = grabImage(report)
            let darkPixelsInHeading = 0
            for (let y = 14; y < 48; ++y) {
                for (let x = 18; x < 230; ++x) {
                    if (image.red(x, y) < 150 && image.green(x, y) < 150 && image.blue(x, y) < 150)
                        ++darkPixelsInHeading
                }
            }
            verify(darkPixelsInHeading > 0, "heading remains painted at the top after scroll round-trip " + (round + 1))
        }

        compare(report.contentHeight, initialHeight)
        verify(findChild(report, "reportMarkdownText").text.indexOf("Report heading") >= 0)
    }
}
