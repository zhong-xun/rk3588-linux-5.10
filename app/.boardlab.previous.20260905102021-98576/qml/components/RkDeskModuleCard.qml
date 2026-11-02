import QtQuick 2.12

Rectangle {
    id: card
    BoardLabTheme { id: theme }

    signal selected()
    property string name: ""
    property string iconName: ""
    property string detail: ""
    property color accent: theme.accent
    property real cardWidth: 112
    property real uiScale: 1.0

    width: card.cardWidth
    height: Math.round(116 * uiScale)
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: card.name
    Accessible.description: card.detail
    radius: Math.round(theme.radiusPanel * uiScale)
    color: pressArea.pressed ? theme.surfaceSoft : (pressArea.containsMouse ? theme.surfaceRaised : theme.surface)
    border.color: activeFocus ? theme.focus : (pressArea.containsMouse ? theme.ruleStrong : theme.rule)
    border.width: 1
    scale: pressArea.pressed ? 0.988 : 1.0

    Behavior on scale { NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: theme.durationFast } }

    Keys.onPressed: {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            card.selected()
            event.accepted = true
        }
    }

    Rectangle {
        x: Math.round(12 * card.uiScale)
        y: Math.round(12 * card.uiScale)
        width: Math.round(42 * card.uiScale)
        height: Math.round(42 * card.uiScale)
        radius: Math.round(theme.radiusControl * card.uiScale)
        color: theme.withAlpha(accent, 0.13)
        LucideIcon {
            anchors.centerIn: parent
            width: Math.round(20 * card.uiScale)
            height: width
            name: card.iconName
            color: card.accent
        }
    }

    Text {
        x: Math.round(12 * card.uiScale)
        y: Math.round(64 * card.uiScale)
        width: parent.width - Math.round(24 * card.uiScale)
        text: card.name
        color: theme.inkStrong
        elide: Text.ElideRight
        font.family: theme.displayFont
        font.pixelSize: Math.round(14 * card.uiScale)
        font.weight: Font.Bold
    }

    Text {
        x: Math.round(12 * card.uiScale)
        y: Math.round(86 * card.uiScale)
        width: parent.width - Math.round(24 * card.uiScale)
        text: card.detail
        color: theme.muted
        elide: Text.ElideRight
        font.family: theme.bodyFont
        font.pixelSize: Math.round(11 * card.uiScale)
    }

    MouseArea {
        id: pressArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: card.selected()
    }
}
