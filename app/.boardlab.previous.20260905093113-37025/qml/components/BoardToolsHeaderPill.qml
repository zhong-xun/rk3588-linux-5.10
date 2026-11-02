import QtQuick 2.12

Rectangle {
    id: pill
    BoardLabTheme { id: theme }
    property string label: ""
    property string value: ""
    property color accent: theme.accent
    property real uiScale: 1.0
    width: Math.round(116 * uiScale)
    height: Math.round(38 * uiScale)
    radius: Math.round(theme.radiusControl * uiScale)
    color: theme.instrumentRaised
    border.color: theme.withAlpha(theme.instrumentInk, 0.16)
    border.width: 1
    Rectangle { x: Math.round(8 * pill.uiScale); width: Math.round(5 * pill.uiScale); height: Math.round(5 * pill.uiScale); anchors.verticalCenter: parent.verticalCenter; radius: width / 2; color: accent }
    Text { x: Math.round(22 * pill.uiScale); anchors.verticalCenter: parent.verticalCenter; text: label; color: theme.instrumentMuted; font.pixelSize: Math.round(10 * pill.uiScale); font.weight: Font.DemiBold }
    Text { anchors.right: parent.right; anchors.rightMargin: Math.round(12 * pill.uiScale); anchors.verticalCenter: parent.verticalCenter; text: value; color: theme.instrumentInk; font.pixelSize: Math.round(14 * pill.uiScale); font.weight: Font.Bold; font.family: theme.monoFont }
}
