import QtQuick 2.12
import QtQuick.Controls 2.12

Rectangle {
    id: root

    BoardLabTheme { id: theme }

    property real uiScale: 1.0
    property string eyebrow: ""
    property string title: ""
    property string detail: ""
    // Kept for source compatibility with older category models. Visible icon
    // treatment is always Lucide-based.
    property string glyph: ""
    property string iconName: ""
    property string countLabel: ""
    property string actionLabel: "打开"
    property color accent: theme.accent
    property color accentDark: theme.accentHover
    signal activated()

    readonly property bool activeHover: pressArea.containsMouse && !pressArea.pressed

    height: Math.round(210 * root.uiScale)
    radius: Math.round(theme.radiusPanel * root.uiScale)
    color: pressArea.pressed ? theme.surfaceSoft : (root.activeHover ? theme.surfaceRaised : theme.surface)
    border.width: 1
    border.color: root.activeHover ? root.accent : theme.rule
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: root.title
    Accessible.description: root.detail

    Behavior on border.color { ColorAnimation { duration: theme.durationFast } }
    Behavior on color { ColorAnimation { duration: theme.durationFast } }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(3, Math.round(4 * root.uiScale))
        color: root.accent
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Math.round(18 * root.uiScale)
        spacing: Math.round(7 * root.uiScale)

        Row {
            width: parent.width
            spacing: Math.round(8 * root.uiScale)

            Rectangle {
                width: Math.round(42 * root.uiScale)
                height: width
                radius: Math.round(theme.radiusControl * root.uiScale)
                color: theme.withAlpha(root.accent, 0.12)

                LucideIcon {
                    anchors.centerIn: parent
                    width: Math.round(20 * root.uiScale)
                    height: width
                    name: root.iconName.length > 0 ? root.iconName : "circuit-board"
                    color: root.accentDark
                }
            }

            Column {
                width: parent.width - parent.spacing - Math.round(42 * root.uiScale)
                spacing: Math.round(1 * root.uiScale)

                Text {
                    width: parent.width
                    text: root.eyebrow
                    color: root.accentDark
                    elide: Text.ElideRight
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(10 * root.uiScale)
                    font.weight: Font.Bold
                }

                Text {
                    width: parent.width
                    text: root.title
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.family: theme.displayFont
                    font.pixelSize: Math.round(19 * root.uiScale)
                    font.weight: Font.Bold
                }
            }
        }

        Text {
            width: parent.width
            height: Math.round(39 * root.uiScale)
            text: root.detail
            color: theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: theme.bodyFont
            font.pixelSize: Math.round(12 * root.uiScale)
            lineHeight: 1.22
        }

        Item { width: 1; height: Math.round(4 * root.uiScale) }

        Row {
            width: parent.width
            spacing: Math.round(8 * root.uiScale)

            Rectangle {
                width: Math.min(parent.width - actionButton.width - parent.spacing, Math.round(124 * root.uiScale))
                height: Math.round(28 * root.uiScale)
                radius: Math.round(theme.radiusControl * root.uiScale)
                color: theme.withAlpha(root.accent, 0.11)

                Text {
                    anchors.centerIn: parent
                    text: root.countLabel
                    color: root.accentDark
                    elide: Text.ElideRight
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(10 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                id: actionButton
                width: Math.round(102 * root.uiScale)
                height: Math.round(30 * root.uiScale)
                radius: Math.round(theme.radiusControl * root.uiScale)
                color: root.activeHover ? root.accentDark : root.accent
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    anchors.centerIn: parent
                    spacing: Math.round(5 * root.uiScale)

                    Text {
                        text: root.actionLabel
                        color: theme.accentInk
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(10 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    LucideIcon {
                        width: Math.round(14 * root.uiScale)
                        height: width
                        name: "chevron-right"
                        color: theme.accentInk
                    }
                }
            }
        }
    }

    MouseArea {
        id: pressArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.activated()
    }
}
