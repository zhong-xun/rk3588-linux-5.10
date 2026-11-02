import QtQuick 2.12

Rectangle {
    id: hero
    BoardLabTheme { id: theme }
    property string label: ""
    property string value: ""
    property color accent: theme.accent
    property real uiScale: 1.0
    width: parent.width
    height: Math.round(104 * hero.uiScale)
    color: theme.surfaceSoft
    border.color: theme.rule
    border.width: 1
    radius: Math.min(theme.radiusPanel, Math.round(theme.radiusPanel * hero.uiScale))

    Rectangle {
        x: Math.round(16 * hero.uiScale)
        y: Math.round(20 * hero.uiScale)
        width: Math.max(6, Math.round(7 * hero.uiScale))
        height: width
        radius: width / 2
        color: hero.accent
    }

    Text {
        x: Math.round(30 * hero.uiScale)
        y: Math.round(16 * hero.uiScale)
        width: parent.width - x - Math.round(16 * hero.uiScale)
        text: hero.label
        color: theme.muted
        elide: Text.ElideRight
        font.family: theme.bodyFont
        font.pixelSize: Math.round(10 * hero.uiScale)
        font.weight: Font.DemiBold
    }

    Text {
        x: Math.round(16 * hero.uiScale)
        y: Math.round(42 * hero.uiScale)
        width: parent.width - Math.round(32 * hero.uiScale)
        height: parent.height - Math.round(52 * hero.uiScale)
        text: hero.value
        color: theme.ink
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        font.family: theme.displayFont
        font.pixelSize: Math.round(22 * hero.uiScale)
        font.weight: Font.DemiBold
        verticalAlignment: Text.AlignVCenter
    }
}
