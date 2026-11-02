import QtQuick 2.12

Rectangle {
    id: overviewTile

    BoardLabTheme { id: theme }

    property string title: ""
    property string detail: ""
    property string value: ""
    property string state: ""
    property string tool: ""
    property color accent: theme.accent
    property real uiScale: 1.0
    property bool enabled: true
    property bool busy: false
    readonly property bool highlighted: activeFocus || mouseArea.containsMouse
    signal toolSelected(string selectedTool)

    implicitWidth: Math.round(168 * overviewTile.uiScale)
    implicitHeight: Math.round(132 * overviewTile.uiScale)
    width: (parent.width - parent.columnSpacing * 3) / 4
    height: (parent.height - parent.rowSpacing * 2) / 3
    color: !overviewTile.enabled ? theme.surfaceSoft
           : (overviewTile.busy ? theme.warningSoft
              : (mouseArea.pressed ? theme.canvasStrong
                 : (overviewTile.highlighted ? theme.accentSoft : theme.surfaceRaised)))
    border.color: overviewTile.activeFocus ? theme.focus
                  : (overviewTile.busy ? theme.warning : theme.rule)
    border.width: overviewTile.activeFocus ? 2 : 1
    radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * overviewTile.uiScale))
    activeFocusOnTab: overviewTile.enabled && !overviewTile.busy
    Accessible.role: Accessible.Button
    Accessible.name: overviewTile.title
    Accessible.description: overviewTile.detail + (overviewTile.state.length > 0 ? "，" + overviewTile.state : "")

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    Text {
        x: Math.round(theme.space16 * overviewTile.uiScale)
        y: Math.round(theme.space16 * overviewTile.uiScale)
        text: value
        color: overviewTile.enabled ? overviewTile.accent : theme.inkFaint
        font.family: theme.monoFont
        font.pixelSize: Math.round(18 * overviewTile.uiScale)
        font.weight: Font.Bold
    }

    Text {
        x: Math.round(theme.space16 * overviewTile.uiScale)
        y: Math.round(50 * overviewTile.uiScale)
        width: parent.width - Math.round(theme.space32 * overviewTile.uiScale)
        text: title
        color: overviewTile.enabled ? theme.inkStrong : theme.inkFaint
        elide: Text.ElideRight
        font.family: theme.displayFont
        font.pixelSize: Math.round(18 * overviewTile.uiScale)
        font.weight: Font.DemiBold
    }

    Text {
        x: Math.round(theme.space16 * overviewTile.uiScale)
        y: Math.round(80 * overviewTile.uiScale)
        width: parent.width - Math.round(theme.space32 * overviewTile.uiScale)
        text: detail
        color: overviewTile.enabled ? theme.muted : theme.inkFaint
        elide: Text.ElideRight
        font.family: theme.bodyFont
        font.pixelSize: Math.round(12 * overviewTile.uiScale)
    }

    Text {
        x: Math.round(theme.space16 * overviewTile.uiScale)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(theme.space16 * overviewTile.uiScale)
        width: parent.width - Math.round(theme.space32 * overviewTile.uiScale)
        text: state
        color: overviewTile.busy ? theme.warning
               : (overviewTile.enabled ? theme.muted : theme.inkFaint)
        elide: Text.ElideRight
        font.family: theme.monoFont
        font.pixelSize: Math.round(11 * overviewTile.uiScale)
        font.weight: Font.DemiBold
    }

    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: Math.round(theme.space16 * overviewTile.uiScale)
        anchors.top: parent.top
        anchors.topMargin: Math.round(theme.space16 * overviewTile.uiScale)
        width: Math.max(7, Math.round(8 * overviewTile.uiScale))
        height: width
        radius: width / 2
        visible: overviewTile.busy
        color: theme.warning
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: overviewTile.enabled && !overviewTile.busy
        hoverEnabled: enabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: overviewTile.forceActiveFocus()
        onClicked: overviewTile.toolSelected(overviewTile.tool)
    }

    Keys.onSpacePressed: {
        if (overviewTile.enabled && !overviewTile.busy) {
            overviewTile.toolSelected(overviewTile.tool)
            event.accepted = true
        }
    }
    Keys.onReturnPressed: {
        if (overviewTile.enabled && !overviewTile.busy) {
            overviewTile.toolSelected(overviewTile.tool)
            event.accepted = true
        }
    }
    Keys.onEnterPressed: {
        if (overviewTile.enabled && !overviewTile.busy) {
            overviewTile.toolSelected(overviewTile.tool)
            event.accepted = true
        }
    }
}
