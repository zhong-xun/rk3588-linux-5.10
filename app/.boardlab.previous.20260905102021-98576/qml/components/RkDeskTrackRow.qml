import QtQuick 2.12

Rectangle {
    id: row
    BoardLabTheme { id: theme }

    property string title: ""
    property string detail: ""
    property string state: ""
    property color accent: theme.accent
    property real uiScale: 1.0

    width: parent.width
    height: Math.round(30 * uiScale)
    radius: Math.round(theme.radiusControl * uiScale)
    color: theme.withAlpha(theme.instrumentInk, 0.08)
    border.color: theme.withAlpha(theme.instrumentInk, 0.12)
    border.width: 1

    Rectangle {
        x: Math.round(8 * row.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(4, Math.round(5 * row.uiScale))
        height: Math.round(18 * row.uiScale)
        radius: Math.round(3 * row.uiScale)
        color: accent
    }

    Column {
        x: Math.round(20 * row.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - stateBadge.width - Math.round(34 * row.uiScale)
        spacing: Math.round(row.uiScale)

        Text {
            width: parent.width
            text: title
            color: theme.instrumentInk
            elide: Text.ElideRight
            font.pixelSize: Math.round(11 * row.uiScale)
            font.weight: Font.Bold
        }

        Text {
            width: parent.width
            text: detail
            color: theme.instrumentMuted
            elide: Text.ElideRight
            font.pixelSize: Math.round(9 * row.uiScale)
        }
    }

    Rectangle {
        id: stateBadge
        anchors.right: parent.right
        anchors.rightMargin: Math.round(8 * row.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(Math.round(48 * row.uiScale), stateText.implicitWidth + Math.round(16 * row.uiScale))
        height: Math.round(20 * row.uiScale)
        radius: Math.round(10 * row.uiScale)
        color: theme.withAlpha(accent, 0.18)

        Text {
            id: stateText
            anchors.centerIn: parent
            text: state
            color: theme.instrumentInk
            font.pixelSize: Math.round(9 * row.uiScale)
            font.weight: Font.Bold
        }
    }
}
