import QtQuick 2.12

Rectangle {
    id: warningBlock

    BoardLabTheme { id: theme }

    property string text: ""
    property real uiScale: 1.0
    readonly property real effectiveUiScale: Math.max(1.0, uiScale)

    width: parent.width
    height: Math.max(Math.round(52 * effectiveUiScale), warningText.implicitHeight + Math.round(22 * effectiveUiScale))
    color: theme.warningSoft
    border.color: theme.warning
    border.width: 1
    radius: Math.round(theme.radiusControl * effectiveUiScale)

    LucideIcon {
        anchors.left: parent.left
        anchors.leftMargin: Math.round(12 * effectiveUiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.round(20 * effectiveUiScale)
        height: width
        name: "triangle-alert"
        color: theme.warning
    }

    Text {
        id: warningText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Math.round(44 * effectiveUiScale)
        anchors.rightMargin: Math.round(12 * effectiveUiScale)
        text: warningBlock.text
        color: theme.inkStrong
        wrapMode: Text.WordWrap
        font.family: theme.bodyFont
        font.pixelSize: Math.round(12 * effectiveUiScale)
        font.weight: Font.Normal
        lineHeight: 1.3
    }
}
