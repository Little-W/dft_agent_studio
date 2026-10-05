import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: control

    property alias text: pathInput.text
    property alias placeholderText: pathInput.placeholderText
    property alias readOnly: pathInput.readOnly
    readonly property bool editorHasFocus: pathInput.editorHasFocus
    property bool folderMode: false
    property bool editorAvailable: false
    property string browseToolTip: folderMode ? "选择目录" : "选择文件"
    property string editToolTip: "在文件编辑器中打开"

    signal browseRequested()
    signal editRequested()

    spacing: 6
    implicitHeight: 36

    ScrollableTextField {
        id: pathInput
        Layout.fillWidth: true
        enabled: control.enabled
        readOnly: false
        selectByMouse: true
        font.pixelSize: Math.round(13 * languageSettings.uiScale)
    }

    ToolbarButton {
        text: ""
        iconName: control.folderMode ? "folder" : "file"
        implicitWidth: 32
        enabled: control.enabled
        tooltipText: control.browseToolTip
        onClicked: control.browseRequested()
    }

    ToolbarButton {
        text: ""
        iconName: "code"
        implicitWidth: 32
        visible: control.editorAvailable
        enabled: control.enabled && control.text.trim().length > 0
        tooltipText: control.editToolTip
        onClicked: control.editRequested()
    }
}
