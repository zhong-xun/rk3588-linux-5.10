import QtQuick 2.12
import QtQuick.Controls 2.12

Button {
    id: commandPill

    BoardLabTheme { id: theme }

    property string label: ""
    property string value: ""
    property real uiScale: 1.0
    signal triggered()

    width: Math.round(128 * uiScale)
    height: Math.max(44, Math.round(44 * uiScale))
    activeFocusOnTab: enabled
    hoverEnabled: enabled
    Accessible.role: Accessible.Button
    Accessible.name: label + (value.length > 0 ? " " + value : "")
    onClicked: commandPill.triggered()

    scale: down && enabled ? 0.985 : 1.0
    Behavior on scale { NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic } }

    background: Rectangle {
        radius: Math.round(theme.radiusControl * commandPill.uiScale)
        color: commandPill.down ? theme.instrument
               : (commandPill.hovered ? theme.instrument : theme.instrumentRaised)
        border.color: commandPill.activeFocus ? theme.focus : theme.withAlpha(theme.instrumentInk, 0.18)
        border.width: commandPill.activeFocus ? 2 : 1

        Behavior on color { ColorAnimation { duration: theme.durationFast } }
    }

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(12 * commandPill.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(12 * commandPill.uiScale)
            anchors.top: parent.top
            anchors.topMargin: Math.round(6 * commandPill.uiScale)
            text: commandPill.label
            color: theme.instrumentMuted
            elide: Text.ElideRight
            font.family: theme.bodyFont
            font.pixelSize: Math.round(10 * commandPill.uiScale)
            font.weight: Font.DemiBold
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(12 * commandPill.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(12 * commandPill.uiScale)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.round(6 * commandPill.uiScale)
            text: commandPill.value
            color: theme.instrumentInk
            elide: Text.ElideRight
            font.family: theme.monoFont
            font.pixelSize: Math.round(12 * commandPill.uiScale)
            font.weight: Font.Bold
        }
    }
}
