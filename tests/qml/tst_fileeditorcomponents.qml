import QtQuick
import QtTest
import "../../qml/components" as UI

TestCase {
    name: "FileEditorComponents"
    when: windowShown
    width: 360
    height: 180

    UI.ToolbarButton {
        id: darkButton
        darkMode: true
        iconName: "refresh"
    }

    UI.SmoothHorizontalListView {
        id: relatedFiles
        y: 48
        width: 140
        height: 40
        model: 8
        spacing: 6
        delegate: Rectangle {
            required property int index
            width: 56
            height: 32
        }
    }

    function test_darkToolbarIconUsesHighContrastColor() {
        verify(Qt.colorEqual(darkButton.iconItem.color, "#f4f6f8"))
    }

    function test_relatedFilesCanScrollHorizontally() {
        tryVerify(function() { return relatedFiles.contentWidth > relatedFiles.width })
        compare(relatedFiles.contentX, relatedFiles.originX)
        verify(relatedFiles.scrollBy(-72))
        tryVerify(function() { return relatedFiles.contentX > relatedFiles.originX })
    }

}
