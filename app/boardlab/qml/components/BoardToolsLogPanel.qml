import QtQuick 2.12
import QtQuick.Controls 2.12

Rectangle {
    id: logPanel

    BoardLabTheme { id: theme }

    property string text: ""
    property real uiScale: 1.0
    readonly property real effectiveUiScale: Math.max(1.0, uiScale)

    width: parent.width
    height: parent.height
    color: theme.instrument
    border.color: logFlick.activeFocus ? theme.focus : theme.withAlpha(theme.instrumentInk, 0.22)
    border.width: logFlick.activeFocus ? 2 : 1
    radius: Math.round(theme.radiusPanel * effectiveUiScale)
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(2, Math.round(3 * effectiveUiScale))
        color: theme.info
    }

    Flickable {
        id: logFlick
        anchors.fill: parent
        anchors.margins: Math.round(14 * effectiveUiScale)
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: logText.implicitHeight
        clip: true
        interactive: contentHeight > height
        activeFocusOnTab: interactive

        Text {
            id: logText
            width: logFlick.width
            text: logPanel.text
            color: theme.instrumentInk
            wrapMode: Text.WrapAnywhere
            font.family: theme.monoFont
            font.pixelSize: Math.round(11 * effectiveUiScale)
            lineHeight: 1.3
        }

        ScrollIndicator.vertical: ScrollIndicator {
            active: logFlick.moving || logFlick.dragging
            width: Math.max(2, Math.min(3, Math.round(2 * effectiveUiScale)))
        }

        onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

        Keys.onPressed: {
            var step = Math.round(32 * logPanel.effectiveUiScale)
            if (event.key === Qt.Key_Up) {
                contentY = Math.max(0, contentY - step)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                contentY = Math.min(Math.max(0, contentHeight - height), contentY + step)
                event.accepted = true
            } else if (event.key === Qt.Key_Home) {
                contentY = 0
                event.accepted = true
            } else if (event.key === Qt.Key_End) {
                contentY = Math.max(0, contentHeight - height)
                event.accepted = true
            }
        }
    }
}
