import QtQuick

Rectangle {
    id: root
    property bool emphasized: false
    color: languageSettings.darkModeEnabled ? "#20262d" : "#ffffff"
    radius: 10
    border.width: 1
    border.color: emphasized ? (languageSettings.darkModeEnabled ? "#365a78" : "#b9d8fb")
                             : (languageSettings.darkModeEnabled ? "#39434e" : "#e3e7ec")
}
