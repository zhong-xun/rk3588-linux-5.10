import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "npu-yolo-demo-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property string actionFeedback: "先检查内置 YOLOv8 示例资源，再开始视频推理"
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)
    readonly property var demoApi: root.resolveDemoApi()
    readonly property var demoStatus: root.normalizedStatus(root.demoApi ? root.demoApi.status : null)
    readonly property string runState: root.textValue(root.demoStatus, "status", "unavailable")
    readonly property bool available: root.boolValue(root.demoStatus, "available", false)
    readonly property bool running: root.boolValue(root.demoStatus, "running", false)
    readonly property bool canStart: root.boolValue(root.demoStatus, "canStart", root.available && !root.running)
    readonly property bool canStop: root.boolValue(root.demoStatus, "canStop", root.running)
    readonly property string previewUrl: root.textValue(root.demoStatus, "previewUrl", "")
    readonly property int previewSerial: root.numberValue(root.demoStatus, "previewSerial", 0)
    // Decode the next JPEG behind the current frame.  The active frame only
    // changes after the replacement is fully ready, so loading never exposes
    // the waiting placeholder between otherwise valid inference frames.
    property int activePreviewSlot: 0
    property bool hasPreviewFrame: false
    property string displayedPreviewSource: ""
    property string loadingPreviewSource: ""
    property int loadingPreviewSlot: -1
    readonly property real progress: root.numberValue(root.demoStatus, "progress", -1)
    readonly property var npuLoad: root.mapValue(root.demoStatus, "npuLoad", {})
    readonly property var npuLoadHistory: root.listValue(root.mapValue(root.demoStatus, "npuLoadHistory", []))
    readonly property bool npuLoadAvailable: root.boolValue(root.npuLoad, "available", false)
    readonly property string npuLoadSource: root.textValue(root.npuLoad, "source", "")
    readonly property var npuCoreIds: root.npuTelemetryCoreIds()

    function resolveDemoApi() {
        // The integration layer supplies npuYoloDemo.  Keeping this guard
        // lets the page fail closed when opened by an older installed binary.
        return typeof npuYoloDemo !== "undefined" && npuYoloDemo ? npuYoloDemo : null
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

    function listValue(value) {
        if (value === undefined || value === null || typeof value === "string" || value.length === undefined)
            return []
        return value
    }

    function validPercent(value) {
        var numeric = Number(value)
        return !isNaN(numeric) && numeric >= 0 && numeric <= 100
    }

    function npuTelemetryCoreIds() {
        var ids = []
        var candidateLists = [
            root.listValue(root.mapValue(root.npuLoad, "coreIds", [])),
            root.listValue(root.mapValue(root.demoStatus, "npuCoreIds", []))
        ]
        for (var listIndex = 0; listIndex < candidateLists.length; ++listIndex) {
            var candidates = candidateLists[listIndex]
            for (var candidateIndex = 0; candidateIndex < candidates.length; ++candidateIndex) {
                var candidate = Number(candidates[candidateIndex])
                if (isNaN(candidate) || Math.floor(candidate) !== candidate || candidate < 0 || candidate > 2
                        || ids.indexOf(candidate) >= 0)
                    continue
                ids.push(candidate)
            }
            if (ids.length > 0)
                return ids
        }

        // Older BoardLab services did not publish coreIds. Derive that legacy
        // shape from their per-core records instead of inventing cards for a
        // new one- or two-core target.
        var cores = root.listValue(root.mapValue(root.npuLoad, "cores", []))
        for (var coreIndex = 0; coreIndex < cores.length; ++coreIndex) {
            var core = cores[coreIndex]
            var coreId = Number(root.mapValue(core, "core", -1))
            if (isNaN(coreId) || Math.floor(coreId) !== coreId || coreId < 0 || coreId > 2
                    || ids.indexOf(coreId) >= 0)
                continue
            ids.push(coreId)
        }
        return ids
    }

    function npuCoreCards() {
        var colors = [theme.focus, theme.accent, theme.warning]
        var cards = []
        for (var index = 0; index < root.npuCoreIds.length; ++index) {
            var coreId = root.npuCoreIds[index]
            cards.push({ "core": coreId, "label": "NPU " + String(coreId), "accent": colors[coreId] })
        }
        return cards
    }

    function npuCoreDetailText() {
        var mode = root.textValue(root.npuLoad, "executionMode",
                                  root.textValue(root.demoStatus, "executionMode", ""))
        return mode.toLowerCase().indexOf("parallel") >= 0 ? "独立核心 · 利用率" : "自动调度 · 利用率"
    }

    function npuCorePercent(coreIndex) {
        var cores = root.listValue(root.mapValue(root.npuLoad, "cores", []))
        for (var index = 0; index < cores.length; ++index) {
            var core = cores[index]
            if (!core || Number(root.mapValue(core, "core", -1)) !== coreIndex)
                continue
            var value = Number(root.mapValue(core, "percent", -1))
            return root.validPercent(value) ? value : -1
        }
        return -1
    }

    function npuCoreHistory(coreIndex) {
        var values = []
        var propertyName = "core" + String(coreIndex)
        for (var index = 0; index < root.npuLoadHistory.length; ++index) {
            var sample = root.npuLoadHistory[index]
            var value = sample && typeof sample === "object"
                    ? Number(root.mapValue(sample, propertyName, -1)) : -1
            values.push(root.validPercent(value) ? value : -1)
        }
        return values
    }

    function npuCoreValueText(coreIndex) {
        var value = root.npuCorePercent(coreIndex)
        return root.validPercent(value) ? String(Math.round(value)) + "%" : "--"
    }

    function npuLoadStateText() {
        if (root.npuLoadAvailable)
            return root.npuLoadSource.length > 0 ? "实时采样 · " + root.npuLoadSource : "实时采样"
        return root.npuLoadSource.length > 0 ? "无法读取 · " + root.npuLoadSource : "RKNPU 负载不可读"
    }

    function npuAverageText() {
        if (!root.npuLoadAvailable)
            return "平均 --"
        var average = root.numberValue(root.npuLoad, "averagePercent", -1)
        return root.validPercent(average) ? "平均 " + String(Math.round(average)) + "%" : "平均 --"
    }

    function npuSamplingDetailText() {
        if (!root.npuLoadAvailable)
            return root.npuLoadStateText()
        var interval = root.numberValue(root.npuLoad, "sampleIntervalMs", -1)
        var detail = interval > 0 ? String(Math.round(interval)) + " ms 采样" : "持续采样"
        var mode = root.textValue(root.npuLoad, "executionMode",
                                  root.textValue(root.demoStatus, "executionMode", ""))
        if (mode.toLowerCase().indexOf("parallel") >= 0 || mode.indexOf("三核") >= 0)
            detail += " · 三核并行"
        else if (mode.toLowerCase().indexOf("auto") >= 0 || mode.indexOf("自动") >= 0)
            detail += " · 自动核心调度"
        return detail
    }

    function npuNoDataText(coreIndex) {
        if (!root.npuLoadAvailable)
            return root.npuLoadSource.length > 0 ? "无法读取 RKNPU 负载" : "RKNPU 负载不可读"
        return "等待核心 " + String(coreIndex) + " 的采样数据"
    }

    function npuLoadSummaryText() {
        if (!root.npuLoadAvailable)
            return "不可读"
        var values = []
        for (var index = 0; index < root.npuCoreIds.length; ++index) {
            var coreId = root.npuCoreIds[index]
            values.push("NPU " + String(coreId) + " " + root.npuCoreValueText(coreId))
        }
        return values.length > 0 ? values.join(" · ") : "等待采样"
    }

    function normalizedStatus(current) {
        if (current !== undefined && current !== null)
            return current
        return {
            "available": false,
            "running": false,
            "status": "unavailable",
            "message": "当前运行版本未提供 YOLOv8 NPU 示例服务。",
            "error": "",
            "previewUrl": "",
            "previewSerial": 0,
            "progress": -1,
            "framesTotal": -1,
            "framesProcessed": -1,
            "detectionsTotal": -1,
            "fps": -1,
            "averageInferenceMs": -1,
            "inferenceMs": -1,
            "elapsedMs": -1,
            "npuLoad": {
                "available": false,
                "source": "",
                "cores": []
            },
            "npuLoadHistory": [],
            "log": "",
            "assets": {}
        }
    }

    function stateText() {
        var labels = {
            "unavailable": "YOLOv8 示例不可用",
            "ready": "示例资源已就绪",
            "initializing": "正在初始化 NPU 推理",
            "starting": "正在启动视频推理",
            "running": "正在对示例视频进行 NPU 推理",
            "stopping": "正在停止视频推理",
            "completed": "视频推理完成",
            "cancelled": "视频推理已取消",
            "stopped": "视频推理已停止",
            "failed": "视频推理失败"
        }
        return labels[root.runState] || root.runState
    }

    function stateDetailText() {
        var message = root.textValue(root.demoStatus, "message", "")
        return message.length > 0 ? message : root.stateText()
    }

    function stateAccent() {
        if (root.runState === "running" || root.runState === "starting"
                || root.runState === "initializing" || root.runState === "stopping")
            return theme.focus
        if (root.runState === "completed")
            return theme.accent
        if (root.runState === "failed" || root.runState === "unavailable")
            return theme.danger
        if (root.runState === "cancelled" || root.runState === "stopped")
            return theme.inkFaint
        return theme.focus
    }

    function errorText() {
        return root.textValue(root.demoStatus, "error", "")
    }

    function sourceVideoText() {
        var assets = root.mapValue(root.demoStatus, "assets", {})
        return root.textValue(assets, "videoPath", "未找到示例 MP4")
    }

    function modelText() {
        var assets = root.mapValue(root.demoStatus, "assets", {})
        return root.textValue(assets, "modelPath", "未找到 YOLOv8 模型")
    }

    function labelsText() {
        var assets = root.mapValue(root.demoStatus, "assets", {})
        return root.textValue(assets, "labelsPath", "未找到标签文件")
    }

    function previewSource() {
        if (root.previewUrl.length === 0)
            return ""
        // previewSerial changes whenever the backend writes a new frame.  It
        // becomes part of the URL so QML reloads even when the file path stays
        // the same; the rendered pixels still come only from the backend.
        return root.previewUrl + (root.previewUrl.indexOf("?") >= 0 ? "&" : "?")
                + "qmlPreviewSerial=" + String(root.previewSerial)
    }

    function clearPreview() {
        previewQueueTimer.stop()
        root.activePreviewSlot = 0
        root.hasPreviewFrame = false
        root.displayedPreviewSource = ""
        root.loadingPreviewSource = ""
        root.loadingPreviewSlot = -1
        previewImageA.requestedSource = ""
        previewImageA.source = ""
        previewImageB.requestedSource = ""
        previewImageB.source = ""
    }

    function schedulePreviewFrame() {
        if (root.previewUrl.length > 0)
            previewQueueTimer.restart()
    }

    function loadLatestPreview() {
        var latestSource = root.previewSource()
        if (latestSource.length === 0 || latestSource === root.displayedPreviewSource)
            return

        var targetSlot = root.activePreviewSlot === 0 ? 1 : 0
        root.loadingPreviewSource = latestSource
        root.loadingPreviewSlot = targetSlot
        if (targetSlot === 0) {
            previewImageA.requestedSource = latestSource
            previewImageA.source = latestSource
        } else {
            previewImageB.requestedSource = latestSource
            previewImageB.source = latestSource
        }
    }

    function commitPreviewFrame(slot, image) {
        if (image.status !== Image.Ready || slot !== root.loadingPreviewSlot
                || image.requestedSource !== root.loadingPreviewSource)
            return

        root.activePreviewSlot = slot
        root.displayedPreviewSource = root.loadingPreviewSource
        root.hasPreviewFrame = true
        root.loadingPreviewSlot = -1

        // A newer backend update may have arrived while this JPEG decoded.
        // Request only that newest frame on the next event-loop turn.
        if (root.previewSource() !== root.displayedPreviewSource)
            previewQueueTimer.restart()
    }

    function hasNumber(key) {
        var value = root.mapValue(root.demoStatus, key, undefined)
        if (value === undefined || value === null || String(value).length === 0)
            return false
        return !isNaN(Number(value))
    }

    function hasRunMetrics() {
        if (root.textValue(root.demoStatus, "startedAt", "").length === 0)
            return false
        return root.runState === "starting" || root.runState === "running"
                || root.runState === "stopping" || root.runState === "completed"
                || root.runState === "stopped" || root.runState === "failed"
    }

    function numericText(key, suffix, precision) {
        if (!root.hasRunMetrics() || !root.hasNumber(key))
            return "--"
        var value = Number(root.mapValue(root.demoStatus, key, 0))
        var multiplier = Math.pow(10, precision)
        return String(Math.round(value * multiplier) / multiplier) + suffix
    }

    function progressValue() {
        if (root.progress < 0)
            return 0
        return Math.max(0, Math.min(1, root.progress))
    }

    function progressText() {
        var total = root.numberValue(root.demoStatus, "framesTotal", -1)
        var processed = root.numberValue(root.demoStatus, "framesProcessed", -1)
        if (!root.hasRunMetrics())
            return "尚未开始"
        if (total > 0 && processed >= 0)
            return String(Math.round(processed)) + " / " + String(Math.round(total)) + " 帧"
        if (root.progress >= 0)
            return String(Math.round(root.progressValue() * 100)) + "%"
        return root.running ? "正在等待首帧" : "尚未开始"
    }

    function elapsedText() {
        if (!root.hasRunMetrics() || !root.hasNumber("elapsedMs"))
            return "--"
        var elapsed = Number(root.mapValue(root.demoStatus, "elapsedMs", 0))
        return elapsed >= 1000
               ? String(Math.round(elapsed / 100) / 10) + " 秒"
               : String(Math.round(elapsed)) + " ms"
    }

    function requestDiscover() {
        if (!root.demoApi) {
            root.actionFeedback = "当前运行版本未提供 YOLOv8 NPU 示例服务"
            return
        }
        if (root.running) {
            root.actionFeedback = "视频推理正在运行，完成或停止后再检查资源"
            return
        }
        var found = root.demoApi.discover()
        if (found === false) {
            var detail = root.errorText()
            root.actionFeedback = detail.length > 0 ? detail : "未找到可运行的 YOLOv8 示例资源"
            return
        }
        root.actionFeedback = "已读取 YOLOv8 模型、标签和示例视频状态"
    }

    function requestStart() {
        if (!root.demoApi) {
            root.actionFeedback = "当前运行版本未提供 YOLOv8 NPU 示例服务"
            return
        }
        if (root.running) {
            root.actionFeedback = "视频推理正在运行"
            return
        }
        if (!root.available || !root.canStart) {
            root.actionFeedback = "示例资源尚未就绪，请先检查资源"
            return
        }
        if (root.demoApi.start()) {
            root.clearPreview()
            root.actionFeedback = "正在对内置 MP4 执行 YOLOv8 NPU 推理…"
        } else {
            var detail = root.errorText()
            root.actionFeedback = detail.length > 0 ? detail : "视频推理未启动"
        }
    }

    function requestStop() {
        if (!root.demoApi || !root.running) {
            root.actionFeedback = "当前没有正在运行的视频推理"
            return
        }
        if (root.demoApi.stop())
            root.actionFeedback = "已请求停止视频推理"
        else
            root.actionFeedback = "停止请求未被接受，请等待当前帧处理完成"
    }

    function stopDemoForExit() {
        if (root.demoApi && root.running) {
            root.demoApi.stop()
            root.actionFeedback = "已在离开页面时请求停止视频推理"
        }
    }

    function demoLog() {
        var lines = []
        lines.push("状态         " + root.stateText())
        lines.push("示例视频     " + root.sourceVideoText())
        lines.push("模型         " + root.modelText())
        lines.push("标签         " + root.labelsText())
        lines.push("进度         " + root.progressText())
        lines.push("检测数量     " + root.numericText("detectionsTotal", "", 0))
        lines.push("吞吐         " + root.numericText("fps", " FPS", 1))
        lines.push("平均推理     " + root.numericText("averageInferenceMs", " ms", 1))
        lines.push("最近推理     " + root.numericText("inferenceMs", " ms", 1))
        lines.push("NPU 负载     " + root.npuLoadSummaryText())
        lines.push("运行时长     " + root.elapsedText())
        var error = root.errorText()
        if (error.length > 0)
            lines.push("错误         " + error)
        var output = root.textValue(root.demoStatus, "log",
                                    root.demoApi ? String(root.demoApi.log || "") : "")
        if (output.length > 0) {
            lines.push("")
            lines.push("YOLOv8 输出")
            lines.push(output)
        } else {
            lines.push("")
            lines.push("尚未产生推理输出。")
        }
        return lines.join("\n")
    }

    onVisibleChanged: {
        if (visible) {
            root.schedulePreviewFrame()
            if (!root.demoApi)
                root.actionFeedback = "当前运行版本未提供 YOLOv8 NPU 示例服务"
            else if (root.runState === "completed")
                root.actionFeedback = "上次视频推理已完成，可检查结果或重新运行"
            else
                root.actionFeedback = root.stateDetailText()
        }
    }

    onPreviewUrlChanged: root.schedulePreviewFrame()
    onPreviewSerialChanged: root.schedulePreviewFrame()

    Component.onCompleted: root.schedulePreviewFrame()

    Timer {
        id: previewQueueTimer
        interval: 0
        repeat: false
        onTriggered: root.loadLatestPreview()
    }

    Connections {
        target: root.demoApi
        onStatusChanged: {
            if (!root.visible)
                return
            if (root.runState === "completed")
                root.actionFeedback = "视频推理完成，已更新预览与性能数据"
            else if (root.runState === "failed")
                root.actionFeedback = root.errorText().length > 0 ? root.errorText() : root.stateText()
            else
                root.actionFeedback = root.stateDetailText()
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
                text: root.compactLayout ? "NPU 视频测试" : "YOLOv8 NPU 视频测试"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactLayout ? 22 : 28) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: "状态：" + root.stateText() + " · " + root.actionFeedback
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
                width: Math.round(root.compactLayout ? 80 : 104 * root.uiScale)
                height: Math.round(40 * root.uiScale)
                text: "检查资源"
                accent: theme.accent
                variant: "primary"
                enabled: root.demoApi !== null && !root.running
                onClicked: root.requestDiscover()
            }

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

    BoardToolsWorkSurface {
        id: npuWorkSurface
        objectName: "npu-yolo-demo-work-surface"
        // Kept as inactive compatibility markup while older package variants
        // are retired. The customer-facing page below is the single-page UI.
        visible: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerBand.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Math.round(24 * root.uiScale)
        fillParent: false
        uiScale: root.uiScale
        narrowStartPage: 1
        leftTitle: "运行控制"
        middleTitle: "实时预览"
        rightTitle: "会话状态"
        mainPageLabel: "控制"
        detailPageLabel: "状态"
        logPageLabel: "推理日志"

        leftContent: Column {
            width: parent.width
            spacing: Math.round(12 * root.uiScale)

            BoardToolsStatusHero {
                uiScale: root.uiScale
                width: parent.width
                label: "YOLOv8 示例"
                value: root.stateText()
                accent: root.stateAccent()
            }

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: parent.width
                text: root.running ? "视频推理运行中" : "运行示例 MP4"
                accent: theme.accent
                variant: "primary"
                enabled: root.demoApi !== null && root.available && root.canStart && !root.running
                         && root.runState !== "starting" && root.runState !== "stopping"
                onClicked: root.requestStart()
            }

            BoardToolsActionButton {
                uiScale: root.uiScale
                width: parent.width
                text: "停止视频推理"
                accent: theme.danger
                variant: "stop"
                enabled: root.running && root.canStop
                onClicked: root.requestStop()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "输入视频"
                value: root.sourceVideoText()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "YOLOv8 模型"
                value: root.modelText()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "标签文件"
                value: root.labelsText()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "运行边界"
                value: "仅使用项目内置模型和示例 MP4；不上传用户文件，也不执行任意命令。"
            }

            BoardToolsWarningBlock {
                uiScale: root.uiScale
                width: parent.width
                visible: root.errorText().length > 0 || root.runState === "unavailable"
                text: root.errorText().length > 0 ? root.errorText() : root.stateDetailText()
            }
        }

        middleContent: Column {
            width: parent.width
            spacing: Math.round(12 * root.uiScale)

            Rectangle {
                width: parent.width
                height: Math.max(Math.round(208 * root.uiScale),
                                 Math.min(Math.round(320 * root.uiScale), Math.round(width * 9 / 16)))
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.instrument
                border.color: theme.instrumentRaised
                border.width: 1
                clip: true

                Image {
                    id: previewImage
                    anchors.fill: parent
                    anchors.margins: Math.round(1 * root.uiScale)
                    // This compatibility surface stays hidden.  Do not let it
                    // decode each live JPEG behind the active single-page UI.
                    source: ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: status === Image.Ready && root.previewUrl.length > 0
                }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - Math.round(36 * root.uiScale)
                    spacing: Math.round(7 * root.uiScale)
                    visible: !previewImage.visible

                    Text {
                        width: parent.width
                        text: root.running ? "正在等待推理预览帧" : "推理预览"
                        color: theme.instrumentInk
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Math.round(18 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: root.running ? "首帧生成后将在这里显示检测结果" : "运行内置示例 MP4 后显示后端生成的检测画面"
                        color: theme.instrumentMuted
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(12 * root.uiScale)
                    width: previewStateLabel.implicitWidth + Math.round(18 * root.uiScale)
                    height: Math.round(26 * root.uiScale)
                    radius: height / 2
                    color: theme.withAlpha(theme.instrument, 0.78)
                    border.color: theme.withAlpha(theme.focus, 0.48)
                    border.width: 1

                    Text {
                        id: previewStateLabel
                        anchors.centerIn: parent
                        text: root.running ? "实时推理" : (root.previewUrl.length > 0 ? "最近结果" : "等待预览")
                        color: theme.instrumentInk
                        font.pixelSize: Math.round(11 * root.uiScale)
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                id: npuTelemetryPanel
                width: parent.width
                height: npuTelemetryContent.implicitHeight + Math.round(28 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceSoft
                border.color: root.npuLoadAvailable ? theme.rule : theme.rule
                border.width: 1

                Column {
                    id: npuTelemetryContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: Math.round(14 * root.uiScale)
                    anchors.rightMargin: Math.round(14 * root.uiScale)
                    anchors.topMargin: Math.round(13 * root.uiScale)
                    spacing: Math.round(10 * root.uiScale)

                    Row {
                        width: parent.width
                        height: Math.round(31 * root.uiScale)
                        spacing: Math.round(10 * root.uiScale)

                        Column {
                            width: Math.max(0, parent.width - telemetryAverage.width - parent.spacing)
                            spacing: Math.round(2 * root.uiScale)

                            Text {
                                width: parent.width
                                text: "NPU 核心利用率"
                                color: theme.instrumentRaised
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(13 * root.uiScale)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                text: root.npuSamplingDetailText()
                                color: root.npuLoadAvailable ? theme.inkFaint : theme.warning
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(9 * root.uiScale)
                            }
                        }

                        Rectangle {
                            id: telemetryAverage
                            width: telemetryAverageText.implicitWidth + Math.round(16 * root.uiScale)
                            height: Math.round(27 * root.uiScale)
                            radius: Math.min(7, Math.round(7 * root.uiScale))
                            color: root.npuLoadAvailable ? theme.readySoft : theme.surfaceSoft
                            border.color: root.npuLoadAvailable ? theme.accentSoft : theme.rule
                            border.width: 1

                            Text {
                                id: telemetryAverageText
                                anchors.centerIn: parent
                                text: root.npuAverageText()
                                color: root.npuLoadAvailable ? theme.accentHover : theme.inkFaint
                                font.pixelSize: Math.round(10 * root.uiScale)
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Grid {
                        id: npuChartGrid
                        width: parent.width
                        // The central work pane is narrower than the window
                        // in desktop mode. Use its actual width so chart axes
                        // stay readable without reserving cards for cores the
                        // active SoC does not expose.
                        property int coreCount: root.npuCoreCards().length
                        property int preferredColumns: width >= 660 ? 3
                                                      : (width >= 430 ? 2 : 1)
                        property int chartColumns: Math.min(coreCount, preferredColumns)
                        columns: Math.max(1, chartColumns)
                        rowSpacing: Math.round(9 * root.uiScale)
                        columnSpacing: Math.round(9 * root.uiScale)
                        property real cardWidth: (width - columnSpacing * Math.max(0, columns - 1)) / columns
                        // The work pane scrolls on narrow screens, so retain a
                        // readable plotting area instead of compressing charts.
                        property real cardHeight: Math.round((columns === 1 ? 202
                                                                             : (columns === 2 ? 186 : 176)) * root.uiScale)
                        height: coreCount > 0
                                ? Math.ceil(coreCount / columns) * cardHeight
                                  + Math.max(0, Math.ceil(coreCount / columns) - 1) * rowSpacing
                                : 0

                        Repeater {
                            model: root.npuCoreCards()

                            delegate: NpuLoadTrendCard {
                                width: npuChartGrid.cardWidth
                                height: npuChartGrid.cardHeight
                                uiScale: root.uiScale
                                coreIndex: modelData.core
                                coreLabel: modelData.label
                                coreDetail: root.npuCoreDetailText()
                                percent: root.npuCorePercent(modelData.core)
                                samples: root.npuCoreHistory(modelData.core)
                                available: root.npuLoadAvailable
                                unavailableText: root.npuNoDataText(modelData.core)
                                accent: modelData.accent
                                compact: npuChartGrid.columns > 1
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: Math.round(42 * root.uiScale)
                radius: Math.min(7, Math.round(7 * root.uiScale))
                color: theme.surfaceSoft
                border.color: theme.rule
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.max(0, parent.width * root.progressValue())
                    radius: parent.radius
                    color: root.stateAccent()
                    opacity: root.progress >= 0 ? 0.82 : 0.20

                    Behavior on width { NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic } }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "处理进度 · " + root.progressText()
                    color: root.progress >= 0 && root.progressValue() > 0.45 ? theme.surfaceRaised : theme.ink
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(12 * root.uiScale)
                    font.weight: Font.DemiBold
                }
            }

            Grid {
                width: parent.width
                columns: root.compactLayout ? 1 : 3
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Repeater {
                    model: [
                        { "label": "检测数量", "value": root.numericText("detectionsTotal", "", 0), "accent": theme.focus },
                        { "label": "推理吞吐", "value": root.numericText("fps", " FPS", 1), "accent": theme.focus },
                        { "label": "平均延迟", "value": root.numericText("averageInferenceMs", " ms", 1), "accent": theme.warning }
                    ]

                    delegate: Rectangle {
                        width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                        height: Math.round(94 * root.uiScale)
                        radius: Math.min(8, Math.round(8 * root.uiScale))
                        color: theme.surfaceSoft
                        border.color: theme.rule
                        border.width: 1

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(12 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(12 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(13 * root.uiScale)
                            text: modelData.label
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(12 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(12 * root.uiScale)
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.round(13 * root.uiScale)
                            text: modelData.value
                            color: modelData.accent
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(19 * root.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        rightContent: Column {
            width: parent.width
            spacing: Math.round(12 * root.uiScale)

            BoardToolsStatusHero {
                uiScale: root.uiScale
                width: parent.width
                label: "当前状态"
                value: root.stateText()
                accent: root.stateAccent()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "处理进度"
                value: root.progressText()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "最近一帧推理"
                value: root.numericText("inferenceMs", " ms", 1)
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "运行时长"
                value: root.elapsedText()
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "开始时间"
                value: root.textValue(root.demoStatus, "startedAt", "尚未开始")
            }

            BoardToolsInfoBlock {
                uiScale: root.uiScale
                width: parent.width
                label: "完成时间"
                value: root.textValue(root.demoStatus, "finishedAt", "尚未完成")
            }
        }

        logContent: BoardToolsLogPanel {
            uiScale: root.uiScale
            text: root.demoLog()
        }
    }

    Flickable {
        id: npuPage
        objectName: "npu-yolo-demo-single-page"
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
            policy: npuPage.contentHeight > npuPage.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: contentColumn
            width: npuPage.width
            spacing: Math.round(16 * root.uiScale)

            Rectangle {
                id: settingsBand
                width: parent.width
                height: root.compactLayout ? Math.round(198 * root.uiScale) : Math.round(72 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Flow {
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(12 * root.uiScale)

                    Column {
                        width: root.compactLayout ? parent.width : Math.max(Math.round(220 * root.uiScale), settingsBand.width - Math.round(600 * root.uiScale))
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
                            text: "仅运行内置模型与示例视频"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(112 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: "检查资源"
                        accent: theme.accent
                        variant: "quiet"
                        enabled: root.demoApi !== null && !root.running
                        onClicked: root.requestDiscover()
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(138 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: root.running ? "推理运行中" : "运行示例 MP4"
                        accent: theme.accent
                        variant: "primary"
                        enabled: root.demoApi !== null && root.available && root.canStart && !root.running
                                 && root.runState !== "starting" && root.runState !== "stopping"
                        onClicked: root.requestStart()
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(112 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: "停止"
                        accent: theme.danger
                        variant: "stop"
                        enabled: root.running && root.canStop
                        onClicked: root.requestStop()
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
                    text: root.npuLoadStateText()
                    color: root.npuLoadAvailable ? theme.accent : theme.warning
                    elide: Text.ElideLeft
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            Rectangle {
                width: parent.width
                height: Math.max(Math.round(240 * root.uiScale),
                                 Math.min(Math.round(360 * root.uiScale), Math.round(width * 9 / 16)))
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.instrument
                border.color: theme.instrumentRaised
                border.width: 1
                clip: true

                Image {
                    id: previewImageA
                    property string requestedSource: ""
                    anchors.fill: parent
                    anchors.margins: Math.round(1 * root.uiScale)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    sourceSize.width: Math.max(1, Math.round(width))
                    sourceSize.height: Math.max(1, Math.round(height))
                    opacity: root.hasPreviewFrame && root.activePreviewSlot === 0 ? 1 : 0
                    onStatusChanged: root.commitPreviewFrame(0, previewImageA)
                }

                Image {
                    id: previewImageB
                    property string requestedSource: ""
                    anchors.fill: parent
                    anchors.margins: Math.round(1 * root.uiScale)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    sourceSize.width: Math.max(1, Math.round(width))
                    sourceSize.height: Math.max(1, Math.round(height))
                    opacity: root.hasPreviewFrame && root.activePreviewSlot === 1 ? 1 : 0
                    onStatusChanged: root.commitPreviewFrame(1, previewImageB)
                }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - Math.round(36 * root.uiScale)
                    spacing: Math.round(7 * root.uiScale)
                    visible: !root.hasPreviewFrame

                    Text {
                        width: parent.width
                        text: root.running ? "正在等待推理预览帧" : "推理预览"
                        color: theme.instrumentInk
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Math.round(18 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: root.running ? "首帧生成后将在这里显示检测结果" : "运行内置示例 MP4 后显示后端生成的检测画面"
                        color: theme.instrumentMuted
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(12 * root.uiScale)
                    width: previewStateLabel.implicitWidth + Math.round(18 * root.uiScale)
                    height: Math.round(26 * root.uiScale)
                    radius: height / 2
                    color: theme.withAlpha(theme.instrument, 0.78)
                    border.color: theme.withAlpha(theme.focus, 0.48)
                    border.width: 1

                    Text {
                        id: previewStateLabel
                        anchors.centerIn: parent
                        text: root.running ? "实时推理" : (root.hasPreviewFrame ? "最近结果" : "等待预览")
                        color: theme.instrumentInk
                        font.pixelSize: Math.round(11 * root.uiScale)
                        font.weight: Font.DemiBold
                    }
                }
            }

            Grid {
                id: npuChartGrid
                width: parent.width
                property int coreCount: root.npuCoreCards().length
                property int preferredColumns: root.width < Math.round(760 * root.uiScale) ? 1 : 3
                columns: Math.max(1, Math.min(coreCount, preferredColumns))
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)
                property real cardWidth: (width - columnSpacing * Math.max(0, columns - 1)) / columns
                property real cardHeight: Math.round((columns === 1 ? 208 : 184) * root.uiScale)
                height: coreCount > 0
                        ? Math.ceil(coreCount / columns) * cardHeight
                          + Math.max(0, Math.ceil(coreCount / columns) - 1) * rowSpacing
                        : 0

                Repeater {
                    model: root.npuCoreCards()

                    delegate: NpuLoadTrendCard {
                        width: npuChartGrid.cardWidth
                        height: npuChartGrid.cardHeight
                        uiScale: root.uiScale
                        coreIndex: modelData.core
                        coreLabel: modelData.label
                        coreDetail: root.npuCoreDetailText()
                        percent: root.npuCorePercent(modelData.core)
                        samples: root.npuCoreHistory(modelData.core)
                        available: root.npuLoadAvailable
                        unavailableText: root.npuNoDataText(modelData.core)
                        accent: modelData.accent
                        compact: npuChartGrid.columns > 1
                    }
                }
            }

            Grid {
                width: parent.width
                columns: root.compactLayout ? 1 : 3
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Repeater {
                    model: [
                        { "label": "检测数量", "value": root.numericText("detectionsTotal", "", 0), "accent": theme.focus },
                        { "label": "推理吞吐", "value": root.numericText("fps", " FPS", 1), "accent": theme.focus },
                        { "label": "平均延迟", "value": root.numericText("averageInferenceMs", " ms", 1), "accent": theme.warning }
                    ]

                    delegate: Rectangle {
                        width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                        height: Math.round(94 * root.uiScale)
                        radius: Math.min(8, Math.round(8 * root.uiScale))
                        color: theme.surfaceRaised
                        border.color: theme.rule
                        border.width: 1

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(12 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(12 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(13 * root.uiScale)
                            text: modelData.label
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(12 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(12 * root.uiScale)
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.round(13 * root.uiScale)
                            text: modelData.value
                            color: modelData.accent
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(19 * root.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: statusColumn.height + Math.round(24 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    id: statusColumn
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

                    Rectangle {
                        width: parent.width
                        height: Math.round(42 * root.uiScale)
                        radius: Math.min(7, Math.round(7 * root.uiScale))
                        color: theme.surfaceSoft
                        border.color: theme.rule
                        border.width: 1

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Math.max(0, parent.width * root.progressValue())
                            radius: parent.radius
                            color: root.stateAccent()
                            opacity: root.progress >= 0 ? 0.82 : 0.20

                            Behavior on width { NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic } }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(12 * root.uiScale)
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(12 * root.uiScale)
                            anchors.verticalCenter: parent.verticalCenter
                            text: "处理进度 · " + root.progressText()
                            color: root.progress >= 0 && root.progressValue() > 0.45 ? theme.surfaceRaised : theme.ink
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(12 * root.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.compactLayout ? 1 : 3
                        rowSpacing: Math.round(8 * root.uiScale)
                        columnSpacing: Math.round(12 * root.uiScale)

                        Repeater {
                            model: [
                                { "label": "最近一帧", "value": root.numericText("inferenceMs", " ms", 1) },
                                { "label": "运行时长", "value": root.elapsedText() },
                                { "label": "NPU 平均利用率", "value": root.npuAverageText() }
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

                    Text {
                        width: parent.width
                        visible: root.errorText().length > 0 || root.runState === "unavailable"
                        text: root.errorText().length > 0 ? root.errorText() : root.stateDetailText()
                        color: theme.danger
                        wrapMode: Text.WordWrap
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }
            }
        }
    }
}
