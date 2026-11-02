import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "resource-monitor-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property bool autoRefreshEnabled: true
    property string actionFeedback: "进入页面后读取板端资源状态"
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)
    readonly property var monitorApi: root.resolveMonitorApi()
    readonly property var monitorStatus: root.normalizedStatus(root.monitorApi ? root.monitorApi.snapshot : null)

    function resolveMonitorApi() {
        // Keep the QML usable on older BoardLab binaries.  The production
        // entry point provides resourceMonitor as a context property.
        return typeof resourceMonitor !== "undefined" && resourceMonitor ? resourceMonitor : null
    }

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
        var raw = root.mapValue(map, key, undefined)
        if (raw === undefined || raw === null || String(raw).length === 0)
            return fallback
        var value = Number(raw)
        return isNaN(value) ? fallback : value
    }

    function firstDefined(map, keys, fallback) {
        for (var index = 0; index < keys.length; ++index) {
            var value = root.mapValue(map, keys[index], undefined)
            if (value !== undefined && value !== null && String(value).length > 0)
                return value
        }
        return fallback
    }

    function normalizedStatus(current) {
        if (current !== undefined && current !== null)
            return current
        return {
            "available": false,
            "status": "unavailable",
            "message": "当前运行版本未提供资源监控服务。",
            "authorizationRequired": false,
            "metrics": {}
        }
    }

    function metricMap(name) {
        var direct = root.mapValue(root.monitorStatus, name, null)
        if (direct && typeof direct === "object")
            return direct

        var metrics = root.mapValue(root.monitorStatus, "metrics", {})
        var nested = root.mapValue(metrics, name, null)
        if (nested && typeof nested === "object")
            return nested

        var statusMap = root.mapValue(root.monitorStatus, name + "Status", null)
        if (statusMap && typeof statusMap === "object")
            return statusMap
        return {}
    }

    function metricPrefix(name) {
        if (name === "memory")
            return "memory"
        return name
    }

    function metricAuthorizationRequired(name) {
        var metric = root.metricMap(name)
        if (root.boolValue(metric, "authorizationRequired", false)
                || root.boolValue(metric, "permissionDenied", false))
            return true
        var prefix = root.metricPrefix(name)
        return root.boolValue(root.monitorStatus, prefix + "AuthorizationRequired", false)
                || root.boolValue(root.monitorStatus, prefix + "PermissionDenied", false)
    }

    function metricAvailable(name) {
        var metric = root.metricMap(name)
        var metricFlag = root.mapValue(metric, "available", undefined)
        if (metricFlag !== undefined && metricFlag !== null)
            return root.boolValue(metric, "available", false)
        var prefix = root.metricPrefix(name)
        var globalFlag = root.mapValue(root.monitorStatus, prefix + "Available", undefined)
        if (globalFlag !== undefined && globalFlag !== null)
            return root.boolValue(root.monitorStatus, prefix + "Available", false)
        return root.metricPercent(name) >= 0 || root.metricRawText(name).length > 0
    }

    function metricPercent(name) {
        var metric = root.metricMap(name)
        if (name === "cpu" && !root.boolValue(metric, "utilizationValid", false))
            return -1

        var local = root.firstDefined(metric,
                                      ["percent", "usage", "usagePercent", "utilization", "utilizationPercent", "load"],
                                      undefined)
        if (local !== undefined) {
            var parsedLocal = Number(local)
            if (!isNaN(parsedLocal))
                return parsedLocal
        }

        // GPU and NPU utilization is exposed by the monitor as a nested
        // driver metric.  Prefer it to frequency so the primary reading
        // answers the question people open this page for: current load.
        var utilization = root.mapValue(metric, "utilization", {})
        var nested = root.firstDefined(utilization,
                                       ["valuePercent", "utilizationPercent", "percent", "usage", "load",
                                        "averagePercent"], undefined)
        if (nested !== undefined) {
            var parsedNested = Number(nested)
            if (!isNaN(parsedNested))
                return parsedNested
        }

        var prefixes = name === "memory" ? ["memory", "ram"] : [name]
        for (var index = 0; index < prefixes.length; ++index) {
            var prefix = prefixes[index]
            var global = root.firstDefined(root.monitorStatus,
                                           [prefix + "Percent", prefix + "Usage", prefix + "UsagePercent",
                                            prefix + "Utilization", prefix + "UtilizationPercent", prefix + "Load"], undefined)
            if (global !== undefined) {
                var parsedGlobal = Number(global)
                if (!isNaN(parsedGlobal))
                    return parsedGlobal
            }
        }
        return -1
    }

    function metricRawText(name) {
        var metric = root.metricMap(name)
        var local = root.firstDefined(metric,
                                      ["valueText", "usageText", "utilizationText", "loadText", "display", "text"],
                                      "")
        if (String(local).length > 0)
            return String(local)

        var prefixes = name === "memory" ? ["memory", "ram"] : [name]
        for (var index = 0; index < prefixes.length; ++index) {
            var prefix = prefixes[index]
            var global = root.firstDefined(root.monitorStatus,
                                           [prefix + "Text", prefix + "UsageText", prefix + "UtilizationText", prefix + "LoadText"],
                                           "")
            if (String(global).length > 0)
                return String(global)
        }
        return ""
    }

    function metricStateText(name) {
        if (root.metricAuthorizationRequired(name))
            return "需要授权"
        if (!root.metricAvailable(name))
            return "未发现"
        var metric = root.metricMap(name)
        var state = root.textValue(metric, "status", "")
        var labels = {
            "initializing": "正在建立采样基线",
            "ready": "可读取",
            "available": "可读取",
            "partial": "部分数据可读取",
            "unavailable": "未发现",
            "authorizationRequired": "需要授权",
            "authorization-required": "需要授权",
            "failed": "读取失败"
        }
        if (state.length > 0)
            return labels[state] || state
        return "已采样"
    }

    function metricFrequencyMap(name) {
        return root.mapValue(root.metricMap(name), "currentFrequency", {})
    }

    function metricFrequencyText(name) {
        var frequency = root.metricFrequencyMap(name)
        if (!root.boolValue(frequency, "available", false)
                || !root.boolValue(frequency, "valid", false))
            return ""
        var mhz = root.numberValue(frequency, "valueMHz", -1)
        if (mhz >= 0)
            return String(Math.round(mhz * 10) / 10) + " MHz"
        var khz = root.numberValue(frequency, "valueKHz", -1)
        return khz >= 0 ? String(Math.round(khz)) + " kHz" : ""
    }

    function metricRuntimeText(name) {
        var runtime = root.mapValue(root.metricMap(name), "runtimeStatus", {})
        if (!root.boolValue(runtime, "available", false)
                || !root.boolValue(runtime, "valid", false))
            return ""
        return root.textValue(runtime, "value", "")
    }

    function metricThermalText(name) {
        var thermal = root.mapValue(root.metricMap(name), "thermal", {})
        if (!root.boolValue(thermal, "available", false)
                || !root.boolValue(thermal, "valid", false))
            return ""
        var value = root.numberValue(thermal, "valueCelsius", -1)
        if (value < 0)
            return ""
        var zone = root.textValue(thermal, "thermalType", root.textValue(thermal, "zone", "温度"))
        return zone + " " + String(Math.round(value * 10) / 10) + " °C"
    }

    function metricValueText(name) {
        if (root.metricAuthorizationRequired(name))
            return "需要授权"
        if (!root.metricAvailable(name))
            return "不可用"
        if (name === "cpu" && !root.boolValue(root.metricMap(name), "utilizationValid", false))
            return "正在建立采样基线"
        var percent = root.metricPercent(name)
        if (percent >= 0)
            return String(Math.round(percent * 10) / 10) + "%"
        if (name === "gpu" || name === "npu") {
            var frequency = root.metricFrequencyText(name)
            if (frequency.length > 0)
                return frequency
            var runtime = root.metricRuntimeText(name)
            if (runtime.length > 0)
                return runtime
            return root.metricStateText(name)
        }
        var raw = root.metricRawText(name)
        if (raw.length > 0)
            return raw
        return "已采样，未提供利用率"
    }

    function metricDetailText(name) {
        if (root.metricAuthorizationRequired(name))
            return "该节点需要当前系统账户授权后才能读取"
        if (!root.metricAvailable(name)) {
            var unavailable = root.textValue(root.metricMap(name), "error",
                                             root.textValue(root.metricMap(name), "message", ""))
            return unavailable.length > 0 ? unavailable : "当前板型或驱动未暴露可读取节点"
        }
        var metric = root.metricMap(name)
        if (name === "cpu") {
            var cores = root.numberValue(metric, "coreCount", -1)
            var load = root.mapValue(metric, "load", {})
            var oneMinute = root.numberValue(load, "oneMinute", -1)
            var cpuDetails = []
            if (cores >= 0)
                cpuDetails.push(String(Math.round(cores)) + " 核")
            if (oneMinute >= 0)
                cpuDetails.push("1 分钟负载 " + String(Math.round(oneMinute * 100) / 100))
            if (cpuDetails.length > 0)
                return cpuDetails.join(" · ")
        }
        if (name === "memory") {
            var totalKiB = root.numberValue(metric, "totalKiB", -1)
            var usedKiB = root.numberValue(metric, "usedKiB", -1)
            if (totalKiB >= 0 && usedKiB >= 0)
                return "已用 " + root.formatKiB(usedKiB) + " / " + root.formatKiB(totalKiB)
        }
        if (name === "gpu" || name === "npu") {
            var details = []
            var hardwareName = root.textValue(metric, "name", "")
            var runtime = root.metricRuntimeText(name)
            var thermal = root.metricThermalText(name)
            if (hardwareName.length > 0)
                details.push(hardwareName)
            if (runtime.length > 0)
                details.push(runtime)
            if (thermal.length > 0)
                details.push(thermal)
            if (details.length > 0)
                return details.join(" · ")
        }
        var local = root.firstDefined(metric,
                                      ["detail", "message", "source", "device", "model", "node", "usedText"],
                                      "")
        if (String(local).length > 0)
            return String(local)
        if (name === "memory") {
            var memory = root.firstDefined(root.monitorStatus,
                                           ["memoryText", "ramText", "memoryDetail", "ramDetail"], "")
            if (String(memory).length > 0)
                return String(memory)
        }
        var prefix = root.metricPrefix(name)
        var global = root.firstDefined(root.monitorStatus,
                                       [prefix + "Detail", prefix + "Source", prefix + "Device", prefix + "Model"], "")
        return String(global).length > 0 ? String(global) : "读取板端实时状态"
    }

    function formatKiB(value) {
        if (value < 0)
            return "--"
        if (value >= 1024 * 1024)
            return String(Math.round(value / (1024 * 1024) * 10) / 10) + " GiB"
        if (value >= 1024)
            return String(Math.round(value / 1024 * 10) / 10) + " MiB"
        return String(Math.round(value)) + " KiB"
    }

    function metricAccent(name) {
        if (root.metricAuthorizationRequired(name))
            return theme.warning
        if (!root.metricAvailable(name))
            return theme.inkFaint
        if (name === "cpu")
            return theme.focus
        if (name === "memory")
            return theme.focus
        if (name === "gpu")
            return theme.warning
        return theme.accent
    }

    function monitorStateText() {
        var state = root.textValue(root.monitorStatus, "status", "idle")
        var message = root.textValue(root.monitorStatus, "message", "")
        if (message.length > 0)
            return message
        var labels = {
            "idle": "等待读取资源状态",
            "ready": "资源状态已更新",
            "partial": "部分资源数据可读取",
            "sampling": "正在读取板端资源状态",
            "running": "正在读取板端资源状态",
            "unavailable": "资源监控服务不可用",
            "authorizationRequired": "部分资源节点需要授权",
            "authorization-required": "部分资源节点需要授权",
            "failed": "资源状态读取失败"
        }
        return labels[state] || state
    }

    function monitorStateAccent() {
        var state = root.textValue(root.monitorStatus, "status", "idle")
        if (state === "failed" || state === "unavailable")
            return theme.danger
        if (state === "authorization-required")
            return theme.warning
        if (state === "sampling" || state === "running")
            return theme.focus
        return theme.accent
    }

    function capturedAtText() {
        return root.textValue(root.monitorStatus, "generatedAt",
                              root.textValue(root.monitorStatus, "capturedAt",
                                             root.textValue(root.monitorStatus, "updatedAt", "尚未采样")))
    }

    function capturedAtShortText() {
        var capturedAt = root.capturedAtText()
        if (capturedAt === "尚未采样")
            return capturedAt
        return capturedAt.replace("T", " ").replace(/\.\d+(Z)?$/, "").replace(/Z$/, "")
    }

    function requestRefresh() {
        // A hidden page must never keep polling the board.  This guard also
        // keeps programmatic callbacks from creating background sampling.
        if (!root.visible)
            return
        if (!root.monitorApi) {
            root.actionFeedback = "当前运行版本未提供资源监控服务"
            return
        }
        var accepted = root.monitorApi.refresh()
        if (accepted === false) {
            var detail = root.textValue(root.monitorStatus, "message", "")
            root.actionFeedback = detail.length > 0 ? detail : "资源状态读取未启动"
            return
        }
        root.actionFeedback = "正在读取 CPU、内存、GPU 与 NPU 状态…"
    }

    Timer {
        id: refreshTimer
        interval: 1000
        repeat: true
        running: root.visible && root.autoRefreshEnabled && root.monitorApi !== null
        onTriggered: root.requestRefresh()
    }

    onVisibleChanged: {
        if (visible)
            root.requestRefresh()
    }

    Connections {
        target: root.monitorApi
        onChanged: {
            if (root.visible)
                root.actionFeedback = root.monitorStateText()
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
        height: root.compactLayout ? Math.round(126 * root.uiScale) : Math.round(88 * root.uiScale)
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
            anchors.leftMargin: Math.round(30 * root.uiScale)
            anchors.right: headerActions.left
            anchors.rightMargin: Math.round(22 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * root.uiScale)

            Text {
                width: parent.width
                text: root.compactLayout ? "资源监控" : "实时资源监控"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactLayout ? 22 : 28) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: "CPU、内存、GPU 与 NPU 的当前状态"
                color: theme.instrumentMuted
                elide: Text.ElideRight
                font.pixelSize: Math.round(12 * root.uiScale)
            }
        }

        Row {
            id: headerActions
            anchors.right: parent.right
            anchors.rightMargin: Math.round(30 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(10 * root.uiScale)

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: Math.round(root.compactLayout ? 80 : 108 * root.uiScale)
                height: Math.round(40 * root.uiScale)
                text: "返回桌面"
                variant: "quiet"
                onClicked: root.closed()
            }
        }
    }

    Flickable {
        id: monitorPage
        objectName: "resource-monitor-single-page"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerBand.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Math.round(24 * root.uiScale)
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: monitorPage.contentHeight > monitorPage.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: contentColumn
            width: monitorPage.width
            spacing: Math.round(16 * root.uiScale)

            Rectangle {
                id: settingsBand
                width: parent.width
                height: root.compactLayout ? Math.round(138 * root.uiScale) : Math.round(72 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Flow {
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(12 * root.uiScale)

                    Column {
                        width: root.compactLayout ? parent.width : Math.max(Math.round(210 * root.uiScale), settingsBand.width - Math.round(510 * root.uiScale))
                        height: Math.round(48 * root.uiScale)
                        spacing: Math.round(2 * root.uiScale)

                        Text {
                            text: "设置"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(14 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            width: parent.width
                            text: "仅在本页面可见时采样"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    BoardToolsSwitchRow {
                        uiScale: root.uiScale
                        width: Math.round(250 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        label: "自动刷新"
                        checked: root.autoRefreshEnabled
                        accent: theme.focus
                        onText: "每秒"
                        offText: "手动"
                        enabled: root.monitorApi !== null
                        onToggled: root.autoRefreshEnabled = checked
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(112 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: "立即刷新"
                        accent: theme.accent
                        variant: "primary"
                        enabled: root.monitorApi !== null
                        onClicked: root.requestRefresh()
                    }
                }
            }

            Item {
                width: parent.width
                height: Math.round(24 * root.uiScale)

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "实时读数"
                    color: theme.inkStrong
                    font.pixelSize: Math.round(17 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "最近更新 " + root.capturedAtShortText()
                    color: theme.muted
                    elide: Text.ElideLeft
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            Grid {
                id: readingsGrid
                width: parent.width
                columns: root.width < Math.round(760 * root.uiScale) ? 1 : (root.width < Math.round(1050 * root.uiScale) ? 2 : 4)
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Repeater {
                    model: [
                        { "id": "cpu", "label": "CPU" },
                        { "id": "memory", "label": "内存" },
                        { "id": "gpu", "label": "GPU" },
                        { "id": "npu", "label": "NPU" }
                    ]

                    delegate: Rectangle {
                        readonly property real usage: root.metricPercent(modelData.id)
                        width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                        height: Math.round(148 * root.uiScale)
                        radius: Math.min(8, Math.round(8 * root.uiScale))
                        color: theme.surfaceRaised
                        border.color: theme.rule
                        border.width: 1

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Math.max(4, Math.round(5 * root.uiScale))
                            radius: width / 2
                            color: root.metricAccent(modelData.id)
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(14 * root.uiScale)
                            text: modelData.label
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(38 * root.uiScale)
                            text: root.metricValueText(modelData.id)
                            color: theme.inkStrong
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(25 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            id: usageTrack
                            visible: parent.usage >= 0 && root.metricAvailable(modelData.id)
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(78 * root.uiScale)
                            height: Math.max(4, Math.round(5 * root.uiScale))
                            radius: height / 2
                            color: theme.rule

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(100, parent.parent.usage)) / 100
                                height: parent.height
                                radius: height / 2
                                color: root.metricAccent(modelData.id)
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.round(14 * root.uiScale)
                            text: root.metricDetailText(modelData.id)
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                            lineHeight: 1.2
                        }
                    }
                }
            }

            Rectangle {
                id: statusBand
                width: parent.width
                height: statusContent.height + Math.round(24 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    id: statusContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(12 * root.uiScale)

                    Row {
                        width: parent.width
                        height: Math.round(22 * root.uiScale)
                        spacing: Math.round(8 * root.uiScale)

                        Text {
                            text: "状态"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(14 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            width: Math.max(6, Math.round(7 * root.uiScale))
                            height: width
                            radius: width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.monitorStateAccent()
                        }

                        Text {
                            width: Math.max(0, parent.width - x)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.monitorStateText()
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.width < Math.round(760 * root.uiScale) ? 2 : 4
                        rowSpacing: Math.round(8 * root.uiScale)
                        columnSpacing: Math.round(8 * root.uiScale)

                        Repeater {
                            model: [
                                { "id": "cpu", "label": "CPU" },
                                { "id": "memory", "label": "内存" },
                                { "id": "gpu", "label": "GPU" },
                                { "id": "npu", "label": "NPU" }
                            ]

                            delegate: Item {
                                width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                                height: Math.round(24 * root.uiScale)

                                Rectangle {
                                    width: Math.max(6, Math.round(7 * root.uiScale))
                                    height: width
                                    radius: width / 2
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: root.metricAccent(modelData.id)
                                }

                                Text {
                                    id: statusLabel
                                    anchors.left: parent.left
                                    anchors.leftMargin: Math.round(14 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: theme.muted
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    anchors.left: statusLabel.right
                                    anchors.leftMargin: Math.round(6 * root.uiScale)
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.metricStateText(modelData.id)
                                    color: root.metricAccent(modelData.id)
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.textValue(root.monitorStatus, "error", "").length > 0
                        text: root.textValue(root.monitorStatus, "error", "")
                        color: theme.danger
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }
            }
        }
    }
}
