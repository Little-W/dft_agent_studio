import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest

TestCase {
    name: "ChatActivityScroll"
    when: windowShown
    width: 360
    height: 360

    Window {
        id: scrollWindow
        width: 360
        height: 360
        visible: true

        ListView {
            id: activityList
            anchors.fill: parent
            clip: true
            interactive: false
            model: 24
            spacing: 4
            delegate: Item {
                id: activityRow
                required property int index
                width: activityList.width
                height: expanded ? 220 : 36
                property bool expanded: false

                Rectangle {
                    anchors.fill: parent
                    color: activityRow.index % 2 ? "#f2f7fb" : "#ffffff"
                    border.color: "#d7e3ed"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Tool group " + activityRow.index + (activityRow.expanded ? " expanded" : "")
                    }
                    TapHandler { onTapped: activityRow.expanded = !activityRow.expanded }
                }
            }

            ScrollBar.vertical: ScrollBar {
                id: activityScrollBar
                objectName: "activityScrollBar"
                policy: ScrollBar.AsNeeded
                active: true
                interactive: true
            }
        }

    }

    function test_expansionAndScrollbarDragStaySynchronized() {
        tryVerify(function() { return activityList.contentHeight > activityList.height })
        const heightBeforeExpansion = activityList.contentHeight
        mouseClick(scrollWindow, 80, 18)
        tryVerify(function() { return activityList.contentHeight > heightBeforeExpansion + 100 })

        activityList.contentY = activityList.originY
            + (activityList.contentHeight - activityList.height) * 0.72
        tryVerify(function() { return activityScrollBar.position > 0.4 })

        const oldY = activityList.contentY
        const thumbY = activityScrollBar.position * activityList.height
            + activityScrollBar.size * activityList.height / 2
        const trackX = activityList.width - activityScrollBar.width / 2
        mousePress(scrollWindow, trackX, thumbY, Qt.LeftButton)
        mouseMove(scrollWindow, trackX, activityList.height * 0.12, 100)
        mouseRelease(scrollWindow, trackX, activityList.height * 0.12, Qt.LeftButton)

        tryVerify(function() { return activityList.contentY < oldY - 100 })
        verify(activityScrollBar.position < 0.25)
    }

}
