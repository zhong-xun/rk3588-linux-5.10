import QtQuick 2.12
import QtQuick.Controls 2.12

Rectangle {
    id: switchRow

    BoardLabTheme { id: theme }

    property string label: ""
    property bool checked: false
    property color accent: theme.accent
    property string onText: "开"
    property string offText: "关"
    property real uiScale: 1.0
    property bool hovered: false
    property bool pressed: false
    property int baseTouchTarget: 48

    readonly property int scaledTouchTarget: Math.round(baseTouchTarget * uiScale)
    readonly property int sidePadding: Math.round(14 * uiScale)
    readonly property int labelPixelSize: Math.round(13 * uiScale)
    readonly property int switchWidth: Math.round(48 * uiScale)
    readonly property int switchHeight: Math.round(28 * uiScale)
    readonly property color disabledTextColor: theme.inkFaint
    readonly property color focusRingColor: theme.focus

    signal toggled(bool checked)

    width: parent ? parent.width : implicitWidth
    implicitWidth: 240
    implicitHeight: Math.max(44, scaledTouchTarget)
    height: Math.max(44, scaledTouchTarget)
    radius: Math.round(theme.radiusControl * uiScale)
    color: enabled ? (hovered ? theme.surfaceSoft : theme.surfaceRaised) : theme.canvasAlt
    border.color: activeFocus ? focusRingColor : theme.ruleStrong
    border.width: activeFocus ? 2 : 1
    activeFocusOnTab: enabled
    opacity: 1.0

    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checked: checked

    Behavior on border.color { ColorAnimation { duration: theme.motionDuration(140) } }
    Behavior on color { ColorAnimation { duration: theme.motionDuration(140) } }

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.leftMargin: switchRow.sidePadding
        anchors.right: stateText.left
        anchors.rightMargin: Math.round(10 * switchRow.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        text: switchRow.label
        color: switchRow.enabled ? theme.ink : switchRow.disabledTextColor
        elide: Text.ElideRight
        font.pixelSize: switchRow.labelPixelSize
        font.weight: Font.DemiBold
        verticalAlignment: Text.AlignVCenter
    }

    Text {
        id: stateText
        anchors.right: switchShell.left
        anchors.rightMargin: Math.round(10 * switchRow.uiScale)
        anchors.verticalCenter: parent.verticalCenter
        width: Math.round(50 * switchRow.uiScale)
        text: switchRow.checked ? switchRow.onText : switchRow.offText
        color: switchRow.enabled ? theme.muted : switchRow.disabledTextColor
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        font.pixelSize: Math.round(11 * switchRow.uiScale)
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: switchShell
        width: switchRow.switchWidth
        height: switchRow.switchHeight
        anchors.right: parent.right
        anchors.rightMargin: switchRow.sidePadding
        anchors.verticalCenter: parent.verticalCenter
        radius: height / 2
        color: switchRow.enabled ? (switchRow.checked ? theme.accent : theme.ruleStrong) : theme.rule
        border.width: 0

        Behavior on color { ColorAnimation { duration: theme.motionDuration(140) } }

        Rectangle {
            id: knobShadow
            width: knob.width
            height: knob.height
            x: knob.x
            y: knob.y + Math.round(1 * switchRow.uiScale)
            radius: height / 2
            color: theme.withAlpha(theme.ink, 0.12)
        }

        Rectangle {
            id: knob
            width: Math.round(24 * switchRow.uiScale)
            height: width
            radius: height / 2
            y: Math.round(2 * switchRow.uiScale)
            x: switchRow.checked ? switchShell.width - width - Math.round(2 * switchRow.uiScale)
                                 : Math.round(2 * switchRow.uiScale)
            scale: switchRow.pressed ? 0.94 : 1.0
            color: theme.surfaceRaised
            border.color: theme.ruleStrong
            border.width: 1

            Behavior on x { NumberAnimation { duration: theme.motionDuration(160); easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: theme.motionDuration(80); easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: switchRow.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: switchRow.hovered = true
        onExited: {
            switchRow.hovered = false
            switchRow.pressed = false
        }
        onPressed: {
            switchRow.pressed = true
            switchRow.forceActiveFocus()
        }
        onReleased: switchRow.pressed = false
        onCanceled: switchRow.pressed = false
        onClicked: switchRow.toggled(!switchRow.checked)
    }

    Keys.onSpacePressed: {
        if (switchRow.enabled)
            switchRow.toggled(!switchRow.checked)
    }

    Keys.onReturnPressed: {
        if (switchRow.enabled)
            switchRow.toggled(!switchRow.checked)
    }

    Keys.onEnterPressed: {
        if (switchRow.enabled)
            switchRow.toggled(!switchRow.checked)
    }
}
