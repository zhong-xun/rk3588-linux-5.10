import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "environment-test-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property bool autoRefreshEnabled: false
    property bool samplingRequestPending: false
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)
    readonly property bool environmentCapabilityEnabled: boardProfile.hasCapability("temperaturehumidity")
    property string refreshFeedback: root.environmentCapabilityEnabled
                                     ? "已显示已发现探头与上次采样结果"
                                     : "当前板型未启用温湿度检测"
    readonly property var environment: root.resolveEnvironmentStatus()
    readonly property var temperature: root.mapValue(root.environment, "temperature", {})
    readonly property var humidity: root.mapValue(root.environment, "humidity", {})
    readonly property var sensors: root.listValue(root.environment, "sensors")
    readonly property bool environmentAvailable: root.boolValue(root.environment, "available", false)
    readonly property bool samplingInFlight: root.samplingRequestPending || boardBackend.environmentRefreshInFlight

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

    function listValue(map, key) {
        var value = root.mapValue(map, key, [])
        return value && value.length !== undefined && typeof value !== "string" ? value : []
    }

    function firstSample(sensors, key) {
        for (var index = 0; index < sensors.length; ++index) {
            var sample = root.mapValue(sensors[index], key, {})
            if (sample && Object.keys(sample).length > 0)
                return sample
        }
        return {}
    }

    function sensorSource(sensor) {
        if (!sensor)
            return ""
        var runtime = root.mapValue(sensor, "runtime", {})
        var deviceTree = root.mapValue(sensor, "deviceTree", {})
        var treeName = root.textValue(deviceTree, "fullName", "")
        var ofName = root.textValue(runtime, "ofFullName", "")
        var deviceName = root.textValue(runtime, "deviceName", "")
        return treeName.length > 0 ? treeName : (ofName.length > 0 ? ofName : deviceName)
    }

    function samplingDescription(status) {
        var sampling = root.mapValue(status, "sampling", null)
        if (sampling && typeof sampling === "object") {
            var sequence = root.textValue(sampling, "sequence", "")
            var minimumDelay = Number(root.mapValue(sampling, "minimumDelayMs", 0))
            if (root.boolValue(sampling, "inFlight", false))
                return "正在执行安全采样"
            if (sequence.length > 0)
                return sequence
            if (!isNaN(minimumDelay) && minimumDelay > 0)
                return "异步只读采样 · 最小间隔 " + minimumDelay + " ms"
        }
        if (typeof sampling === "string" && sampling.length > 0)
            return sampling
        var minimumDelay = Number(root.mapValue(status, "minimumSampleDelayMs", 0))
        if (!isNaN(minimumDelay) && minimumDelay > 0)
            return "异步只读采样 · 最小间隔 " + minimumDelay + " ms"
        return "异步只读采样"
    }

    function normalizeEnvironmentStatus(current) {
        var sensors = root.listValue(current, "sensors")
        var temperature = root.mapValue(current, "temperature", {})
        var humidity = root.mapValue(current, "humidity", root.mapValue(current, "relativeHumidity", {}))
        if (!temperature || Object.keys(temperature).length === 0)
            temperature = root.firstSample(sensors, "temperature")
        if (!humidity || Object.keys(humidity).length === 0)
            humidity = root.firstSample(sensors, "relativeHumidity")

        var source = root.textValue(current, "source", "")
        if (source.length === 0 && sensors.length > 0)
            source = root.sensorSource(sensors[0])

        var sampledAt = root.textValue(current, "sampledAt", root.textValue(current, "updatedAt", ""))
        var status = root.textValue(current, "status", "")
        var error = root.textValue(current, "error", "")
        if (status.length === 0)
            status = sensors.length > 0 ? "已发现环境传感器" : "未发现可读取的温湿度探头"

        return {
            "available": root.boolValue(current, "available", sensors.length > 0),
            "status": status,
            "error": error,
            "source": source,
            "updatedAt": sampledAt,
            "sampling": root.samplingDescription(current),
            "sensors": sensors,
            "temperature": temperature,
            "humidity": humidity
        }
    }

    function resolveEnvironmentStatus() {
        // New builds expose environmentState. environmentStatus is retained as
        // a compatibility alias while deployment packages update independently.
        var current = boardBackend.environmentState
        if (current !== undefined && current !== null)
            return root.normalizeEnvironmentStatus(current)

        current = boardBackend.environmentStatus
        if (current !== undefined && current !== null)
            return root.normalizeEnvironmentStatus(current)

        return {
            "available": false,
            "status": "当前运行版本未发现环境传感器",
            "source": "",
            "updatedAt": "",
            "sampling": "按需读取",
            "sensors": [],
            "temperature": {},
            "humidity": {}
        }
    }

    function sampleText(sample, fallback) {
        if (!sample || !root.boolValue(sample, "valid", true))
            return fallback
        var value = root.textValue(sample, "value", "")
        if (value.length === 0)
            return fallback
        var unit = root.textValue(sample, "unit", "")
        return value + (unit.length > 0 ? " " + unit : "")
    }

    function sampleSource(sample, fallback) {
        var path = root.textValue(sample, "path", "")
        return path.length > 0 ? path : fallback
    }

    function sampleTimestamp(sample) {
        var timestamp = root.textValue(sample, "timestamp", "")
        return timestamp.length > 0 ? timestamp : root.textValue(root.environment, "updatedAt", "等待读取")
    }

    function sensorLabel(sensor, index) {
        var name = root.textValue(sensor, "name", "")
        var channel = root.textValue(sensor, "channel", root.textValue(sensor, "path", ""))
        if (name.length > 0 && channel.length > 0)
            return name + " · " + channel
        if (name.length > 0 || channel.length > 0)
            return name.length > 0 ? name : channel
        var id = root.textValue(sensor, "id", "")
        var source = root.sensorSource(sensor)
        if (id.length > 0 && source.length > 0)
            return id + " · " + source
        return id.length > 0 ? id : (source.length > 0 ? source : "环境探头 " + (index + 1))
    }

    function sensorReading(sensor) {
        var temperature = root.mapValue(sensor, "temperature", {})
        var humidity = root.mapValue(sensor, "relativeHumidity", root.mapValue(sensor, "humidity", {}))
        var readings = []
        var temperatureText = root.sampleText(temperature, "")
        var humidityText = root.sampleText(humidity, "")
        if (temperatureText.length > 0)
            readings.push("温度 " + temperatureText)
        if (humidityText.length > 0)
            readings.push("湿度 " + humidityText)
        if (readings.length > 0)
            return readings.join(" · ")
        var value = root.textValue(sensor, "value", root.textValue(sensor, "raw", "--"))
        var unit = root.textValue(sensor, "unit", "")
        return value + (unit.length > 0 ? " " + unit : "")
    }

    function stateText() {
        if (!root.environmentCapabilityEnabled)
            return "当前板型未启用温湿度检测"
        var status = root.textValue(root.environment, "status", "")
        var error = root.textValue(root.environment, "error", "")
        var label = {
            "ready": "已发现探头，尚未采样",
            "sampling": "正在采样",
            "sampled": "本轮采样完成",
            "partial": "本轮采样部分完成",
            "read-error": "本轮采样未读到有效值",
            "unavailable": "未发现温湿度探头"
        }[status]
        if (!label || label.length === 0)
            label = root.environmentAvailable ? "环境探头已就绪" : "未发现可读取的温湿度探头"
        return error.length > 0 ? label + " · " + error : label
    }

    function stateAccent() {
        if (!root.environmentCapabilityEnabled)
            return theme.inkFaint
        var status = root.textValue(root.environment, "status", "")
        if (root.samplingInFlight || status === "sampling")
            return theme.focus
        if (status === "read-error")
            return theme.danger
        if (status === "partial" || !root.environmentAvailable)
            return theme.warning
        return theme.accent
    }

    function requestRefresh(manual) {
        if (!root.environmentCapabilityEnabled) {
            root.autoRefreshEnabled = false
            root.refreshFeedback = "当前板型未启用温湿度检测"
            return
        }
        if (root.samplingInFlight) {
            if (manual)
                root.refreshFeedback = "采样正在进行，请等待本次结果"
            return
        }
        if (manual)
            root.refreshFeedback = "正在重新读取温湿度探头…"
        if (typeof boardBackend.refreshEnvironmentAsync === "function") {
            root.samplingRequestPending = true
            var scheduled = boardBackend.refreshEnvironmentAsync()
            if (scheduled === false) {
                root.samplingRequestPending = false
                root.refreshFeedback = "采样正在进行，请等待本次结果"
                return
            }
            return
        }
        boardBackend.refresh()
    }

    onEnvironmentCapabilityEnabledChanged: {
        if (!root.environmentCapabilityEnabled) {
            root.autoRefreshEnabled = false
            root.refreshFeedback = "当前板型未启用温湿度检测"
        }
    }

    Connections {
        target: boardBackend
        onChanged: {
            // refreshEnvironmentAsync() publishes a changed signal both when
            // the worker starts and when it completes.  The local flag only
            // bridges the synchronous QML call; keeping it after either
            // notification would leave the page falsely stuck at “sampling”.
            if (root.samplingRequestPending)
                root.samplingRequestPending = false
            if (root.visible) {
                root.refreshFeedback = !root.environmentCapabilityEnabled
                                     ? "当前板型未启用温湿度检测"
                                     : (root.samplingInFlight
                                        ? "正在执行安全采样…"
                                        : (root.environmentAvailable ? "环境状态已更新" : "已刷新，等待温湿度探头出现"))
            }
        }
    }

    Timer {
        id: autoRefreshTimer
        interval: 5000
        repeat: true
        running: root.visible && root.environmentCapabilityEnabled && root.autoRefreshEnabled && !root.samplingInFlight
        onTriggered: root.requestRefresh(false)
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
            anchors.leftMargin: Math.round(30 * root.uiScale)
            anchors.right: headerActions.left
            anchors.rightMargin: Math.round(22 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(3 * root.uiScale)

            Text {
                width: parent.width
                text: "温湿度检测"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactLayout ? 24 : 28) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: root.refreshFeedback
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

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: Math.round(root.compactLayout ? 92 : 108 * root.uiScale)
                height: Math.round(40 * root.uiScale)
                text: "返回桌面"
                variant: "quiet"
                onClicked: root.closed()
            }
        }
    }

    Flickable {
        id: environmentPage
        objectName: "environment-test-single-page"
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
            policy: environmentPage.contentHeight > environmentPage.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: contentColumn
            width: environmentPage.width
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
                            text: root.environmentCapabilityEnabled ? "按需读取，不修改传感器配置" : "当前板型未启用温湿度检测"
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
                        enabled: root.environmentCapabilityEnabled && !root.samplingInFlight
                        accent: theme.accent
                        onText: "每 5 秒"
                        offText: "手动"
                        onToggled: root.autoRefreshEnabled = root.environmentCapabilityEnabled && checked
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(112 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: root.samplingInFlight ? "正在采样…" : "立即读取"
                        accent: theme.accent
                        variant: "primary"
                        enabled: root.environmentCapabilityEnabled && !root.samplingInFlight
                        onClicked: root.requestRefresh(true)
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
                    text: "最近更新 " + (root.environmentCapabilityEnabled
                                          ? root.textValue(root.environment, "updatedAt", root.sampleTimestamp(root.temperature))
                                          : "--")
                    color: theme.muted
                    elide: Text.ElideLeft
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            Grid {
                id: readingsGrid
                width: parent.width
                columns: root.width < Math.round(760 * root.uiScale) ? 1 : 2
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Repeater {
                    model: [
                        { "id": "temperature", "label": "温度", "accent": theme.warning },
                        { "id": "humidity", "label": "相对湿度", "accent": theme.focus }
                    ]

                    delegate: Rectangle {
                        readonly property var sample: modelData.id === "temperature" ? root.temperature : root.humidity
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
                            color: modelData.accent
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
                            text: root.environmentCapabilityEnabled ? root.sampleText(parent.sample, "未读取") : "当前板型未启用"
                            color: theme.inkStrong
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(25 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.round(14 * root.uiScale)
                            text: root.environmentCapabilityEnabled
                                  ? root.sampleSource(parent.sample, "等待传感器通道")
                                  : "--"
                            color: theme.muted
                            wrapMode: Text.WrapAnywhere
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
                            color: root.stateAccent()
                        }

                        Text {
                            width: Math.max(0, parent.width - x)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.stateText()
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.width < Math.round(760 * root.uiScale) ? 1 : 3
                        rowSpacing: Math.round(8 * root.uiScale)
                        columnSpacing: Math.round(12 * root.uiScale)

                        Repeater {
                            model: [
                                { "label": "已发现探头", "value": root.environmentCapabilityEnabled ? root.sensors.length + " 个" : "--" },
                                { "label": "来源", "value": root.environmentCapabilityEnabled ? root.textValue(root.environment, "source", "等待后端发现") : "--" },
                                { "label": "采样方式", "value": root.environmentCapabilityEnabled ? root.textValue(root.environment, "sampling", "按需读取") : "--" }
                            ]

                            delegate: Item {
                                width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                                height: Math.round(24 * root.uiScale)

                                Text {
                                    id: statusLabel
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    anchors.left: statusLabel.right
                                    anchors.leftMargin: Math.round(8 * root.uiScale)
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.value
                                    color: theme.ink
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
