import QtQuick 2.12
import QtQuick.Controls 2.12

Slider {
    id: control
    BoardLabTheme { id: theme }
    property real uiScale: 1.0

    implicitHeight: Math.round(44 * uiScale)
    hoverEnabled: true

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Math.round(4 * control.uiScale)

        Rectangle { anchors.fill: parent; radius: height / 2; color: theme.canvasStrong }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.enabled ? theme.accent : theme.inkFaint
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: Math.round(18 * control.uiScale)
        height: width
        radius: width / 2
        color: theme.surfaceRaised
        border.color: control.activeFocus || control.pressed ? theme.focus : theme.ruleStrong
        border.width: control.pressed ? 2 : 1
        scale: control.pressed ? 1.08 : 1.0
        Behavior on scale { NumberAnimation { duration: theme.motionDuration(80); easing.type: Easing.OutCubic } }
    }
}
