import QtQuick 2.12

Rectangle {
    id: chip
    BoardLabTheme { id: theme }

    property string label: ""
    property string value: ""
    property color accent: theme.accent
    property real uiScale: 1.0

    implicitWidth: Math.round(100 * uiScale)
    height: Math.round(28 * uiScale)
    radius: Math.round(theme.radiusControl * uiScale)
    color: theme.surfaceSoft
    border.color: theme.rule
    border.width: 1

    Rectangle {
        x: Math.round(8 * chip.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(4, Math.round(5 * chip.uiScale))
        height: Math.round(14 * chip.uiScale)
        radius: Math.round(3 * chip.uiScale)
        color: accent
    }

    Text {
        x: Math.round(18 * chip.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - Math.round(54 * chip.uiScale)
        text: label
        color: theme.muted
        elide: Text.ElideRight
        font.family: theme.bodyFont
        font.pixelSize: Math.round(10 * chip.uiScale)
        font.weight: Font.Bold
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: Math.round(8 * chip.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.round(40 * chip.uiScale)
        text: value
        color: theme.inkStrong
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        font.family: theme.monoFont
        font.pixelSize: Math.round(10 * chip.uiScale)
        font.weight: Font.Bold
    }
}
