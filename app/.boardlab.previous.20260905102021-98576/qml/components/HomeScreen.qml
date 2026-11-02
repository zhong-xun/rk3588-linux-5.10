import QtQuick 2.12
import QtQuick.Controls 2.12
import "BoardLabCapabilityMap.js" as BoardLabCapabilities

Item {
    id: root
    objectName: "boardlab-home-workbench"

    BoardLabTheme { id: theme }

    signal appRequested(string title)
    signal quitRequested()

    property bool active: true
    visible: root.active
    enabled: root.active
    property real uiScale: 1.0
    property int startupPage: 0
    property string activeSection: "tools"
    property string searchQuery: ""
    property int selectedCategory: 0
    readonly property var categories: ["全部工具", "接口与连接", "传感与计算", "音视频", "系统与存储"]

    readonly property bool npuYoloDemoEnabled: boardProfile.hasCapability("npuYoloDemo")
    readonly property string boardLabel: boardProfile.displayName.length > 0
                                             ? boardProfile.displayName : "Linux Board"
    readonly property bool portraitLayout: height > width
    readonly property bool compactHeight: height <= 600
    readonly property int outerGutter: Math.round((compactHeight || width < Math.round(760 * uiScale) ? 12 : 20) * uiScale)
    readonly property int gridGap: Math.round(12 * uiScale)
    // Keep the directory dense at desk resolutions while giving a 4K kiosk
    // enough internal scale to remain readable at normal operating distance.
    readonly property bool wideCatalogLayout: width >= Math.round(2500 * uiScale)
                                           && height >= Math.round(1400 * uiScale)
    readonly property real catalogTileScale: wideCatalogLayout ? Math.max(uiScale, 1.16) : uiScale
    readonly property int maximumWorkSurfaceWidth: Math.round(3600 * uiScale)
    readonly property real availableWorkSurfaceWidth: Math.max(1, width - outerGutter * 2)
    readonly property real workSurfaceWidth: Math.min(maximumWorkSurfaceWidth, availableWorkSurfaceWidth)
    readonly property int toolColumns: workSurfaceWidth >= Math.round(3000 * uiScale) ? 5
                                       : (workSurfaceWidth >= Math.round(1320 * uiScale) ? 4
                                       : (workSurfaceWidth >= Math.round(980 * uiScale) ? 3
                                          : (workSurfaceWidth >= Math.round(760 * uiScale) ? 2 : 1)))
    readonly property int availableToolCount: enabledAppModel.length
    readonly property var enabledAppModel: appModel.filter(function(entry) { return root.appEntryEnabled(entry) })
    readonly property int catalogRowCount: Math.max(1, Math.ceil(availableToolCount / toolColumns))
    // At 4K the five-column catalog should occupy the work area instead of
    // leaving a second, visually unused screen below the last tool.  The cap
    // preserves the compact scan rhythm on shorter wide displays.
    readonly property int wideCatalogGridTargetHeight: Math.round(Math.max(980 * uiScale,
                                                                          Math.min(1530 * uiScale,
                                                                                   (height - Math.round(176 * uiScale)) * 0.78)))
    readonly property int wideCatalogTileHeight: Math.max(Math.round(176 * uiScale),
                                                           Math.floor((wideCatalogGridTargetHeight
                                                                       - gridGap * Math.max(0, catalogRowCount - 1))
                                                                      / catalogRowCount))
    readonly property int toolTileHeight: wideCatalogLayout ? wideCatalogTileHeight
                                                             : Math.round((compactHeight ? 96 : 116) * uiScale)
    readonly property string runtimeStatus: boardBackend.statusText.length > 0
                                            ? boardBackend.statusText : "等待设备状态"
    readonly property var visibleAppModel: root.filteredAppModel()

    property var appModel: [
        ["Keys", "按键检测", "usb", "板载按键事件监听与结果记录", true],
        ["Environment", "温湿度检测", "sun-medium", "环境传感器实时采样与来源确认", true],
        ["Demo:GPIO", "GPIO", "circuit-board", "输入输出与电平读回", true],
        ["Demo:UART", "串口", "usb", "tty 设备 TX/RX 样本", true],
        ["Demo:Ethernet", "以太网", "network", "链路、DHCP、静态 IP 与 Ping", true],
        ["GPU", "GPU 测试", "circuit-board", "glmark2-es 固定离屏基准与得分", true],
        ["Monitor", "资源监控", "chart-no-axes-combined", "CPU、内存、GPU 与 NPU 实时状态", true],
        ["NPU YOLO", "YOLOv8 NPU", "circuit-board", "内置 MP4 视频目标检测示例", true],
        ["WiFi", "Wi-Fi", "wifi", "NetworkManager 状态、扫描与连接", true],
        ["Demo:CAN", "CAN", "network", "SocketCAN 与循环发送", true],
        ["Demo:Cellular", "4G / 5G", "radio-tower", "APN、拨号与联网测试", false],
        ["Demo:Camera", "摄像头", "camera", "自动识别节点并预览", false],
        ["Demo:HDMIIN", "HDMI 输入", "camera", "HDMIRX 预览与链路确认", false],
        ["Demo:Watchdog", "看门狗", "timer", "wdctl 状态与复位测试", false],
        ["Demo:Music", "音乐", "music-2", "播放、录音回放与系统音量", false],
        ["Demo:Buzzer", "蜂鸣器", "volume-2", "提示音与心跳测试", false],
        ["Demo:LED", "LED", "lightbulb", "板载指示灯亮度与闪烁验证", false],
        ["Storage", "存储", "hard-drive", "U 盘、TF 卡与 NVMe 测速", false],
        ["Diagnostics", "系统诊断", "scan-line", "RTC、RKNPU 与权限状态", false],
        ["Demo:Backlight", "背光", "sun-medium", "亮度读写与结果确认", false],
        ["Demo:Files", "文件", "folder", "浏览板端目录与文件", false],
        ["Qt Web", "局域网控制台", "network", "扫码进入局域网 Web 控制台", false]
    ]

    function appEntryEnabled(entry) {
        return entry && entry.length > 0
                && (entry[0] !== "NPU YOLO" || root.npuYoloDemoEnabled)
                && BoardLabCapabilities.appEnabled(entry[0], boardProfile)
    }

    function filteredAppModel() {
        var entries = []
        var query = root.searchQuery.trim().toLowerCase()
        for (var index = 0; index < root.appModel.length; ++index) {
            var entry = root.appModel[index]
            if (root.appEntryEnabled(entry)
                    && (root.selectedCategory === 0 || root.appCategory(entry[0]) === root.selectedCategory)
                    && (query.length === 0 || entry.slice(0, 4).join(" ").toLowerCase().indexOf(query) >= 0))
                entries.push(entry)
        }
        return entries
    }

    function appCategory(id) {
        if (["Demo:GPIO", "Demo:UART", "Demo:Ethernet", "WiFi", "Demo:CAN", "Demo:Cellular", "Qt Web"].indexOf(id) >= 0)
            return 1
        if (["Keys", "Environment", "GPU", "Monitor", "NPU YOLO"].indexOf(id) >= 0)
            return 2
        if (["Demo:Camera", "Demo:HDMIIN", "Demo:Music", "Demo:Buzzer"].indexOf(id) >= 0)
            return 3
        return 4
    }

    function revealTile(tile) {
        var top = tile.mapToItem(workScroller.contentItem, 0, 0).y
        var bottom = top + tile.height
        if (top < workScroller.contentY)
            workScroller.contentY = top
        else if (bottom > workScroller.contentY + workScroller.height)
            workScroller.contentY = bottom - workScroller.height
    }

    onSearchQueryChanged: workScroller.contentY = 0
    onSelectedCategoryChanged: workScroller.contentY = 0

    function toolAccent(entry) {
        const applicationId = entry && entry.length > 0 ? entry[0] : ""
        const connectivityApps = ["Demo:Ethernet", "WiFi", "Demo:CAN", "Demo:Cellular", "Qt Web"]
        const sensingAndComputeApps = ["Environment", "Demo:Camera", "Demo:HDMIIN", "GPU", "Monitor", "NPU YOLO"]
        const systemApps = ["Demo:Music", "Demo:Buzzer", "Storage", "Diagnostics", "Demo:Backlight", "Demo:Files"]

        if (connectivityApps.indexOf(applicationId) >= 0)
            return theme.info
        if (sensingAndComputeApps.indexOf(applicationId) >= 0)
            return theme.focus
        if (systemApps.indexOf(applicationId) >= 0)
            return theme.warning
        return theme.accent
    }

    function showTools() {
        root.activeSection = "tools"
        workScroller.contentY = Math.max(0, catalogHeader.y - root.outerGutter)
    }

    function headerMetricValue(metricId) {
        if (metricId === "cpu")
            return systemInfo.cpuUsageValid ? Math.round(systemInfo.cpuUsage) + "%" : "--"
        if (metricId === "memory")
            return Math.round(systemInfo.memoryUsage) + "%"
        if (metricId === "disk")
            return Math.round(systemInfo.diskUsage) + "%"
        return "--"
    }

    Component.onCompleted: {
        if (startupPage > 0)
            showTools()
    }

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Rectangle {
        id: instrumentHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round((root.compactHeight ? 64 : 76) * root.uiScale)
        color: theme.instrument

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.withAlpha(theme.instrumentInk, 0.16)
        }

        Item {
            id: brandBlock
            anchors.left: parent.left
            anchors.leftMargin: root.outerGutter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round((root.width < Math.round(720 * root.uiScale) ? 150 : 208) * root.uiScale)
            height: parent.height

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(36 * root.uiScale)
                height: width
                radius: Math.round(theme.radiusControl * root.uiScale)
                color: theme.accent

                LucideIcon {
                    anchors.centerIn: parent
                    color: theme.accentInk
                    width: Math.round(22 * root.uiScale)
                    height: width
                    name: "circuit-board"
                }
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(48 * root.uiScale)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(2 * root.uiScale)

                Text {
                    width: parent.width
                    text: "BOARDLAB"
                    color: theme.instrumentInk
                    elide: Text.ElideRight
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(12 * root.uiScale)
                    font.weight: Font.Bold
                }

                Text {
                    width: parent.width
                    text: "迅为开发板测试平台"
                    color: theme.instrumentMuted
                    elide: Text.ElideRight
                    visible: root.width >= Math.round(720 * root.uiScale)
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(9 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }
        }

        Item {
            id: headerActions
            anchors.right: parent.right
            anchors.rightMargin: root.outerGutter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round((root.width < Math.round(620 * root.uiScale) ? 92 : 250) * root.uiScale)
            height: Math.round(44 * root.uiScale)

            Rectangle {
                id: localMode
                visible: root.width >= Math.round(700 * root.uiScale)
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(138 * root.uiScale)
                height: parent.height
                radius: Math.round(theme.radiusControl * root.uiScale)
                color: theme.instrumentRaised
                border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(7 * root.uiScale)
                    height: width
                    radius: width / 2
                    color: localWebServer.running ? theme.ready : theme.instrumentMuted
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(28 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(10 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: localWebServer.running ? "远程控制已开启" : "本地运行"
                    color: theme.instrumentInk
                    elide: Text.ElideRight
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(10 * root.uiScale)
                    font.weight: Font.Bold
                }
            }

            Button {
                id: quitApplicationAction
                objectName: "home-quit-application"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(88 * root.uiScale)
                height: parent.height
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: "退出程序"
                Accessible.description: "关闭迅为开发板测试平台并返回系统桌面"
                onClicked: root.quitRequested()

                background: Rectangle {
                    radius: Math.round(theme.radiusControl * root.uiScale)
                    color: quitApplicationAction.down ? theme.danger : (quitApplicationAction.hovered ? theme.danger : theme.withAlpha(theme.instrumentInk, 0.10))
                    border.color: quitApplicationAction.activeFocus ? theme.focus : theme.withAlpha(theme.instrumentInk, 0.18)
                    border.width: quitApplicationAction.activeFocus ? 2 : 1
                    Behavior on color {
                        ColorAnimation { duration: theme.durationFast }
                    }
                }

                contentItem: Text {
                    text: "退出"
                    color: quitApplicationAction.down || quitApplicationAction.hovered ? theme.accentInk : theme.instrumentInk
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(12 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }
        }

        Row {
            id: runtimeMetricRail
            visible: root.width >= Math.round(1180 * root.uiScale)
            anchors.right: headerActions.left
            anchors.rightMargin: Math.round(20 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            height: Math.round(44 * root.uiScale)
            spacing: Math.round(8 * root.uiScale)

            Repeater {
                model: [
                    { "id": "cpu", "label": "CPU", "tone": theme.ready },
                    { "id": "memory", "label": "RAM", "tone": theme.focus },
                    { "id": "disk", "label": "DISK", "tone": theme.warning }
                ]

                delegate: Rectangle {
                    width: Math.round(76 * root.uiScale)
                    height: runtimeMetricRail.height
                    radius: Math.round(theme.radiusControl * root.uiScale)
                    color: theme.instrumentRaised
                    border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                    border.width: 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(9 * root.uiScale)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(3, Math.round(4 * root.uiScale))
                        height: Math.round(24 * root.uiScale)
                        radius: Math.round(2 * root.uiScale)
                        color: modelData.tone
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(20 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(7 * root.uiScale)
                        text: modelData.label
                        color: theme.instrumentMuted
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(9 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(20 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(7 * root.uiScale)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Math.round(7 * root.uiScale)
                        text: root.headerMetricValue(modelData.id)
                        color: theme.instrumentInk
                        elide: Text.ElideRight
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(12 * root.uiScale)
                        font.weight: Font.Bold
                    }
                }
            }
        }

        Column {
            anchors.left: brandBlock.right
            anchors.leftMargin: Math.round(24 * root.uiScale)
            anchors.right: runtimeMetricRail.visible ? runtimeMetricRail.left : headerActions.left
            anchors.rightMargin: Math.round(20 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * root.uiScale)

            Text {
                width: parent.width
                text: root.boardLabel
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.family: theme.displayFont
                font.pixelSize: Math.round(17 * root.uiScale)
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: (boardProfile.socFamily.length > 0 ? boardProfile.socFamily : "PROFILE PENDING")
                      + "  /  " + (boardProfile.detectionSource.length > 0 ? boardProfile.detectionSource : "本地检测")
                color: theme.instrumentMuted
                elide: Text.ElideRight
                font.family: theme.monoFont
                font.pixelSize: Math.round(10 * root.uiScale)
            }
        }
    }

    Rectangle {
        id: commandStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: instrumentHeader.bottom
        height: Math.round(50 * root.uiScale)
        color: theme.surfaceRaised

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.rule
        }

        Row {
            id: commandNav
            anchors.left: parent.left
            anchors.leftMargin: root.outerGutter
            anchors.verticalCenter: parent.verticalCenter
            height: Math.round(36 * root.uiScale)
            spacing: Math.round(6 * root.uiScale)

            Repeater {
                model: [
                    { "label": "测试目录", "mode": "tools", "app": "" },
                    { "label": "资源监测", "mode": "monitor", "app": "Monitor" }
                ]

                delegate: Button {
                    id: commandAction
                    visible: modelData.app.length === 0 || BoardLabCapabilities.appEnabled(modelData.app, boardProfile)
                    width: Math.round((root.width < Math.round(680 * root.uiScale) ? 80 : 102) * root.uiScale)
                    height: parent.height
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.name: modelData.label
                    onClicked: {
                        if (modelData.app.length > 0)
                            root.appRequested(modelData.app)
                        else
                            root.showTools()
                    }

                    background: Rectangle {
                        radius: Math.round(theme.radiusControl * root.uiScale)
                        color: root.activeSection === modelData.mode ? theme.accentSoft
                              : (commandAction.down ? theme.surfaceSoft : (commandAction.hovered ? theme.surfaceSoft : theme.withAlpha(theme.surfaceRaised, 0)))
                        border.color: commandAction.activeFocus ? theme.focus
                                      : (root.activeSection === modelData.mode ? theme.withAlpha(theme.accent, 0.36) : theme.withAlpha(theme.surfaceRaised, 0))
                        border.width: commandAction.activeFocus ? 2 : 1
                    }

                    contentItem: Text {
                        text: root.width < Math.round(680 * root.uiScale) && modelData.mode === "tools" ? "目录" : modelData.label
                        color: root.activeSection === modelData.mode ? theme.accentHover : theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(11 * root.uiScale)
                        font.weight: root.activeSection === modelData.mode ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }

        Text {
            anchors.left: commandNav.right
            anchors.leftMargin: Math.round(14 * root.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: root.outerGutter
            anchors.verticalCenter: parent.verticalCenter
            visible: root.width >= Math.round(860 * root.uiScale)
            text: root.runtimeStatus
            color: theme.inkFaint
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            font.family: theme.monoFont
            font.pixelSize: Math.round(10 * root.uiScale)
        }
    }

    Item {
        id: workbench
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: commandStrip.bottom
        anchors.bottom: parent.bottom
        clip: true

        Flickable {
            id: workScroller
            objectName: "home-tool-scroller"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true
            contentWidth: width
            contentHeight: workColumn.height + root.outerGutter * 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            Column {
                id: workColumn
                x: Math.max(0, Math.round((workScroller.width - width) / 2))
                y: root.outerGutter
                width: root.workSurfaceWidth
                spacing: Math.round((root.compactHeight ? 12 : 16) * root.uiScale)

                Item {
                    id: catalogHeader
                    width: parent.width
                    height: Math.round((root.compactHeight ? 56 : 72) * root.uiScale)

                    Column {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(2 * root.uiScale)
                        anchors.right: catalogMeta.visible ? catalogMeta.left : parent.right
                        anchors.rightMargin: catalogMeta.visible ? Math.round(20 * root.uiScale) : 0
                        spacing: Math.round(5 * root.uiScale)

                        Text {
                            width: parent.width
                            text: "开发板工具"
                            color: theme.inkStrong
                            elide: Text.ElideRight
                            font.family: theme.displayFont
                            font.pixelSize: root.compactHeight ? 22 : 28
                            font.weight: Font.Bold
                        }

                        Text {
                            width: parent.width
                            text: boardProfile.configurationError.length > 0
                                  ? "板型配置异常：" + boardProfile.configurationError
                                  : root.boardLabel + " · " + root.availableToolCount + " 项已启用"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.family: theme.bodyFont
                            font.pixelSize: Math.round(12 * root.uiScale)
                        }
                    }

                    Item {
                        id: catalogMeta
                        visible: root.width >= Math.round(900 * root.uiScale)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.round(330 * root.uiScale)
                        height: Math.round(40 * root.uiScale)

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            height: parent.height
                            spacing: Math.round(8 * root.uiScale)

                            Rectangle {
                                width: Math.round(204 * root.uiScale)
                                height: parent.height
                                radius: Math.round(theme.radiusControl * root.uiScale)
                                color: theme.surfaceSoft
                                border.color: theme.rule
                                border.width: 1

                                LucideIcon {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Math.round(10 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.round(18 * root.uiScale)
                                    height: width
                                    name: "circuit-board"
                                    color: theme.accentHover
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Math.round(38 * root.uiScale)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Math.round(10 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.boardLabel
                                    color: theme.inkStrong
                                    elide: Text.ElideRight
                                    font.family: theme.bodyFont
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }
                            }

                            Rectangle {
                                width: Math.round(118 * root.uiScale)
                                height: parent.height
                                radius: Math.round(theme.radiusControl * root.uiScale)
                                color: theme.instrument
                                border.color: theme.withAlpha(theme.instrumentInk, 0.14)
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: root.visibleAppModel.length + " / " + root.availableToolCount + " 项"
                                    color: theme.instrumentInk
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(10 * root.uiScale)
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: theme.rule
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    BoardToolsComboBox {
                        id: categoryFilter
                        objectName: "home-category-filter"
                        width: Math.min(180, parent.width * 0.38)
                        height: 48
                        model: root.categories
                        currentIndex: root.selectedCategory
                        Accessible.name: "工具分类"
                        onActivated: root.selectedCategory = currentIndex
                    }

                    BoardToolsTextField {
                        id: catalogSearch
                        objectName: "home-tool-search"
                        width: parent.width - categoryFilter.width - 56 - parent.spacing * 2
                        height: 48
                        placeholderText: "搜索工具"
                        Accessible.name: "搜索工具"
                        virtualKeyboardEnabled: kioskMode
                        text: root.searchQuery
                        onTextChanged: root.searchQuery = text
                        onAccepted: {
                            if (root.visibleAppModel.length === 1)
                                root.appRequested(root.visibleAppModel[0][0])
                        }
                    }

                    Button {
                        id: clearSearch
                        objectName: "home-clear-filter"
                        width: 48
                        height: 48
                        enabled: root.searchQuery.length > 0 || root.selectedCategory !== 0
                        Accessible.name: "清除筛选"
                        ToolTip.visible: hovered
                        ToolTip.text: "清除筛选"
                        hoverEnabled: true
                        onClicked: {
                            root.searchQuery = ""
                            root.selectedCategory = 0
                        }
                        contentItem: LucideIcon {
                            name: "x"
                            color: clearSearch.enabled ? theme.ink : theme.disabled
                        }
                        background: Rectangle {
                            color: clearSearch.down ? theme.surfaceSoft : theme.surfaceRaised
                            radius: theme.radiusControl
                            border.color: clearSearch.activeFocus ? theme.focus : theme.rule
                            border.width: clearSearch.activeFocus ? 2 : 1
                        }
                    }
                }

                Grid {
                    id: toolGrid
                    width: parent.width
                    columns: root.toolColumns
                    rowSpacing: root.gridGap
                    columnSpacing: root.gridGap
                    readonly property int itemCount: root.visibleAppModel.length
                    readonly property int rowCount: Math.max(1, Math.ceil(itemCount / columns))
                    readonly property real tileWidth: Math.max(1, (width - root.gridGap * Math.max(0, columns - 1)) / columns)
                    height: itemCount > 0 ? rowCount * root.toolTileHeight + Math.max(0, rowCount - 1) * root.gridGap : 0

                    Repeater {
                        model: root.visibleAppModel

                        delegate: WorkspaceTile {
                            objectName: "home-tool-" + modelData[0]
                            width: toolGrid.tileWidth
                            height: root.toolTileHeight
                            title: modelData[1]
                            iconName: modelData[2]
                            detail: modelData[3]
                            indexLabel: String(index + 1)
                            topColor: root.toolAccent(modelData)
                            uiScale: root.catalogTileScale
                            onTriggered: root.appRequested(modelData[0])
                            onFocusEntered: root.revealTile(this)
                        }
                    }
                }

                Rectangle {
                    visible: root.visibleAppModel.length === 0
                    width: parent.width
                    height: Math.round(126 * root.uiScale)
                    radius: Math.round(theme.radiusPanel * root.uiScale)
                    color: theme.warningSoft
                    border.color: theme.withAlpha(theme.warning, 0.42)
                    border.width: 1

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(18 * root.uiScale)
                        text: root.availableToolCount > 0 ? "没有匹配的工具" : "暂无已启用工具"
                        color: theme.warning
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(10 * root.uiScale)
                        font.weight: Font.Bold
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(18 * root.uiScale)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Math.round(18 * root.uiScale)
                        text: root.availableToolCount > 0 ? "当前分类或搜索条件下没有结果。"
                                                         : "板型配置未提供工具入口。"
                        color: theme.ink
                        wrapMode: Text.WordWrap
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                }

                Item {
                    width: parent.width
                    height: Math.round(8 * root.uiScale)
                }
            }
        }
    }
}
