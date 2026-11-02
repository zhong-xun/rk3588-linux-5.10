import QtQuick 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    property color foreground: theme.instrumentInk
    property bool darkText: false
    property bool active: true
    property alias timeText: clock.text
    property string networkLabel: networkSummary(systemInfo.networkText)
    property real uiScale: 1.0
    readonly property bool compactLayout: width < Math.round(700 * root.uiScale)
    readonly property int horizontalMargin: Math.round((compactLayout ? 18 : 42) * root.uiScale)

    implicitHeight: Math.round(58 * root.uiScale)

    function networkSummary(text) {
        if (!text || text.length === 0 || text === "Unavailable")
            return "离线"
        var lower = text.toLowerCase()
        if (lower.indexOf("wwan") >= 0 || lower.indexOf("ppp") >= 0 || lower.indexOf("cell") >= 0)
            return "4G / 5G"
        if (lower.indexOf("wlan") >= 0 || lower.indexOf("wifi") >= 0 || lower.indexOf("wi-fi") >= 0)
            return "Wi-Fi"
        if (lower.indexOf("eth") >= 0 || lower.indexOf("en") === 0)
            return text.split(" ")[0]
        return text.split(" ")[0]
    }

    Timer {
        interval: 1000
        running: root.active
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var d = new Date()
            var h = d.getHours().toString()
            var m = d.getMinutes().toString()
            if (m.length < 2)
                m = "0" + m
            clock.text = h + ":" + m
        }
    }

    Text {
        id: clock
        x: root.horizontalMargin
        anchors.verticalCenter: parent.verticalCenter
        color: root.darkText ? theme.ink : root.foreground
        font.family: theme.monoFont
        font.pixelSize: Math.round(17 * root.uiScale)
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: statusSummary
        anchors.right: parent.right
        anchors.rightMargin: root.horizontalMargin
        anchors.verticalCenter: parent.verticalCenter
        width: root.compactLayout
               ? Math.max(1, Math.min(Math.round(164 * root.uiScale),
                                      parent.width - clock.x - clock.implicitWidth - Math.round(20 * root.uiScale)))
               : Math.round(304 * root.uiScale)
        height: Math.round(34 * root.uiScale)
        radius: 0
        color: "transparent"
        border.width: 0

        Row {
            anchors.fill: parent
            anchors.leftMargin: Math.round(12 * root.uiScale)
            anchors.rightMargin: Math.round(12 * root.uiScale)

            Item {
                width: root.compactLayout
                       ? Math.max(Math.round(54 * root.uiScale), parent.width - webStatus.width)
                       : Math.round(100 * root.uiScale)
                height: parent.height

                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Math.round(6 * root.uiScale)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.round(6 * root.uiScale)
                        height: width
                        radius: width / 2
                        color: root.networkLabel === "离线" ? theme.inkFaint : theme.ready
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(1, parent.width - Math.round(12 * root.uiScale))
                        text: root.networkLabel
                        color: root.darkText ? theme.muted : root.foreground
                        font.family: theme.bodyFont
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(12 * root.uiScale)
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                visible: !root.compactLayout
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Math.round(12 * root.uiScale)
                color: root.darkText ? theme.rule : theme.withAlpha(theme.instrumentInk, 0.28)
            }

            Item {
                visible: !root.compactLayout
                width: Math.round(90 * root.uiScale)
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text: systemInfo.cpuUsageValid ? ("CPU " + Math.round(systemInfo.cpuUsage) + "%") : "CPU --"
                    color: root.darkText ? theme.muted : root.foreground
                    font.family: theme.monoFont
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(12 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                visible: !root.compactLayout
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Math.round(12 * root.uiScale)
                color: root.darkText ? theme.rule : theme.withAlpha(theme.instrumentInk, 0.28)
            }

            Item {
                id: webStatus
                visible: localWebServer.running
                width: visible ? (root.compactLayout ? Math.round(54 * root.uiScale)
                                                    : Math.round(88 * root.uiScale)) : 0
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text: root.compactLayout ? "Web" : "Web 已开启"
                    color: root.darkText ? theme.muted : root.foreground
                    font.family: theme.bodyFont
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(12 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
