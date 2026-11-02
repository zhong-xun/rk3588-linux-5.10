import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    property bool locked: true
    property real uiScale: 1.0
    signal unlocked()

    visible: locked || opacity > 0
    opacity: locked ? 1.0 : 0.0

    function unlock() {
        root.locked = false
        root.unlocked()
    }

    Behavior on opacity {
        NumberAnimation {
            duration: theme.durationBase
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.instrument
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: theme.withAlpha(theme.instrumentInk, 0.16)
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: Math.round(24 * root.uiScale)
        anchors.top: parent.top
        anchors.topMargin: Math.round(24 * root.uiScale)
        spacing: Math.round(3 * root.uiScale)

        Text {
            text: "BOARDLAB"
            color: theme.instrumentInk
            font.family: theme.monoFont
            font.pixelSize: Math.round(12 * root.uiScale)
            font.weight: Font.Bold
        }

        Text {
            text: "SIGNAL CONSOLE / LOCKED"
            color: theme.instrumentMuted
            font.family: theme.monoFont
            font.pixelSize: Math.round(10 * root.uiScale)
            font.weight: Font.DemiBold
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Math.round(24 * root.uiScale)
        anchors.rightMargin: Math.round(24 * root.uiScale)
        spacing: Math.round(8 * root.uiScale)

        Rectangle {
            width: Math.round(36 * root.uiScale)
            height: Math.max(2, Math.round(3 * root.uiScale))
            color: theme.accent
        }

        Text {
            id: lockClock
            width: parent.width
            text: "09:41"
            color: theme.instrumentInk
            horizontalAlignment: Text.AlignLeft
            font.family: theme.monoFont
            font.pixelSize: Math.round(Math.min(root.width < Math.round(520 * root.uiScale) ? 46 : 64,
                                                root.height < Math.round(540 * root.uiScale) ? 46 : 64) * root.uiScale)
            font.weight: Font.DemiBold
        }

        Text {
            width: parent.width
            text: Qt.formatDate(new Date(), "yyyy年M月d日")
            color: theme.instrumentMuted
            horizontalAlignment: Text.AlignLeft
            font.family: theme.bodyFont
            font.pixelSize: Math.round(16 * root.uiScale)
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            text: "触控屏已就绪。解锁后进入板端工作台。"
            color: theme.instrumentMuted
            wrapMode: Text.Wrap
            font.family: theme.bodyFont
            font.pixelSize: Math.round(13 * root.uiScale)
            lineHeight: 1.35
        }
    }

    Button {
        id: unlockButton
        anchors.left: parent.left
        anchors.leftMargin: Math.round(24 * root.uiScale)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(28 * root.uiScale)
        width: Math.min(Math.max(Math.round(220 * root.uiScale), implicitWidth),
                        parent.width - Math.round(48 * root.uiScale))
        height: Math.max(Math.round(48 * root.uiScale), Math.round(44 * root.uiScale))
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: "进入桌面"
        onClicked: root.unlock()

        background: Rectangle {
            radius: Math.round(theme.radiusControl * root.uiScale)
            color: unlockButton.down ? theme.accentHover
                                     : (unlockButton.hovered ? theme.accentHover : theme.accent)
            border.color: unlockButton.activeFocus ? theme.focus : theme.accent
            border.width: unlockButton.activeFocus ? 2 : 1

            Behavior on color {
                ColorAnimation { duration: theme.durationFast }
            }
        }

        contentItem: Text {
            text: "进入桌面"
            color: theme.accentInk
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: theme.bodyFont
            font.pixelSize: Math.round(14 * root.uiScale)
            font.weight: Font.DemiBold
        }
    }

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var d = new Date()
            var h = d.getHours().toString()
            var m = d.getMinutes().toString()
            if (m.length < 2)
                m = "0" + m
            lockClock.text = h + ":" + m
        }
    }
}
