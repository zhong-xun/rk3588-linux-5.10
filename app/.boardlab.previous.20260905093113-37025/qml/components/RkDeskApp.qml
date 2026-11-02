import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    signal closed()
    signal toolRequested(string tool)

    property real uiScale: 1.0
    readonly property string boardLabel: boardProfile.displayName.length > 0
                                             ? boardProfile.displayName : "Linux Board"
    readonly property int outerGutter: Math.round((width < Math.round(620 * uiScale) ? 12 : 20) * uiScale)
    readonly property int gridGap: Math.round(10 * uiScale)
    readonly property int toolColumns: width < Math.round(520 * uiScale) ? 1
                                      : (width < Math.round(920 * uiScale) ? 2 : 3)
    property int hardwareSurfaceCount: boardBackend.ethernetInterfaces.length
                                       + boardBackend.canInterfaces.length
                                       + boardBackend.cellularDevices.length
                                       + boardBackend.serialDevices.length
                                       + (boardBackend.backlightAvailable ? 1 : 0)
    property bool localhostBridge: localWebServer.bindAddress === "127.0.0.1"
                                   || localWebServer.bindAddress === "::1"
                                   || localWebServer.bindAddress === "localhost"
    property int readinessScore: Math.min(100,
                                          ((systemInfo.cpuUsage < 85 && systemInfo.memoryUsage < 90 && systemInfo.diskUsage < 92) ? 22 : 12)
                                          + (hardwareSurfaceCount >= 3 ? 22 : (hardwareSurfaceCount > 0 ? 14 : 8))
                                          + (localWebServer.running ? 20 : 8)
                                          + 18
                                          + (localhostBridge ? 18 : 12))

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Flickable {
        id: workScroller
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: contentColumn.height + root.outerGutter * 2
        clip: true

        Column {
            id: contentColumn
            x: root.outerGutter
            y: root.outerGutter
            width: Math.max(1, workScroller.width - root.outerGutter * 2)
            spacing: Math.round(16 * root.uiScale)

            Item {
                width: parent.width
                height: Math.round(64 * root.uiScale)

                Column {
                    anchors.left: parent.left
                    anchors.right: returnAction.left
                    anchors.rightMargin: Math.round(16 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Math.round(2 * root.uiScale)

                    Text {
                        width: parent.width
                        text: "RK 桌面"
                        color: theme.inkStrong
                        elide: Text.ElideRight
                        font.family: theme.displayFont
                        font.pixelSize: Math.round(24 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        width: parent.width
                        text: root.boardLabel + " / 外设与状态入口"
                        color: theme.muted
                        elide: Text.ElideRight
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(13 * root.uiScale)
                    }
                }

                BoardToolsActionButton {
                    id: returnAction
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    uiScale: root.uiScale
                    width: Math.round(110 * root.uiScale)
                    height: Math.round(44 * root.uiScale)
                    touchTarget: Math.round(44 * root.uiScale)
                    text: "返回桌面"
                    variant: "secondary"
                    onClicked: root.closed()
                }
            }

            Rectangle {
                width: parent.width
                height: Math.round(root.width < Math.round(620 * root.uiScale) ? 156 : 148 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.instrument
                border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.max(3, Math.round(4 * root.uiScale))
                    color: theme.accent
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(20 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(root.width < Math.round(620 * root.uiScale) ? 20 : 168 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(18 * root.uiScale)
                    spacing: Math.round(7 * root.uiScale)

                    Text {
                        width: parent.width
                        text: root.boardLabel + " 实时板端"
                        color: theme.instrumentInk
                        elide: Text.ElideRight
                        font.family: theme.displayFont
                        font.pixelSize: Math.round(20 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        width: parent.width
                        text: systemInfo.kernelText
                        color: theme.instrumentMuted
                        elide: Text.ElideRight
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Row {
                        spacing: Math.round(8 * root.uiScale)
                        RkDeskMiniMetric { uiScale: root.uiScale; label: "CPU"; value: systemInfo.cpuUsageValid ? (Math.round(systemInfo.cpuUsage) + "%") : "--"; accent: theme.ready }
                        RkDeskMiniMetric { uiScale: root.uiScale; label: "RAM"; value: Math.round(systemInfo.memoryUsage) + "%"; accent: theme.focus }
                        RkDeskMiniMetric { uiScale: root.uiScale; label: "就绪"; value: root.readinessScore + "%"; accent: theme.warning }
                    }
                }

                Column {
                    visible: root.width >= Math.round(620 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(20 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(130 * root.uiScale)
                    spacing: Math.round(5 * root.uiScale)

                    Text {
                        anchors.right: parent.right
                        text: "温度"
                        color: theme.instrumentMuted
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(10 * root.uiScale)
                    }

                    Text {
                        anchors.right: parent.right
                        width: parent.width
                        text: systemInfo.temperatureText
                        color: theme.instrumentInk
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(14 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        anchors.right: parent.right
                        width: parent.width
                        text: systemInfo.networkText
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        color: theme.instrumentMuted
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Text {
                        anchors.right: parent.right
                        text: "磁盘 " + Math.round(systemInfo.diskUsage) + "%"
                        color: theme.instrumentMuted
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }
            }

            Grid {
                id: moduleGrid
                width: parent.width
                columns: root.toolColumns
                rowSpacing: root.gridGap
                columnSpacing: root.gridGap

                Repeater {
                    model: [
                        { name: "以太网", tool: "Ethernet", iconName: "network", detail: boardBackend.ethernetInterfaces.length + " 项" },
                        { name: "CAN", tool: "CAN", iconName: "network", detail: boardBackend.canInterfaces.length + " 路" },
                        { name: "蜂窝", tool: "Cellular", iconName: "radio-tower", detail: boardBackend.cellularDevices.length + " 项" },
                        { name: "GPIO", tool: "GPIO", iconName: "circuit-board", detail: boardBackend.gpioPreview("GPIO1_B1", true).split(" -> ")[1] },
                        { name: "UART", tool: "UART", iconName: "usb", detail: boardBackend.serialDevices.length + " tty" },
                        { name: "音乐", tool: "Music", iconName: "music-2", detail: "ALSA / 录音" },
                        { name: "蜂鸣器", tool: "Buzzer", iconName: "volume-2", detail: boardBackend.buzzerAvailable ? (boardBackend.buzzerControlMethod === "input-event" ? "输入事件" : "不可用") : "未发现" },
                        { name: "LED", tool: "LED", iconName: "lightbulb", detail: boardBackend.ledAvailable ? boardBackend.ledDevices.length + " 路" : "未发现" },
                        { name: "摄像头", tool: "Camera", iconName: "camera", detail: boardBackend.videoDevices.length + " 路" },
                        { name: "看门狗", tool: "Watchdog", iconName: "timer", detail: boardBackend.watchdogDevices.length + " 项" },
                        { name: "背光", tool: "Backlight", iconName: "sun-medium", detail: boardBackend.backlightAvailable ? boardBackend.backlightPercent + "%" : "无" },
                        { name: "文件", tool: "Files", iconName: "folder", detail: "浏览" }
                    ]

                    RkDeskModuleCard {
                        uiScale: root.uiScale
                        cardWidth: (moduleGrid.width - (moduleGrid.columns - 1) * moduleGrid.columnSpacing) / moduleGrid.columns
                        name: modelData.name
                        iconName: modelData.iconName
                        detail: modelData.detail
                        accent: theme.accent
                        onSelected: root.toolRequested(modelData.tool)
                    }
                }
            }

            Rectangle {
                id: statusPanel
                width: parent.width
                height: Math.round((statusGrid.columns === 1 ? 214 : 148) * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: Math.round(16 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Text {
                        text: "板端状态"
                        color: theme.inkStrong
                        font.family: theme.displayFont
                        font.pixelSize: Math.round(17 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        width: parent.width
                        text: boardBackend.statusText
                        color: theme.muted
                        elide: Text.ElideRight
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(13 * root.uiScale)
                    }

                    Rectangle {
                        width: parent.width
                        height: Math.round(6 * root.uiScale)
                        radius: height / 2
                        color: theme.surfaceSoft

                        Rectangle {
                            width: parent.width * Math.max(0.12, Math.min(1, systemInfo.cpuUsageValid ? systemInfo.cpuUsage / 100 : 0))
                            height: parent.height
                            radius: parent.radius
                            color: theme.accent
                        }
                    }

                    Text {
                        width: parent.width
                        text: "硬件 " + root.hardwareSurfaceCount + " 项  /  当前路径 " + boardBackend.currentPath
                        color: theme.inkFaint
                        elide: Text.ElideRight
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(10 * root.uiScale)
                    }

                    Grid {
                        id: statusGrid
                        width: parent.width
                        columns: root.width < Math.round(520 * root.uiScale) ? 1 : 3
                        rowSpacing: Math.round(6 * root.uiScale)
                        columnSpacing: Math.round(8 * root.uiScale)

                        RkDeskStatusChip {
                            uiScale: root.uiScale
                            width: (statusGrid.width - (statusGrid.columns - 1) * statusGrid.columnSpacing) / statusGrid.columns
                            label: "Web"
                            value: localWebServer.running ? "运行" : "离线"
                            accent: localWebServer.running ? theme.ready : theme.inkFaint
                        }
                        RkDeskStatusChip {
                            uiScale: root.uiScale
                            width: (statusGrid.width - (statusGrid.columns - 1) * statusGrid.columnSpacing) / statusGrid.columns
                            label: "安全"
                            value: root.localhostBridge ? "本机" : "局域网"
                            accent: root.localhostBridge ? theme.focus : theme.warning
                        }
                        RkDeskStatusChip {
                            uiScale: root.uiScale
                            width: (statusGrid.width - (statusGrid.columns - 1) * statusGrid.columnSpacing) / statusGrid.columns
                            label: "硬件"
                            value: root.hardwareSurfaceCount + " 项"
                            accent: theme.accent
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: ledgerColumn.implicitHeight + Math.round(32 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.instrument
                border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                border.width: 1

                Column {
                    id: ledgerColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Math.round(16 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Text {
                        text: root.boardLabel + " 交付项目"
                        color: theme.instrumentInk
                        font.family: theme.displayFont
                        font.pixelSize: Math.round(18 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    RkDeskTrackRow { uiScale: root.uiScale; title: "全屏 Qt 桌面"; detail: "QML 界面、kiosk 全屏、截图验收"; state: "Qt 5.12+"; accent: theme.ready }
                    RkDeskTrackRow { uiScale: root.uiScale; title: "板端调试"; detail: "GPIO、UART、以太网、CAN、4G、音频/蜂鸣器"; state: root.hardwareSurfaceCount + " 项"; accent: theme.focus }
                    RkDeskTrackRow { uiScale: root.uiScale; title: "局域网 Web"; detail: "QtNetwork 仪表盘、二维码和 JSON 接口"; state: "LAN"; accent: theme.warning }
                    RkDeskTrackRow { uiScale: root.uiScale; title: "交付留证"; detail: "构建、部署、硬件验收"; state: root.readinessScore + "%"; accent: theme.accent }
                }
            }
        }
    }
}
