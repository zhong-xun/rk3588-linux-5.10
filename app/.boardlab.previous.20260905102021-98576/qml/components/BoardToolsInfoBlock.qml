import QtQuick 2.12

Rectangle {
    id: infoBlock

    BoardLabTheme { id: theme }

    property string label: ""
    property string value: ""
    property real uiScale: 1.0
    readonly property real effectiveUiScale: Math.max(1.0, uiScale)
    readonly property int contentPadding: Math.round(12 * effectiveUiScale)

    width: parent.width
    height: Math.max(Math.round(54 * effectiveUiScale),
                     Math.round(26 * effectiveUiScale) + valueText.implicitHeight + contentPadding)
    color: theme.surfaceRaised
    border.color: theme.rule
    border.width: 1
    radius: Math.round(theme.radiusControl * effectiveUiScale)

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(2, Math.round(3 * effectiveUiScale))
        color: theme.info
    }

    Text {
        x: contentPadding
        y: Math.round(8 * effectiveUiScale)
        width: parent.width - contentPadding * 2
        text: infoBlock.label
        color: theme.muted
        elide: Text.ElideRight
        font.family: theme.monoFont
        font.pixelSize: Math.round(10 * effectiveUiScale)
        font.weight: Font.DemiBold
    }

    Text {
        id: valueText
        x: contentPadding
        y: Math.round(26 * effectiveUiScale)
        width: parent.width - contentPadding * 2
        text: infoBlock.value
        color: theme.ink
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 3
        elide: Text.ElideRight
        font.family: theme.bodyFont
        font.pixelSize: Math.round(13 * effectiveUiScale)
        font.weight: Font.Medium
    }
}
