import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.12

TextField {
    id: field
    BoardLabTheme { id: theme }
    property real uiScale: 1.0
    // When false, focus will not auto-open the on-screen keyboard (rare).
    property bool virtualKeyboardEnabled: true

    implicitHeight: Math.max(44, Math.round(46 * uiScale))
    height: implicitHeight
    leftPadding: Math.round(14 * uiScale)
    rightPadding: Math.round(14 * uiScale)
    topPadding: 0
    bottomPadding: 0
    color: enabled ? theme.ink : theme.inkFaint
    placeholderTextColor: theme.inkFaint
    selectionColor: theme.accent
    selectedTextColor: theme.accentInk
    selectByMouse: true
    hoverEnabled: enabled
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Math.round(13 * uiScale)

    background: Rectangle {
        radius: Math.round(theme.radiusControl * field.uiScale)
        color: field.enabled ? (field.hovered ? theme.surfaceSoft : theme.surfaceRaised) : theme.canvasAlt
        border.color: field.activeFocus ? theme.focus : theme.ruleStrong
        border.width: field.activeFocus ? 2 : 1
    }

    function requestVirtualKeyboard() {
        if (!field.virtualKeyboardEnabled || !field.enabled)
            return
        var host = Window.window
        if (!host || host.showVirtualKeyboard === undefined)
            return
        host.showVirtualKeyboard(field)
    }

    onActiveFocusChanged: {
        if (activeFocus)
            field.requestVirtualKeyboard()
    }

    // A second tap on an already-focused field still needs the keyboard on a
    // touch kiosk after the user dismissed it with "收起".
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onPressed: {
            if (!field.activeFocus)
                field.forceActiveFocus()
            else
                field.requestVirtualKeyboard()
            mouse.accepted = false
        }
    }
}
