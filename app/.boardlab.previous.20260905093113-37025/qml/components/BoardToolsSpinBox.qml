import QtQuick 2.12
import QtQuick.Controls 2.12

SpinBox {
    id: control

    BoardLabTheme { id: theme }

    property real uiScale: 1.0
    property string suffixText: ""
    readonly property real effectiveUiScale: Math.max(1.0, uiScale)
    readonly property int controlHeight: Math.max(44, Math.round(46 * effectiveUiScale))
    readonly property int stepperWidth: Math.max(44, Math.round(46 * effectiveUiScale))

    implicitHeight: controlHeight
    height: controlHeight
    editable: false
    activeFocusOnTab: enabled
    hoverEnabled: enabled
    leftPadding: stepperWidth
    rightPadding: stepperWidth
    font.family: theme.monoFont
    font.pixelSize: Math.round(13 * effectiveUiScale)
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.textFromValue(control.value, control.locale) + control.suffixText
        color: control.enabled ? theme.ink : theme.inkFaint
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: control.font
    }

    down.indicator: Rectangle {
        x: 0
        y: 0
        width: control.stepperWidth
        height: control.height
        color: control.down.pressed ? theme.surfaceSoft : theme.withAlpha(theme.surfaceRaised, 0)

        LucideIcon {
            anchors.centerIn: parent
            color: control.enabled ? theme.muted : theme.inkFaint
            width: Math.round(18 * control.effectiveUiScale)
            height: width
            name: "minus"
        }

        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: Math.round(22 * control.effectiveUiScale)
            color: theme.rule
        }
    }

    up.indicator: Rectangle {
        x: control.width - width
        y: 0
        width: control.stepperWidth
        height: control.height
        color: control.up.pressed ? theme.surfaceSoft : theme.withAlpha(theme.surfaceRaised, 0)

        LucideIcon {
            anchors.centerIn: parent
            color: control.enabled ? theme.muted : theme.inkFaint
            width: Math.round(18 * control.effectiveUiScale)
            height: width
            name: "plus"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: Math.round(22 * control.effectiveUiScale)
            color: theme.rule
        }
    }

    background: Rectangle {
        radius: Math.round(theme.radiusControl * control.effectiveUiScale)
        color: control.enabled ? (control.hovered ? theme.surfaceSoft : theme.surfaceRaised) : theme.canvasAlt
        border.color: control.activeFocus ? theme.focus : theme.ruleStrong
        border.width: control.activeFocus ? 2 : 1

        Behavior on color { ColorAnimation { duration: theme.durationFast } }
    }
}
