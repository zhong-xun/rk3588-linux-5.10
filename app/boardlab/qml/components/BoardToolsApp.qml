import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    signal closed()
    signal toolRequested(string tool)

    property real uiScale: 1.0
    property string currentTool: "GPIO"
    property string standaloneTool: ""
    readonly property bool standaloneMode: standaloneTool.length > 0
    readonly property string activeTool: standaloneMode ? standaloneTool : currentTool
    property bool gpioHigh: true
    property bool gpioInputMode: true
    property bool gpioAutoPoll: false
    property string gpioPin: "GPIO1_B1"
    property string gpioReadback: "未采样"
    property string uartDevice: ""
    property int uartBaudRate: 115200
    property int uartDataBits: 8
    property string uartParity: "None"
    property int uartStopBits: 1
    property string uartFlowControl: "None"
    property string uartLog: "串口控制台就绪\n"
    property string uartResultText: "等待串口操作"
    property string uartTxStatusText: "TX 等待发送"
    property string uartRxStatusText: "RX 等待读取"
    property string uartTxFrameText: "55 AA 01 00 FE"
    property string uartRxSampleText: "等待 RX 样本"
    property string pendingPingInterface: ""
    property string pendingPingTarget: ""
    property string pendingPingContext: ""
    property string pendingEthernetInterface: ""
    property string pendingEthernetMode: ""
    property string pendingUartDevice: ""
    property string pendingUartFormat: ""
    property string pendingCanInterface: ""
    property string pendingCanMode: ""
    property string pendingCellularInterface: ""
    property string pendingCellularApn: ""
    property string pendingPreviewContext: ""
    property string pendingPreviewDevice: ""
    property string pendingWatchdogDevice: ""
    property int backlightTargetPercent: boardBackend.backlightAvailable ? boardBackend.backlightPercent : 50
    property int ethernetInterfaceIndex: 0
    property string canInterface: ""
    property int canBitrate: 1000000
    property int canDataBitrate: 3000000
    property bool canFd: false
    property bool canControlEnabled: false
    property bool canLoopStarting: false
    property bool canLoopRunning: false
    property bool canLoopStopping: false
    property bool canLoopStopWhenReady: false
    property string canLoopInterface: ""
    property string canLog: "CAN 控制台就绪\n"
    property string canResultText: "等待总线操作"
    property string canBusStatusText: "总线等待打开"
    property string canTxStatusText: "TX 等待发送"
    property string canRxStatusText: "RX 等待读取"
    property string canTxFrameText: "STD/EFF/RTR 0x11"
    property string canRxSampleText: "等待 CAN 样本"
    property int beepPercent: 50
    property int fanManualPercent: 50
    // Music and the buzzer share the audio backend, but they are separate
    // customer-facing tools.  Keep their result history separate so a music
    // result never appears as the result of a buzzer action after navigation.
    property string musicResultText: "等待音乐与录音操作"
    property string buzzerResultText: "等待蜂鸣器操作"
    property string fanResultText: "等待风扇操作"
    property string ledResultText: "等待 LED 操作"
    property string ledDeviceName: ""
    property int ledPercent: 100
    property bool audioBackendBusy: false
    property string pendingAudioOperation: ""
    property string activeAudioOperation: ""
    property string ethernetLog: "以太网工具就绪\n先确认接口、网线链路与 IPv4，再执行默认 Ping 百度连通性测试。\n"
    property string ethernetResultText: "等待网络操作"
    property string ethernetPingResultText: "等待 Ping"
    property bool cellularControlEnabled: false
    property string cellularLog: "=== 4G 模组监视器 ===\n就绪。\n"
    property string cellularResultText: "等待拨号操作"
    property string cellularDetailText: "等待状态详情"
    property string cellularApn: ""
    property string cellularInterface: ""
    property bool ethernetEditEnabled: false
    property string ethernetStaticIp: "192.168.1.100"
    property string ethernetNetmask: "255.255.255.0"
    property string ethernetGateway: "192.168.1.1"
    property string ethernetDns1: "8.8.8.8"
    property string ethernetDns2: "114.114.114.114"
    property string ethernetIpMode: "dhcp"
    property string ethernetPingTarget: "www.baidu.com"
    property string cameraLog: "摄像头实时预览就绪\n"
    property string cameraStateText: "实时预览未启动"
    property string cameraMode: "mipi"
    property int cameraDeviceIndex: 0
    property string cameraSelectedPath: ""
    property bool cameraAutoSelectionInitialized: false
    property bool cameraPreviewRequested: false
    property string hdmiInLog: "HDMI 输入测试就绪\n"
    property string hdmiInStateText: "采集未启动"
    property int hdmiInDeviceIndex: 0
    property bool hdmiInPreviewRequested: false
    // This surface lives inside a work-surface content Component, whose id is
    // not visible from this root item. Keep an explicit root reference for
    // the HDMI-IN fullscreen overlay so entering or leaving it cannot throw a
    // QML ReferenceError and leave the input-blocking overlay in place.
    property var hdmiInPreviewSurface: null
    // Only HDMI-IN has a fullscreen mode. It mirrors the inline appsink frames
    // instead of opening a second V4L2 capture pipeline.
    property bool videoFullscreenRequested: false
    property string videoFullscreenSource: ""
    property string videoFullscreenDevice: ""
    property bool videoFullscreenRestoreInline: false
    readonly property bool videoFullscreenVisible: videoFullscreenRequested
    property string watchdogLog: "看门狗状态就绪\n"
    property string watchdogResultText: "等待状态读取"
    property string watchdogStatusDetail: "等待 wdctl 状态"
    property string watchdogDevice: ""
    property string filesStatusText: "文件浏览就绪"
    property int audioVolumeTargetPercent: boardBackend.audioVolumePercent
    property int audioPlaybackDeviceIndex: 0
    property int audioCaptureDeviceIndex: 0
    property int watchdogTimeout: 5
    property bool watchdogResetConfirmed: false
    readonly property bool playbackDeviceAvailable: root.selectedPlaybackDevice().length > 0
    readonly property bool captureDeviceAvailable: root.selectedCaptureDevice().length > 0
    readonly property var cellularModem: boardBackend.cellularModemStatus || ({})
    readonly property bool cellularUsbModemDetected: cellularModem && cellularModem.detected === true
    readonly property bool cellularDialerReady: cellularModem && cellularModem.dialerAvailable === true
    readonly property bool cellularDhcpReady: !cellularModem || cellularModem.dhcpClientRequired !== true
                                              || cellularModem.dhcpClientAvailable === true
    readonly property string cellularAutoInterface: {
        var interfaceName = cellularModem && cellularModem.preferredInterface
                ? String(cellularModem.preferredInterface).trim() : ""
        if (interfaceName.length === 0)
            interfaceName = String(boardBackend.preferredCellularInterface() || "").trim()
        return interfaceName
    }
    // A Quectel CM process may create usb0/wwan0 after it is launched, so an
    // identified USB modem is sufficient to expose the tool before a network
    // node exists.
    readonly property bool cellularAvailable: cellularUsbModemDetected || boardBackend.cellularDevices.length > 0
    readonly property bool cellularCanStart: cellularAvailable && (!cellularUsbModemDetected || (cellularDialerReady && cellularDhcpReady))
    readonly property bool watchdogAvailable: boardBackend.watchdogDevices.length > 0
    readonly property string selectedWatchdogIdentity: root.selectedWatchdogDevice()
    readonly property bool operationBusy: boardBackend.backendBusy || root.audioBackendBusy || boardBackend.audioOperationBusy
    property int touchTarget: Math.round(44 * uiScale)
    property int inputFontSize: Math.round(13 * uiScale)
    property int gutter: Math.round((width >= 1600 ? 56 : (width >= 1100 ? 32 : 18)) * uiScale)
    readonly property bool compactHeader: width < Math.round(720 * uiScale)
    // The command strip carries the runtime summary below this width. Keeping
    // only icon-level navigation in the instrument header prevents controls
    // from truncating while preserving the 44 px touch target.
    readonly property bool headerIconActions: width < 980
    readonly property bool showHeaderStatus: width >= 980
    // 1920×1080 is the primary presentation surface.  Keep the tool workbench
    // generous and remove the old decorative footer from its vertical budget.
    property int workTop: Math.round(88 * uiScale)
    property int workBottom: Math.round(20 * uiScale)
    property int hardwareSurfaceCount: boardBackend.ethernetInterfaces.length
                                       + boardBackend.canInterfaces.length
                                       + boardBackend.cellularDevices.length
                                       + boardBackend.serialDevices.length
                                       + boardBackend.videoDevices.length
                                       + boardBackend.watchdogDevices.length
                                       + (boardBackend.backlightAvailable ? 1 : 0)
                                       + boardBackend.ledDevices.length
    property bool localhostBridge: localWebServer.bindAddress === "127.0.0.1"
                                   || localWebServer.bindAddress === "::1"
                                   || localWebServer.bindAddress === "localhost"
    property int readinessScore: Math.min(100,
                                          ((systemInfo.cpuUsage < 85 && systemInfo.memoryUsage < 90 && systemInfo.diskUsage < 92) ? 22 : 12)
                                          + (hardwareSurfaceCount >= 3 ? 22 : (hardwareSurfaceCount > 0 ? 14 : 8))
                                          + (localWebServer.running ? 20 : 8)
                                          + 18
                                          + (localhostBridge ? 18 : 12))

    onActiveToolChanged: {
        if (!root.isAudioTool(root.activeTool))
            root.resetAudioUiState()
        if (root.activeTool !== "CAN") {
            root.requestCanLoopStop("已离开 CAN 页面，正在停止循环发送")
            root.canControlEnabled = false
        }
        if (root.activeTool !== "Camera")
            root.stopCameraPreview("已离开摄像头页面，实时预览已停止")
        if (root.activeTool !== "HDMIIN")
            root.stopHdmiInPreview("已离开 HDMI 输入页面，实时预览已停止")
        if (root.activeTool !== "Watchdog")
            root.watchdogResetConfirmed = false
        if (root.videoFullscreenVisible && !root.videoFullscreenMatchesActiveTool())
            root.closeVideoFullscreen(false)
    }

    onVisibleChanged: {
        if (!root.visible) {
            root.resetAudioUiState()
            root.prepareForNavigation()
        }
    }

    onCellularAvailableChanged: {
        if (!root.cellularAvailable)
            root.cellularControlEnabled = false
    }

    onCellularAutoInterfaceChanged: {
        if (root.cellularInterface.length === 0 && root.cellularAutoInterface.length > 0)
            root.cellularInterface = root.cellularAutoInterface
    }

    onWatchdogAvailableChanged: {
        if (!root.watchdogAvailable)
            root.watchdogResetConfirmed = false
    }

    onSelectedWatchdogIdentityChanged: root.watchdogResetConfirmed = false

    function prepareForNavigation() {
        root.requestCanLoopStop("正在停止 CAN 循环发送")
        root.canControlEnabled = false
        root.stopCameraPreview("已离开摄像头页面，实时预览已停止")
        root.stopHdmiInPreview("已离开 HDMI 输入页面，实时预览已停止")
        root.watchdogResetConfirmed = false
        if (root.videoFullscreenVisible)
            root.closeVideoFullscreen(false)
    }

    function toolDisplayName(tool) {
        var names = {
            "Overview": "总览",
            "GPIO": "GPIO",
            "UART": "串口 / 485",
            "Backlight": "背光",
            "Music": "音乐与录音",
            "Buzzer": "蜂鸣器",
            "Fan": "风扇",
            "LED": "板载 LED",
            "Ethernet": "以太网",
            "CAN": "CAN",
            "Cellular": "4G / 5G",
            "Camera": "摄像头",
            "HDMIIN": "HDMI 输入",
            "Watchdog": "看门狗",
            "Files": "文件"
        }
        return names[tool] || tool
    }

    function toolSubtitle(tool) {
        var names = {
            "Overview": "板端发现、运行余量与测试入口",
            "GPIO": "sysfs 输入输出、电平读回、自动轮询",
            "UART": "tty 设备、波特率、数据位、校验、停止位、流控、TX/RX 样本",
            "Backlight": "背光设备、亮度写入和状态确认",
            "Music": "耳机 WAV、录音回放与系统音量",
            "Buzzer": "PWM 频率、音量与心跳提示",
            "Fan": "温控自动模式与手动 PWM 转速",
            "LED": "板载指示灯亮度与闪烁验证",
            "Ethernet": "链路、MAC、IPv4、DHCP、静态 IP、Ping",
            "CAN": "SocketCAN、bitrate、RX、TX、source 循环发送",
            "Cellular": "Quectel 拨号、APN、接口、状态与 Ping",
            "Camera": "选择 rkisp/USB 摄像头采集节点验证",
            "HDMIIN": "自动选择 rk_hdmirx HDMI 输入节点验证",
            "Watchdog": "wdctl 状态与现场可控复位测试",
            "Files": "板端文件浏览和目录定位"
        }
        return names[tool] || ""
    }

    // Tool identity comes from copy and the current work surface, not a
    // rainbow of action colours. Moss remains the one normal forward action.
    function toolAccent(tool) {
        return theme.accent
    }

    function toolCount(tool) {
        if (tool === "GPIO")
            return boardBackend.gpioPreview(root.gpioPin, true).split(" -> ")[0]
        if (tool === "UART")
            return boardBackend.serialDevices.length + " tty"
        if (tool === "Backlight")
            return boardBackend.backlightAvailable ? boardBackend.backlightPercent + "%" : "无"
        if (tool === "Music")
            return "ALSA"
        if (tool === "Buzzer")
            return boardBackend.buzzerAvailable
                    ? (boardBackend.buzzerControlMethod === "input-event" ? "输入事件" : "不可用")
                    : "未发现"
        if (tool === "Fan")
            return boardBackend.fanStatus && boardBackend.fanStatus.available
                    ? (boardBackend.fanStatus.percent + "%") : "未发现"
        if (tool === "LED")
            return boardBackend.ledAvailable ? boardBackend.ledDevices.length + " 路" : "无"
        if (tool === "Ethernet")
            return boardBackend.ethernetInterfaces.length + " eth"
        if (tool === "CAN")
            return boardBackend.canInterfaces.length + " can"
        if (tool === "Cellular")
            return root.cellularUsbModemDetected
                    ? root.cellularModemLabel()
                    : boardBackend.cellularDevices.length + " 节点"
        if (tool === "Camera")
            return root.selectedCameraDevice().length > 0 ? root.selectedCameraDevice() : "未检测"
        if (tool === "HDMIIN")
            return boardBackend.hdmiInVideoDevice.length > 0 ? boardBackend.hdmiInVideoDevice : "未检测"
        if (tool === "Watchdog")
            return boardBackend.watchdogDevices.length + " wd"
        if (tool === "Files")
            return "浏览"
        return root.hardwareSurfaceCount + " 项"
    }

    function cellularModemLabel() {
        var modem = root.cellularModem
        if (modem && modem.detected) {
            var name = String(modem.displayName || modem.module || "已识别 USB 模组")
            var usbId = String(modem.usbId || "").trim()
            return usbId.length > 0 ? name + " · " + usbId : name
        }
        if (modem && modem.state === "multiple-modems")
            return "检测到多个受支持模组"
        if (modem && modem.state === "identity-conflict")
            return "USB 身份冲突"
        if (modem && modem.state === "lsusb-unavailable")
            return "无法读取 lsusb"
        return boardBackend.cellularDevices.length > 0 ? boardBackend.cellularDevices.join(", ") : "未发现模组"
    }

    function cellularDialerLabel() {
        var modem = root.cellularModem
        if (modem && modem.dialer) {
            return String(modem.dialer)
                    + (modem.dialerAvailable === true ? "" : " · 不可用")
        }
        return root.cellularUsbModemDetected ? "未找到兼容拨号程序" : "等待 USB 识别"
    }

    function cellularDhcpLabel() {
        var modem = root.cellularModem
        if (!modem || modem.dhcpClientRequired !== true)
            return "模组内置 DHCP"
        return modem.dhcpClientAvailable === true ? "dhclient 已就绪" : "缺少 dhclient"
    }

    function cellularInterfaceDisplay() {
        return root.cellularAutoInterface.length > 0
                ? root.cellularAutoInterface
                : "等待拨号程序创建接口"
    }

    function cellularInterfaceForRequest() {
        return root.cellularInterface.length > 0
                ? root.cellularInterface.trim()
                : root.cellularAutoInterface
    }

    function selectedEthernet() {
        if (boardBackend.ethernetInterfaces.length === 0)
            return {"name": "No Ethernet", "state": "not found", "address": "", "ipv4": "", "ipv4Assigned": false, "linkUp": false, "carrier": false, "diagnostic": "未检测到以太网接口"}
        var index = Math.max(0, Math.min(root.ethernetInterfaceIndex, boardBackend.ethernetInterfaces.length - 1))
        return boardBackend.ethernetInterfaces[Math.min(index, boardBackend.ethernetInterfaces.length - 1)]
    }

    function ethernetFlag(value) {
        return value === true || value === 1 || String(value).toLowerCase() === "true" || String(value) === "1"
    }

    function ethernetHasField(iface, field) {
        return iface && iface[field] !== undefined && iface[field] !== null
    }

    function ethernetCarrierPresent() {
        var iface = root.selectedEthernet()
        if (root.ethernetHasField(iface, "carrier"))
            return root.ethernetFlag(iface.carrier)
        if (root.ethernetHasField(iface, "linkUp"))
            return root.ethernetFlag(iface.linkUp)
        var state = String(iface.state || "").toLowerCase()
        return state === "up" || state === "connected"
    }

    function ethernetLinkLabel() {
        var iface = root.selectedEthernet()
        var state = String(iface.state || "unknown").toLowerCase()
        if (state === "not found")
            return "未检测到接口"
        if (root.ethernetHasField(iface, "carrier") || root.ethernetHasField(iface, "linkUp"))
            return root.ethernetCarrierPresent() ? "网线已连接" : "未检测到网线链路"
        if (state === "up" || state === "connected")
            return "链路已连接"
        if (state === "down" || state === "disconnected")
            return "链路未连接"
        return state === "unknown" ? "链路状态未知" : state
    }

    function ethernetIpv4Label() {
        var address = root.ethernetIpv4Address()
        if (address.length > 0)
            return address
        var iface = root.selectedEthernet()
        if (root.ethernetFlag(iface.ipv4Assigned))
            return "已分配，正在读取地址"
        return root.ethernetCarrierPresent() ? "尚未分配 IPv4" : "等待网线连接"
    }

    // Board profiles and NetworkManager expose the same address under
    // different field names. Prefer a real address and never promote a
    // placeholder into the prominent IPv4 status card.
    function ethernetIpv4Address() {
        var iface = root.selectedEthernet()
        var fields = ["ipv4", "ipv4Address", "ipAddress"]
        for (var index = 0; index < fields.length; ++index) {
            var address = String(iface[fields[index]] || "").trim()
            if (address.length === 0 || address === "--"
                    || address === "未分配"
                    || address.toLowerCase() === "not assigned")
                continue
            return address
        }
        return ""
    }

    function ethernetPingSourceLabel(interfaceName) {
        var iface = String(interfaceName || root.selectedEthernet().name || "")
        var address = root.ethernetIpv4Address()
        return address.length > 0 ? iface + "（IPv4 " + address + "）" : iface
    }

    function ethernetDiagnosticLabel() {
        var iface = root.selectedEthernet()
        var diagnostic = String(iface.diagnostic || "").trim()
        if (diagnostic.length > 0)
            return diagnostic
        if (String(iface.name || "").indexOf("No Ethernet") === 0)
            return "未检测到以太网接口"
        if (!root.ethernetCarrierPresent())
            return "请检查网线、交换机端口和供电"
        if (root.ethernetIpv4Label().indexOf("尚未") === 0)
            return "链路正常，等待 DHCP 分配 IPv4"
        return "链路和 IPv4 已就绪，可执行 Ping 测试"
    }

    function ethernetPingTargetLabel() {
        var target = root.ethernetPingTarget.trim()
        return target.length > 0 ? target : "www.baidu.com"
    }

    function ethernetLinkAccent() {
        if (root.ethernetCarrierPresent())
            return root.toolAccent("Ethernet")
        if (String(root.selectedEthernet().name || "").indexOf("No Ethernet") === 0)
            return theme.inkFaint
        return theme.warning
    }

    function valueIndex(values, value, fallbackIndex) {
        for (var index = 0; index < values.length; ++index) {
            if (String(values[index]) === String(value))
                return index
        }
        return fallbackIndex
    }

    function selectedSerialDevice() {
        var device = root.uartDevice
        if (root.valueIndex(boardBackend.serialDevices, device, -1) < 0)
            device = boardBackend.serialDevices.length > 0 ? boardBackend.serialDevices[0] : ""
        if (device.length === 0 || device === "No UART" || device.indexOf("No serial") === 0)
            return ""
        return device
    }

    function serialReady() {
        var device = root.selectedSerialDevice()
        return device.length > 0 && device.indexOf("No serial") !== 0
    }

    function selectedCanInterface() {
        if (root.valueIndex(boardBackend.canInterfaces, root.canInterface, -1) >= 0)
            return root.canInterface
        if (boardBackend.canInterfaces.length > 0)
            return boardBackend.canInterfaces[0]
        return "No CAN"
    }

    function canReady() {
        return root.selectedCanInterface().indexOf("No CAN") !== 0
    }

    function canModeText() {
        return root.canBitrate + " / Classic"
    }

    function uartFormatText() {
        var parityCode = root.uartParity === "Even" ? "E" : (root.uartParity === "Odd" ? "O" : "N")
        return root.uartBaudRate + " / " + root.uartDataBits + parityCode + root.uartStopBits + " / " + root.uartFlowControl
    }

    function cameraDeviceOptions() {
        var options = []
        for (var i = 0; i < boardBackend.cameraCaptureDevices.length; ++i)
            options.push(boardBackend.cameraCaptureDevices[i])
        if (options.length === 0) {
            for (var j = 0; j < boardBackend.cameraVideoDevices.length; ++j) {
                var path = boardBackend.cameraVideoDevices[j]
                // A raw fallback path has no type evidence.  The backend will
                // use the current discovered capture metadata instead of
                // inferring USB/MIPI from a kernel-dependent video number.
                options.push({"path": path, "label": path, "group": "", "name": "", "mode": ""})
            }
        }
        // The backend publishes an evidence-based preferred flag.  Keep that
        // choice first in Qt as well: RK3588 rkcif raw nodes often have lower
        // /dev/video numbers than the viewable rkisp_mainpath stream.
        options.sort(function(left, right) {
            var leftPreferred = left && left.preferred === true
            var rightPreferred = right && right.preferred === true
            if (leftPreferred !== rightPreferred)
                return leftPreferred ? -1 : 1
            var leftPath = left && left.path ? String(left.path) : ""
            var rightPath = right && right.path ? String(right.path) : ""
            if (leftPath === rightPath)
                return 0
            return leftPath < rightPath ? -1 : 1
        })
        return options
    }

    function cameraDeviceDropdownOptions() {
        var options = root.cameraDeviceOptions()
        if (options.length > 0)
            return options
        return [{"path": "", "label": "No camera capture node", "group": "", "name": "No camera"}]
    }

    function selectedCameraDevice() {
        var options = root.cameraDeviceOptions()
        if (options.length === 0)
            return ""
        var index = Math.max(0, Math.min(root.cameraDeviceIndex, options.length - 1))
        return options[index].path ? options[index].path : ""
    }

    function preferredCameraDeviceIndex(options) {
        for (var index = 0; index < options.length; ++index) {
            if (options[index] && options[index].preferred === true)
                return index
        }
        return options.length > 0 ? 0 : -1
    }

    function ensureCameraDeviceSelection() {
        var options = root.cameraDeviceOptions()
        if (options.length === 0) {
            root.cameraDeviceIndex = 0
            root.cameraSelectedPath = ""
            root.cameraAutoSelectionInitialized = false
            root.cameraPreviewRequested = false
            return
        }

        var selectedIndex = -1
        for (var index = 0; index < options.length; ++index) {
            if (options[index].path === root.cameraSelectedPath) {
                selectedIndex = index
                break
            }
        }
        if (!root.cameraAutoSelectionInitialized || selectedIndex < 0) {
            selectedIndex = root.preferredCameraDeviceIndex(options)
            root.cameraDeviceIndex = selectedIndex
            root.cameraSelectedPath = options[selectedIndex].path || ""
            root.cameraAutoSelectionInitialized = true
        } else {
            root.cameraDeviceIndex = selectedIndex
        }
    }

    function selectCameraDevice(index) {
        var options = root.cameraDeviceOptions()
        if (index < 0 || index >= options.length)
            return
        var nextPath = options[index].path || ""
        if (root.cameraPreviewRequested && nextPath !== root.cameraSelectedPath) {
            root.cameraPreviewRequested = false
            root.cameraStateText = "已停止预览；请选择新节点后重新启动"
            root.appendBoundedLog("cameraLog", "SWITCH camera device " + root.cameraSelectedPath + " -> " + nextPath + "\n")
        }
        root.cameraDeviceIndex = index
        root.cameraSelectedPath = nextPath
        root.cameraAutoSelectionInitialized = true
    }

    function stopCameraPreview(stateText) {
        if (!root.cameraPreviewRequested)
            return
        var device = root.selectedCameraDevice()
        root.cameraPreviewRequested = false
        root.cameraStateText = stateText
        root.appendBoundedLog("cameraLog", "STOP live GStreamer appsink preview " + device + "\n")
    }

    function selectedCameraMode() {
        var options = root.cameraDeviceOptions()
        if (options.length === 0)
            return "mipi"
        var index = Math.max(0, Math.min(root.cameraDeviceIndex, options.length - 1))
        var option = options[index]
        if (option.mode && option.mode.length > 0)
            return option.mode
        var text = ((option.label ? option.label : "") + " "
                    + (option.name ? option.name : "") + " "
                    + (option.group ? option.group : "")).toLowerCase()
        if (text.indexOf("usb") >= 0 || text.indexOf("uvc") >= 0 || text.indexOf("webcam") >= 0)
            return "usb"
        return "mipi"
    }

    function cameraDeviceLabels() {
        var labels = []
        var options = root.cameraDeviceOptions()
        for (var i = 0; i < options.length; ++i)
            labels.push(options[i].label ? options[i].label : options[i].path)
        return labels
    }

    function hdmiInDeviceLabels() {
        var labels = []
        for (var i = 0; i < boardBackend.hdmiInCaptureDevices.length; ++i)
            labels.push(boardBackend.hdmiInCaptureDevices[i].label)
        if (labels.length === 0) {
            for (var j = 0; j < boardBackend.hdmiInVideoDevices.length; ++j)
                labels.push(boardBackend.hdmiInVideoDevices[j])
        }
        return labels
    }

    function hdmiInDeviceOptions() {
        var options = []
        for (var i = 0; i < boardBackend.hdmiInCaptureDevices.length; ++i)
            options.push(boardBackend.hdmiInCaptureDevices[i])
        if (options.length === 0) {
            for (var j = 0; j < boardBackend.hdmiInVideoDevices.length; ++j) {
                var path = boardBackend.hdmiInVideoDevices[j]
                options.push({"path": path, "label": path, "group": "", "name": "", "mode": "hdmiin"})
            }
        }
        return options
    }

    function hdmiInDeviceDropdownOptions() {
        var options = root.hdmiInDeviceOptions()
        if (options.length > 0)
            return options
        return [{"path": "", "label": "No HDMIIN capture node", "group": "", "name": "No HDMIIN"}]
    }

    function selectedHdmiInDevice() {
        var options = root.hdmiInDeviceOptions()
        if (options.length === 0)
            return ""
        var index = Math.max(0, Math.min(root.hdmiInDeviceIndex, options.length - 1))
        return options[index].path ? options[index].path : ""
    }

    function selectHdmiInDevice(index) {
        var options = root.hdmiInDeviceOptions()
        if (index < 0 || index >= options.length)
            return
        var currentPath = root.selectedHdmiInDevice()
        var nextPath = options[index].path || ""
        if (root.videoFullscreenSource === "hdmiin" && nextPath !== currentPath)
            root.closeVideoFullscreen(false)
        if (root.hdmiInPreviewRequested && nextPath !== currentPath) {
            root.hdmiInPreviewRequested = false
            root.hdmiInStateText = "已停止预览；请选择新 HDMI 输入节点后重新启动"
            root.appendBoundedLog("hdmiInLog", "SWITCH HDMIIN device " + currentPath + " -> " + nextPath + "\n")
        }
        root.hdmiInDeviceIndex = index
    }

    function stopHdmiInPreview(stateText) {
        if (root.videoFullscreenVisible)
            root.closeVideoFullscreen(false)
        if (!root.hdmiInPreviewRequested)
            return
        var device = root.selectedHdmiInDevice()
        root.hdmiInPreviewRequested = false
        root.hdmiInStateText = stateText
        root.appendBoundedLog("hdmiInLog", "STOP live GStreamer appsink HDMIIN preview " + device + "\n")
    }

    function videoSourceTitle(source) {
        return source === "hdmiin" ? "HDMI 输入" : "摄像头"
    }

    function selectedVideoDevice(source) {
        return source === "hdmiin" ? root.selectedHdmiInDevice() : root.selectedCameraDevice()
    }

    function inlineVideoPreview(source) {
        return source === "hdmiin" ? root.hdmiInPreviewSurface : null
    }

    function inlineVideoPreviewBackend(source) {
        var preview = root.inlineVideoPreview(source)
        return preview && preview.previewBackend ? preview.previewBackend : null
    }

    function videoFullscreenMatchesActiveTool() {
        return root.videoFullscreenSource === "hdmiin" && root.activeTool === "HDMIIN"
    }

    function appendVideoPreviewLog(source, entry) {
        root.appendBoundedLog(source === "hdmiin" ? "hdmiInLog" : "cameraLog", entry + "\n")
    }

    function setVideoPreviewState(source, state) {
        if (source === "hdmiin")
            root.hdmiInStateText = state
        else
            root.cameraStateText = state
    }

    function requestVideoFullscreen(source) {
        // Camera preview deliberately remains embedded. HDMI-IN is the only
        // capture source with an operator-facing enlarged preview.
        if (source !== "hdmiin") {
            root.appendVideoPreviewLog(source, "SKIP fullscreen preview: only HDMI-IN supports fullscreen")
            return
        }

        var device = root.selectedVideoDevice(source)
        if (device.length === 0)
            return

        var inlinePreview = root.inlineVideoPreview(source)
        if (!inlinePreview || !inlinePreview.previewBackend || !inlinePreview.active) {
            root.setVideoPreviewState(source, "请等待内嵌实时画面显示后再放大")
            root.appendVideoPreviewLog(source, "SKIP fullscreen preview: inline source is not active")
            return
        }

        root.videoFullscreenSource = source
        root.videoFullscreenDevice = device
        root.videoFullscreenRestoreInline = root.hdmiInPreviewRequested
        // Keep the inline appsink pipeline alive and mirror its frames into
        // the overlay.  Opening the V4L2 node again can race a single-open
        // driver, especially with HDMI-RX multi-planar capture.
        root.videoFullscreenRequested = true
        root.setVideoPreviewState(source, "大屏实时预览正在显示")
        root.appendVideoPreviewLog(source, "OPEN fullscreen live preview " + device)
    }

    function closeVideoFullscreen(allowRestore) {
        if (!root.videoFullscreenVisible)
            return

        var source = root.videoFullscreenSource
        var continuesInline = allowRestore !== false
                              && root.videoFullscreenRestoreInline
                              && root.videoFullscreenMatchesActiveTool()
                              && root.visible
        var inlinePreview = root.inlineVideoPreview(source)
        root.videoFullscreenRequested = false
        if (source.length > 0)
            root.setVideoPreviewState(source, continuesInline
                                      ? (inlinePreview ? inlinePreview.statusText : "已退出大屏，继续内嵌预览")
                                      : "大屏预览已关闭")
        if (source.length > 0)
            root.appendVideoPreviewLog(source, "CLOSE fullscreen live preview " + root.videoFullscreenDevice)
        root.videoFullscreenSource = ""
        root.videoFullscreenDevice = ""
        root.videoFullscreenRestoreInline = false
    }

    function selectedWatchdogDevice() {
        if (root.valueIndex(boardBackend.watchdogDevices, root.watchdogDevice, -1) >= 0)
            return root.watchdogDevice
        if (boardBackend.watchdogDevices.length > 0)
            return boardBackend.watchdogDevices[0]
        return ""
    }

    function requestCanLoopStop(statusText) {
        root.canLoopStopWhenReady = root.canLoopStarting || root.canLoopRunning || root.canLoopStopping
        if (!root.canLoopStopWhenReady)
            return
        if (statusText && statusText.length > 0) {
            root.canResultText = statusText
            root.canTxStatusText = statusText
        }
        if (root.canLoopStarting || root.canLoopStopping || boardBackend.backendBusy)
            return
        if (!root.canLoopRunning) {
            root.canLoopStopWhenReady = false
            return
        }

        var iface = root.canLoopInterface.length > 0
                ? root.canLoopInterface : root.selectedCanInterface()
        root.pendingCanInterface = iface
        root.pendingCanMode = root.canModeText()
        root.canLoopStopping = true
        if (!boardBackend.runHardwareOperationAsync("can-loop-stop", {"interface": iface}))
            root.canLoopStopping = false
    }

    function pathBreadcrumbText() {
        var path = boardBackend.currentPath
        if (path.length === 0 || path === "/")
            return "/"
        var parts = path.split("/")
        var compact = []
        for (var i = 0; i < parts.length; ++i) {
            if (parts[i].length > 0)
                compact.push(parts[i])
        }
        if (compact.length > 4)
            compact = ["..."].concat(compact.slice(compact.length - 3))
        return "/ " + compact.join(" / ")
    }

    function commandSucceeded(output) {
        var normalized = output.toLowerCase()
        return normalized.indexOf(" 0% packet loss") >= 0
                || normalized.indexOf("bytes from") >= 0
                || normalized.indexOf("connected") >= 0
                || normalized.indexOf("success") >= 0
    }

    function operationLogEntry(title, details, body, status) {
        var entry = "\n=== " + title + " ===\n"
        if (details.length > 0)
            entry += details + "\n"
        if (body.length > 0)
            entry += body + "\n"
        if (status.length > 0)
            entry += "STATUS " + status + "\n"
        return entry
    }

    function boundedLogText(currentText, entry) {
        var merged = currentText + entry
        var maxLength = 14000
        if (merged.length <= maxLength)
            return merged
        return "... 日志已截断，仅保留最近 " + maxLength + " 字符 ...\n" + merged.slice(merged.length - maxLength)
    }

    function appendBoundedLog(propertyName, entry) {
        root[propertyName] = root.boundedLogText(root[propertyName], entry)
    }

    function setGpioInputMode(enabled) {
        root.gpioInputMode = enabled
        if (!enabled)
            root.gpioAutoPoll = false
    }

    function resetAudioUiState() {
        audioBackendDelay.stop()
        root.pendingAudioOperation = ""
        root.activeAudioOperation = ""
        root.audioBackendBusy = false
    }

    function isAudioTool(tool) {
        return tool === "Music" || tool === "Buzzer"
    }

    function audioOperationTool(operation) {
        return operation.indexOf("beep-") === 0 ? "Buzzer" : "Music"
    }

    function audioStatusForTool(tool) {
        return tool === "Buzzer" ? root.buzzerResultText : root.musicResultText
    }

    function setAudioStatus(operation, text) {
        // stop-all is issued while leaving the workbench and is relevant to
        // both tools.  All other operations belong to exactly one tool.
        if (operation === "stop-all") {
            root.musicResultText = text
            root.buzzerResultText = text
            return
        }
        if (root.audioOperationTool(operation) === "Buzzer")
            root.buzzerResultText = text
        else
            root.musicResultText = text
    }

    function audioQueueStatusForTool(tool) {
        var operation = root.pendingAudioOperation.length > 0
                        ? root.pendingAudioOperation : root.activeAudioOperation
        if (operation.length === 0) {
            if (root.audioBackendBusy || boardBackend.audioOperationBusy)
                return "音频后端处理中"
            return "无待处理操作"
        }
        if (operation === "stop-all" || root.audioOperationTool(operation) === tool)
            return root.audioOperationLabel(operation) + (root.pendingAudioOperation.length > 0 ? " 排队中" : " 处理中")
        return "其他音频任务处理中"
    }

    function audioOperationAffectsTool(tool) {
        var operation = root.pendingAudioOperation.length > 0
                        ? root.pendingAudioOperation : root.activeAudioOperation
        return operation.length > 0
                && (operation === "stop-all" || root.audioOperationTool(operation) === tool)
    }

    function audioActivityStatusText() {
        var operation = root.activeAudioOperation.length > 0
                        ? root.activeAudioOperation : root.pendingAudioOperation
        return operation.length > 0 ? root.audioOperationLabel(operation) + " 处理中" : "音频后端处理中"
    }

    function audioCanStart() {
        return !root.audioBackendBusy && !boardBackend.audioOperationBusy && !audioBackendDelay.running
    }

    function audioCanStop() {
        return !audioBackendDelay.running
    }

    function buzzerCanStart() {
        return boardBackend.buzzerAvailable && root.audioCanStart()
    }

    function buzzerCanStop() {
        return boardBackend.buzzerAvailable && root.audioCanStop()
    }

    function buzzerUsesInputEvent() {
        return boardBackend.buzzerControlMethod === "input-event"
    }

    function buzzerValueText() {
        var frequency = root.beepPercent === 0
                ? 0 : 600 + Math.floor(root.beepPercent * 18)
        return root.beepPercent + "% · " + frequency + " Hz"
    }

    function audioOperationLabel(operation) {
        if (operation === "play-test")
            return "播放耳机测试音频"
        if (operation === "record")
            return "录音 2 秒"
        if (operation === "play-recorded")
            return "播放录音"
        if (operation === "stop-audio")
            return "停止音频 / 录音"
        if (operation === "beep-apply")
            return "应用蜂鸣器"
        if (operation === "beep-heartbeat")
            return "蜂鸣器心跳"
        if (operation === "beep-stop")
            return "停止蜂鸣器"
        if (operation === "volume")
            return "应用系统音量"
        return "音频操作"
    }

    function selectedAudioDevice(devices, index) {
        if (!devices || devices.length === 0 || index < 0 || index >= devices.length)
            return ""
        return String(devices[index].device || "")
    }

    function selectedPlaybackDevice() {
        return root.selectedAudioDevice(boardBackend.audioPlaybackDevices, root.audioPlaybackDeviceIndex)
    }

    function selectedCaptureDevice() {
        return root.selectedAudioDevice(boardBackend.audioCaptureDevices, root.audioCaptureDeviceIndex)
    }

    function musicOperationUnavailableReason(operation) {
        if ((operation === "play-test" || operation === "play-recorded")
                && !root.playbackDeviceAvailable)
            return "未发现可用播放设备"
        if (operation === "record" && !root.captureDeviceAvailable)
            return "未发现可用录音设备"
        return ""
    }

    function musicDeviceSummary() {
        if (!root.playbackDeviceAvailable && !root.captureDeviceAvailable)
            return "未发现播放或录音设备"
        if (!root.playbackDeviceAvailable)
            return "未发现可用播放设备"
        if (!root.captureDeviceAvailable)
            return "未发现可用录音设备"
        return "播放与录音设备已发现"
    }

    function queueAudioOperation(operation) {
        var unavailableReason = root.musicOperationUnavailableReason(operation)
        if (unavailableReason.length > 0) {
            root.setAudioStatus(operation, unavailableReason)
            return
        }
        if ((operation === "beep-apply" || operation === "beep-heartbeat")
                && !boardBackend.buzzerAvailable) {
            root.setAudioStatus(operation, boardBackend.buzzerAvailabilityStatus)
            return
        }
        if (operation === "volume") {
            root.activeAudioOperation = "volume"
            root.audioBackendBusy = true
            root.setAudioStatus(operation, root.audioOperationLabel(operation) + " 处理中")
            boardBackend.runAudioOperationAsync("volume", root.audioVolumeTargetPercent,
                                                root.selectedPlaybackDevice())
            return
        }

        if (operation === "stop-audio" || operation === "stop-all") {
            audioBackendDelay.stop()
            root.pendingAudioOperation = ""
            root.activeAudioOperation = operation
            root.audioBackendBusy = true
            root.setAudioStatus(operation, root.audioOperationLabel(operation) + " 处理中")
            if (operation === "stop-audio")
                boardBackend.runAudioOperationAsync("stop-audio", -1)
            else
                boardBackend.runAudioOperationAsync("stop-all", -1)
            return
        }

        if (!root.audioCanStart()) {
            root.setAudioStatus(operation, "音频后端处理中")
            return
        }

        root.pendingAudioOperation = operation
        root.setAudioStatus(operation, root.audioOperationLabel(operation) + " 排队中")
        audioBackendDelay.restart()
    }

    function runPendingAudioOperation() {
        if (!root.visible || !root.isAudioTool(root.activeTool)) {
            root.resetAudioUiState()
            return
        }
        if (root.pendingAudioOperation.length === 0)
            return

        var operation = root.pendingAudioOperation
        root.pendingAudioOperation = ""
        var unavailableReason = root.musicOperationUnavailableReason(operation)
        if (unavailableReason.length > 0) {
            root.setAudioStatus(operation, unavailableReason)
            return
        }
        root.audioBackendBusy = true
        root.activeAudioOperation = operation
        root.setAudioStatus(operation, root.audioOperationLabel(operation) + " 处理中")
        boardBackend.runAudioOperationAsync(operation, operation === "beep-apply" ? root.beepPercent : -1,
                                           root.selectedPlaybackDevice(), root.selectedCaptureDevice())
    }

    function activeStatusSummary() {
        if (root.activeTool === "UART")
            return root.uartResultText
        if (root.activeTool === "Ethernet")
            return root.pendingPingContext === "ethernet" ? root.ethernetPingResultText : root.ethernetResultText
        if (root.activeTool === "CAN")
            return root.canResultText
        if (root.activeTool === "GPIO")
            return root.gpioInputMode ? root.gpioReadback : (root.gpioHigh ? "GPIO 输出预览 HIGH" : "GPIO 输出预览 LOW")
        if (root.activeTool === "Camera")
            return root.cameraStateText
        if (root.activeTool === "HDMIIN")
            return root.hdmiInStateText
        if (root.isAudioTool(root.activeTool))
            return root.audioStatusForTool(root.activeTool)
        if (root.activeTool === "Fan")
            return root.fanResultText
        if (root.activeTool === "LED")
            return root.ledResultText
        if (root.activeTool === "Cellular")
            return root.cellularResultText
        if (root.activeTool === "Watchdog")
            return root.watchdogResultText
        if (root.activeTool === "Backlight")
            return boardBackend.backlightAvailable ? "当前亮度 " + boardBackend.backlightPercent + "%" : "未检测到背光设备"
        if (root.activeTool === "Files")
            return root.filesStatusText
        return boardBackend.statusText.length > 0 ? boardBackend.statusText : "后端就绪"
    }

    function operationDisplayName(operation) {
        var names = {
            "gpio-apply": "写入 GPIO",
            "gpio-read": "读取 GPIO",
            "uart-tx": "发送串口测试帧",
            "uart-rx": "读取串口样本",
            "backlight-set": "应用背光亮度",
            "ethernet-dhcp": "应用 DHCP 配置",
            "ethernet-static": "应用静态 IP",
            "ethernet-refresh": "刷新网络状态",
            "ping-interface": "执行连通性测试",
            "can-open": "打开 CAN 总线",
            "can-close": "关闭 CAN 总线",
            "can-tx": "发送 CAN 测试帧",
            "can-rx": "读取 CAN 样本",
            "can-loop-start": "启动 CAN 循环发送",
            "can-loop-stop": "停止 CAN 循环发送",
            "cellular-start": "初始化蜂窝网络",
            "cellular-stop": "停止蜂窝网络",
            "cellular-status": "读取蜂窝网络状态",
            "camera-start": "启动摄像头验证",
            "camera-stop": "停止摄像头验证",
            "hdmiin-start": "启动 HDMI 输入验证",
            "hdmiin-stop": "停止 HDMI 输入验证",
            "watchdog-status": "读取看门狗状态",
            "watchdog-start": "启动看门狗测试",
            "fan-auto": "切换风扇自动温控",
            "fan-manual": "设置风扇手动转速",
            "storage-scan": "扫描存储设备",
            "storage-mount": "挂载存储设备",
            "storage-unmount": "卸载存储设备",
            "storage-speed": "测试存储速度"
        }
        return names[operation] || operation
    }

    Connections {
        target: boardBackend
        onOperationFinished: {
            if (operation === "ping-interface") {
                var iface = root.pendingPingInterface
                var target = root.pendingPingTarget
                if (root.pendingPingContext === "ethernet") {
                    var source = root.ethernetPingSourceLabel(iface)
                    root.ethernetPingResultText = success ? "Ping 成功：" + source + " 可访问 " + target
                                                          : "Ping 未通过：" + source + " 无法访问 " + target
                    root.ethernetResultText = root.ethernetPingResultText
                    root.appendBoundedLog("ethernetLog", root.operationLogEntry(success ? "ETHERNET PING OK" : "ETHERNET PING CHECK",
                                                                                "接口 " + source + "\n目标 " + target + "\n链路 " + root.ethernetLinkLabel() + "\nIPv4 " + root.ethernetIpv4Label(),
                                                                                result + "\nRESULT " + root.ethernetResultText,
                                                                                status))
                } else if (root.pendingPingContext === "cellular") {
                    root.cellularResultText = success ? "联网测试成功: " + iface : "联网测试失败: " + status
                    root.cellularDetailText = result.length > 0 ? result : status
                    root.cellularLog = result.length > 0 ? result : status
                }
                root.pendingPingInterface = ""
                root.pendingPingTarget = ""
                root.pendingPingContext = ""
            } else if (operation === "uart-tx") {
                var uartTxDevice = root.pendingUartDevice.length > 0 ? root.pendingUartDevice : result
                root.uartResultText = success ? "TX 已发送: " + uartTxDevice : "TX 失败: " + uartTxDevice
                root.uartTxStatusText = root.uartResultText
                root.uartTxFrameText = "55 AA 01 00 FE / " + root.pendingUartFormat
                root.appendBoundedLog("uartLog", root.operationLogEntry("UART TX",
                                                                         "DEVICE " + uartTxDevice + "\nFORMAT " + root.pendingUartFormat,
                                                                         "FRAME 55 AA 01 00 FE\nRESULT " + root.uartResultText,
                                                                         status))
                root.pendingUartDevice = ""
                root.pendingUartFormat = ""
            } else if (operation === "uart-rx") {
                var uartRxDevice = root.pendingUartDevice
                var hasSerialSample = success && result.length > 0 && result !== "<no data>"
                root.uartResultText = hasSerialSample ? "RX 已读取: " + uartRxDevice
                                                       : (success && result === "<no data>" ? "RX 暂无数据: " + uartRxDevice : "RX 失败: " + uartRxDevice)
                root.uartRxStatusText = root.uartResultText
                root.uartRxSampleText = result.length > 0 ? result : "RX FAILED"
                root.appendBoundedLog("uartLog", root.operationLogEntry("UART RX",
                                                                         "DEVICE " + uartRxDevice + "\nFORMAT " + root.pendingUartFormat,
                                                                         (result.length > 0 ? result : "RX FAILED") + "\nRESULT " + root.uartResultText,
                                                                         status))
                root.pendingUartDevice = ""
                root.pendingUartFormat = ""
            } else if (operation.indexOf("audio-") === 0) {
                var audioOperation = operation.substring(6)
                if (audioOperation === "volume") {
                    root.activeAudioOperation = ""
                    root.audioBackendBusy = false
                    if (success)
                        root.audioVolumeTargetPercent = boardBackend.audioVolumePercent
                    root.setAudioStatus(audioOperation, success ? root.audioOperationLabel(audioOperation) + " 已应用: " + boardBackend.audioVolumePercent + "%"
                                                               : root.audioOperationLabel(audioOperation) + " 失败: " + status)
                    return
                }
                if (audioOperation === "stop-audio" || audioOperation === "stop-all") {
                    root.resetAudioUiState()
                    root.setAudioStatus(audioOperation, success ? root.audioOperationLabel(audioOperation) + " 已提交"
                                                               : root.audioOperationLabel(audioOperation) + " 失败: " + status)
                    return
                }
                if (root.activeAudioOperation.length > 0 && root.activeAudioOperation !== audioOperation) {
                    if (!boardBackend.audioOperationBusy)
                        root.audioBackendBusy = false
                    return
                }
                root.activeAudioOperation = ""
                root.audioBackendBusy = false
                root.setAudioStatus(audioOperation, success ? root.audioOperationLabel(audioOperation) + " 已提交"
                                                           : root.audioOperationLabel(audioOperation) + " 失败: " + status)
            } else if (operation.indexOf("led-") === 0) {
                root.ledResultText = success ? status : "LED 操作失败: " + status
            } else if (operation === "fan-auto" || operation === "fan-manual") {
                root.fanResultText = success ? status : "风扇操作失败: " + status
            } else if (operation === "gpio-apply" || operation === "gpio-read") {
                root.gpioReadback = success ? result : ("GPIO 操作失败: " + status)
            } else if (operation === "backlight-set") {
                root.backlightTargetPercent = success && result.length > 0 ? Number(result) : root.backlightTargetPercent
            } else if (operation === "ethernet-dhcp" || operation === "ethernet-static") {
                var ethIface = root.pendingEthernetInterface
                var ethMode = root.pendingEthernetMode
                root.ethernetResultText = success
                        ? (ethMode === "static" ? "静态 IP 已应用: " + result : "DHCP 已应用: " + ethIface)
                        : "配置失败: " + status
                root.appendBoundedLog("ethernetLog", root.operationLogEntry("ETHERNET CONFIG",
                                                                            "接口 " + ethIface + "\n模式 " + ethMode,
                                                                            "RESULT " + root.ethernetResultText,
                                                                            status))
                root.pendingEthernetInterface = ""
                root.pendingEthernetMode = ""
            } else if (operation === "ethernet-refresh") {
                var refreshIface = root.pendingEthernetInterface.length > 0 ? root.pendingEthernetInterface : result
                root.ethernetResultText = success ? "状态已刷新: " + root.ethernetPingSourceLabel(refreshIface) : "刷新失败: " + status
                root.appendBoundedLog("ethernetLog", root.operationLogEntry("ETHERNET REFRESH",
                                                                            "接口 " + refreshIface,
                                                                            "链路 " + root.ethernetLinkLabel()
                                                                            + "\nIPv4 " + root.ethernetIpv4Label()
                                                                            + "\n诊断 " + root.ethernetDiagnosticLabel()
                                                                            + "\nRESULT " + root.ethernetResultText,
                                                                            status))
                root.pendingEthernetInterface = ""
                root.pendingEthernetMode = ""
            } else if (operation === "can-open") {
                var canOpenIface = root.pendingCanInterface.length > 0 ? root.pendingCanInterface : result
                root.canResultText = success ? "已打开: " + canOpenIface + " / " + root.pendingCanMode : "打开失败: " + canOpenIface
                root.canBusStatusText = root.canResultText
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN OPEN",
                                                                       "IFACE " + canOpenIface + "\nMODE " + root.pendingCanMode,
                                                                       "RESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
            } else if (operation === "can-rx") {
                var canRxIface = root.pendingCanInterface
                var hasCanSample = success && result.length > 0 && result !== "<no CAN frames>"
                root.canResultText = hasCanSample ? "RX 样本: " + canRxIface
                                                  : (success && result === "<no CAN frames>" ? "RX 暂无样本: " + canRxIface : "RX 失败: " + canRxIface)
                root.canRxStatusText = root.canResultText
                root.canRxSampleText = result.length > 0 ? result : "RX FAILED"
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN RX",
                                                                       "IFACE " + canRxIface + "\nMODE " + root.pendingCanMode,
                                                                       (result.length > 0 ? result : "RX FAILED") + "\nRESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
            } else if (operation === "can-tx") {
                var canTxIface = root.pendingCanInterface.length > 0 ? root.pendingCanInterface : result
                root.canResultText = success ? "已发送测试帧: " + canTxIface : "发送失败: " + canTxIface
                root.canTxStatusText = root.canResultText
                root.canTxFrameText = "STD 0x11 / EFF 0x11 / RTR 0x11"
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN TX",
                                                                       "IFACE " + canTxIface + "\nMODE " + root.pendingCanMode,
                                                                       "FRAME STD 0x11, EFF 0x11, RTR 0x11\nRESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
            } else if (operation === "can-close") {
                var canCloseIface = root.pendingCanInterface.length > 0 ? root.pendingCanInterface : result
                root.canResultText = success ? "已关闭: " + canCloseIface : "关闭失败: " + status
                root.canBusStatusText = root.canResultText
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN CLOSE",
                                                                       "IFACE " + canCloseIface,
                                                                       "RESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
            } else if (operation === "can-loop-start") {
                var canLoopIface = root.pendingCanInterface.length > 0 ? root.pendingCanInterface : result
                root.canLoopStarting = false
                root.canLoopRunning = success
                root.canLoopInterface = success ? canLoopIface : ""
                root.canResultText = success ? "循环发送中: " + canLoopIface : "循环发送失败: " + status
                root.canTxStatusText = root.canResultText
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN LOOP START",
                                                                       "IFACE " + canLoopIface + "\nBITRATE " + root.canBitrate,
                                                                       "RESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
                if (success && root.canLoopStopWhenReady)
                    Qt.callLater(function() { root.requestCanLoopStop("正在停止 CAN 循环发送") })
                else if (!success)
                    root.canLoopStopWhenReady = false
            } else if (operation === "can-loop-stop") {
                var canStopIface = root.pendingCanInterface
                root.canLoopStopping = false
                root.canLoopRunning = false
                root.canLoopStarting = false
                root.canLoopStopWhenReady = false
                root.canLoopInterface = ""
                root.canResultText = success ? "循环发送已停止" : "停止失败: " + status
                root.canTxStatusText = root.canResultText
                root.appendBoundedLog("canLog", root.operationLogEntry("CAN LOOP STOP",
                                                                       "IFACE " + canStopIface,
                                                                       "RESULT " + root.canResultText,
                                                                       status))
                root.pendingCanInterface = ""
                root.pendingCanMode = ""
            } else if (operation === "cellular-start") {
                var startIface = root.pendingCellularInterface.length > 0
                        ? root.pendingCellularInterface
                        : (result.length > 0 ? result : root.cellularAutoInterface)
                var startTarget = startIface.length > 0 ? startIface : "等待自动发现接口"
                root.cellularResultText = success ? "拨号程序已启动: " + startTarget : "拨号启动失败: " + status
                root.cellularDetailText = status.length > 0 ? status : root.cellularResultText
                root.appendBoundedLog("cellularLog", "START 4G/5G\nAPN "
                                      + (root.pendingCellularApn.length > 0 ? root.pendingCellularApn : "模块默认")
                                      + "\nINTERFACE " + startTarget + "\n" + status + "\n")
                root.pendingCellularInterface = ""
                root.pendingCellularApn = ""
            } else if (operation === "cellular-stop") {
                root.cellularResultText = success ? "拨号已停止" : "停止失败: " + status
                root.cellularDetailText = status.length > 0 ? status : root.cellularResultText
                root.appendBoundedLog("cellularLog", "\nSTOP 4G/5G\n" + status + "\n")
            } else if (operation === "cellular-status") {
                root.cellularResultText = success ? "状态已读取" : "状态读取失败: " + status
                root.cellularDetailText = result.length > 0 ? result : status
                root.appendBoundedLog("cellularLog", "\n--- CHECK IP ---\n" + (result.length > 0 ? result : status) + "\n")
            } else if (operation === "camera-start") {
                root.cameraStateText = success ? "采集中: " + root.cameraMode + " / " + root.pendingPreviewDevice
                                               : "采集启动失败: " + root.pendingPreviewDevice
                root.appendBoundedLog("cameraLog", "START camera capture " + root.cameraMode + " " + root.pendingPreviewDevice + "\n" + status + "\n")
                root.pendingPreviewContext = ""
                root.pendingPreviewDevice = ""
            } else if (operation === "camera-stop") {
                root.cameraStateText = success ? "采集已停止" : "停止采集失败: " + status
                root.appendBoundedLog("cameraLog", "STOP camera capture\n" + status + "\n")
            } else if (operation === "hdmiin-start") {
                root.hdmiInStateText = success ? "采集中: hdmiin / " + root.pendingPreviewDevice
                                               : "采集启动失败: " + root.pendingPreviewDevice
                root.appendBoundedLog("hdmiInLog", "START HDMIIN capture " + root.pendingPreviewDevice + "\n" + status + "\n")
                root.pendingPreviewContext = ""
                root.pendingPreviewDevice = ""
            } else if (operation === "hdmiin-stop") {
                root.hdmiInStateText = success ? "采集已停止" : "停止采集失败: " + status
                root.appendBoundedLog("hdmiInLog", "STOP HDMIIN capture\n" + status + "\n")
            } else if (operation === "watchdog-status") {
                var watchdogStatusDevice = root.pendingWatchdogDevice.length > 0 ? root.pendingWatchdogDevice : root.selectedWatchdogDevice()
                root.watchdogResultText = success ? "状态已读取: " + watchdogStatusDevice : "状态读取失败: " + status
                root.watchdogStatusDetail = result.length > 0 ? result : status
                root.appendBoundedLog("watchdogLog", "\n--- STATUS ---\n" + (result.length > 0 ? result : status) + "\n")
                root.pendingWatchdogDevice = ""
            } else if (operation === "watchdog-start") {
                var watchdogStartDevice = root.pendingWatchdogDevice.length > 0 ? root.pendingWatchdogDevice : result
                root.watchdogResultText = success ? "测试已启动: " + watchdogStartDevice : "启动失败: " + status
                root.watchdogStatusDetail = status.length > 0 ? status : root.watchdogResultText
                root.appendBoundedLog("watchdogLog", "START watchdog " + watchdogStartDevice + " " + root.watchdogTimeout + "\n" + status + "\n")
                root.pendingWatchdogDevice = ""
            } else if (operation === "files-open" || operation === "files-up") {
                root.filesStatusText = success ? "已打开: " + result : "打开失败: " + status
            }
        }

        onChanged: {
            root.ensureCameraDeviceSelection()
        }

        onOperationStateChanged: {
            if (!boardBackend.backendBusy && root.canLoopStopWhenReady)
                Qt.callLater(function() { root.requestCanLoopStop("正在停止 CAN 循环发送") })
        }
    }

    function uartResultAccent() {
        if (root.uartResultText.indexOf("失败") >= 0)
            return theme.danger
        if (root.uartResultText.indexOf("中") >= 0)
            return theme.focus
        if (root.uartResultText.indexOf("等待") >= 0 || root.uartResultText.indexOf("暂无") >= 0)
            return theme.inkFaint
        if (root.uartResultText.indexOf("RX") >= 0)
            return theme.ready
        return root.toolAccent("UART")
    }

    function statusAccent(text, fallback) {
        if (text.indexOf("失败") >= 0 || text.indexOf("未通过") >= 0 || text.indexOf("无法访问") >= 0)
            return theme.danger
        if (text.indexOf("正在") >= 0)
            return theme.focus
        if (text.indexOf("检查") >= 0)
            return theme.warning
        if (text.indexOf("等待") >= 0 || text.indexOf("暂无") >= 0 || text.indexOf("未") >= 0)
            return theme.inkFaint
        return fallback
    }

    function ethernetResultAccent() {
        if (root.ethernetResultText.indexOf("成功") >= 0
                || root.ethernetResultText.indexOf("已应用") >= 0
                || root.ethernetResultText.indexOf("已刷新") >= 0)
            return root.toolAccent("Ethernet")
        if (root.ethernetResultText.indexOf("失败") >= 0)
            return theme.danger
        if (root.ethernetResultText.indexOf("检查") >= 0)
            return theme.warning
        return root.selectedEthernet().linkUp ? root.toolAccent("Ethernet") : theme.inkFaint
    }

    function canResultAccent() {
        if (root.canResultText.indexOf("失败") >= 0)
            return theme.danger
        if (root.canResultText.indexOf("中") >= 0)
            return theme.focus
        if (root.canResultText.indexOf("等待") >= 0
                || root.canResultText.indexOf("暂无") >= 0
                || root.canResultText.indexOf("已关闭") >= 0
                || root.canResultText.indexOf("已停止") >= 0)
            return theme.inkFaint
        return root.toolAccent("CAN")
    }

    function audioResultAccent(tool) {
        var resultText = root.audioStatusForTool(tool || root.activeTool)
        if (root.audioOperationAffectsTool(tool || root.activeTool))
            return theme.warning
        if (resultText.indexOf("失败") >= 0)
            return theme.danger
        if (resultText.indexOf("等待") >= 0)
            return theme.inkFaint
        return root.toolAccent((tool || root.activeTool) === "Buzzer" ? "Buzzer" : "Music")
    }
    function deviceAvailability(count, fallback) {
        return count > 0 ? count + " 项可用" : fallback
    }

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Rectangle {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round((root.compactHeader ? 68 : 76) * root.uiScale)
        color: theme.instrument

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.withAlpha(theme.instrumentInk, 0.16)
        }

        Rectangle {
            id: brandMark
            anchors.left: parent.left
            anchors.leftMargin: root.gutter
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
            anchors.left: brandMark.right
            anchors.leftMargin: Math.round(12 * root.uiScale)
            anchors.right: root.showHeaderStatus ? workspaceStatusSummary.left : toolPicker.left
            anchors.rightMargin: Math.round(14 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(2 * root.uiScale)

            Text {
                width: parent.width
                text: root.toolDisplayName(root.activeTool)
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactHeader ? 16 : 18) * root.uiScale)
                font.weight: Font.DemiBold
                font.family: theme.displayFont
            }
            Text {
                visible: !root.compactHeader
                width: parent.width
                text: root.toolSubtitle(root.activeTool)
                color: theme.instrumentMuted
                elide: Text.ElideRight
                font.pixelSize: Math.round(11 * root.uiScale)
                font.weight: Font.Normal
                font.family: theme.bodyFont
            }
        }

        BoardToolsActionButton {
            id: closeAction
            uiScale: root.uiScale
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            width: root.headerIconActions ? Math.max(44, Math.round(44 * root.uiScale))
                                          : Math.round(122 * root.uiScale)
            touchTarget: root.touchTarget
            text: root.headerIconActions ? "" : "全部工具"
            iconName: "arrow-left"
            accessibleName: "返回全部工具"
            variant: "secondary"
            onClicked: root.closed()
        }

        BoardToolsToolPicker {
            id: toolPicker
            anchors.right: closeAction.left
            anchors.rightMargin: Math.round(10 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            uiScale: root.uiScale
            profile: boardProfile
            currentTool: root.activeTool
            onToolRequested: root.toolRequested(tool)
        }

        Rectangle {
            id: workspaceStatusSummary
            visible: root.showHeaderStatus
            anchors.right: toolPicker.left
            anchors.rightMargin: Math.round(12 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.round(400 * root.uiScale), parent.width * 0.38)
            height: Math.round(38 * root.uiScale)
            radius: Math.round(theme.radiusControl * root.uiScale)
            color: theme.instrumentRaised
            border.color: root.operationBusy ? theme.warning : theme.withAlpha(theme.instrumentInk, 0.18)
            border.width: 1

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(11 * root.uiScale)
                anchors.right: parent.right
                anchors.rightMargin: Math.round(11 * root.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(8 * root.uiScale)

                Rectangle {
                    width: Math.max(6, Math.round(7 * root.uiScale))
                    height: width
                    radius: width / 2
                    color: root.operationBusy ? theme.warning : theme.ready
                }

                Text {
                    width: parent.width - parent.spacing - Math.max(6, Math.round(7 * root.uiScale))
                    text: root.operationBusy
                          ? "正在执行 · " + (boardBackend.backendBusy
                                               ? root.operationDisplayName(boardBackend.backendBusyOperation)
                                               : root.audioActivityStatusText())
                          : root.activeStatusSummary()
                    color: root.operationBusy ? theme.warning : theme.instrumentInk
                    elide: Text.ElideMiddle
                    font.pixelSize: Math.round(11 * root.uiScale)
                    font.weight: Font.DemiBold
                    font.family: theme.bodyFont
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    Rectangle {
        id: commandStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topBar.bottom
        height: Math.round(48 * root.uiScale)
        color: theme.surfaceRaised

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.rule
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(8 * root.uiScale)

            Rectangle {
                width: Math.max(7, Math.round(8 * root.uiScale))
                height: width
                radius: width / 2
                color: root.operationBusy ? theme.warning : theme.ready
            }

            Text {
                text: root.operationBusy
                      ? "正在执行 · " + (boardBackend.backendBusy
                                           ? root.operationDisplayName(boardBackend.backendBusyOperation)
                                           : root.audioActivityStatusText())
                      : "工作台就绪"
                color: root.operationBusy ? theme.warning : theme.ink
                elide: Text.ElideRight
                font.family: theme.bodyFont
                font.pixelSize: Math.round(12 * root.uiScale)
                font.weight: Font.DemiBold
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(260 * root.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.compactHeader
            text: root.activeStatusSummary()
            color: theme.muted
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            font.family: theme.monoFont
            font.pixelSize: Math.round(10 * root.uiScale)
        }
    }

    Item {
        id: workspace
        anchors.left: parent.left
        anchors.leftMargin: root.gutter
        anchors.right: parent.right
        anchors.rightMargin: root.gutter
        anchors.top: commandStrip.bottom
        anchors.topMargin: root.gutter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.gutter
        clip: true

        Item {
            id: body
            anchors.fill: parent

            BoardToolsWorkSurface {
                objectName: "fan-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Fan"
                leftTitle: "运行时风扇"
                middleTitle: "转速控制"
                rightTitle: "温控状态"
                leftContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "节点"; value: boardBackend.fanStatus && boardBackend.fanStatus.available ? boardBackend.fanStatus.name : "未发现 pwmfan" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前 PWM"; value: boardBackend.fanStatus && boardBackend.fanStatus.available ? boardBackend.fanStatus.pwm + " / " + boardBackend.fanStatus.pwmMax : "不可用" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "控制模式"; value: boardBackend.fanStatus ? (boardBackend.fanStatus.mode === "auto" ? "自动温控" : (boardBackend.fanStatus.mode === "manual" ? "手动" : "不可用")) : "不可用" }
                }
                middleContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    Text { text: "手动转速 " + root.fanManualPercent + "%"; color: theme.inkStrong; font.pixelSize: Math.round(15 * root.uiScale); font.weight: Font.Bold }
                    BoardToolsSlider { uiScale: root.uiScale; width: parent.width; from: 0; to: 100; value: root.fanManualPercent; enabled: boardBackend.fanStatus && boardBackend.fanStatus.available && boardBackend.fanStatus.mode !== "unsupported"; onMoved: root.fanManualPercent = Math.round(value) }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: "应用手动转速"; accent: root.toolAccent("Fan"); variant: "primary"; enabled: boardBackend.fanStatus && boardBackend.fanStatus.available && boardBackend.fanStatus.mode !== "unsupported"; onClicked: boardBackend.setFanManualPercent(root.fanManualPercent) }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: "恢复自动温控"; accent: theme.accent; variant: "secondary"; enabled: boardBackend.fanStatus && boardBackend.fanStatus.available && boardBackend.fanStatus.mode !== "unsupported"; onClicked: boardBackend.setFanAuto() }
                }
                rightContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale; label: "风扇状态"; value: root.fanResultText; accent: root.toolAccent("Fan") }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前温控"; value: boardBackend.fanStatus ? boardBackend.fanStatus.message : "未检测" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端"; value: boardBackend.backendBusy ? "处理中" : "空闲" }
                }
            }

            BoardToolsWorkSurface {
                objectName: "gpio-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "GPIO"
                leftTitle: "GPIO 配置"
                middleTitle: "输入 / 输出控制"
                rightTitle: "读回状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsTextField {
                        id: gpioPinField
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        font.pixelSize: root.inputFontSize
                        text: root.gpioPin
                        placeholderText: "GPIO1_B1"
                        selectByMouse: true
                        inputMethodHints: Qt.ImhUppercaseOnly
                        onEditingFinished: root.gpioPin = text.trim()
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: ["GPIO1_B1", "GPIO1_C0", "GPIO3_A2"]
                            BoardToolsToolButtonLite {
                                uiScale: root.uiScale
                                width: (parent.width - parent.spacing * 2) / 3
                                text: modelData
                                accent: root.toolAccent("GPIO")
                                checkedState: root.gpioPin === modelData
                                onTriggered: {
                                    root.gpioPin = modelData
                                    gpioPinField.text = modelData
                                    root.gpioReadback = "未采样"
                                    root.gpioAutoPoll = false
                                }
                            }
                        }
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "换算"; value: boardBackend.gpioPreview(root.gpioPin, root.gpioHigh) }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "切换为输出前，请确认该引脚没有连接电源、复位或关键外设。" }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    Row {
                        width: parent.width
                        spacing: 10
                        BoardToolsToolButtonLite {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "输入模式"
                            accent: root.toolAccent("GPIO")
                            checkedState: root.gpioInputMode
                            onTriggered: root.setGpioInputMode(true)
                        }
                        BoardToolsToolButtonLite {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "输出模式"
                            accent: root.toolAccent("GPIO")
                            checkedState: !root.gpioInputMode
                            onTriggered: root.setGpioInputMode(false)
                        }
                    }
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        visible: !root.gpioInputMode
                        label: "输出高电平"
                        checked: root.gpioHigh
                        accent: root.toolAccent("GPIO")
                        onText: "HIGH"
                        offText: "LOW"
                        onToggled: root.gpioHigh = checked
                    }
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        visible: root.gpioInputMode
                        label: "自动轮询"
                        checked: root.gpioAutoPoll
                        accent: root.toolAccent("GPIO")
                        onText: "自动"
                        offText: "手动"
                        onToggled: root.gpioAutoPoll = checked
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.gpioInputMode ? "应用输入" : "应用输出"
                            accent: root.toolAccent("GPIO")
                            variant: root.gpioInputMode ? "primary" : "danger"
                            onClicked: {
                                root.gpioReadback = root.gpioInputMode ? "输入采样中" : "输出应用中"
                                boardBackend.runHardwareOperationAsync("gpio-apply", {
                                                                          "pin": root.gpioPin,
                                                                          "mode": root.gpioInputMode ? "in" : "out",
                                                                          "high": root.gpioHigh
                                                                      })
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "读取"
                            variant: "secondary"
                            onClicked: {
                                root.gpioReadback = "输入采样中"
                                boardBackend.runHardwareOperationAsync("gpio-read", {"pin": root.gpioPin})
                            }
                        }
                    }
                }

                rightContent: Column {
                    spacing: 14
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: root.gpioInputMode ? "输入状态" : "输出预览"
                        value: root.gpioInputMode ? root.gpioReadback : (root.gpioHigh ? "HIGH (1)" : "LOW (0)")
                        accent: root.gpioReadback.indexOf("HIGH") >= 0 || (!root.gpioInputMode && root.gpioHigh)
                                ? theme.accent : theme.muted
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前引脚"; value: root.gpioPin }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "换算"; value: boardBackend.gpioPreview(root.gpioPin, root.gpioHigh) }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端状态"; value: boardBackend.statusText.length > 0 ? boardBackend.statusText : "后端就绪" }
                }
            }

            BoardToolsWorkSurface {
                objectName: "uart-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "UART"
                leftTitle: "串口参数"
                middleTitle: "收发操作"
                rightTitle: "串口状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsComboBox {
                        id: uartDeviceBox
                        objectName: "uart-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "uart-dropdown"
                        model: boardBackend.serialDevices.length > 0 && boardBackend.serialDevices[0].indexOf("No serial") !== 0 ? boardBackend.serialDevices : ["No UART"]
                        currentIndex: Math.max(0, root.valueIndex(model, root.uartDevice, 0))
                        onActivated: root.uartDevice = currentText
                    }
                    BoardToolsComboBox {
                        id: uartBaudBox
                        objectName: "uart-baud-dropdown"
                        property var values: [4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "uart-baud-dropdown"
                        model: ["4800", "9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"]
                        currentIndex: Math.max(0, root.valueIndex(values, root.uartBaudRate, 5))
                        onActivated: root.uartBaudRate = values[currentIndex]
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        BoardToolsComboBox {
                            id: uartDataBitsBox
                            objectName: "uart-data-bits-dropdown"
                            property var values: [5, 6, 7, 8]
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            height: root.touchTarget
                            openOnReady: startupDropdown === "uart-data-bits-dropdown"
                            model: ["5 数据位", "6 数据位", "7 数据位", "8 数据位"]
                            currentIndex: Math.max(0, root.valueIndex(values, root.uartDataBits, 3))
                            onActivated: root.uartDataBits = values[currentIndex]
                        }
                        BoardToolsComboBox {
                            id: uartParityBox
                            objectName: "uart-parity-dropdown"
                            property var values: ["None", "Even", "Odd"]
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            height: root.touchTarget
                            openOnReady: startupDropdown === "uart-parity-dropdown"
                            model: ["无校验", "偶校验", "奇校验"]
                            currentIndex: Math.max(0, root.valueIndex(values, root.uartParity, 0))
                            onActivated: root.uartParity = values[currentIndex]
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        BoardToolsComboBox {
                            id: uartStopBitsBox
                            objectName: "uart-stop-bits-dropdown"
                            property var values: [1, 2]
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            height: root.touchTarget
                            openOnReady: startupDropdown === "uart-stop-bits-dropdown"
                            model: ["1 停止位", "2 停止位"]
                            currentIndex: Math.max(0, root.valueIndex(values, root.uartStopBits, 0))
                            onActivated: root.uartStopBits = values[currentIndex]
                        }
                        BoardToolsComboBox {
                            id: uartFlowControlBox
                            objectName: "uart-flow-control-dropdown"
                            property var values: ["None", "Software", "Hardware"]
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            height: root.touchTarget
                            openOnReady: startupDropdown === "uart-flow-control-dropdown"
                            model: ["无流控", "XON/XOFF", "RTS/CTS"]
                            currentIndex: Math.max(0, root.valueIndex(values, root.uartFlowControl, 0))
                            onActivated: root.uartFlowControl = values[currentIndex]
                        }
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "格式"; value: root.uartFormatText() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "最后结果"; value: root.uartResultText }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: "发送测试帧 55 AA 01 00 FE"
                        enabled: root.serialReady() && !boardBackend.backendBusy
                        accent: root.toolAccent("UART")
                        variant: "primary"
                        onClicked: {
                            var device = root.selectedSerialDevice()
                            root.pendingUartDevice = device
                            root.pendingUartFormat = root.uartFormatText()
                            root.uartResultText = "TX 发送中: " + device
                            root.uartTxStatusText = root.uartResultText
                            boardBackend.runHardwareOperationAsync("uart-tx", {
                                                                       "device": device,
                                                                       "baudRate": root.uartBaudRate,
                                                                       "dataBits": root.uartDataBits,
                                                                       "parity": root.uartParity,
                                                                       "stopBits": root.uartStopBits,
                                                                       "flowControl": root.uartFlowControl
                                                                   })
                        }
                    }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: "读取样本"
                        enabled: root.serialReady() && !boardBackend.backendBusy
                        variant: "secondary"
                        onClicked: {
                            var device = root.selectedSerialDevice()
                            root.pendingUartDevice = device
                            root.pendingUartFormat = root.uartFormatText()
                            root.uartResultText = "RX 读取中: " + device
                            root.uartRxStatusText = root.uartResultText
                            boardBackend.runHardwareOperationAsync("uart-rx", {
                                                                       "device": device,
                                                                       "baudRate": root.uartBaudRate,
                                                                       "dataBits": root.uartDataBits,
                                                                       "parity": root.uartParity,
                                                                       "stopBits": root.uartStopBits,
                                                                       "flowControl": root.uartFlowControl
                                                                   })
                        }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "环回或对端设备验收前，请确认 tty、波特率、数据位、校验、停止位和流控与对端一致。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: "串口状态"
                        value: root.uartResultText
                        accent: root.uartResultAccent()
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "发送状态"; value: root.uartTxStatusText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "接收状态"; value: root.uartRxStatusText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "TX 帧"; value: root.uartTxFrameText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "RX 样本"; value: root.uartRxSampleText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前设备"; value: root.selectedSerialDevice().length > 0 ? root.selectedSerialDevice() : "未检测到串口" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前格式"; value: root.uartFormatText() }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.uartLog }
            }

            BoardToolsWorkSurface {
                objectName: "backlight-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Backlight"
                leftTitle: "背光设备"
                middleTitle: "亮度控制"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "设备"; value: boardBackend.backlightAvailable ? "可用" : "未发现" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前亮度"; value: boardBackend.backlightPercent + "%" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "路径"; value: "/sys/class/backlight/*/brightness" }
                }

                middleContent: Column {
                    spacing: 16
                    width: parent.width
                    height: parent.height
                    BoardToolsSlider {
                        id: boardBacklightSlider
                        uiScale: root.uiScale
                        width: parent.width
                        height: Math.round(54 * root.uiScale)
                        from: 1
                        to: 100
                        value: boardBackend.backlightAvailable ? boardBackend.backlightPercent : 50
                        enabled: boardBackend.backlightAvailable
                        onMoved: root.backlightTargetPercent = Math.round(value)
                        Component.onCompleted: root.backlightTargetPercent = Math.round(value)
                    }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: boardBackend.backlightAvailable ? "应用亮度" : "未发现背光 sysfs 设备"
                        enabled: boardBackend.backlightAvailable
                        accent: root.toolAccent("Backlight")
                        variant: "primary"
                        onClicked: boardBackend.runHardwareOperationAsync("backlight-set", {"percent": root.backlightTargetPercent})
                    }
                }
            }

            BoardToolsWorkSurface {
                objectName: "music-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Music"
                leftTitle: "音频路径"
                middleTitle: "播放 / 录音"
                rightTitle: "音频状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    Text { text: "播放设备"; color: theme.muted; font.pixelSize: Math.round(13 * root.uiScale); font.weight: Font.DemiBold; font.family: theme.bodyFont }
                    BoardToolsComboBox {
                        uiScale: root.uiScale
                        width: parent.width
                        model: boardBackend.audioPlaybackDevices.length > 0
                               ? boardBackend.audioPlaybackDevices : [{"label": "未发现播放设备", "device": ""}]
                        textRole: "label"
                        enabled: root.playbackDeviceAvailable
                        currentIndex: Math.max(0, Math.min(root.audioPlaybackDeviceIndex, count - 1))
                        onActivated: root.audioPlaybackDeviceIndex = currentIndex
                    }
                    Text { text: "录音设备"; color: theme.muted; font.pixelSize: Math.round(13 * root.uiScale); font.weight: Font.DemiBold; font.family: theme.bodyFont }
                    BoardToolsComboBox {
                        uiScale: root.uiScale
                        width: parent.width
                        model: boardBackend.audioCaptureDevices.length > 0
                               ? boardBackend.audioCaptureDevices : [{"label": "未发现录音设备", "device": ""}]
                        textRole: "label"
                        enabled: root.captureDeviceAvailable
                        currentIndex: Math.max(0, Math.min(root.audioCaptureDeviceIndex, count - 1))
                        onActivated: root.audioCaptureDeviceIndex = currentIndex
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "设备状态"; value: root.musicDeviceSummary() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "录音文件"; value: "/tmp/boardlab-audio-test.wav" }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: (root.audioBackendBusy || boardBackend.audioOperationBusy) ? "音频后端处理中"
                                                                              : (root.playbackDeviceAvailable ? "播放耳机测试音频" : "未发现播放设备")
                        accent: root.toolAccent("Music")
                        variant: "primary"
                        enabled: root.audioCanStart() && root.playbackDeviceAvailable
                        onClicked: root.queueAudioOperation("play-test")
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.captureDeviceAvailable ? "录音 2 秒" : "未发现录音设备"
                            variant: "secondary"
                            enabled: root.audioCanStart() && root.captureDeviceAvailable
                            onClicked: root.queueAudioOperation("record")
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.playbackDeviceAvailable ? "播放录音" : "未发现播放设备"
                            variant: "secondary"
                            enabled: root.audioCanStart() && root.playbackDeviceAvailable
                            onClicked: root.queueAudioOperation("play-recorded")
                        }
                    }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: "停止音频 / 录音"
                        variant: "stop"
                        enabled: root.audioCanStop()
                        onClicked: root.queueAudioOperation("stop-audio")
                    }
                    Text {
                        text: root.playbackDeviceAvailable
                              ? "播放设备音量 " + root.audioVolumeTargetPercent + "%"
                              : "未发现播放设备"
                        color: theme.inkStrong
                        font.pixelSize: Math.round(15 * root.uiScale)
                        font.weight: Font.Bold
                    }
                    BoardToolsSlider {
                        uiScale: root.uiScale
                        width: parent.width
                        from: 0
                        to: 100
                        value: root.audioVolumeTargetPercent
                        enabled: root.playbackDeviceAvailable && !root.audioBackendBusy && !boardBackend.audioOperationBusy
                        onMoved: root.audioVolumeTargetPercent = Math.round(value)
                    }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.playbackDeviceAvailable ? "应用播放设备音量" : "未发现播放设备"
                        accent: root.toolAccent("Music")
                        variant: "primary"
                        enabled: root.playbackDeviceAvailable && !root.audioBackendBusy && !boardBackend.audioOperationBusy
                        onClicked: root.queueAudioOperation("volume")
                    }
                    Text { visible: false; text: "蜂鸣器音量 " + root.beepPercent + "%"; color: theme.inkStrong; font.pixelSize: Math.round(15 * root.uiScale); font.weight: Font.Bold }
                    BoardToolsSlider {
                        uiScale: root.uiScale
                        visible: false
                        width: parent.width
                        from: 0
                        to: 100
                        value: root.beepPercent
                        enabled: root.audioCanStart()
                        onMoved: root.beepPercent = Math.round(value)
                    }
                    Row {
                        visible: false
                        width: parent.width
                        spacing: 8
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing * 2) / 3
                            text: "应用"
                            accent: root.toolAccent("Buzzer")
                            variant: "primary"
                            enabled: root.audioCanStart()
                            onClicked: root.queueAudioOperation("beep-apply")
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing * 2) / 3
                            text: "心跳"
                            accent: theme.accent
                            variant: "secondary"
                            enabled: root.audioCanStart()
                            onClicked: root.queueAudioOperation("beep-heartbeat")
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing * 2) / 3
                            text: "停止"
                            variant: "stop"
                            enabled: root.audioCanStart()
                            onClicked: root.queueAudioOperation("beep-stop")
                        }
                    }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: "音频状态"
                        value: root.audioStatusForTool("Music")
                        accent: root.audioResultAccent("Music")
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "播放设备音量"; value: boardBackend.audioVolumePercent + "%" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "设备状态"; value: root.musicDeviceSummary() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "队列状态"; value: root.audioQueueStatusForTool("Music") }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端状态"; value: boardBackend.statusText.length > 0 ? boardBackend.statusText : "后端就绪" }
                }
            }

            BoardToolsWorkSurface {
                objectName: "buzzer-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Buzzer"
                leftTitle: "pwm-beeper 蜂鸣器"
                middleTitle: "提示音控制"
                rightTitle: "执行状态"
                leftContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "运行时状态"; value: boardBackend.buzzerAvailabilityStatus }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前音调"; value: root.buzzerValueText() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "控制方式"; value: root.buzzerUsesInputEvent() ? "pwm-beeper 输入事件" : "不可用" }
                }
                middleContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    Text { text: "提示音调 " + root.buzzerValueText(); color: theme.inkStrong; font.pixelSize: Math.round(15 * root.uiScale); font.weight: Font.Bold }
                    BoardToolsSlider { uiScale: root.uiScale; width: parent.width; from: 0; to: 100; value: root.beepPercent; enabled: root.buzzerCanStart(); onMoved: root.beepPercent = Math.round(value) }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: boardBackend.buzzerAvailable ? "应用蜂鸣器" : "蜂鸣器不可用"; accent: root.toolAccent("Buzzer"); variant: "primary"; enabled: root.buzzerCanStart(); onClicked: root.queueAudioOperation("beep-apply") }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: "心跳提示"; accent: theme.accent; variant: "secondary"; enabled: root.buzzerCanStart(); onClicked: root.queueAudioOperation("beep-heartbeat") }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: "停止蜂鸣器"; variant: "stop"; enabled: root.buzzerCanStop(); onClicked: root.queueAudioOperation("beep-stop") }
                }
                rightContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale; label: "蜂鸣器状态"; value: root.audioStatusForTool("Buzzer"); accent: root.audioResultAccent("Buzzer") }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "队列"; value: root.audioQueueStatusForTool("Buzzer") }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端"; value: boardBackend.audioOperationBusy ? "处理中" : "空闲" }
                }
            }

            BoardToolsWorkSurface {
                objectName: "led-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "LED"
                leftTitle: "已识别 LED"
                middleTitle: "亮度与闪烁"
                rightTitle: "写入确认"
                leftContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    Repeater { model: boardBackend.ledDevices; delegate: BoardToolsInfoBlock { uiScale: root.uiScale; width: parent.width; label: modelData.name; value: modelData.percent + "%" } }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "设备"; value: boardBackend.ledAvailable ? "已加载板级路径" : "未发现可写 LED，请检查驱动与权限" }
                }
                middleContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsComboBox { id: ledDeviceBox; uiScale: root.uiScale; width: parent.width; height: root.touchTarget; model: boardBackend.ledDevices; textRole: "name"; onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex < boardBackend.ledDevices.length) root.ledDeviceName = boardBackend.ledDevices[currentIndex].name }
                    Text { text: "亮度 " + root.ledPercent + "%"; color: theme.inkStrong; font.pixelSize: Math.round(15 * root.uiScale); font.weight: Font.Bold }
                    BoardToolsSlider { uiScale: root.uiScale; width: parent.width; from: 0; to: 100; value: root.ledPercent; enabled: boardBackend.ledAvailable && !boardBackend.backendBusy; onMoved: root.ledPercent = Math.round(value) }
                    BoardToolsActionButton { uiScale: root.uiScale; width: parent.width; text: "应用当前 LED"; accent: root.toolAccent("LED"); variant: "primary"; enabled: boardBackend.ledAvailable && !boardBackend.backendBusy; onClicked: boardBackend.runHardwareOperationAsync("led-set", {"name": root.ledDeviceName, "percent": root.ledPercent}) }
                    Row { width: parent.width; spacing: 8
                        BoardToolsActionButton { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; text: "全亮"; accent: root.toolAccent("LED"); variant: "secondary"; enabled: boardBackend.ledAvailable && !boardBackend.backendBusy; onClicked: boardBackend.runHardwareOperationAsync("led-all-on", {}) }
                        BoardToolsActionButton { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; text: "全灭"; variant: "secondary"; enabled: boardBackend.ledAvailable && !boardBackend.backendBusy; onClicked: boardBackend.runHardwareOperationAsync("led-all-off", {}) }
                        BoardToolsActionButton { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; text: "闪烁"; variant: "primary"; accent: root.toolAccent("LED"); enabled: boardBackend.ledAvailable && !boardBackend.backendBusy; onClicked: boardBackend.runHardwareOperationAsync("led-blink", {}) }
                    }
                }
                rightContent: Column {
                    spacing: 12; width: parent.width; height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale; label: "LED 状态"; value: root.ledResultText; accent: root.toolAccent("LED") }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "保护"; value: "仅写入当前板型白名单" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "回读"; value: "写入后自动读取 brightness" }
                }
            }

            BoardToolsWorkSurface {
                objectName: "ethernet-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Ethernet"
                leftTitle: "接口与链路"
                middleTitle: "Ping 与配置"
                rightTitle: "测试结果"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    BoardToolsComboBox {
                        id: ethernetInterfaceBox
                        objectName: "ethernet-interface-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "ethernet-interface-dropdown"
                        model: boardBackend.ethernetInterfaces.length > 0 ? boardBackend.ethernetInterfaces : [{"name": "No Ethernet", "state": "not found", "address": "", "ipv4": "未分配", "linkUp": false}]
                        textRole: "name"
                        currentIndex: Math.max(0, Math.min(root.ethernetInterfaceIndex, count - 1))
                        onActivated: root.ethernetInterfaceIndex = currentIndex
                    }
                    BoardToolsStatusHero {
                        objectName: "ethernet-link-status"
                        uiScale: root.uiScale
                        width: parent.width
                        label: "当前接口与物理链路"
                        value: root.selectedEthernet().name + " · " + root.ethernetLinkLabel()
                        accent: root.ethernetLinkAccent()
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前 IPv4"; value: root.ethernetIpv4Label() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "MAC"; value: root.selectedEthernet().address }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "诊断"; value: root.ethernetDiagnosticLabel() }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: "刷新接口状态"
                        enabled: !boardBackend.backendBusy && String(root.selectedEthernet().name || "").indexOf("No Ethernet") !== 0
                        accent: root.toolAccent("Ethernet")
                        variant: "secondary"
                        onClicked: {
                            var iface = root.selectedEthernet().name
                            root.pendingEthernetInterface = iface
                            root.pendingEthernetMode = "refresh"
                            root.ethernetResultText = "正在读取 " + iface + " 的链路和 IPv4…"
                            boardBackend.runHardwareOperationAsync("ethernet-refresh", {"interface": iface})
                        }
                    }
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        label: "解锁修改"
                        checked: root.ethernetEditEnabled
                        accent: root.toolAccent("Ethernet")
                        onText: "解锁"
                        offText: "锁定"
                        onToggled: root.ethernetEditEnabled = checked
                    }
                }

                middleContent: Column {
                    spacing: 10
                    width: parent.width
                    Text {
                        width: parent.width
                        text: "连通性测试"
                        color: theme.inkStrong
                        font.pixelSize: Math.round(15 * root.uiScale)
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: "当前来源：" + root.ethernetPingSourceLabel(root.selectedEthernet().name)
                              + "。确认网线已连接且 IPv4 已分配后，默认 Ping www.baidu.com。"
                        color: theme.muted
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                    BoardToolsTextField {
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        font.pixelSize: root.inputFontSize
                        placeholderText: "默认目标：www.baidu.com"
                        text: root.ethernetPingTarget
                        onTextChanged: root.ethernetPingTarget = text
                    }
                    BoardToolsActionButton {
                        id: ethernetPingButton
                        objectName: "ethernet-ping-baidu-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.ethernetPingTargetLabel() === "www.baidu.com" ? "Ping 百度" : "Ping " + root.ethernetPingTargetLabel()
                        enabled: !boardBackend.backendBusy && String(root.selectedEthernet().name || "").indexOf("No Ethernet") !== 0
                        accent: root.toolAccent("Ethernet")
                        variant: "primary"
                        onClicked: {
                            var iface = root.selectedEthernet().name
                            var target = root.ethernetPingTargetLabel()
                            root.pendingPingInterface = iface
                            root.pendingPingTarget = target
                            root.pendingPingContext = "ethernet"
                            root.ethernetPingResultText = "正在通过 " + root.ethernetPingSourceLabel(iface) + " Ping " + target + "…"
                            root.ethernetResultText = root.ethernetPingResultText
                            boardBackend.pingInterfaceAsync(iface, target)
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: theme.rule }
                    Text {
                        width: parent.width
                        text: "地址配置"
                        color: theme.inkStrong
                        font.pixelSize: Math.round(15 * root.uiScale)
                        font.weight: Font.Bold
                    }
                    BoardToolsComboBox {
                        id: ethernetModeBox
                        objectName: "ethernet-mode-dropdown"
                        property var values: ["dhcp", "static"]
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        enabled: root.ethernetEditEnabled || startupDropdown === "ethernet-mode-dropdown"
                        openOnReady: startupDropdown === "ethernet-mode-dropdown"
                        model: ["DHCP 自动获取", "静态 IPv4"]
                        currentIndex: root.ethernetIpMode === "static" ? 1 : 0
                        onActivated: root.ethernetIpMode = values[currentIndex]
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.ethernetIpMode === "static" ? "应用静态" : "应用 DHCP"
                            enabled: root.ethernetEditEnabled && !boardBackend.backendBusy && root.selectedEthernet().name.indexOf("No Ethernet") !== 0
                            accent: root.toolAccent("Ethernet")
                            variant: "primary"
                            onClicked: {
                                var iface = root.selectedEthernet().name
                                root.pendingEthernetInterface = iface
                                root.pendingEthernetMode = root.ethernetIpMode
                                root.ethernetResultText = "配置中: " + iface + " / " + root.ethernetIpMode
                                boardBackend.runHardwareOperationAsync(root.ethernetIpMode === "static" ? "ethernet-static" : "ethernet-dhcp", {
                                                                           "interface": iface,
                                                                           "address": root.ethernetStaticIp,
                                                                           "netmask": root.ethernetNetmask,
                                                                           "gateway": root.ethernetGateway,
                                                                           "dns1": root.ethernetDns1,
                                                                           "dns2": root.ethernetDns2
                                                                       })
                            }
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        BoardToolsTextField { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; height: root.touchTarget; font.pixelSize: root.inputFontSize; enabled: root.ethernetEditEnabled && root.ethernetIpMode === "static"; placeholderText: "IP"; text: root.ethernetStaticIp; onTextChanged: root.ethernetStaticIp = text }
                        BoardToolsTextField { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; height: root.touchTarget; font.pixelSize: root.inputFontSize; enabled: root.ethernetEditEnabled && root.ethernetIpMode === "static"; placeholderText: "掩码/前缀"; text: root.ethernetNetmask; onTextChanged: root.ethernetNetmask = text }
                        BoardToolsTextField { uiScale: root.uiScale; width: (parent.width - parent.spacing * 2) / 3; height: root.touchTarget; font.pixelSize: root.inputFontSize; enabled: root.ethernetEditEnabled && root.ethernetIpMode === "static"; placeholderText: "网关"; text: root.ethernetGateway; onTextChanged: root.ethernetGateway = text }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        BoardToolsTextField { uiScale: root.uiScale; width: (parent.width - parent.spacing) / 2; height: root.touchTarget; font.pixelSize: root.inputFontSize; enabled: root.ethernetEditEnabled; placeholderText: "DNS1"; text: root.ethernetDns1; onTextChanged: root.ethernetDns1 = text }
                        BoardToolsTextField { uiScale: root.uiScale; width: (parent.width - parent.spacing) / 2; height: root.touchTarget; font.pixelSize: root.inputFontSize; enabled: root.ethernetEditEnabled; placeholderText: "DNS2"; text: root.ethernetDns2; onTextChanged: root.ethernetDns2 = text }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "修改以太网配置会调用 NetworkManager nmcli connection modify/up；请先解锁修改。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    BoardToolsStatusHero { uiScale: root.uiScale
                        label: "Ping 结果"
                        value: root.ethernetPingResultText
                        accent: root.statusAccent(root.ethernetPingResultText, root.toolAccent("Ethernet"))
                    }
                    BoardToolsStatusHero { uiScale: root.uiScale
                        label: "当前以太网 IPv4"
                        value: root.ethernetIpv4Label()
                        accent: root.ethernetLinkAccent()
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "Ping 来源"; value: root.ethernetPingSourceLabel(root.selectedEthernet().name) }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "物理链路"; value: root.ethernetLinkLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "测试目标"; value: root.ethernetPingTargetLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "诊断"; value: root.ethernetDiagnosticLabel() }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.ethernetLog }
            }

            BoardToolsWorkSurface {
                objectName: "can-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "CAN"
                leftTitle: "总线参数"
                middleTitle: "SocketCAN 控制"
                rightTitle: "CAN 状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsComboBox {
                        id: canInterfaceBox
                        objectName: "can-interface-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "can-interface-dropdown"
                        model: boardBackend.canInterfaces.length > 0 ? boardBackend.canInterfaces : ["No CAN"]
                        currentIndex: Math.max(0, root.valueIndex(model, root.canInterface, 0))
                        onActivated: root.canInterface = currentText
                    }
                    BoardToolsComboBox {
                        id: canBitrateBox
                        objectName: "can-bitrate-dropdown"
                        property var values: [50000, 100000, 125000, 250000, 500000, 1000000]
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "can-bitrate-dropdown"
                        model: ["50k", "100k", "125k", "250k", "500k", "1M"]
                        currentIndex: Math.max(0, root.valueIndex(values, root.canBitrate, 5))
                        onActivated: root.canBitrate = values[currentIndex]
                    }
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        label: "CAN FD（暂不支持）"
                        checked: false
                        enabled: false
                        accent: root.toolAccent("CAN")
                        onText: "FD"
                        offText: "Classic"
                        onToggled: root.canFd = false
                    }
                    BoardToolsComboBox {
                        id: canDataBitrateBox
                        objectName: "can-data-bitrate-dropdown"
                        visible: root.canFd || startupDropdown === "can-data-bitrate-dropdown"
                        property var values: [500000, 1000000, 2000000, 3000000, 4000000, 5000000]
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "can-data-bitrate-dropdown"
                        model: ["500k", "1M", "2M", "3M", "4M", "5M"]
                        currentIndex: Math.max(0, root.valueIndex(values, root.canDataBitrate, 3))
                        onActivated: root.canDataBitrate = values[currentIndex]
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前配置"; value: root.selectedCanInterface() + " / " + root.canModeText() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "最后结果"; value: root.canResultText }
                }

                middleContent: Column {
                    spacing: 10
                    width: parent.width
                    height: parent.height
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        label: "控制总线"
                        checked: root.canControlEnabled
                        accent: root.toolAccent("CAN")
                        onText: "控制"
                        offText: "只读"
                        onToggled: root.canControlEnabled = checked
                    }
                    Grid {
                        width: parent.width
                        columns: 2
                        spacing: 8
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "打开"
                            enabled: root.canControlEnabled && root.canReady() && !boardBackend.backendBusy
                            accent: root.toolAccent("CAN")
                            variant: "danger"
                            onClicked: {
                                var iface = root.selectedCanInterface()
                                root.pendingCanInterface = iface
                                root.pendingCanMode = root.canModeText()
                                root.canResultText = "打开中: " + iface + " / " + root.canModeText()
                                root.canBusStatusText = root.canResultText
                                boardBackend.runHardwareOperationAsync("can-open", {
                                                                           "interface": iface,
                                                                           "bitrate": root.canBitrate,
                                                                           "dataBitrate": root.canDataBitrate,
                                                                           "fdMode": false,
                                                                           "confirmCan": root.canControlEnabled
                                                                       })
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "RX"
                            enabled: root.canReady() && !boardBackend.backendBusy
                            variant: "secondary"
                            onClicked: {
                                var iface = root.selectedCanInterface()
                                root.pendingCanInterface = iface
                                root.pendingCanMode = root.canModeText()
                                root.canResultText = "RX 读取中: " + iface
                                root.canRxStatusText = root.canResultText
                                boardBackend.runHardwareOperationAsync("can-rx", {"interface": iface})
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "TX 单次"
                            enabled: root.canControlEnabled && root.canReady() && !boardBackend.backendBusy
                            accent: root.toolAccent("CAN")
                            variant: "danger"
                            onClicked: {
                                var iface = root.selectedCanInterface()
                                root.pendingCanInterface = iface
                                root.pendingCanMode = root.canModeText()
                                root.canResultText = "TX 发送中: " + iface
                                root.canTxStatusText = root.canResultText
                                boardBackend.runHardwareOperationAsync("can-tx", {"interface": iface, "confirmCan": root.canControlEnabled})
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "关闭"
                            enabled: root.canControlEnabled && root.canReady() && !boardBackend.backendBusy
                            variant: "stop"
                            onClicked: {
                                var iface = root.selectedCanInterface()
                                root.pendingCanInterface = iface
                                root.pendingCanMode = root.canModeText()
                                root.canResultText = "关闭中: " + iface
                                root.canBusStatusText = root.canResultText
                                boardBackend.runHardwareOperationAsync("can-close", {"interface": iface, "confirmCan": root.canControlEnabled})
                            }
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "参考循环发送"
                            enabled: root.canControlEnabled && root.canReady() && !boardBackend.backendBusy
                                     && !root.canLoopStarting && !root.canLoopRunning && !root.canLoopStopping
                            accent: root.toolAccent("CAN")
                            variant: "danger"
                            onClicked: {
                                var iface = root.selectedCanInterface()
                                root.pendingCanInterface = iface
                                root.pendingCanMode = root.canModeText()
                                root.canResultText = "循环发送启动中: " + iface
                                root.canTxStatusText = root.canResultText
                                root.canLoopInterface = iface
                                root.canLoopStarting = true
                                root.canLoopStopWhenReady = false
                                if (!boardBackend.runHardwareOperationAsync("can-loop-start", {
                                                                           "interface": iface,
                                                                           "bitrate": root.canBitrate,
                                                                           "confirmCan": root.canControlEnabled
                                                                       })) {
                                    root.canLoopStarting = false
                                    root.canLoopInterface = ""
                                }
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: "停止参考发送"
                            enabled: (root.canLoopStarting || root.canLoopRunning) && !root.canLoopStopping
                            variant: "stop"
                            onClicked: {
                                root.requestCanLoopStop("停止循环发送中")
                            }
                        }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "当前仅支持经典 CAN 标准数据帧；CAN FD 未验证并已禁用。打开、关闭、TX 和参考循环发送都会操作真实 CAN 总线。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: "操作结果"
                        value: root.canResultText
                        accent: root.canResultAccent()
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "总线状态"; value: root.canBusStatusText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "发送状态"; value: root.canTxStatusText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "接收状态"; value: root.canRxStatusText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "TX 帧"; value: root.canTxFrameText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "RX 样本"; value: root.canRxSampleText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前接口"; value: root.selectedCanInterface() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "模式 / 授权"; value: root.canModeText() + " / " + (root.canControlEnabled ? "控制总线" : "只读模式") }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.canLog }
            }

            BoardToolsWorkSurface {
                objectName: "cellular-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Cellular"
                leftTitle: "模组 / 接口"
                middleTitle: "拨号控制"
                rightTitle: "拨号状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "检测模组"; value: root.cellularModemLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "拨号程序"; value: root.cellularDialerLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "DHCP 客户端"; value: root.cellularDhcpLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "数据接口"; value: root.cellularInterfaceDisplay() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "最后结果"; value: root.cellularAvailable ? root.cellularResultText : "未发现蜂窝模组，无法读取或控制" }
                    BoardToolsTextField { uiScale: root.uiScale; width: parent.width; height: root.touchTarget; font.pixelSize: root.inputFontSize; placeholderText: "APN，可留空"; text: root.cellularApn; enabled: root.cellularAvailable; onTextChanged: root.cellularApn = text }
                    BoardToolsTextField { uiScale: root.uiScale; width: parent.width; height: root.touchTarget; font.pixelSize: root.inputFontSize; placeholderText: "数据接口由拨号后自动发现，可留空"; text: root.cellularInterface; enabled: root.cellularAvailable; onTextChanged: root.cellularInterface = text }
                }

                middleContent: Column {
                    spacing: 10
                    width: parent.width
                    height: parent.height
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        label: "控制模组"
                        checked: root.cellularControlEnabled
                        enabled: root.cellularAvailable && !boardBackend.backendBusy
                        accent: root.toolAccent("Cellular")
                        onText: "控制"
                        offText: "检查"
                        onToggled: root.cellularControlEnabled = checked
                    }
                    Grid {
                        width: parent.width
                        columns: 2
                        spacing: 8
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.cellularCanStart ? "识别后拨号" : (root.cellularAvailable ? (root.cellularDhcpReady ? "拨号器不可用" : "缺少 DHCP 客户端") : "未发现蜂窝模组")
                            enabled: root.cellularCanStart && root.cellularControlEnabled && !boardBackend.backendBusy
                            accent: root.toolAccent("Cellular")
                            variant: "danger"
                            onClicked: {
                                root.pendingCellularApn = root.cellularApn
                                root.pendingCellularInterface = root.cellularInterfaceForRequest()
                                root.cellularResultText = "正在启动受控拨号程序"
                                boardBackend.runHardwareOperationAsync("cellular-start", {
                                                                           "apn": root.cellularApn,
                                                                           "interface": root.cellularInterfaceForRequest()
                                                                       })
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.cellularAvailable ? "检查" : "未发现蜂窝模组"
                            enabled: root.cellularAvailable && !boardBackend.backendBusy
                            variant: "secondary"
                            onClicked: {
                                root.cellularResultText = "状态读取中"
                                boardBackend.runHardwareOperationAsync("cellular-status", {})
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.cellularInterfaceForRequest().length > 0 ? "联网测试" : "等待接口"
                            enabled: root.cellularAvailable && root.cellularControlEnabled && root.cellularInterfaceForRequest().length > 0 && !boardBackend.backendBusy
                            accent: root.toolAccent("Cellular")
                            variant: "danger"
                            onClicked: {
                                var iface = root.cellularInterfaceForRequest()
                                root.pendingPingInterface = iface
                                root.pendingPingTarget = "www.baidu.com"
                                root.pendingPingContext = "cellular"
                                root.cellularResultText = "联网测试中: " + iface
                                root.appendBoundedLog("cellularLog", "\n--- PING " + iface + " ---\n")
                                boardBackend.pingInterfaceAsync(iface, "www.baidu.com")
                            }
                        }
                        BoardToolsActionButton {
                            uiScale: root.uiScale
                            width: (parent.width - parent.spacing) / 2
                            text: root.cellularAvailable ? "停止" : "未发现蜂窝模组"
                            enabled: root.cellularAvailable && root.cellularControlEnabled && !boardBackend.backendBusy
                            variant: "stop"
                            onClicked: {
                                root.cellularResultText = "正在停止受控拨号程序"
                                boardBackend.runHardwareOperationAsync("cellular-stop", {
                                                                           "interface": root.cellularInterfaceForRequest()
                                                                       })
                            }
                        }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: root.cellularAvailable ? (root.cellularDhcpReady ? "后端按 USB 身份和当前 SoC 自动选定 Quectel 程序；V1.6.8 会验证 usb0/wwan 的 IPv4 获取结果；停止仅影响本应用启动的拨号进程。" : "当前 V1.6.8 需要 dhclient；请安装 isc-dhcp-client 后重新拨号。") : "未发现蜂窝模组；连接模块后重新扫描设备，再执行拨号控制。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: root.cellularAvailable ? (root.cellularControlEnabled ? "拨号控制已授权" : "状态检查模式") : "未发现蜂窝模组"
                        value: root.cellularAvailable ? root.cellularResultText : "未发现蜂窝模组"
                        accent: root.cellularAvailable && root.cellularControlEnabled ? root.toolAccent("Cellular") : theme.inkFaint
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "操作状态"; value: root.cellularAvailable ? root.cellularResultText : "等待发现蜂窝模组" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "状态详情"; value: root.cellularDetailText }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "自动选择"; value: root.cellularModemLabel() + " → " + root.cellularDialerLabel() }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "APN / 接口"; value: (root.cellularApn.length > 0 ? root.cellularApn : "模块默认 APN") + " / " + (root.cellularInterfaceForRequest().length > 0 ? root.cellularInterfaceForRequest() : "等待自动发现") }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.cellularLog }
            }

            BoardToolsWorkSurface {
                id: cameraWorkSurface
                objectName: "camera-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Camera"
                leftTitle: "视频设备"
                middleTitle: "实时预览"
                rightTitle: "预览状态"
                onPageIndexChanged: {
                    if (narrowLayout && pageIndex !== 1)
                        root.stopCameraPreview("已离开实时预览页，摄像头已停止")
                }

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsComboBox {
                        id: cameraDeviceBox
                        objectName: "camera-device-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "camera-device-dropdown"
                        model: root.cameraDeviceDropdownOptions()
                        textRole: "label"
                        currentIndex: Math.max(0, Math.min(root.cameraDeviceIndex, count - 1))
                        onActivated: root.selectCameraDevice(currentIndex)
                        Component.onCompleted: root.ensureCameraDeviceSelection()
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "默认节点"; value: root.selectedCameraDevice().length > 0 ? root.selectedCameraDevice() : "未检测到摄像头采集节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "筛选依据"; value: "优先 rkisp_mainpath/mainpath 与 USB/UVC；rkcif 原始节点保留为手动选项。" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "HDMIIN 节点"; value: boardBackend.hdmiInVideoDevice.length > 0 ? boardBackend.hdmiInVideoDevice : "未检测到 rk_hdmirx" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "已检测"; value: root.cameraDeviceLabels().length > 0 ? root.cameraDeviceLabels().join(", ") : "未发现可用摄像头节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "预览状态"; value: root.cameraStateText }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsActionButton {
                        objectName: "camera-preview-start-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.selectedCameraDevice().length > 0 ? "启动实时预览" : "未检测到摄像头节点"
                        enabled: root.selectedCameraDevice().length > 0 && !root.cameraPreviewRequested
                                 && !root.videoFullscreenVisible && !boardBackend.backendBusy
                        accent: root.toolAccent("Camera")
                        variant: "primary"
                        onClicked: {
                            root.ensureCameraDeviceSelection()
                            root.pendingPreviewDevice = root.selectedCameraDevice()
                            root.cameraMode = root.selectedCameraMode()
                            root.cameraStateText = "正在打开实时预览: " + root.cameraMode + " / " + root.pendingPreviewDevice
                            root.cameraPreviewRequested = true
                            root.appendBoundedLog("cameraLog", "START live GStreamer appsink preview " + root.cameraMode + " " + root.pendingPreviewDevice + "\n")
                        }
                    }
                    BoardToolsActionButton {
                        objectName: "camera-preview-stop-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: "停止实时预览"
                        enabled: root.cameraPreviewRequested
                        variant: "stop"
                        onClicked: root.stopCameraPreview("实时预览已停止")
                    }
                    Loader {
                        id: cameraPreviewLoader
                        objectName: "camera-preview-loader"
                        width: parent.width
                        height: Math.max(Math.round(246 * root.uiScale), Math.round(width * 9 / 16))
                        active: root.activeTool === "Camera"
                        source: active ? "qrc:/components/CameraPreviewSurface.qml" : ""
                        onLoaded: {
                            item.uiScale = Qt.binding(function() { return root.uiScale })
                            item.sourceTitle = "摄像头"
                            item.devicePath = Qt.binding(function() { return root.selectedCameraDevice() })
                            item.requested = Qt.binding(function() {
                                return root.cameraPreviewRequested && root.activeTool === "Camera"
                            })
                        }
                        onStatusChanged: {
                            if (status === Loader.Error) {
                                root.cameraPreviewRequested = false
                                root.cameraStateText = "GStreamer 内嵌预览组件不可用"
                                root.appendBoundedLog("cameraLog", "GStreamer appsink preview load failed for "
                                                      + root.selectedCameraDevice() + "\n")
                            }
                        }
                    }
                    Connections {
                        target: cameraPreviewLoader.item
                        ignoreUnknownSignals: true
                        onStatusTextChanged: {
                            if (!target)
                                return
                            root.cameraStateText = target.statusText
                            if (target.requested && target.statusText.indexOf("预览失败:") === 0)
                                root.appendBoundedLog("cameraLog", "GStreamer appsink " + root.selectedCameraDevice()
                                                      + "\n" + target.statusText + "\n")
                        }
                    }
                    BoardToolsInfoBlock {
                        uiScale: root.uiScale
                        label: "视频后端"
                        value: cameraPreviewLoader.item
                               ? cameraPreviewLoader.item.detailText
                               : (cameraPreviewLoader.status === Loader.Error
                                  ? "GStreamer 内嵌预览组件加载失败"
                                  : "正在加载 GStreamer 内嵌预览组件")
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "实时预览通过 GStreamer appsink 直接嵌入当前 Qt 桌面会话；不创建独立桌面窗口。切换节点或离开页面会自动释放摄像头。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: root.selectedCameraDevice()
                        value: root.cameraStateText
                        accent: root.cameraPreviewRequested ? root.toolAccent("Camera") : theme.inkFaint
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "当前节点"; value: root.selectedCameraDevice().length > 0 ? root.selectedCameraDevice() : "未检测到摄像头采集节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "已检测"; value: root.cameraDeviceLabels().length > 0 ? root.cameraDeviceLabels().join(", ") : "未发现可用摄像头节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端状态"; value: boardBackend.statusText.length > 0 ? boardBackend.statusText : "后端就绪" }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.cameraLog }
            }

            BoardToolsWorkSurface {
                id: hdmiInWorkSurface
                objectName: "hdmiin-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "HDMIIN"
                leftTitle: "HDMI 输入"
                middleTitle: "实时预览"
                rightTitle: "预览状态"
                onPageIndexChanged: {
                    if (narrowLayout && pageIndex !== 1)
                        root.stopHdmiInPreview("已离开实时预览页，HDMI 输入已停止")
                }

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsComboBox {
                        id: hdmiInDeviceBox
                        objectName: "hdmiin-device-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "hdmiin-device-dropdown"
                        model: root.hdmiInDeviceDropdownOptions()
                        textRole: "label"
                        currentIndex: Math.max(0, Math.min(root.hdmiInDeviceIndex, count - 1))
                        onActivated: root.selectHdmiInDevice(currentIndex)
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "自动节点"; value: root.selectedHdmiInDevice().length > 0 ? root.selectedHdmiInDevice() : "未检测到 rk_hdmirx" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "摄像头节点"; value: boardBackend.cameraVideoDevice.length > 0 ? boardBackend.cameraVideoDevice : "未检测到 rkisp_mainpath" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "已检测"; value: root.hdmiInDeviceLabels().length > 0 ? root.hdmiInDeviceLabels().join(", ") : "未发现 HDMIIN 采集节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "采集状态"; value: root.hdmiInStateText }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsActionButton {
                        objectName: "hdmiin-preview-start-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.selectedHdmiInDevice().length > 0 ? "启动 HDMI 实时预览" : "未检测到 HDMI 输入节点"
                        enabled: root.selectedHdmiInDevice().length > 0 && !root.hdmiInPreviewRequested
                                 && !root.videoFullscreenVisible && !boardBackend.backendBusy
                        accent: root.toolAccent("HDMIIN")
                        variant: "primary"
                        onClicked: {
                            root.hdmiInStateText = "正在打开 HDMI 实时预览: " + root.selectedHdmiInDevice()
                            root.hdmiInPreviewRequested = true
                            root.appendBoundedLog("hdmiInLog", "START live GStreamer appsink HDMIIN preview "
                                                  + root.selectedHdmiInDevice() + "\n")
                        }
                    }
                    BoardToolsActionButton {
                        objectName: "hdmiin-preview-stop-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: "停止实时预览"
                        enabled: root.hdmiInPreviewRequested
                        variant: "stop"
                        onClicked: root.stopHdmiInPreview("HDMI 实时预览已停止")
                    }
                    BoardToolsActionButton {
                        objectName: "hdmiin-preview-fullscreen-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: "放大预览"
                        enabled: root.hdmiInPreviewRequested && !root.videoFullscreenVisible
                        accent: root.toolAccent("HDMIIN")
                        variant: "secondary"
                        onClicked: root.requestVideoFullscreen("hdmiin")
                    }
                    Loader {
                        id: hdmiInPreviewLoader
                        objectName: "hdmiin-preview-loader"
                        width: parent.width
                        height: Math.max(Math.round(246 * root.uiScale), Math.round(width * 9 / 16))
                        active: root.activeTool === "HDMIIN"
                        source: active ? "qrc:/components/CameraPreviewSurface.qml" : ""
                        onLoaded: {
                            root.hdmiInPreviewSurface = item
                            item.uiScale = Qt.binding(function() { return root.uiScale })
                            item.sourceTitle = "HDMI 输入"
                            item.devicePath = Qt.binding(function() { return root.selectedHdmiInDevice() })
                            item.requested = Qt.binding(function() {
                                return root.hdmiInPreviewRequested && root.activeTool === "HDMIIN"
                            })
                            item.presentationSuspended = Qt.binding(function() {
                                return root.videoFullscreenRequested
                                       && root.videoFullscreenSource === "hdmiin"
                            })
                        }
                        onItemChanged: {
                            root.hdmiInPreviewSurface = item
                        }
                        onStatusChanged: {
                            if (status === Loader.Error) {
                                root.hdmiInPreviewRequested = false
                                root.hdmiInStateText = "GStreamer HDMI 内嵌预览组件不可用"
                                root.appendBoundedLog("hdmiInLog", "GStreamer appsink HDMI preview load failed for "
                                                      + root.selectedHdmiInDevice() + "\n")
                            }
                        }
                    }
                    Connections {
                        target: hdmiInPreviewLoader.item
                        ignoreUnknownSignals: true
                        onStatusTextChanged: {
                            if (!target)
                                return
                            root.hdmiInStateText = target.statusText
                            if (target.requested && target.statusText.indexOf("预览失败:") === 0)
                                root.appendBoundedLog("hdmiInLog", "GStreamer appsink " + root.selectedHdmiInDevice()
                                                      + "\n" + target.statusText + "\n")
                        }
                    }
                    BoardToolsInfoBlock {
                        uiScale: root.uiScale
                        label: "视频后端"
                        value: hdmiInPreviewLoader.item
                               ? hdmiInPreviewLoader.item.detailText
                               : (hdmiInPreviewLoader.status === Loader.Error
                                  ? "GStreamer HDMI 内嵌预览组件加载失败"
                                  : "正在加载 GStreamer HDMI 内嵌预览组件")
                    }
                    BoardToolsActionButton {
                        objectName: "hdmiin-capture-smoke-test-button"
                        uiScale: root.uiScale
                        width: parent.width
                        text: "验证 HDMI 采集链路"
                        enabled: root.selectedHdmiInDevice().length > 0 && !root.hdmiInPreviewRequested
                                 && !root.videoFullscreenVisible && !boardBackend.backendBusy
                        variant: "quiet"
                        onClicked: {
                            root.pendingPreviewContext = "hdmiin"
                            root.pendingPreviewDevice = root.selectedHdmiInDevice()
                            root.hdmiInStateText = "正在验证 HDMI 采集链路: " + root.pendingPreviewDevice
                            boardBackend.runHardwareOperationAsync("hdmiin-start", {
                                                                       "mode": "hdmiin",
                                                                       "device": root.pendingPreviewDevice
                                                                   })
                        }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: "实时预览通过 GStreamer appsink 直接嵌入当前 Qt 桌面会话；没有 HDMI 信号时请先检查线缆和输入源。链路验证会独占节点，不能与预览同时运行。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: root.selectedHdmiInDevice().length > 0 ? root.selectedHdmiInDevice() : "No HDMIIN"
                        value: root.hdmiInStateText
                        accent: root.hdmiInPreviewRequested ? root.toolAccent("HDMIIN") : theme.inkFaint
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "自动节点"; value: root.selectedHdmiInDevice().length > 0 ? root.selectedHdmiInDevice() : "未检测到 rk_hdmirx" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "已检测"; value: root.hdmiInDeviceLabels().length > 0 ? root.hdmiInDeviceLabels().join(", ") : "未发现 HDMIIN 采集节点" }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "后端状态"; value: boardBackend.statusText.length > 0 ? boardBackend.statusText : "后端就绪" }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.hdmiInLog }
            }

            BoardToolsWorkSurface {
                objectName: "watchdog-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Watchdog"
                leftTitle: "Watchdog"
                middleTitle: "复位测试 (需确认)"
                rightTitle: "看门狗状态"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "已检测"; value: boardBackend.watchdogDevices.length > 0 ? boardBackend.watchdogDevices.join(", ") : "未发现 /dev/watchdog*" }
                    BoardToolsComboBox {
                        id: watchdogDeviceBox
                        objectName: "watchdog-device-dropdown"
                        uiScale: root.uiScale
                        width: parent.width
                        height: root.touchTarget
                        openOnReady: startupDropdown === "watchdog-device-dropdown"
                        model: root.watchdogAvailable ? boardBackend.watchdogDevices : ["未发现看门狗设备"]
                        enabled: root.watchdogAvailable
                        currentIndex: Math.max(0, root.valueIndex(model, root.watchdogDevice, 0))
                        onActivated: root.watchdogDevice = currentText
                    }
                    Column {
                        width: parent.width
                        spacing: Math.round(6 * root.uiScale)

                        Text {
                            width: parent.width
                            text: "超时时间"
                            color: theme.inkFaint
                            font.pixelSize: Math.round(11 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        BoardToolsSpinBox {
                            id: watchdogTimeoutBox
                            uiScale: root.uiScale
                            width: parent.width
                            height: root.touchTarget
                            from: 5
                            to: 120
                            value: root.watchdogTimeout
                            enabled: root.watchdogAvailable
                            suffixText: " 秒"
                            onValueModified: root.watchdogTimeout = value
                        }
                    }
                }

                middleContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.watchdogAvailable ? "读取 wdctl 状态" : "未发现看门狗设备"
                        enabled: root.watchdogAvailable && !boardBackend.backendBusy
                        variant: "secondary"
                        onClicked: {
                            var device = root.selectedWatchdogDevice()
                            root.pendingWatchdogDevice = device
                            root.watchdogResultText = "状态读取中: " + device
                            boardBackend.runHardwareOperationAsync("watchdog-status", {"device": device})
                        }
                    }
                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        label: "确认允许复位"
                        checked: root.watchdogResetConfirmed
                        enabled: root.watchdogAvailable && !boardBackend.backendBusy
                        accent: root.toolAccent("Watchdog")
                        onText: "允许"
                        offText: "禁止"
                        onToggled: root.watchdogResetConfirmed = checked
                    }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: root.watchdogAvailable ? "启动看门狗测试" : "未发现看门狗设备"
                        enabled: root.watchdogAvailable && root.watchdogResetConfirmed && !boardBackend.backendBusy
                        accent: root.toolAccent("Watchdog")
                        variant: "danger"
                        onClicked: {
                            var device = root.selectedWatchdogDevice()
                            var resetConfirmed = root.watchdogResetConfirmed
                            root.pendingWatchdogDevice = device
                            root.watchdogResultText = "启动中: " + device
                            root.watchdogResetConfirmed = false
                            boardBackend.runHardwareOperationAsync("watchdog-start", {
                                                                       "device": device,
                                                                       "timeout": root.watchdogTimeout,
                                                                       "confirm": resetConfirmed
                                                                   })
                        }
                    }
                    BoardToolsWarningBlock { uiScale: root.uiScale; text: root.watchdogAvailable ? "启动后可能在所设超时时间后触发板卡复位；请确认现场允许重启，并保持可恢复的远程连接。" : "未发现看门狗设备；无法读取状态或执行复位测试。" }
                }

                rightContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsStatusHero { uiScale: root.uiScale;
                        label: root.watchdogAvailable ? root.selectedWatchdogDevice() : "未发现看门狗设备"
                        value: root.watchdogAvailable ? root.watchdogResultText : "等待发现 /dev/watchdog*"
                        accent: root.watchdogAvailable ? root.statusAccent(root.watchdogResultText, root.toolAccent("Watchdog")) : theme.inkFaint
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "wdctl 详情"; value: root.watchdogStatusDetail }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "超时"; value: root.watchdogTimeout + " 秒" }
                }

                logContent: BoardToolsLogPanel { uiScale: root.uiScale; text: root.watchdogLog }
            }

            BoardToolsWorkSurface {
                objectName: "files-work-surface"
                uiScale: root.uiScale
                visible: root.activeTool === "Files"
                leftTitle: "当前位置"
                middleTitle: "目录"

                leftContent: Column {
                    spacing: 12
                    width: parent.width
                    height: parent.height
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "路径"; value: root.pathBreadcrumbText() }
                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: parent.width
                        text: "上一级"
                        variant: "secondary"
                        onClicked: {
                            root.filesStatusText = "正在打开上一级"
                            boardBackend.goUpAsync()
                        }
                    }
                    BoardToolsInfoBlock { uiScale: root.uiScale; label: "状态"; value: root.filesStatusText }
                }

                middleContent: Column {
                    spacing: 8
                    width: parent.width
                    height: parent.height
                    Repeater {
                        model: boardBackend.fileEntries
                        Rectangle {
                            id: fileEntryRow
                            width: parent.width
                            height: Math.round(46 * root.uiScale)
                            radius: Math.min(8, Math.round(8 * root.uiScale))
                            activeFocusOnTab: modelData.directory
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.directory ? "打开目录 " + modelData.name : "文件 " + modelData.name
                            Accessible.description: modelData.directory ? modelData.path : modelData.size
                            readonly property bool highlighted: activeFocus
                                                              || (modelData.directory && fileEntryMouse.containsMouse)
                            color: highlighted ? theme.accentSoft
                                               : (modelData.directory ? theme.surfaceSoft : theme.surfaceRaised)
                            border.color: activeFocus ? theme.focus : theme.rule
                            border.width: 1

                            Behavior on color { ColorAnimation { duration: theme.motionDuration(120) } }
                            Behavior on border.color { ColorAnimation { duration: theme.motionDuration(120) } }

                            Text {
                                x: Math.round(12 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Math.round(108 * root.uiScale)
                                text: modelData.name
                                color: modelData.directory ? theme.inkStrong : theme.ink
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(13 * root.uiScale)
                                font.weight: modelData.directory ? Font.DemiBold : Font.Normal
                            }

                            Item {
                                anchors.right: parent.right
                                anchors.rightMargin: Math.round(12 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.round(84 * root.uiScale)

                                LucideIcon {
                                    anchors.centerIn: parent
                                    width: Math.round(18 * root.uiScale)
                                    height: width
                                    visible: modelData.directory
                                    name: "chevron-right"
                                    color: theme.focus
                                }

                                Text {
                                    anchors.fill: parent
                                    visible: !modelData.directory
                                    horizontalAlignment: Text.AlignRight
                                    verticalAlignment: Text.AlignVCenter
                                    text: modelData.size
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                }
                            }

                            MouseArea {
                                id: fileEntryMouse
                                anchors.fill: parent
                                enabled: modelData.directory
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPressed: fileEntryRow.forceActiveFocus()
                                onClicked: {
                                    root.filesStatusText = "正在打开: " + modelData.path
                                    boardBackend.openPathAsync(modelData.path)
                                }
                            }

                            Keys.onSpacePressed: {
                                if (modelData.directory) {
                                    root.filesStatusText = "正在打开: " + modelData.path
                                    boardBackend.openPathAsync(modelData.path)
                                }
                            }
                            Keys.onReturnPressed: {
                                if (modelData.directory) {
                                    root.filesStatusText = "正在打开: " + modelData.path
                                    boardBackend.openPathAsync(modelData.path)
                                }
                            }
                            Keys.onEnterPressed: {
                                if (modelData.directory) {
                                    root.filesStatusText = "正在打开: " + modelData.path
                                    boardBackend.openPathAsync(modelData.path)
                                }
                            }
                        }
                    }
                }
            }

        }
    }

    Item {
        id: videoFullscreenOverlay
        objectName: "video-fullscreen-overlay"
        anchors.fill: parent
        visible: root.videoFullscreenVisible
        z: 100
        focus: visible

        onVisibleChanged: {
            if (visible)
                forceActiveFocus()
        }

        Keys.onReleased: {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                root.closeVideoFullscreen()
                event.accepted = true
            }
        }

        Rectangle {
            anchors.fill: parent
            // This is deliberately opaque.  During fullscreen there is no
            // reason to composite the expensive inline video preview below.
            color: theme.instrument
        }

        Loader {
            id: videoFullscreenPreviewLoader
            objectName: "video-fullscreen-preview-loader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: Math.round(20 * root.uiScale)
            anchors.rightMargin: Math.round(20 * root.uiScale)
            anchors.topMargin: Math.round(82 * root.uiScale)
            anchors.bottomMargin: Math.round(20 * root.uiScale)
            active: root.videoFullscreenRequested
            source: active ? "qrc:/components/CameraPreviewSurface.qml" : ""
            onLoaded: {
                item.uiScale = Qt.binding(function() { return root.uiScale })
                item.sourceTitle = Qt.binding(function() {
                    return root.videoSourceTitle(root.videoFullscreenSource)
                })
                item.devicePath = Qt.binding(function() { return root.videoFullscreenDevice })
                item.mirrorOnly = true
                item.mirrorSource = Qt.binding(function() {
                    return root.inlineVideoPreviewBackend(root.videoFullscreenSource)
                })
                item.requested = Qt.binding(function() {
                    return root.videoFullscreenRequested && root.videoFullscreenDevice.length > 0
                           && item.mirrorSource !== null
                })
            }
        }

        Rectangle {
            id: videoFullscreenHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.round(66 * root.uiScale)
            color: theme.instrumentRaised
            border.color: theme.withAlpha(theme.instrumentInk, 0.18)
            border.width: 1

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Math.round(24 * root.uiScale)
                anchors.right: fullscreenExitButton.left
                anchors.rightMargin: Math.round(18 * root.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(2 * root.uiScale)

                Text {
                    width: parent.width
                    text: root.videoSourceTitle(root.videoFullscreenSource) + " · 大屏实时预览"
                    color: theme.instrumentInk
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(18 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    text: root.videoFullscreenRequested
                          ? root.videoFullscreenDevice + " · Esc / 返回键可退出"
                          : "正在释放视频节点并切换预览，请稍候…"
                    color: theme.instrumentMuted
                    elide: Text.ElideMiddle
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            BoardToolsActionButton {
                id: fullscreenExitButton
                objectName: "video-fullscreen-exit-button"
                anchors.right: parent.right
                anchors.rightMargin: Math.round(18 * root.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                uiScale: root.uiScale
                width: Math.round(128 * root.uiScale)
                height: Math.round(42 * root.uiScale)
                text: "退出大屏"
                iconName: "x"
                variant: "stop"
                onClicked: root.closeVideoFullscreen()
            }
        }

        Column {
            anchors.centerIn: parent
            visible: !root.videoFullscreenRequested
            spacing: Math.round(8 * root.uiScale)

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "正在切换视频预览…"
                color: theme.instrumentInk
                font.pixelSize: Math.round(20 * root.uiScale)
                font.weight: Font.DemiBold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.videoFullscreenDevice
                color: theme.instrumentMuted
                font.pixelSize: Math.round(12 * root.uiScale)
            }
        }
    }

    Timer {
        id: audioBackendDelay
        interval: 80
        onTriggered: root.runPendingAudioOperation()
    }

    Timer {
        interval: 900
        running: root.visible && root.activeTool === "GPIO" && root.gpioInputMode && root.gpioAutoPoll
        repeat: true
        onTriggered: {
            if (!boardBackend.backendBusy) {
                boardBackend.runHardwareOperationAsync("gpio-read", {"pin": root.gpioPin})
            }
        }
    }











}
