import QtQuick 2.12

Rectangle {
    id: metric

    BoardLabTheme { id: theme }

    property string label: ""
    property string value: ""
    property color accent: theme.info
    property real progress: 0
    property real uiScale: 1.0
    width: (parent.width - parent.spacing * 2) / 3
    height: parent.height
    radius: Math.round(theme.radiusPanel * uiScale)
    color: theme.surface
    border.color: theme.rule
    border.width: 1
    Text { x: Math.round(12 * metric.uiScale); y: Math.round(11 * metric.uiScale); text: label; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(11 * metric.uiScale); font.weight: Font.DemiBold }
    Text { x: Math.round(12 * metric.uiScale); y: Math.round(34 * metric.uiScale); width: parent.width - Math.round(24 * metric.uiScale); text: value; color: theme.inkStrong; elide: Text.ElideRight; font.pixelSize: Math.round(20 * metric.uiScale); font.weight: Font.Bold; font.family: theme.monoFont }
    Rectangle {
        x: Math.round(12 * metric.uiScale)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(12 * metric.uiScale)
        width: parent.width - Math.round(24 * metric.uiScale)
        height: Math.max(5, Math.round(5 * metric.uiScale))
        radius: Math.round(3 * metric.uiScale)
        color: theme.rule
        Rectangle { width: parent.width * Math.max(0.04, Math.min(1, progress)); height: parent.height; radius: parent.radius; color: accent }
    }
}
