import QtQuick 2.12
import QtQuick.Controls 2.12

Rectangle {
    id: pane

    BoardLabTheme { id: theme }

    property string title: ""
    property Component content
    property bool loadContent: visible
    property real uiScale: 1.0

    color: theme.surfaceRaised
    border.color: theme.rule
    border.width: 1
    radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * pane.uiScale))
    clip: true

    Rectangle {
        id: paneHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(44, Math.round(48 * pane.uiScale))
        color: theme.surfaceSoft

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.rule
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(theme.space16 * pane.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(theme.space16 * pane.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            text: pane.title
            color: theme.ink
            elide: Text.ElideRight
            font.pixelSize: Math.round(13 * pane.uiScale)
            font.weight: Font.DemiBold
            font.family: theme.displayFont
        }
    }

    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: paneHeader.height
        anchors.bottom: parent.bottom
        contentWidth: Math.max(0, width)
        contentHeight: Math.max(height, loader.y + loader.height + Math.round(theme.space16 * pane.uiScale))
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Loader {
            id: loader
            x: Math.round(theme.space16 * pane.uiScale)
            y: Math.round(theme.space16 * pane.uiScale)
            width: Math.max(0, flick.width - Math.round(theme.space32 * pane.uiScale))
            height: item && item.implicitHeight > 0
                    ? Math.max(0, Math.max(flick.height - Math.round(theme.space32 * pane.uiScale), item.implicitHeight))
                    : Math.max(0, flick.height - Math.round(theme.space32 * pane.uiScale))
            active: pane.loadContent
            sourceComponent: pane.content
        }

        ScrollIndicator.vertical: ScrollIndicator {
            active: flick.moving || flick.dragging || flick.activeFocus
            width: Math.max(6, Math.round(6 * pane.uiScale))
        }
    }
}
