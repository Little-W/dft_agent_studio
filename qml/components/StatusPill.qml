import QtQuick

Rectangle {
    id: root
    property string status: "ready"
    property string label: status === "ready" ? "Ready" : status
    implicitWidth: labelText.implicitWidth + 18
    implicitHeight: 22
    radius: 11
    color: languageSettings.darkModeEnabled
        ? status === "disabled" ? "#303941" : status === "verified" || status === "ready" ? "#203b2d" : status === "running" ? "#20384d" : status === "stopped" || status === "failed" ? "#482c2c" : "#443823"
        : status === "disabled" ? "#edf0f3" : status === "verified" || status === "ready" ? "#e4f6ea" : status === "running" ? "#e6f1ff" : status === "stopped" || status === "failed" ? "#fde8e8" : "#fff3d9"
    Text { id: labelText; anchors.centerIn: parent; text: root.label; color: languageSettings.darkModeEnabled ? root.status === "disabled" ? "#a0acb8" : root.status === "verified" || root.status === "ready" ? "#9bd5ad" : root.status === "running" ? "#9bc8ef" : root.status === "stopped" || root.status === "failed" ? "#e6a5a0" : "#dec17f" : root.status === "disabled" ? "#7d8997" : root.status === "verified" || root.status === "ready" ? "#1f6b3b" : root.status === "running" ? "#1768bd" : root.status === "stopped" || root.status === "failed" ? "#a64040" : "#986a13"; font.pixelSize: Math.round(11 * languageSettings.uiScale); font.weight: Font.Medium }
}
