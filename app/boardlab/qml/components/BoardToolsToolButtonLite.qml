import QtQuick 2.12
import QtQuick.Controls 2.12

Button {
    id: toolButtonLite

    BoardLabTheme { id: theme }

    // Keep the public property for existing callers. Normal tool navigation
    // deliberately resolves to the one Cobalt action colour.
    property bool checkedState: false
    property color accent: theme.accent
    property real uiScale: 1.0
    signal triggered()

    height: Math.max(44, Math.round(44 * uiScale))
    activeFocusOnTab: enabled
    hoverEnabled: enabled
    font.family: theme.bodyFont
    font.pixelSize: Math.round(12 * uiScale)
    font.weight: Font.DemiBold
    Accessible.role: Accessible.Button
    Accessible.name: text
    onClicked: triggered()

    background: Rectangle {
        radius: Math.round(theme.radiusControl * toolButtonLite.uiScale)
        color: toolButtonLite.checkedState
               ? theme.accentSoft
               : (toolButtonLite.hovered ? theme.surfaceSoft : theme.surfaceRaised)
        border.width: toolButtonLite.activeFocus ? 2 : 1
        border.color: toolButtonLite.activeFocus || toolButtonLite.checkedState
                      ? theme.accent : theme.ruleStrong
    }

    contentItem: Text {
        width: parent.width - Math.round(16 * toolButtonLite.uiScale)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: toolButtonLite.text
        color: toolButtonLite.enabled
               ? (toolButtonLite.checkedState ? theme.accent : theme.ink)
               : theme.inkFaint
        elide: Text.ElideRight
        font: toolButtonLite.font
    }
}
