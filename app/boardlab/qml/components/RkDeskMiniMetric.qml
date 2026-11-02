import QtQuick 2.12

Rectangle {
    id: metric
    BoardLabTheme { id: theme }

    property string label: ""
    property string value: ""
    property color accent: theme.accent
    property real uiScale: 1.0
    width: Math.round(60 * uiScale)
    height: Math.round(42 * uiScale)
    radius: Math.round(theme.radiusControl * uiScale)
    color: theme.withAlpha(theme.instrumentInk, 0.10)
    border.color: theme.withAlpha(theme.instrumentInk, 0.16)
    border.width: 1
    Rectangle {
        x: Math.round(8 * metric.uiScale)
        y: Math.round(9 * metric.uiScale)
        width: Math.max(4, Math.round(5 * metric.uiScale))
        height: Math.round(24 * metric.uiScale)
        radius: Math.round(3 * metric.uiScale)
        color: accent
    }
    Text {
        x: Math.round(20 * metric.uiScale)
        y: Math.round(7 * metric.uiScale)
        text: label
        color: theme.instrumentMuted
        font.family: theme.bodyFont
        font.pixelSize: Math.round(10 * metric.uiScale)
    }
    Text {
        x: Math.round(20 * metric.uiScale)
        y: Math.round(20 * metric.uiScale)
        text: value
        color: theme.instrumentInk
        font.pixelSize: Math.round(13 * metric.uiScale)
        font.weight: Font.Bold
        font.family: theme.monoFont
    }
}
