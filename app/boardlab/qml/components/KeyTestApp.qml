import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "key-test-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property string selectedDeviceId: ""
    readonly property bool keyCapabilityEnabled: boardProfile.hasCapability("keys")
    property string actionFeedback: root.keyCapabilityEnabled ? "选择板载按键后开始监听" : "当前板型未启用按键检测"
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)
    readonly property bool narrowSettings: width < Math.round(960 * root.uiScale)
    readonly property bool monitorApiAvailable: boardBackend.keyTestStatus !== undefined && boardBackend.keyTestStatus !== null
    readonly property var keyStatus: root.resolveKeyTestStatus()
    readonly property var keyDevices: root.listValue(root.keyStatus, "devices")
    readonly property var keyOptions: root.makeDeviceOptions()
    readonly property var expectedKeys: root.listValue(root.keyStatus, "expectedKeys")
    readonly property var eventHistory: root.listValue(root.keyStatus, "events")
    readonly property var lastEvent: root.mapValue(root.keyStatus, "lastEvent", {})
    readonly property bool monitoring: root.boolValue(root.keyStatus, "monitoring", false)
    readonly property bool keyInputAvailable: root.boolValue(root.keyStatus, "available", false)

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

    function resolveKeyTestStatus() {
        var current = boardBackend.keyTestStatus
        if (current !== undefined && current !== null)
            return current

        var legacyInputs = boardBackend.inputDevices ? boardBackend.inputDevices : []
        var devices = []
        for (var index = 0; index < legacyInputs.length; ++index) {
            var input = legacyInputs[index]
            if (!input || !input.boardInput || input.kind !== "key")
                continue
            devices.push({
                "id": input.event || ("legacy-key-" + index),
                "event": input.event || "",
                "name": input.name || "板载按键",
                "supportedKeys": [],
                "eligible": false,
                "blockedReason": "当前运行版本尚未提供按键事件监听"
            })
        }
        return {
            "available": devices.length > 0,
            "status": devices.length > 0 ? "兼容模式：已发现按键设备，等待事件监听服务" : "未发现可监听的板载按键",
            "monitoring": false,
            "selectedDeviceId": "",
            "selectedEvent": "",
            "devices": devices,
            "expectedKeys": [],
            "events": [],
            "lastEvent": {},
            "pressCount": 0,
            "releaseCount": 0,
            "passedCount": 0
        }
    }

    function makeDeviceOptions() {
        if (!root.keyCapabilityEnabled) {
            return [{
                "id": "",
                "label": "当前板型未启用按键检测",
                "name": "当前板型未启用按键检测",
                "path": "",
                "eligible": false,
                "blockedReason": "当前板型未启用按键检测"
            }]
        }
        var options = []
        for (var index = 0; index < root.keyDevices.length; ++index) {
            var device = root.keyDevices[index]
            var name = root.textValue(device, "name", "板载按键 " + (index + 1))
            var event = root.textValue(device, "event", "未分配事件节点")
            options.push({
                "id": root.textValue(device, "id", ""),
                "label": name + " · " + event,
                "name": name,
                "path": event,
                "eligible": root.boolValue(device, "eligible", false),
                "blockedReason": root.textValue(device, "blockedReason", "")
            })
        }
        if (options.length === 0) {
            options.push({
                "id": "",
                "label": "未发现可监听的板载按键",
                "name": "未发现可监听的板载按键",
                "path": "",
                "eligible": false,
                "blockedReason": "等待设备树与输入设备发现"
            })
        }
        return options
    }

    function findDevice(id) {
        for (var index = 0; index < root.keyDevices.length; ++index) {
            if (root.textValue(root.keyDevices[index], "id", "") === id)
                return root.keyDevices[index]
        }
        return {}
    }

    function selectedDeviceIsEligible() {
        return root.boolValue(root.findDevice(root.selectedDeviceId), "eligible", false)
    }

    function optionIndexForId(id) {
        for (var index = 0; index < root.keyOptions.length; ++index) {
            if (root.keyOptions[index].id === id)
                return index
        }
        return 0
    }

    function selectOption(index) {
        if (root.keyCapabilityEnabled && index >= 0 && index < root.keyOptions.length)
            root.selectedDeviceId = root.keyOptions[index].id
    }

    function ensureSelectedDevice() {
        if (!root.keyCapabilityEnabled) {
            root.selectedDeviceId = ""
            return
        }
        if (root.selectedDeviceId.length > 0 && root.findDevice(root.selectedDeviceId).id !== undefined)
            return
        for (var index = 0; index < root.keyDevices.length; ++index) {
            if (root.boolValue(root.keyDevices[index], "eligible", false)) {
                root.selectedDeviceId = root.textValue(root.keyDevices[index], "id", "")
                return
            }
        }
        if (root.keyDevices.length > 0)
            root.selectedDeviceId = root.textValue(root.keyDevices[0], "id", "")
    }

    function expectedKeysText() {
        if (!root.keyCapabilityEnabled)
            return "当前板型未启用按键检测"
        if (root.expectedKeys.length === 0)
            return "未由驱动声明预期按键"
        var values = []
        for (var index = 0; index < root.expectedKeys.length; ++index) {
            var key = root.expectedKeys[index]
            if (typeof key === "object")
                values.push(root.textValue(key, "label", root.textValue(key, "name", root.textValue(key, "code", "按键"))))
            else
                values.push(String(key))
        }
        return values.join(" · ")
    }

    function eventText(entry) {
        if (!entry || Object.keys(entry).length === 0)
            return root.monitoring ? "正在等待按键按下" : "尚未收到按键事件"
        var key = root.textValue(entry, "label", root.textValue(entry, "name", root.textValue(entry, "key", root.textValue(entry, "code", "按键事件"))))
        var state = root.textValue(entry, "state", root.numberValue(entry, "value", 1) === 0 ? "松开" : "按下")
        var timestamp = root.textValue(entry, "timestamp", root.textValue(entry, "time", ""))
        return key + " · " + state + (timestamp.length > 0 ? " · " + timestamp : "")
    }

    function eventDetail(entry) {
        var device = root.textValue(entry, "device", root.textValue(entry, "event", ""))
        var result = root.boolValue(entry, "passed", false) ? "已匹配预期按键" : root.textValue(entry, "result", "已记录")
        return (device.length > 0 ? device + " · " : "") + result
    }

    function statusText() {
        if (!root.keyCapabilityEnabled)
            return "当前板型未启用按键检测"
        if (root.monitoring)
            return "正在监听"
        if (!root.keyInputAvailable)
            return "监听不可用"
        return root.textValue(root.keyStatus, "status", "等待开始监听")
    }

    function statusAccent() {
        if (!root.keyCapabilityEnabled)
            return theme.inkFaint
        if (root.monitoring)
            return theme.accent
        return root.keyInputAvailable ? theme.focus : theme.warning
    }

    function startMonitoring() {
        if (!root.keyCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用按键检测"
            return
        }
        if (!root.monitorApiAvailable) {
            root.actionFeedback = "当前运行版本尚未提供按键事件监听服务"
            return
        }
        if (!root.selectedDeviceIsEligible()) {
            root.actionFeedback = root.textValue(root.findDevice(root.selectedDeviceId), "blockedReason", "请选择可监听的板载按键")
            return
        }
        root.actionFeedback = "正在启动按键监听…"
        boardBackend.clearKeyTestEvents()
        boardBackend.startKeyMonitoring(root.selectedDeviceId)
    }

    function stopMonitoring(message) {
        if (root.monitorApiAvailable)
            boardBackend.stopKeyMonitoring()
        if (message && message.length > 0)
            root.actionFeedback = message
    }

    function clearEvents() {
        if (!root.keyCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用按键检测"
            return
        }
        if (!root.monitorApiAvailable) {
            root.actionFeedback = "当前运行版本尚未提供按键事件记录"
            return
        }
        boardBackend.clearKeyTestEvents()
        root.actionFeedback = "已清空本次按键记录"
    }

    onKeyCapabilityEnabledChanged: {
        if (!root.keyCapabilityEnabled) {
            root.selectedDeviceId = ""
            root.stopMonitoring("当前板型未启用按键检测")
        }
    }

    onKeyDevicesChanged: root.ensureSelectedDevice()
    onVisibleChanged: {
        if (visible) {
            if (!root.keyCapabilityEnabled) {
                root.selectedDeviceId = ""
                root.actionFeedback = "当前板型未启用按键检测"
                return
            }
            root.ensureSelectedDevice()
            boardBackend.refresh()
        } else if (root.monitoring) {
            root.stopMonitoring("已离开按键检测，监听已停止")
        }
    }

    Component.onCompleted: root.ensureSelectedDevice()

    Connections {
        target: boardBackend
        onChanged: {
            if (!root.keyCapabilityEnabled) {
                root.selectedDeviceId = ""
                if (root.visible)
                    root.actionFeedback = "当前板型未启用按键检测"
                return
            }
            root.ensureSelectedDevice()
            if (root.visible && root.monitoring)
                root.actionFeedback = "正在监听，请按下所选板载按键"
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
        height: root.narrowSettings ? Math.round(132 * root.uiScale) : Math.round(88 * root.uiScale)
        color: theme.instrument

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.instrumentRaised
        }

        Column {
            x: Math.round(30 * root.uiScale)
            y: root.narrowSettings ? Math.round(16 * root.uiScale) : Math.round((headerBand.height - height) / 2)
            width: root.narrowSettings
                   ? headerBand.width - Math.round(60 * root.uiScale)
                   : headerActions.x - x - Math.round(22 * root.uiScale)
            spacing: Math.round(3 * root.uiScale)

            Text {
                width: parent.width
                text: "按键检测"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.narrowSettings ? 24 : 28) * root.uiScale)
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

        Row {
            id: headerActions
            anchors.right: parent.right
            anchors.rightMargin: Math.round(30 * root.uiScale)
            y: root.narrowSettings
               ? Math.round(76 * root.uiScale)
               : Math.round((headerBand.height - height) / 2)
            spacing: Math.round(10 * root.uiScale)

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: Math.round(root.narrowSettings ? 96 : 112 * root.uiScale)
                height: Math.round(40 * root.uiScale)
                text: "刷新设备"
                accent: theme.accent
                variant: "primary"
                enabled: root.keyCapabilityEnabled && !root.monitoring
                onClicked: {
                    root.actionFeedback = "正在刷新可监听的板载按键…"
                    boardBackend.refresh()
                }
            }

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: Math.round(root.narrowSettings ? 92 : 108 * root.uiScale)
                height: Math.round(40 * root.uiScale)
                text: "返回桌面"
                variant: "quiet"
                onClicked: {
                    root.stopMonitoring("已停止按键监听")
                    root.closed()
                }
            }
        }
    }

    Flickable {
        id: keyTestPage
        objectName: "key-test-single-page"
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
            policy: keyTestPage.contentHeight > keyTestPage.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: contentColumn
            width: parent.width
            spacing: Math.round(16 * root.uiScale)

            Rectangle {
                id: settingsBand
                objectName: "key-test-settings"
                width: parent.width
                height: root.narrowSettings ? Math.round(184 * root.uiScale) : Math.round(108 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: Math.round(14 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Text {
                        width: parent.width
                        text: "设置"
                        color: theme.inkStrong
                        font.pixelSize: Math.round(14 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Flow {
                        width: parent.width
                        spacing: Math.round(12 * root.uiScale)

                        Column {
                            width: root.narrowSettings
                                   ? parent.width
                                   : Math.max(Math.round(320 * root.uiScale), parent.width - Math.round(380 * root.uiScale))
                            spacing: Math.round(4 * root.uiScale)

                            Text {
                                width: parent.width
                                text: "监听设备  ·  " + (root.keyInputAvailable ? root.keyDevices.length + " 个可发现" : "等待发现")
                                color: theme.muted
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }

                            BoardToolsComboBox {
                                id: keyDeviceBox
                                objectName: "key-test-device-dropdown"
                                uiScale: root.uiScale
                                width: parent.width
                                model: root.keyOptions
                                textRole: "label"
                                currentIndex: root.optionIndexForId(root.selectedDeviceId)
                                enabled: root.keyCapabilityEnabled && !root.monitoring && root.keyDevices.length > 0
                                onActivated: root.selectOption(currentIndex)
                            }
                        }

                        Column {
                            width: root.narrowSettings ? parent.width : Math.round(356 * root.uiScale)
                            spacing: Math.round(4 * root.uiScale)

                            Text {
                                width: parent.width
                                text: "操作"
                                color: theme.muted
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }

                            Row {
                                spacing: Math.round(8 * root.uiScale)

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: Math.round(100 * root.uiScale)
                                    height: Math.round(48 * root.uiScale)
                                    text: root.monitoring ? "正在监听" : "开始监听"
                                    accent: theme.accent
                                    variant: "primary"
                                    enabled: root.keyCapabilityEnabled && root.monitorApiAvailable
                                             && root.selectedDeviceId.length > 0 && !root.monitoring
                                    onClicked: root.startMonitoring()
                                }

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: Math.round(100 * root.uiScale)
                                    height: Math.round(48 * root.uiScale)
                                    text: "停止监听"
                                    variant: "stop"
                                    enabled: root.monitorApiAvailable && root.monitoring
                                    onClicked: root.stopMonitoring("已停止按键监听")
                                }

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: Math.round(116 * root.uiScale)
                                    height: Math.round(48 * root.uiScale)
                                    text: "清空记录"
                                    variant: "secondary"
                                    enabled: root.keyCapabilityEnabled && root.monitorApiAvailable && root.eventHistory.length > 0 && !root.monitoring
                                    onClicked: root.clearEvents()
                                }
                            }
                        }
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
                    width: parent.width * 0.45
                    text: root.monitoring ? "正在记录按键事件" : "最近一次测试记录"
                    color: theme.muted
                    elide: Text.ElideLeft
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            Grid {
                id: readingsGrid
                width: parent.width
                columns: root.narrowSettings ? 1 : 3
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Rectangle {
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
                        width: Math.round(5 * root.uiScale)
                        radius: width / 2
                        color: root.eventHistory.length > 0 ? theme.accent : root.statusAccent()
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(16 * root.uiScale)
                        text: "最近按键"
                        color: theme.muted
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(16 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(42 * root.uiScale)
                        height: Math.round(46 * root.uiScale)
                        text: root.keyCapabilityEnabled ? root.eventText(root.lastEvent) : "当前板型未启用"
                        color: theme.ink
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(18 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(16 * root.uiScale)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Math.round(14 * root.uiScale)
                        text: root.eventHistory.length > 0 ? root.eventDetail(root.lastEvent) : "开始监听后会在这里显示"
                        color: theme.muted
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }

                Rectangle {
                    width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                    height: Math.round(148 * root.uiScale)
                    radius: Math.min(8, Math.round(8 * root.uiScale))
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    border.width: 1

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(16 * root.uiScale)
                        text: "本次次数"
                        color: theme.muted
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(18 * root.uiScale)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Math.round(19 * root.uiScale)
                        height: Math.round(76 * root.uiScale)

                        Item {
                            width: parent.width / 2
                            height: parent.height

                            Text {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                text: root.numberValue(root.keyStatus, "pressCount", 0)
                                color: theme.inkStrong
                                font.pixelSize: Math.round(32 * root.uiScale)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                text: "按下"
                                color: theme.muted
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }
                        }

                        Item {
                            width: parent.width / 2
                            height: parent.height

                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: -Math.round(12 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                width: 1
                                height: Math.round(52 * root.uiScale)
                                color: theme.rule
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                text: root.numberValue(root.keyStatus, "releaseCount", 0)
                                color: theme.inkStrong
                                font.pixelSize: Math.round(32 * root.uiScale)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                text: "松开"
                                color: theme.muted
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }
                        }
                    }
                }

                Rectangle {
                    width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                    height: Math.round(148 * root.uiScale)
                    radius: Math.min(8, Math.round(8 * root.uiScale))
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    border.width: 1

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(16 * root.uiScale)
                        text: "预期按键匹配"
                        color: theme.muted
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(41 * root.uiScale)
                        text: root.numberValue(root.keyStatus, "passedCount", 0) + " 次"
                        color: theme.accent
                        font.pixelSize: Math.round(30 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(18 * root.uiScale)
                        anchors.right: parent.right
                        anchors.rightMargin: Math.round(16 * root.uiScale)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Math.round(14 * root.uiScale)
                        text: root.expectedKeysText()
                        color: theme.muted
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }
            }

            Item {
                width: parent.width
                height: Math.round(24 * root.uiScale)

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "状态"
                    color: theme.inkStrong
                    font.pixelSize: Math.round(17 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                id: statusPanel
                readonly property int eventRowHeight: root.narrowSettings ? Math.round(64 * root.uiScale) : Math.round(52 * root.uiScale)
                width: parent.width
                height: Math.round(122 * root.uiScale)
                        + (root.eventHistory.length > 0 ? Math.min(5, root.eventHistory.length) * eventRowHeight : Math.round(34 * root.uiScale))
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.round(5 * root.uiScale)
                    radius: width / 2
                    color: root.statusAccent()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(23 * root.uiScale)
                    width: Math.max(7, Math.round(8 * root.uiScale))
                    height: width
                    radius: width / 2
                    color: root.statusAccent()
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(32 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(17 * root.uiScale)
                    text: "监听状态"
                    color: theme.muted
                    font.pixelSize: Math.round(11 * root.uiScale)
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(32 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(34 * root.uiScale)
                    text: root.statusText()
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(17 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(68 * root.uiScale)
                    text: root.actionFeedback
                    color: theme.muted
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(11 * root.uiScale)
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(94 * root.uiScale)
                    text: "近期事件"
                    color: theme.inkStrong
                    font.pixelSize: Math.round(13 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    visible: root.eventHistory.length === 0
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(122 * root.uiScale)
                    text: root.monitoring ? "正在等待按键事件" : "开始监听后，最新事件会显示在这里"
                    color: theme.muted
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(11 * root.uiScale)
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(116 * root.uiScale)
                    visible: root.eventHistory.length > 0

                    Repeater {
                        model: root.eventHistory.slice(0, 5)

                        delegate: Item {
                            width: parent.width
                            height: statusPanel.eventRowHeight
                            property var keyEvent: modelData

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: Math.round(8 * root.uiScale)
                                text: root.eventText(parent.keyEvent)
                                color: theme.ink
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(12 * root.uiScale)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Math.round(8 * root.uiScale)
                                text: root.eventDetail(parent.keyEvent)
                                color: theme.muted
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(10 * root.uiScale)
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: theme.rule
                                visible: index < Math.min(4, root.eventHistory.length - 1)
                            }
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: Math.round(2 * root.uiScale)
            }
        }
    }
}
