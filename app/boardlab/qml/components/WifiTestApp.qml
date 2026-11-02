import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "wifi-test-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property string selectedInterface: ""
    property string selectedNetworkId: ""
    // The password field is conditionally visible, so the transient secret
    // lives on the page rather than being referenced by id.
    // It is cleared on every navigation, scan, selection change and submit.
    property string pendingWifiPassword: ""
    property string actionFeedback: root.wifiCapabilityEnabled
                                    ? "点击读取状态以检查由 NetworkManager 管理的 Wi-Fi 网卡"
                                    : "当前板型未启用 Wi-Fi 测试"
    property string pendingAction: ""
    property var pendingNetwork: ({})
    // Connectivity check after association: same backend path as Ethernet.
    property string wifiPingTarget: "www.baidu.com"
    property string wifiPingResultText: "等待 Ping"
    property bool wifiPingPending: false
    property string pendingPingInterface: ""
    property string pendingPingTarget: ""
    readonly property bool wifiCapabilityEnabled: boardProfile.hasCapability("wifi")
    readonly property var wifiState: root.normalizedWifiState(boardBackend.wifiManagerStatus)
    readonly property var wifiDevices: root.listValue(root.wifiState, "devices")
    readonly property var interfaceOptions: root.makeInterfaceOptions()
    readonly property var accessPoints: boardBackend.wifiAccessPoints ? boardBackend.wifiAccessPoints : []
    readonly property bool wifiBusy: boardBackend.wifiManagerBusy || root.boolValue(root.wifiState, "busy", false)
    readonly property bool backendBusy: boardBackend.backendBusy
    readonly property bool wifiAvailable: root.boolValue(root.wifiState, "available", false)
    readonly property bool radioEnabled: root.boolValue(root.wifiState, "radioEnabled", false)
    readonly property bool hasReadStatus: root.textValue(root.wifiState, "status", "idle") !== "idle"
    readonly property var selectedNetwork: root.findNetwork(root.selectedNetworkId)
    readonly property bool selectedNetworkExists: Object.keys(root.selectedNetwork).length > 0
    readonly property bool selectedNetworkHidden: root.boolValue(root.selectedNetwork, "hidden", false)
    readonly property bool selectedNetworkActive: root.boolValue(root.selectedNetwork, "active", false)
    readonly property bool scanCacheValid: root.boolValue(root.wifiState, "scanCacheValid", false)
    readonly property bool selectedNetworkMatchesInterface: root.textValue(root.selectedNetwork, "interface", "") === root.selectedInterface
    readonly property bool selectedNetworkCanConnect: root.wifiCapabilityEnabled
                                                   && !root.wifiBusy
                                                   && !root.backendBusy
                                                   && root.selectedInterface.length > 0
                                                   && root.selectedNetworkId.length > 0
                                                   && root.selectedNetworkExists
                                                   && !root.selectedNetworkHidden
                                                   && !root.selectedNetworkActive
                                                   && root.scanCacheValid
                                                   && root.selectedNetworkMatchesInterface
    readonly property bool selectedNetworkSecured: root.boolValue(root.selectedNetwork, "secured", false)
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)

    function mapValue(map, key, fallback) {
        if (!map || map[key] === undefined || map[key] === null)
            return fallback
        return map[key]
    }

    function textValue(map, key, fallback) {
        var value = root.mapValue(map, key, undefined)
        if (value === undefined || value === null || String(value).length === 0)
            return fallback
        return String(value)
    }

    function boolValue(map, key, fallback) {
        var value = root.mapValue(map, key, fallback)
        if (value === true || value === 1)
            return true
        return String(value).toLowerCase() === "true"
    }

    function numberValue(map, key, fallback) {
        var value = Number(root.mapValue(map, key, fallback))
        return isNaN(value) ? fallback : value
    }

    function listValue(map, key) {
        var value = root.mapValue(map, key, [])
        return value && value.length !== undefined && typeof value !== "string" ? value : []
    }

    function addressValue(value) {
        if (value === undefined || value === null)
            return ""
        if (typeof value === "string" || typeof value === "number")
            return String(value).trim()
        if (value.length !== undefined) {
            for (var index = 0; index < value.length; ++index) {
                var nested = root.addressValue(value[index])
                if (nested.length > 0)
                    return nested
            }
            return ""
        }
        if (typeof value === "object") {
            var fields = ["address", "ipv4", "ipAddress"]
            for (var fieldIndex = 0; fieldIndex < fields.length; ++fieldIndex) {
                var candidate = root.addressValue(value[fields[fieldIndex]])
                if (candidate.length > 0)
                    return candidate
            }
        }
        return ""
    }

    function currentWifiIpv4() {
        var device = root.selectedDevice()
        var values = [
            root.mapValue(device, "ipv4", ""),
            root.mapValue(device, "ipv4Address", ""),
            root.mapValue(device, "ipAddress", ""),
            root.mapValue(device, "ipv4Cidr", ""),
            root.mapValue(device, "ipv4Addresses", [])
        ]
        // The top-level NetworkManager address belongs to selectedDevice.
        // Do not reuse it after the operator selects a different Wi-Fi card.
        if (root.selectedInterfaceMatchesStatusDevice()) {
            values.push(root.mapValue(root.wifiState, "ipv4", ""))
            values.push(root.mapValue(root.wifiState, "ipv4Address", ""))
            values.push(root.mapValue(root.wifiState, "ipAddress", ""))
            values.push(root.mapValue(root.wifiState, "ipv4Cidr", ""))
            values.push(root.mapValue(root.wifiState, "ipv4Addresses", []))
        }
        for (var index = 0; index < values.length; ++index) {
            var address = root.addressValue(values[index])
            if (address.length > 0)
                return address
        }
        return ""
    }

    function wifiIpv4Assigned() {
        return root.boolValue(root.selectedDevice(), "ipv4Assigned", false)
                || (root.selectedInterfaceMatchesStatusDevice()
                    && root.boolValue(root.wifiState, "ipv4Assigned", false))
    }

    function wifiConnected() {
        var state = root.textValue(root.selectedDevice(), "state", "").toLowerCase()
        return root.boolValue(root.selectedDevice(), "connected", false)
                || state === "connected" || state === "activated"
                || root.selectedConnectionName() !== "未连接"
    }

    function wifiIpv4Display() {
        var address = root.currentWifiIpv4()
        if (address.length > 0)
            return address
        if (root.wifiIpv4Assigned())
            return "已分配，正在读取地址"
        if (root.wifiConnected())
            return "已连接，等待地址分配"
        return "连接后显示 IPv4"
    }

    function wifiIpv4Accent() {
        if (root.currentWifiIpv4().length > 0)
            return theme.accent
        if (root.wifiBusy)
            return theme.focus
        return theme.inkFaint
    }

    function normalizedWifiState(current) {
        if (current !== undefined && current !== null)
            return current
        return {
            "available": false,
            "nmcliAvailable": false,
            "status": "unavailable",
            "message": "当前运行版本未提供 Wi-Fi 管理服务。",
            "devices": [],
            "selectedDevice": "",
            "busy": false
        }
    }

    function deviceLabel(device) {
        var name = root.textValue(device, "device", "Wi-Fi 网卡")
        var state = root.textValue(device, "state", "未知")
        var connection = root.textValue(device, "connection", "")
        var note = ""
        var lower = String(state).toLowerCase()
        if (lower === "unmanaged")
            note = " · 未被 NM 管理"
        else if (lower === "unavailable")
            note = " · 不可用"
        else if (lower === "disabled")
            note = " · 已禁用"
        return name + " · " + state + note + (connection.length > 0 ? " · " + connection : "")
    }

    function makeInterfaceOptions() {
        var options = []
        for (var index = 0; index < root.wifiDevices.length; ++index) {
            var device = root.wifiDevices[index]
            var id = root.textValue(device, "device", "")
            if (id.length === 0)
                continue
            options.push({"id": id, "label": root.deviceLabel(device)})
        }
        if (options.length === 0)
            options.push({"id": "", "label": "请先读取 Wi-Fi 状态"})
        return options
    }

    function optionIndexForInterface(interfaceName) {
        for (var index = 0; index < root.interfaceOptions.length; ++index) {
            if (root.interfaceOptions[index].id === interfaceName)
                return index
        }
        return 0
    }

    function selectedDevice() {
        for (var index = 0; index < root.wifiDevices.length; ++index) {
            var candidate = root.wifiDevices[index]
            if (root.textValue(candidate, "device", "") === root.selectedInterface)
                return candidate
        }
        return {}
    }

    function selectedInterfaceMatchesStatusDevice() {
        var statusDevice = root.textValue(root.wifiState, "selectedDevice", "")
        return root.selectedInterface.length > 0
                && statusDevice.length > 0
                && root.selectedInterface === statusDevice
    }

    function selectInterface(interfaceName) {
        var next = String(interfaceName || "")
        if (root.selectedInterface === next)
            return
        root.selectedInterface = next
        root.selectedNetworkId = ""
        root.pendingWifiPassword = ""
        if (next.length > 0)
            root.actionFeedback = "已选择 " + next + "，请扫描该网卡的 Wi-Fi 网络"
    }

    function reconcileSelection() {
        var currentIsKnown = false
        for (var index = 0; index < root.wifiDevices.length; ++index) {
            if (root.textValue(root.wifiDevices[index], "device", "") === root.selectedInterface) {
                currentIsKnown = true
                break
            }
        }
        if (currentIsKnown)
            return
        var preferred = root.textValue(root.wifiState, "selectedDevice", "")
        if (preferred.length > 0)
            root.selectInterface(preferred)
        else if (root.wifiDevices.length > 0)
            root.selectInterface(root.textValue(root.wifiDevices[0], "device", ""))
        else
            root.selectInterface("")
    }

    function findNetwork(networkId) {
        for (var index = 0; index < root.accessPoints.length; ++index) {
            var network = root.accessPoints[index]
            if (root.textValue(network, "id", "") === networkId)
                return network
        }
        return {}
    }

    function isSelectedNetwork(network) {
        return root.textValue(network, "id", "") === root.selectedNetworkId
    }

    function networkName(network) {
        return root.textValue(network, "displayName", "请选择扫描结果")
    }

    function selectedConnectionName() {
        return root.textValue(root.selectedDevice(), "connection", "未连接")
    }

    function wifiPingTargetLabel() {
        var target = String(root.wifiPingTarget || "").trim()
        return target.length > 0 ? target : "www.baidu.com"
    }

    function wifiDeviceStateText() {
        return root.textValue(root.selectedDevice(), "state", "").toLowerCase()
    }

    function wifiHasIpv4() {
        return root.currentWifiIpv4().length > 0
    }

    function wifiPingReady() {
        if (!root.wifiCapabilityEnabled || root.wifiBusy || root.backendBusy)
            return false
        if (root.selectedInterface.length === 0)
            return false
        // Prefer an associated/connected interface with an address; still allow
        // ping when the operator has an IPv4 even if NM wording varies.
        if (root.wifiHasIpv4())
            return true
        var state = root.wifiDeviceStateText()
        return state.indexOf("connected") >= 0 || state === "activated"
    }

    function wifiPingUnavailableMessage() {
        if (!root.wifiCapabilityEnabled)
            return "当前板型未启用 Wi-Fi 测试"
        if (root.backendBusy)
            return root.backendBusyFeedback()
        if (root.wifiBusy)
            return "正在执行 Wi-Fi 操作，请等待完成"
        if (root.wifiPingPending)
            return "正在 Ping，请稍候"
        if (root.selectedInterface.length === 0)
            return "请先读取状态并选择 Wi-Fi 网卡"
        if (!root.wifiHasIpv4())
            return "当前 Wi-Fi 尚未获得 IPv4，请先连接热点后再 Ping"
        return ""
    }

    function requestWifiPing() {
        var blocked = root.wifiPingUnavailableMessage()
        if (blocked.length > 0) {
            root.actionFeedback = blocked
            root.wifiPingResultText = blocked
            return
        }
        var iface = root.selectedInterface
        var target = root.wifiPingTargetLabel()
        root.pendingPingInterface = iface
        root.pendingPingTarget = target
        root.wifiPingPending = true
        root.wifiPingResultText = "正在通过 " + iface + " Ping " + target + "…"
        root.actionFeedback = root.wifiPingResultText
        var accepted = boardBackend.pingInterfaceAsync(iface, target)
        if (!accepted) {
            root.wifiPingPending = false
            root.pendingPingInterface = ""
            root.pendingPingTarget = ""
            root.wifiPingResultText = "无法启动 Ping（可能有其他板端任务占用）"
            root.actionFeedback = root.wifiPingResultText
        }
    }

    function statusText() {
        if (!root.wifiCapabilityEnabled)
            return "当前板型未启用 Wi-Fi 测试"
        return root.textValue(root.wifiState, "message", "尚未读取 Wi-Fi 状态。")
    }

    function statusTextWithIpv4() {
        var status = root.statusText()
        var address = root.currentWifiIpv4()
        if (address.length > 0 && status.indexOf(address) < 0)
            return status + " · IPv4 " + address
        return status
    }

    function backendBusyFeedback() {
        var operation = String(boardBackend.backendBusyOperation || "").trim()
        return operation.length > 0
               ? "板端正在执行 " + operation + "，请等待完成"
               : "板端正在执行其他操作，请等待完成"
    }

    function connectionUnavailableMessage() {
        if (!root.wifiCapabilityEnabled)
            return "当前板型未启用 Wi-Fi 测试"
        if (root.backendBusy)
            return root.backendBusyFeedback()
        if (root.wifiBusy)
            return "正在执行 Wi-Fi 操作，请等待完成"
        if (root.selectedInterface.length === 0)
            return "请先读取状态并选择可用 Wi-Fi 网卡"
        if (root.selectedNetworkId.length === 0)
            return "请先扫描并选择一个 Wi-Fi 网络"
        if (!root.selectedNetworkExists)
            return "所选网络已不在当前扫描结果中，请重新扫描并选择网络"
        if (root.selectedNetworkActive)
            return "所选网络已是当前连接，无需重复连接"
        if (root.selectedNetworkHidden)
            return "隐藏网络不能从扫描结果直接连接，请选择有名称的网络"
        if (!root.scanCacheValid)
            return "扫描结果已过期，请重新扫描后再连接"
        if (!root.selectedNetworkMatchesInterface)
            return "所选网络不属于 " + root.selectedInterface + " 的当前扫描结果，请重新扫描"
        return ""
    }

    function statusAccent() {
        if (!root.wifiCapabilityEnabled)
            return theme.inkFaint
        var status = root.textValue(root.wifiState, "status", "idle")
        if (root.wifiBusy || status === "refreshing" || status === "scanning"
                || status === "connecting" || status === "disconnecting")
            return theme.focus
        if (status === "error")
            return theme.danger
        if (root.wifiAvailable && root.radioEnabled)
            return theme.accent
        return theme.warning
    }

    function signalText(network) {
        var strength = root.numberValue(network, "signal", -1)
        return strength >= 0 ? String(strength) + "%" : "--"
    }

    function securityText(network) {
        if (!network || Object.keys(network).length === 0)
            return "请选择网络"
        return root.boolValue(network, "secured", false)
               ? root.textValue(network, "security", "已加密")
               : "开放网络"
    }

    function requestRefresh() {
        if (!root.wifiCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用 Wi-Fi 测试"
            return
        }
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.wifiBusy) {
            root.actionFeedback = "正在执行 Wi-Fi 操作，请等待完成"
            return
        }
        if (boardBackend.refreshWifiAsync())
            root.actionFeedback = "正在读取 Wi-Fi 状态…"
        else
            root.actionFeedback = root.statusText()
    }

    function requestScan() {
        if (!root.wifiCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用 Wi-Fi 测试"
            return
        }
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.wifiBusy) {
            root.actionFeedback = "正在执行 Wi-Fi 操作，请等待完成"
            return
        }
        if (root.selectedInterface.length === 0) {
            root.actionFeedback = "请先读取状态并选择可用 Wi-Fi 网卡"
            return
        }
        root.selectedNetworkId = ""
        root.pendingWifiPassword = ""
        if (boardBackend.scanWifiAsync(root.selectedInterface))
            root.actionFeedback = "正在扫描 " + root.selectedInterface + " 的 Wi-Fi 网络…"
        else
            root.actionFeedback = root.statusText()
    }

    function selectNetwork(network) {
        root.selectedNetworkId = root.textValue(network, "id", "")
        root.pendingWifiPassword = ""
        root.actionFeedback = root.boolValue(network, "hidden", false)
                              ? "已选择隐藏网络；请选择有名称的网络后再连接"
                              : "已选择 " + root.networkName(network)
    }

    function requestConnectConfirmation() {
        var validationMessage = root.connectionUnavailableMessage()
        if (validationMessage.length > 0) {
            root.actionFeedback = validationMessage
            return
        }
        root.pendingAction = "connect"
        root.pendingNetwork = root.selectedNetwork
        confirmationPopup.open()
    }

    function requestDisconnectConfirmation() {
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.wifiBusy) {
            root.actionFeedback = "正在执行 Wi-Fi 操作，请等待完成"
            return
        }
        if (root.selectedInterface.length === 0) {
            root.actionFeedback = "请先读取状态并选择可用 Wi-Fi 网卡"
            return
        }
        root.pendingAction = "disconnect"
        root.pendingNetwork = ({})
        confirmationPopup.open()
    }

    function acceptPendingAction() {
        var action = root.pendingAction
        var network = root.pendingNetwork
        root.pendingAction = ""
        root.pendingNetwork = ({})
        confirmationPopup.close()

        if (action === "connect") {
            var validationMessage = root.connectionUnavailableMessage()
            if (validationMessage.length > 0) {
                root.pendingWifiPassword = ""
                root.actionFeedback = validationMessage
                return
            }
            network = root.selectedNetwork
            var password = root.pendingWifiPassword
            var accepted = boardBackend.connectWifiAsync(root.selectedInterface,
                                                          root.textValue(network, "id", ""),
                                                          password)
            // The password must not survive in QML state or be placed into a
            // status/log string. The backend also clears it once nmcli returns.
            root.pendingWifiPassword = ""
            root.actionFeedback = accepted
                                  ? "正在连接 " + root.networkName(network) + "…"
                                  : root.statusText()
            return
        }
        if (action === "disconnect") {
            var disconnected = boardBackend.disconnectWifiAsync(root.selectedInterface)
            root.actionFeedback = disconnected
                                  ? "正在断开 " + root.selectedInterface + "…"
                                  : root.statusText()
        }
    }

    function resetTransientInput() {
        root.pendingWifiPassword = ""
        root.pendingAction = ""
        root.pendingNetwork = ({})
        root.wifiPingPending = false
        root.pendingPingInterface = ""
        root.pendingPingTarget = ""
        if (confirmationPopup.visible)
            confirmationPopup.close()
    }

    function activityLog() {
        var lines = []
        lines.push("状态       " + root.statusText())
        lines.push("网卡       " + (root.selectedInterface.length > 0 ? root.selectedInterface : "未选择"))
        lines.push("连接       " + root.selectedConnectionName())
        lines.push("IPv4       " + root.wifiIpv4Display())
        lines.push("无线开关   " + (root.radioEnabled ? "已开启" : "未读取或关闭"))
        lines.push("扫描缓存   " + (root.boolValue(root.wifiState, "scanCacheValid", false) ? "可用于连接" : "尚未扫描或已过期"))
        lines.push("扫描结果   " + String(root.accessPoints.length) + " 个网络")
        lines.push("Ping 目标  " + root.wifiPingTargetLabel())
        lines.push("Ping 结果  " + root.wifiPingResultText)
        if (root.selectedNetworkId.length > 0) {
            lines.push("")
            lines.push("已选择网络")
            lines.push("名称       " + root.networkName(root.selectedNetwork))
            lines.push("BSSID      " + root.textValue(root.selectedNetwork, "bssid", "--"))
            lines.push("安全性     " + root.securityText(root.selectedNetwork))
            lines.push("信号       " + root.signalText(root.selectedNetwork))
        }
        var warning = root.textValue(root.wifiState, "networkSwitchWarning", "")
        if (warning.length > 0) {
            lines.push("")
            lines.push("连接提示")
            lines.push(warning)
        }
        lines.push("")
        lines.push("密码不会显示、记录或写入此操作日志。")
        return lines.join("\n")
    }

    onVisibleChanged: {
        if (visible) {
            root.reconcileSelection()
            root.actionFeedback = root.backendBusy ? root.backendBusyFeedback() : root.statusText()
        } else {
            root.resetTransientInput()
        }
    }

    Connections {
        target: boardBackend
        // Ubuntu 20.04 ships Qt 5.12.  Connections only recognizes the
        // classic onSignal property syntax there; the function onSignal()
        // form is treated as an ordinary method and never receives signals.
        onChanged: {
            root.reconcileSelection()
            if (!root.visible)
                return
            if (root.backendBusy)
                root.actionFeedback = root.backendBusyFeedback()
            else if (root.wifiBusy)
                root.actionFeedback = root.statusText()
        }

        onOperationStateChanged: {
            if (!root.visible)
                return
            root.actionFeedback = root.backendBusy ? root.backendBusyFeedback() : root.statusText()
        }

        onOperationFinished: {
            if (operation === "ping-interface") {
                if (!root.wifiPingPending)
                    return
                var iface = root.pendingPingInterface
                var target = root.pendingPingTarget
                root.wifiPingPending = false
                root.pendingPingInterface = ""
                root.pendingPingTarget = ""
                var detail = status && String(status).length > 0
                             ? String(status)
                             : (result && String(result).length > 0 ? String(result) : "")
                if (success) {
                    root.wifiPingResultText = "Ping 成功：" + iface + " 可访问 " + target
                    root.actionFeedback = root.wifiPingResultText
                            + (detail.length > 0 ? "\n" + detail : "")
                } else {
                    root.wifiPingResultText = "Ping 未通过：" + iface + " 无法访问 " + target
                    root.actionFeedback = root.wifiPingResultText
                            + (detail.length > 0 ? "\n" + detail : "")
                }
                return
            }
            if (operation.indexOf("wifi-") !== 0)
                return
            root.reconcileSelection()
            root.actionFeedback = success
                                  ? root.statusTextWithIpv4()
                                  : (status && String(status).length > 0 ? String(status) : root.statusText())
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Rectangle {
        id: headerBand
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(88 * root.uiScale)
        color: theme.instrument

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.instrumentRaised
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(24 * root.uiScale)
            anchors.right: headerBackButton.left
            anchors.rightMargin: Math.round(18 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * root.uiScale)

            Text {
                width: parent.width
                text: "Wi-Fi 测试"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactLayout ? 22 : 26) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: root.actionFeedback
                color: theme.instrumentMuted
                elide: Text.ElideRight
                font.pixelSize: Math.round(12 * root.uiScale)
            }
        }

        BoardToolsActionButton {
            id: headerBackButton
            anchors.right: parent.right
            anchors.rightMargin: Math.round(24 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            uiScale: root.uiScale
            width: Math.round((root.compactLayout ? 84 : 108) * root.uiScale)
            height: Math.round(40 * root.uiScale)
            text: root.compactLayout ? "返回" : "返回桌面"
            variant: "quiet"
            onClicked: root.closed()
        }
    }

    Flickable {
        id: pageScroller
        objectName: "wifi-test-single-page"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerBand.bottom
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(height, pageContent.y + pageContent.height + pageMargin)

        readonly property int pageMargin: Math.round((root.compactLayout ? 16 : 24) * root.uiScale)

        Item {
            id: pageContent
            x: Math.max(pageScroller.pageMargin, Math.round((pageScroller.width - width) / 2))
            y: pageScroller.pageMargin
            width: Math.min(Math.max(0, pageScroller.width - pageScroller.pageMargin * 2), Math.round(1120 * root.uiScale))
            height: pageColumn.implicitHeight

            Column {
                id: pageColumn
                width: parent.width
                spacing: Math.round(16 * root.uiScale)

                Rectangle {
                    id: settingsSection
                    width: parent.width
                    height: settingsContent.implicitHeight + Math.round(32 * root.uiScale)
                    radius: Math.min(8, Math.round(8 * root.uiScale))
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    border.width: 1

                    Column {
                        id: settingsContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Math.round(16 * root.uiScale)
                        spacing: Math.round(12 * root.uiScale)

                        Text {
                            width: parent.width
                            text: "设置"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(17 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Grid {
                            id: settingsGrid
                            width: parent.width
                            columns: root.compactLayout ? 1 : 3
                            rowSpacing: Math.round(12 * root.uiScale)
                            columnSpacing: Math.round(12 * root.uiScale)
                            property real cellWidth: (width - columnSpacing * (columns - 1)) / columns

                            Column {
                                width: settingsGrid.cellWidth
                                spacing: Math.round(6 * root.uiScale)

                                Text {
                                    width: parent.width
                                    text: "Wi-Fi 网卡"
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                BoardToolsComboBox {
                                    id: interfacePicker
                                    objectName: "wifi-interface-dropdown"
                                    uiScale: root.uiScale
                                    width: parent.width
                                    model: root.interfaceOptions
                                    textRole: "label"
                                    currentIndex: root.optionIndexForInterface(root.selectedInterface)
                                    enabled: root.wifiCapabilityEnabled && !root.wifiBusy && !root.backendBusy && root.wifiDevices.length > 0
                                    onActivated: root.selectInterface(root.interfaceOptions[currentIndex].id)
                                }
                            }

                            Column {
                                width: settingsGrid.cellWidth
                                spacing: Math.round(6 * root.uiScale)

                                Text {
                                    width: parent.width
                                    text: "服务状态"
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: parent.width
                                    text: root.wifiBusy ? "正在读取状态" : "读取 Wi-Fi 状态"
                                    accent: theme.accent
                                    variant: "primary"
                                    enabled: root.wifiCapabilityEnabled && !root.wifiBusy && !root.backendBusy
                                    onClicked: root.requestRefresh()
                                }
                            }

                            Column {
                                width: settingsGrid.cellWidth
                                spacing: Math.round(6 * root.uiScale)

                                Text {
                                    width: parent.width
                                    text: "可用网络"
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: parent.width
                                    text: root.wifiBusy ? "正在扫描网络" : "扫描 Wi-Fi 网络"
                                    accent: theme.accent
                                    variant: "secondary"
                                    enabled: root.wifiCapabilityEnabled && !root.wifiBusy && !root.backendBusy && root.selectedInterface.length > 0
                                    onClicked: root.requestScan()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: readingsSection
                    width: parent.width
                    height: readingsContent.implicitHeight + Math.round(32 * root.uiScale)
                    radius: Math.min(8, Math.round(8 * root.uiScale))
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    border.width: 1

                    Column {
                        id: readingsContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Math.round(16 * root.uiScale)
                        spacing: Math.round(12 * root.uiScale)

                        Text {
                            width: parent.width
                            text: "实时读数"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(17 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        BoardToolsStatusHero {
                            objectName: "wifi-ipv4-status"
                            uiScale: root.uiScale
                            width: parent.width
                            label: root.currentWifiIpv4().length > 0 ? "当前 Wi-Fi IPv4" : "Wi-Fi 服务"
                            value: !root.wifiCapabilityEnabled
                                   ? "当前板型未启用"
                                   : (root.currentWifiIpv4().length > 0
                                      ? root.currentWifiIpv4()
                                      : (root.backendBusy ? "板端操作中" : (root.wifiBusy ? "操作进行中" : (root.wifiAvailable ? root.wifiIpv4Display() : "等待读取"))))
                            accent: root.currentWifiIpv4().length > 0 ? root.wifiIpv4Accent() : root.statusAccent()
                        }

                        Grid {
                            id: readingGrid
                            width: parent.width
                            columns: root.compactLayout ? 1 : 3
                            rowSpacing: Math.round(10 * root.uiScale)
                            columnSpacing: Math.round(10 * root.uiScale)
                            property real cellWidth: (width - columnSpacing * (columns - 1)) / columns

                            BoardToolsInfoBlock {
                                uiScale: root.uiScale
                                width: readingGrid.cellWidth
                                label: "当前连接"
                                value: root.selectedConnectionName()
                            }

                            BoardToolsInfoBlock {
                                uiScale: root.uiScale
                                width: readingGrid.cellWidth
                                label: "IPv4 状态"
                                value: root.wifiIpv4Display()
                            }

                            BoardToolsInfoBlock {
                                uiScale: root.uiScale
                                width: readingGrid.cellWidth
                                label: "NetworkManager"
                                value: {
                                    if (!root.boolValue(root.wifiState, "nmcliAvailable", false))
                                        return "未找到 nmcli"
                                    var mode = root.textValue(root.wifiState, "authorizationMode", "")
                                    if (mode === "root-nmcli")
                                        return "nmcli（root 直连）"
                                    if (mode === "restricted-sudo-helper")
                                        return "nmcli（sudo 助手）"
                                    return "nmcli 已就绪"
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Math.round(8 * root.uiScale)

                            Text {
                                text: "扫描网络"
                                color: theme.ink
                                font.pixelSize: Math.round(14 * root.uiScale)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                text: root.wifiBusy ? "正在更新" : (root.accessPoints.length > 0 ? String(root.accessPoints.length) + " 个" : "尚未扫描")
                                color: root.wifiBusy ? theme.focus : theme.muted
                                font.pixelSize: Math.round(12 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        Text {
                            width: parent.width
                            visible: root.accessPoints.length === 0 && !root.wifiBusy
                            text: root.hasReadStatus ? "选择网卡后扫描，结果只在本次连接前有效。" : "先读取 Wi-Fi 状态，再选择网卡并扫描。"
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: Math.round(12 * root.uiScale)
                            lineHeight: 1.35
                        }

                        Repeater {
                            model: root.accessPoints

                            Rectangle {
                                id: accessPointCard
                                width: parent.width
                                height: Math.round(68 * root.uiScale)
                                readonly property bool selectable: !root.wifiBusy && !root.backendBusy
                                activeFocusOnTab: selectable
                                Accessible.role: Accessible.Button
                                Accessible.name: root.networkName(modelData)
                                Accessible.description: root.securityText(modelData) + "，信号 " + root.signalText(modelData)
                                radius: Math.min(6, Math.round(6 * root.uiScale))
                                color: root.isSelectedNetwork(modelData) ? theme.accentSoft : theme.surfaceSoft
                                border.color: root.isSelectedNetwork(modelData) ? theme.accent
                                                                            : (activeFocus ? theme.focus : theme.rule)
                                border.width: root.isSelectedNetwork(modelData) ? 2 : 1

                                Keys.onPressed: {
                                    if (accessPointCard.selectable
                                            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
                                        root.selectNetwork(modelData)
                                        event.accepted = true
                                    }
                                }

                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Math.round(14 * root.uiScale)
                                    anchors.right: accessPointSignal.left
                                    anchors.rightMargin: Math.round(12 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Math.round(3 * root.uiScale)

                                    Text {
                                        width: parent.width
                                        text: root.networkName(modelData)
                                        color: theme.inkStrong
                                        elide: Text.ElideRight
                                        font.pixelSize: Math.round(14 * root.uiScale)
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        width: parent.width
                                        text: root.securityText(modelData) + " · CH "
                                              + root.textValue(modelData, "channel", "--")
                                              + (root.boolValue(modelData, "active", false) ? " · 当前连接" : "")
                                        color: theme.muted
                                        elide: Text.ElideRight
                                        font.pixelSize: Math.round(11 * root.uiScale)
                                    }
                                }

                                Text {
                                    id: accessPointSignal
                                    anchors.right: parent.right
                                    anchors.rightMargin: Math.round(14 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.signalText(modelData)
                                    color: theme.accentHover
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: accessPointCard.selectable
                                    hoverEnabled: enabled
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: root.selectNetwork(modelData)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: statusSection
                    width: parent.width
                    height: statusContent.implicitHeight + Math.round(32 * root.uiScale)
                    radius: Math.min(8, Math.round(8 * root.uiScale))
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    border.width: 1

                    Column {
                        id: statusContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Math.round(16 * root.uiScale)
                        spacing: Math.round(12 * root.uiScale)

                        Text {
                            width: parent.width
                            text: "状态"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(17 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            width: parent.width
                            height: feedbackText.implicitHeight + Math.round(20 * root.uiScale)
                            radius: Math.min(6, Math.round(6 * root.uiScale))
                            color: theme.surfaceSoft
                            border.color: theme.rule
                            border.width: 1

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: Math.max(3, Math.round(4 * root.uiScale))
                                color: root.statusAccent()
                            }

                            Text {
                                id: feedbackText
                                anchors.left: parent.left
                                anchors.leftMargin: Math.round(14 * root.uiScale)
                                anchors.right: parent.right
                                anchors.rightMargin: Math.round(14 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.actionFeedback
                                color: theme.muted
                                wrapMode: Text.WordWrap
                                font.pixelSize: Math.round(12 * root.uiScale)
                                lineHeight: 1.35
                            }
                        }

                        Grid {
                            id: selectedNetworkGrid
                            width: parent.width
                            columns: root.compactLayout ? 1 : 2
                            rowSpacing: Math.round(10 * root.uiScale)
                            columnSpacing: Math.round(10 * root.uiScale)
                            property real cellWidth: (width - columnSpacing * (columns - 1)) / columns

                            BoardToolsInfoBlock {
                                uiScale: root.uiScale
                                width: selectedNetworkGrid.cellWidth
                                label: "已选择网络"
                                value: root.networkName(root.selectedNetwork)
                            }

                            BoardToolsInfoBlock {
                                uiScale: root.uiScale
                                width: selectedNetworkGrid.cellWidth
                                label: "安全性"
                                value: root.securityText(root.selectedNetwork)
                            }
                        }

                        Text {
                            width: parent.width
                            visible: root.selectedNetworkSecured
                            text: "Wi-Fi 密码"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(12 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        BoardToolsTextField {
                            id: wifiPasswordField
                            objectName: "wifi-password-field"
                            uiScale: root.uiScale
                            width: parent.width
                            visible: root.selectedNetworkSecured
                            enabled: root.selectedNetworkCanConnect
                            placeholderText: "连接时临时使用，不会写入日志"
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                            text: root.pendingWifiPassword
                            onTextChanged: {
                                if (root.pendingWifiPassword !== text)
                                    root.pendingWifiPassword = text
                            }
                        }

                        Grid {
                            id: actionGrid
                            width: parent.width
                            columns: root.compactLayout ? 1 : 2
                            rowSpacing: Math.round(10 * root.uiScale)
                            columnSpacing: Math.round(10 * root.uiScale)
                            property real cellWidth: (width - columnSpacing * (columns - 1)) / columns

                            Item {
                                width: actionGrid.cellWidth
                                height: Math.round(50 * root.uiScale)

                                BoardToolsActionButton {
                                    id: connectButton
                                    anchors.fill: parent
                                    uiScale: root.uiScale
                                    text: "连接所选网络"
                                    accent: theme.accent
                                    variant: "primary"
                                    enabled: root.selectedNetworkCanConnect
                                    onClicked: root.requestConnectConfirmation()
                                }

                                // A disabled Qt Quick Button does not receive clicks. Keep its
                                // disabled visual state while still explaining the unmet guard.
                                MouseArea {
                                    objectName: "wifi-connect-disabled-feedback"
                                    anchors.fill: parent
                                    z: 1
                                    enabled: !connectButton.enabled
                                    hoverEnabled: enabled
                                    cursorShape: enabled ? Qt.ForbiddenCursor : Qt.ArrowCursor
                                    onClicked: root.requestConnectConfirmation()

                                    ToolTip.visible: enabled && containsMouse
                                    ToolTip.delay: 300
                                    ToolTip.text: root.connectionUnavailableMessage()
                                }
                            }

                            BoardToolsActionButton {
                                uiScale: root.uiScale
                                width: actionGrid.cellWidth
                                text: "断开当前 Wi-Fi"
                                accent: theme.danger
                                variant: "stop"
                                enabled: root.wifiCapabilityEnabled && !root.wifiBusy && !root.backendBusy && root.selectedInterface.length > 0
                                onClicked: root.requestDisconnectConfirmation()
                            }
                        }

                        Text {
                            width: parent.width
                            text: "连通性测试"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(15 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            width: parent.width
                            text: "连接成功并获得 IPv4 后，可通过当前 Wi-Fi 网卡 Ping 外网，确认不仅关联热点，还能真正上网。默认目标 www.baidu.com。"
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: Math.round(12 * root.uiScale)
                            lineHeight: 1.35
                        }

                        Row {
                            width: parent.width
                            spacing: Math.round(10 * root.uiScale)

                            BoardToolsTextField {
                                id: wifiPingTargetField
                                objectName: "wifi-ping-target-field"
                                uiScale: root.uiScale
                                width: parent.width - wifiPingButtonHost.width - parent.spacing
                                height: Math.round(50 * root.uiScale)
                                placeholderText: "Ping 目标，如 www.baidu.com"
                                text: root.wifiPingTarget
                                enabled: root.wifiCapabilityEnabled && !root.wifiPingPending
                                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                                onTextChanged: {
                                    if (root.wifiPingTarget !== text)
                                        root.wifiPingTarget = text
                                }
                            }

                            Item {
                                id: wifiPingButtonHost
                                width: Math.round(150 * root.uiScale)
                                height: Math.round(50 * root.uiScale)

                                BoardToolsActionButton {
                                    id: wifiPingButton
                                    objectName: "wifi-ping-baidu-button"
                                    anchors.fill: parent
                                    uiScale: root.uiScale
                                    text: root.wifiPingPending
                                          ? "Ping 中…"
                                          : (root.wifiPingTargetLabel() === "www.baidu.com"
                                             ? "Ping 百度"
                                             : "Ping")
                                    accent: theme.focus
                                    variant: "primary"
                                    enabled: root.wifiPingReady() && !root.wifiPingPending
                                    onClicked: root.requestWifiPing()
                                }

                                // Disabled Qt Quick buttons ignore clicks; explain the guard.
                                MouseArea {
                                    objectName: "wifi-ping-disabled-feedback"
                                    anchors.fill: parent
                                    z: 1
                                    enabled: !wifiPingButton.enabled
                                    cursorShape: enabled ? Qt.ForbiddenCursor : Qt.ArrowCursor
                                    onClicked: root.requestWifiPing()
                                }
                            }
                        }

                        BoardToolsInfoBlock {
                            uiScale: root.uiScale
                            width: parent.width
                            label: "Ping 结果"
                            value: root.wifiPingResultText
                        }

                        BoardToolsInfoBlock {
                            uiScale: root.uiScale
                            width: parent.width
                            label: "Ping 来源"
                            value: root.selectedInterface.length > 0
                                   ? (root.selectedInterface + " · " + root.wifiIpv4Display())
                                   : "未选择网卡"
                        }

                        BoardToolsWarningBlock {
                            uiScale: root.uiScale
                            width: parent.width
                            text: "连接或断开 Wi-Fi 可能会中断远程桌面与 Web 访问；确认后请按新的网络地址重新连接。Ping 使用当前 Wi-Fi 网卡作为出接口。"
                        }
                    }
                }
            }
        }

        ScrollIndicator.vertical: ScrollIndicator {
            active: pageScroller.moving || pageScroller.dragging
            width: Math.max(2, Math.min(3, Math.round(2 * root.uiScale)))
        }
    }

    Popup {
        id: confirmationPopup
        objectName: "wifi-network-change-confirmation"
        parent: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: Math.round(20 * root.uiScale)
        width: Math.max(Math.round(300 * root.uiScale),
                        Math.min(Math.round(520 * root.uiScale),
                                 parent ? parent.width - Math.round(40 * root.uiScale) : Math.round(520 * root.uiScale)))
        height: confirmationContent.implicitHeight + padding * 2
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0

        background: Rectangle {
            radius: Math.min(8, Math.round(8 * root.uiScale))
            color: theme.surfaceRaised
            border.color: theme.ruleStrong
            border.width: 1
        }

        contentItem: Column {
            id: confirmationContent
            width: parent.width
            spacing: Math.round(12 * root.uiScale)

            Text {
                width: parent.width
                text: root.pendingAction === "disconnect" ? "确认断开 Wi-Fi" : "确认切换 Wi-Fi"
                color: theme.inkStrong
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(20 * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: root.pendingAction === "disconnect"
                      ? "将断开 " + root.selectedInterface + "。"
                      : "将连接到 “" + root.networkName(root.pendingNetwork) + "”。"
                color: theme.muted
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(13 * root.uiScale)
                lineHeight: 1.35
            }

            Rectangle {
                width: parent.width
                height: warningText.implicitHeight + Math.round(24 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.warningSoft
                border.color: theme.warning
                border.width: 1

                Text {
                    id: warningText
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "此操作可能使当前远程桌面或 Web 会话断开。请确认已记录可重新访问开发板的网络地址。"
                    color: theme.warning
                    wrapMode: Text.WordWrap
                    font.pixelSize: Math.round(12 * root.uiScale)
                    lineHeight: 1.35
                }
            }

            Row {
                width: parent.width
                spacing: Math.round(10 * root.uiScale)

                BoardToolsActionButton {
                    uiScale: root.uiScale
                    width: Math.max(Math.round(110 * root.uiScale), (parent.width - parent.spacing) / 2)
                    text: "取消"
                    variant: "quiet"
                    onClicked: {
                        root.pendingAction = ""
                        root.pendingNetwork = ({})
                        confirmationPopup.close()
                    }
                }

                BoardToolsActionButton {
                    uiScale: root.uiScale
                    width: Math.max(Math.round(110 * root.uiScale), (parent.width - parent.spacing) / 2)
                    text: root.pendingAction === "disconnect" ? "确认断开" : "确认连接"
                    accent: root.pendingAction === "disconnect" ? theme.danger : theme.accent
                    variant: root.pendingAction === "disconnect" ? "danger" : "primary"
                    onClicked: root.acceptPendingAction()
                }
            }
        }

        onClosed: {
            if (root.pendingAction.length > 0) {
                root.pendingAction = ""
                root.pendingNetwork = ({})
            }
        }
    }
}
