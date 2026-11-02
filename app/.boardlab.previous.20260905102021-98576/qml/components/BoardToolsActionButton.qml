import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.12

Button {
    id: actionButton
    BoardLabTheme { id: theme }

    // Preserve the existing Button API while giving long-running operations a
    // stable, visible state instead of making their labels disappear.
    property color accent: theme.accent
    property string variant: "primary"
    property real uiScale: 1.0
    property int touchTarget: Math.round(50 * uiScale)
    property bool busy: false
    property string busyText: ""
    property string iconName: ""
    property string accessibleName: ""
    readonly property color focusRingColor: theme.focus
    readonly property color disabledFillTop: theme.surfaceSoft
    readonly property color disabledFillBottom: theme.surfaceSoft
    readonly property color disabledText: theme.inkFaint
    readonly property color resolvedFill: variant === "secondary" ? theme.surfaceRaised
                                       : (variant === "danger" ? theme.danger
                                       : (variant === "stop" ? theme.dangerSoft
                                       : (variant === "quiet" ? theme.surfaceSoft : accent)))
    readonly property color resolvedHoverFill: variant === "secondary" ? theme.surfaceSoft
                                            : (variant === "danger" ? theme.dangerHover
                                            : (variant === "stop" ? theme.dangerSoftHover
                                            : (variant === "quiet" ? theme.accentSoft
                                            : (accent === theme.accent ? theme.accentHover : accent))))
    readonly property color resolvedBorder: variant === "secondary" || variant === "stop" || variant === "quiet" ? theme.ruleStrong : "transparent"
    readonly property color resolvedText: variant === "secondary" || variant === "quiet" ? theme.ink : (variant === "stop" ? theme.danger : theme.accentInk)
    height: Math.max(Math.round(44 * actionButton.uiScale), actionButton.touchTarget)
    activeFocusOnTab: actionButton.enabled && !actionButton.busy
    hoverEnabled: actionButton.enabled && !actionButton.busy
    font.pixelSize: Math.round(13 * actionButton.uiScale)
    font.weight: Font.DemiBold
    font.family: theme.bodyFont
    Accessible.role: Accessible.Button
    Accessible.name: actionButton.accessibleName.length > 0 ? actionButton.accessibleName : actionButton.text
    Accessible.description: actionButton.busy ? "正在处理" : (!actionButton.enabled ? "当前不可用" : "")

    opacity: actionButton.enabled ? (actionButton.busy ? 0.92 : 1.0) : 0.62
    scale: actionButton.down && actionButton.enabled && !actionButton.busy ? 0.985 : 1.0
    Behavior on scale { NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic } }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: {
        if (actionButton.busy
                && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            event.accepted = true
    }
    
    background: Rectangle {
        id: buttonBackground
        radius: Math.min(theme.radiusControl, Math.round(theme.radiusControl * actionButton.uiScale))
        border.color: actionButton.activeFocus ? actionButton.focusRingColor
                                               : (actionButton.enabled ? actionButton.resolvedBorder : theme.rule)
        border.width: actionButton.activeFocus ? 2 : (actionButton.resolvedBorder === "transparent" ? 0 : 1)
        color: actionButton.enabled
               ? (actionButton.busy || actionButton.down || actionButton.hovered
                  ? actionButton.resolvedHoverFill : actionButton.resolvedFill)
               : actionButton.disabledFillTop

        Behavior on color {
            ColorAnimation { duration: theme.durationFast }
        }
    }

    contentItem: Item {
        implicitWidth: buttonLabel.implicitWidth
                       + Math.round((actionButton.busy || leadingIcon.visible ? 48 : 20) * actionButton.uiScale)
        implicitHeight: Math.max(buttonLabel.implicitHeight, busyIndicator.height, leadingIcon.height)

        BusyIndicator {
            id: busyIndicator
            anchors.left: parent.left
            anchors.leftMargin: Math.round(12 * actionButton.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(16 * actionButton.uiScale)
            height: width
            running: actionButton.busy
            visible: running
        }

        LucideIcon {
            id: leadingIcon
            anchors.left: parent.left
            anchors.leftMargin: Math.round(12 * actionButton.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(16 * actionButton.uiScale)
            height: width
            visible: !actionButton.busy && actionButton.iconName.length > 0
            name: actionButton.iconName
            color: actionButton.enabled ? actionButton.resolvedText : actionButton.disabledText
        }

        Text {
            id: buttonLabel
            anchors.left: actionButton.busy ? busyIndicator.right
                                           : (leadingIcon.visible ? leadingIcon.right : parent.left)
            anchors.leftMargin: actionButton.busy || leadingIcon.visible
                                    ? Math.round(7 * actionButton.uiScale) : Math.round(10 * actionButton.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(10 * actionButton.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            text: actionButton.busy && actionButton.busyText.length > 0
                  ? actionButton.busyText : actionButton.text
            color: actionButton.enabled ? actionButton.resolvedText : actionButton.disabledText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font: actionButton.font
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 1
        enabled: actionButton.busy
        onPressed: mouse.accepted = true
        onReleased: mouse.accepted = true
        onClicked: mouse.accepted = true
    }

    ToolTip.visible: actionButton.hovered
                     && actionButton.enabled
                     && actionButton.iconName.length > 0
                     && actionButton.text.length === 0
    ToolTip.delay: 800
    ToolTip.text: actionButton.accessibleName.length > 0 ? actionButton.accessibleName : actionButton.iconName
}
