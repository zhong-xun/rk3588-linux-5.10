import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root
    objectName: "gpu-test-app"

    BoardLabTheme { id: theme }

    signal closed()

    property real uiScale: 1.0
    property string actionFeedback: root.gpuCapabilityEnabled
                                    ? "先确认基准工具，再运行固定离屏测试"
                                    : "当前板型未启用 GPU 测试"
    readonly property bool gpuCapabilityEnabled: boardProfile.hasCapability("gpu")
    readonly property var benchmark: root.normalizedBenchmark(boardBackend.gpuBenchmarkStatus)
    readonly property bool benchmarkRunning: root.boolValue(root.benchmark, "running", false)
    readonly property bool benchmarkAvailable: root.boolValue(root.benchmark, "available", false)
    readonly property string benchmarkState: root.textValue(root.benchmark, "status", "idle")
    readonly property bool benchmarkStopping: root.benchmarkState === "stopping"
    readonly property int benchmarkScore: root.numberValue(root.benchmark, "score", -1)
    readonly property bool previewRunning: root.boolValue(root.benchmark, "previewRunning", false)
    readonly property bool previewAvailable: root.boolValue(root.benchmark, "previewAvailable", false)
    readonly property string previewState: root.textValue(root.benchmark, "previewStatus", "idle")
    readonly property string previewTarget: root.textValue(root.benchmark, "previewTarget", "not-checked")
    readonly property int previewScore: root.numberValue(root.benchmark, "previewScore", -1)
    // An embedded preview has no independent window.  It is valid only while
    // this page is actually available to render it.
    readonly property bool previewSurfaceReady: root.visible && root.enabled
    readonly property bool backendBusy: boardBackend.backendBusy
    readonly property bool compactLayout: width < Math.round(760 * root.uiScale)
    readonly property bool wideWorkSurface: width >= Math.round(2200 * root.uiScale)
    readonly property int maximumWorkSurfaceWidth: Math.round(2500 * root.uiScale)
    readonly property real availableWorkSurfaceWidth: Math.max(1, width - Math.round(48 * root.uiScale))
    readonly property real resolvedWorkSurfaceWidth: root.wideWorkSurface
                                                     ? Math.min(root.availableWorkSurfaceWidth, root.maximumWorkSurfaceWidth)
                                                     : root.availableWorkSurfaceWidth
    // On an ultra-wide display, retain the same task order but give the two
    // real-time evidence surfaces a usable aspect ratio rather than widening
    // them into thin strips.
    readonly property int previewBandHeight: root.compactLayout ? Math.round(244 * root.uiScale)
                                                                 : (root.wideWorkSurface
                                                                    ? Math.round(Math.max(258 * root.uiScale,
                                                                                         Math.min(400 * root.uiScale,
                                                                                                  root.resolvedWorkSurfaceWidth * 0.16)))
                                                                    : Math.round(258 * root.uiScale))
    readonly property int telemetryChartHeight: root.wideWorkSurface
                                                ? Math.round(Math.max(126 * root.uiScale,
                                                                     Math.min(200 * root.uiScale,
                                                                              root.resolvedWorkSurfaceWidth * 0.08)))
                                                : Math.round(126 * root.uiScale)
    // ResourceMonitor reads the kernel devfreq load node exposed by the Mali
    // driver.  Keep a small history here so the GPU test itself proves that
    // the driver reported activity, instead of only showing glmark2 text.
    readonly property var gpuTelemetryApi: root.resolveGpuTelemetryApi()
    readonly property var gpuTelemetrySnapshot: root.gpuTelemetryApi
                                                ? root.gpuTelemetryApi.snapshot : ({})
    readonly property var gpuTelemetryMetric: root.gpuMetricFromSnapshot(root.gpuTelemetrySnapshot)
    readonly property var gpuUtilizationMetric: root.mapValue(root.gpuTelemetryMetric, "utilization", ({}))
    readonly property int gpuLoadPercent: root.gpuLoadPercentForMetric(root.gpuTelemetryMetric)
    readonly property real gpuFrequencyMHz: root.gpuFrequencyMHzForMetric(root.gpuTelemetryMetric)
    readonly property bool gpuTelemetryAvailable: root.boolValue(root.gpuTelemetryMetric, "available", false)
                                                   && root.gpuLoadPercent >= 0
    readonly property string gpuTelemetryPath: root.textValue(root.gpuUtilizationMetric, "path", "")
    // The chart is evidence for one benchmark or preview session.  A negative
    // sample is an intentional gap: the load node could not be read then.
    readonly property bool gpuTaskRunning: root.benchmarkRunning || root.previewRunning
    property var gpuLoadHistory: []

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

    function resolveGpuTelemetryApi() {
        // Keep the GPU page loadable with an older BoardLab binary.  The
        // production entry point supplies ResourceMonitor as a context object.
        return typeof resourceMonitor !== "undefined" && resourceMonitor ? resourceMonitor : null
    }

    function gpuMetricFromSnapshot(snapshot) {
        var metric = root.mapValue(snapshot, "gpu", ({}))
        return metric && typeof metric === "object" ? metric : ({})
    }

    function gpuLoadPercentForMetric(metric) {
        var utilization = root.mapValue(metric, "utilization", ({}))
        var load = root.numberValue(utilization, "valuePercent", -1)
        if (load < 0)
            load = root.numberValue(utilization, "utilizationPercent", -1)
        return load < 0 ? -1 : Math.max(0, Math.min(100, Math.round(load)))
    }

    function gpuFrequencyMHzForMetric(metric) {
        var frequency = root.mapValue(metric, "currentFrequency", ({}))
        var mhz = root.numberValue(frequency, "valueMHz", -1)
        if (mhz >= 0)
            return mhz
        var hz = root.numberValue(frequency, "valueHz", -1)
        return hz >= 0 ? hz / 1000000.0 : -1
    }

    function refreshGpuTelemetry() {
        if (!root.visible || !root.gpuTelemetryApi)
            return
        var snapshot = root.gpuTelemetryApi.refresh()
        if (!snapshot || typeof snapshot !== "object")
            snapshot = root.gpuTelemetryApi.snapshot
        if (!root.gpuTaskRunning)
            return
        var load = root.gpuLoadPercentForMetric(root.gpuMetricFromSnapshot(snapshot))
        var samples = root.gpuLoadHistory.slice(0)
        samples.push(load < 0 ? -1 : load)
        while (samples.length > 30)
            samples.shift()
        root.gpuLoadHistory = samples
    }

    function beginGpuTelemetrySession() {
        root.gpuLoadHistory = []
        root.refreshGpuTelemetry()
    }

    function gpuLoadText() {
        return root.gpuLoadPercent >= 0 ? String(root.gpuLoadPercent) + "%" : "--"
    }

    function gpuFrequencyText() {
        return root.gpuFrequencyMHz >= 0
               ? String(Math.round(root.gpuFrequencyMHz * 10) / 10) + " MHz" : "--"
    }

    function gpuTelemetryStateText() {
        if (!root.gpuTelemetryApi)
            return "当前运行版本未提供 GPU 驱动状态服务"
        if (!root.gpuTelemetryAvailable) {
            return root.gpuTaskRunning
                   ? "GPU 任务运行中，驱动负载节点暂不可读取"
                   : "未发现可读取的 GPU 驱动负载节点"
        }
        if (root.benchmarkRunning || root.previewRunning)
            return "GPU 任务运行中，正在读取 Mali 驱动负载"
        if (root.gpuLoadHistory.length > 0)
            return "本次 GPU 任务趋势已保留"
        return "Mali 驱动负载采样已就绪"
    }

    function gpuGraphSamples() {
        return root.gpuLoadHistory
    }

    function gpuGraphPeakText() {
        if (root.gpuLoadHistory.length === 0)
            return "等待采样"
        var peak = 0
        var validSampleCount = 0
        for (var index = 0; index < root.gpuLoadHistory.length; ++index)
        {
            var sample = Number(root.gpuLoadHistory[index])
            if (sample < 0)
                continue
            peak = Math.max(peak, sample)
            ++validSampleCount
        }
        if (validSampleCount === 0)
            return "等待有效读数"
        return "峰值 " + String(Math.round(peak)) + "%"
    }

    function gpuGraphSampleLabel() {
        if (root.gpuLoadHistory.length === 0)
            return root.gpuTaskRunning ? "等待第一个样本" : "等待下一次任务"
        var gaps = 0
        for (var index = 0; index < root.gpuLoadHistory.length; ++index) {
            if (Number(root.gpuLoadHistory[index]) < 0)
                ++gaps
        }
        var label = "本次任务 " + String(root.gpuLoadHistory.length) + " 个样本"
        return gaps > 0 ? label + " · " + String(gaps) + " 个读取断点" : label
    }

    function normalizedBenchmark(current) {
        if (current !== undefined && current !== null)
            return current
        return {
            "available": false,
            "running": false,
            "status": "unavailable",
            "error": "当前运行版本未提供 GPU 基准服务。",
            "score": -1,
            "renderer": "",
            "log": "",
            "previewAvailable": false,
            "previewRunning": false,
            "previewStatus": "unavailable",
            "previewTarget": "unavailable",
            "previewError": "当前运行版本未提供图形预览服务。",
            "previewMessage": "",
            "previewLog": ""
        }
    }

    function stateText() {
        if (!root.gpuCapabilityEnabled)
            return "当前板型未启用 GPU 测试"
        var labels = {
            "idle": "等待检查基准工具",
            "ready": "基准工具已就绪",
            "running": "固定离屏基准正在运行",
            "stopping": "正在停止 GPU 基准",
            "completed": "基准运行完成",
            "stopped": "基准已停止",
            "failed": "基准运行失败",
            "unavailable": "未找到 glmark2-es"
        }
        return labels[root.benchmarkState] || root.benchmarkState
    }

    function stateAccent() {
        if (!root.gpuCapabilityEnabled)
            return theme.inkFaint
        if (root.benchmarkRunning || root.benchmarkState === "stopping")
            return theme.focus
        if (root.benchmarkState === "completed")
            return theme.accent
        if (root.benchmarkState === "failed" || root.benchmarkState === "unavailable")
            return theme.danger
        return theme.warning
    }

    function previewStateText() {
        var labels = {
            "idle": "等待内嵌预览就绪",
            "ready": "可启动内嵌 GPU 预览",
            "running": "GPU 内嵌预览正在实时渲染",
            "stopping": "正在停止内嵌 GPU 预览",
            "completed": "GPU 内嵌预览已完成",
            "stopped": "GPU 内嵌预览已停止",
            "failed": "GPU 内嵌预览失败",
            "blocked": "等待当前 GPU 任务结束",
            "unavailable": "图形预览不可用"
        }
        return labels[root.previewState] || root.previewState
    }

    function previewOperatorStatusText() {
        if (root.previewRunning)
            return "当前 GPU 测试页正在实时渲染图形"
        return root.previewStateText()
    }

    function previewAccent() {
        if (root.previewRunning || root.previewState === "stopping")
            return theme.focus
        if (root.previewState === "completed")
            return theme.accent
        if (root.previewState === "failed" || root.previewState === "unavailable")
            return theme.danger
        return theme.muted
    }

    function previewTargetText() {
        var display = root.textValue(root.benchmark, "previewDisplay", "未设置")
        if (root.previewTarget === "gpu-test-page")
            return "当前 GPU 测试页内嵌窗口 · " + display
        return root.previewTarget === "unavailable" ? "未找到可用图形会话" : display
    }

    function previewWarningText() {
        var error = root.textValue(root.benchmark, "previewError", "")
        return error.length > 0 ? error : ""
    }

    function scoreText() {
        return root.benchmarkScore >= 0 ? String(root.benchmarkScore) + " 分" : "--"
    }

    function elapsedText() {
        var seconds = root.numberValue(root.benchmark, "elapsedSeconds", -1)
        if (seconds < 0)
            return "--"
        return String(Math.round(seconds * 10) / 10) + " 秒"
    }

    function previewElapsedText() {
        var seconds = root.numberValue(root.benchmark, "previewElapsedSeconds", -1)
        if (seconds < 0)
            return "--"
        return String(Math.round(seconds * 10) / 10) + " 秒"
    }

    function rendererText() {
        return root.textValue(root.benchmark, "renderer", "等待基准输出")
    }

    function binaryText() {
        return root.textValue(root.benchmark, "binary", "未检测")
    }

    function errorText() {
        return root.textValue(root.benchmark, "error", "无")
    }

    function previewErrorText() {
        return root.textValue(root.benchmark, "previewError", "无")
    }

    function previewRendererText() {
        return root.textValue(root.benchmark, "previewRenderer", "等待图形预览输出")
    }

    function backendBusyFeedback() {
        var operation = String(boardBackend.backendBusyOperation || "").trim()
        return operation.length > 0
               ? "板端正在执行 " + operation + "，请等待完成"
               : "板端正在执行其他操作，请等待完成"
    }

    function requestDiscovery() {
        if (!root.gpuCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用 GPU 测试"
            return
        }
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.benchmarkRunning || root.previewRunning) {
            root.actionFeedback = "GPU 任务正在运行，完成后可再次检查工具"
            return
        }
        var available = boardBackend.discoverGpuBenchmark()
        root.actionFeedback = available
                              ? "已找到固定 GPU 基准工具"
                              : root.errorText()
    }

    function requestStart() {
        if (!root.gpuCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用 GPU 测试"
            return
        }
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.benchmarkStopping) {
            root.actionFeedback = "正在停止 GPU 基准，请等待进程退出"
            return
        }
        if (root.benchmarkRunning) {
            root.actionFeedback = "固定离屏基准正在运行"
            return
        }
        if (root.previewRunning) {
            root.actionFeedback = "图形预览正在运行，结束后可运行固定基准"
            return
        }
        if (!boardBackend.startGpuBenchmark()) {
            root.actionFeedback = root.errorText()
            if (root.actionFeedback.length === 0 || root.actionFeedback === "无")
                root.actionFeedback = "基准未启动，请先检查工具与图形会话"
            return
        }
        root.actionFeedback = "正在运行固定 glmark2-es 离屏基准…"
    }

    function requestStop() {
        if (root.benchmarkStopping) {
            root.actionFeedback = "正在停止 GPU 基准，请等待进程退出"
            return
        }
        if (!root.benchmarkRunning) {
            root.actionFeedback = "当前没有正在运行的 GPU 基准"
            return
        }
        if (boardBackend.stopGpuBenchmark())
            root.actionFeedback = "正在停止 GPU 基准…"
        else
            root.actionFeedback = "停止请求未被接受，请等待当前进程结束"
    }

    function requestPreview() {
        if (!root.previewSurfaceReady) {
            root.actionFeedback = "请先打开 GPU 测试页，再启动内嵌图形预览"
            return
        }
        if (!root.gpuCapabilityEnabled) {
            root.actionFeedback = "当前板型未启用 GPU 测试"
            return
        }
        if (root.backendBusy) {
            root.actionFeedback = root.backendBusyFeedback()
            return
        }
        if (root.benchmarkRunning) {
            root.actionFeedback = "固定离屏基准正在运行，结束后可启动内嵌图形预览"
            return
        }
        if (root.previewRunning) {
            root.actionFeedback = "GPU 内嵌图形预览正在实时渲染"
            return
        }
        if (!root.previewAvailable) {
            root.actionFeedback = root.previewErrorText()
            return
        }
        if (!boardBackend.startGpuPreview()) {
            root.actionFeedback = root.previewErrorText()
            return
        }
        root.actionFeedback = "GPU 内嵌实时预览已启动，图形显示在当前页面"
    }

    function requestStopPreview() {
        if (!root.previewRunning) {
            root.actionFeedback = "当前没有正在运行的 GPU 内嵌预览"
            return
        }
        if (boardBackend.stopGpuPreview())
            root.actionFeedback = "已停止 GPU 内嵌预览"
        else
            root.actionFeedback = "GPU 内嵌预览停止请求未被接受，请稍候"
    }

    function stopBenchmarkForExit() {
        if (root.benchmarkRunning) {
            boardBackend.stopGpuBenchmark()
            root.actionFeedback = "已在离开页面时请求停止 GPU 基准"
        }
        if (root.previewRunning) {
            boardBackend.stopGpuPreview()
            root.actionFeedback = "已在离开页面时停止 GPU 内嵌预览"
        }
    }

    function benchmarkLog() {
        var lines = []
        lines.push("状态       " + root.stateText())
        lines.push("工具       " + root.binaryText())
        lines.push("固定参数   --off-screen  --size 640x480")
        lines.push("场景       refract:duration=3")
        lines.push("耗时       " + root.elapsedText())
        lines.push("得分       " + root.scoreText())
        lines.push("渲染器     " + root.rendererText())
        if (root.errorText() !== "无")
            lines.push("错误       " + root.errorText())
        var output = root.textValue(root.benchmark, "log", "")
        if (output.length > 0) {
            lines.push("")
            lines.push("glmark2 输出")
            lines.push(output)
        } else {
            lines.push("")
            lines.push("尚未产生基准输出。")
        }
        if (root.boolValue(root.benchmark, "logTruncated", false))
            lines.push("\n输出已截断，仅保留最近 16 KiB。")
        lines.push("")
        lines.push("GPU 内嵌实时预览")
        lines.push("状态       " + root.previewStateText())
        lines.push("渲染方式   Qt Quick ShaderEffect · 当前页面内嵌窗口")
        lines.push("显示目标   " + root.previewTargetText())
        lines.push("耗时       " + root.previewElapsedText())
        lines.push("渲染器     " + root.previewRendererText())
        var previewError = root.previewErrorText()
        if (previewError !== "无")
            lines.push("错误       " + previewError)
        var previewOutput = root.textValue(root.benchmark, "previewLog", "")
        if (previewOutput.length > 0) {
            lines.push("")
            lines.push("图形预览输出")
            lines.push(previewOutput)
        }
        if (root.boolValue(root.benchmark, "previewLogTruncated", false))
            lines.push("\n图形预览输出已截断，仅保留最近 16 KiB。")
        return lines.join("\n")
    }

    onVisibleChanged: {
        if (visible) {
            actionFeedback = root.backendBusy ? root.backendBusyFeedback() : root.stateText()
            root.refreshGpuTelemetry()
        }
    }

    onGpuTaskRunningChanged: {
        if (root.gpuTaskRunning)
            root.beginGpuTelemetrySession()
    }

    onPreviewRunningChanged: {
        // The backend state can also be changed by a control API.  Do not
        // leave a page-only workload marked as running when its surface is
        // hidden or not ready to render.
        if (root.previewRunning && !root.previewSurfaceReady) {
            boardBackend.stopGpuPreview()
            root.actionFeedback = "GPU 测试页未打开，已停止内嵌图形预览"
        }
    }

    Timer {
        id: gpuTelemetryTimer
        interval: root.benchmarkRunning || root.previewRunning ? 700 : 1500
        repeat: true
        running: root.visible && root.gpuTelemetryApi !== null
        onTriggered: root.refreshGpuTelemetry()
    }

    Connections {
        target: boardBackend
        onChanged: {
            if (!root.visible)
                return
            if (root.benchmarkStopping)
                root.actionFeedback = "正在停止 GPU 基准…"
            else if (root.backendBusy && !root.benchmarkRunning && !root.previewRunning)
                root.actionFeedback = root.backendBusyFeedback()
            else if (root.benchmarkRunning)
                root.actionFeedback = "固定离屏基准正在运行…"
            else if (root.previewRunning)
                root.actionFeedback = "当前页面正在实时渲染 GPU 图形…"
            else if (root.benchmarkState === "completed")
                root.actionFeedback = "基准运行完成，已更新得分与渲染器"
            else if (root.previewState === "completed")
                root.actionFeedback = "GPU 内嵌预览已完成"
            else if (root.benchmarkState === "failed")
                root.actionFeedback = root.errorText()
            else if (root.previewState === "failed")
                root.actionFeedback = root.previewErrorText()
        }

        onOperationStateChanged: {
            if (!root.visible)
                return
            if (root.benchmarkStopping)
                root.actionFeedback = "正在停止 GPU 基准…"
            else if (root.backendBusy && !root.benchmarkRunning && !root.previewRunning)
                root.actionFeedback = root.backendBusyFeedback()
            else if (!root.benchmarkRunning && !root.previewRunning)
                root.actionFeedback = root.stateText()
        }

        onOperationFinished: {
            if (operation !== "gpu-start" && operation !== "gpu-preview-start")
                return
            if (operation === "gpu-preview-start") {
                root.actionFeedback = success
                                      ? "GPU 内嵌预览已停止"
                                      : (status && String(status).length > 0 ? String(status) : root.previewErrorText())
            } else {
                var stopped = root.benchmarkState === "stopped"
                              || (status && String(status).trim().toLowerCase() === "stopped")
                root.actionFeedback = success
                                      ? (stopped ? "GPU 基准已停止" : "GPU 基准已完成")
                                      : (status && String(status).length > 0 ? String(status) : root.errorText())
            }
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
                text: "GPU 测试"
                color: theme.instrumentInk
                elide: Text.ElideRight
                font.pixelSize: Math.round((root.compactLayout ? 24 : 28) * root.uiScale)
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
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(10 * root.uiScale)

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
        id: gpuPage
        objectName: "gpu-test-single-page"
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
            policy: gpuPage.contentHeight > gpuPage.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: contentColumn
            x: Math.max(0, Math.round((gpuPage.width - width) / 2))
            width: root.resolvedWorkSurfaceWidth
            spacing: Math.round(16 * root.uiScale)

            Rectangle {
                id: controlBand
                width: parent.width
                height: Math.max(root.compactLayout ? Math.round(138 * root.uiScale) : Math.round(72 * root.uiScale),
                                 actionFlow.implicitHeight + Math.round(24 * root.uiScale))
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Flow {
                    id: actionFlow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Math.round(12 * root.uiScale)
                    height: implicitHeight
                    spacing: Math.round(12 * root.uiScale)

                    Column {
                        width: root.compactLayout ? parent.width : Math.min(Math.round(360 * root.uiScale), parent.width)
                        height: Math.round(48 * root.uiScale)
                        spacing: Math.round(2 * root.uiScale)

                        Text {
                            text: "操作"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(14 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            width: parent.width
                            text: "固定场景与参数，结果便于比较"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(102 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: "检查工具"
                        accent: theme.warning
                        variant: "secondary"
                        enabled: root.gpuCapabilityEnabled && !root.benchmarkRunning && !root.previewRunning && !root.backendBusy
                        onClicked: root.requestDiscovery()
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(126 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        // BoardToolsActionButton consumes pointer and keyboard
                        // activation while busy, so a stopping request cannot
                        // be submitted twice before the process exits.
                        text: root.benchmarkStopping ? "正在停止" : (root.benchmarkRunning ? "停止固定基准" : "运行固定基准")
                        accent: theme.accent
                        variant: root.benchmarkRunning ? "stop" : "primary"
                        busy: root.benchmarkStopping
                        enabled: root.benchmarkRunning || (root.gpuCapabilityEnabled && !root.previewRunning && !root.backendBusy)
                        onClicked: {
                            if (root.benchmarkRunning)
                                root.requestStop()
                            else
                                root.requestStart()
                        }
                    }

                    BoardToolsActionButton {
                        uiScale: root.uiScale
                        width: Math.round(190 * root.uiScale)
                        height: Math.round(48 * root.uiScale)
                        text: root.previewRunning ? "停止内嵌 GPU 预览" : "启动内嵌 GPU 预览"
                        accent: theme.accent
                        variant: root.previewRunning ? "stop" : "secondary"
                        enabled: root.previewRunning || (root.gpuCapabilityEnabled && root.previewAvailable && !root.benchmarkRunning && !root.backendBusy)
                        onClicked: {
                            if (root.previewRunning)
                                root.requestStopPreview()
                            else
                                root.requestPreview()
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: Math.round(24 * root.uiScale)

                Text {
                    id: resultHeading
                    anchors.left: parent.left
                    anchors.right: resultSummary.left
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "读数与结果"
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(17 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    id: resultSummary
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(Math.round(parent.width * 0.62), implicitWidth)
                    text: "固定离屏基准 · 640 x 480 · 3 秒"
                    color: theme.muted
                    elide: Text.ElideLeft
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Math.round(11 * root.uiScale)
                }
            }

            Grid {
                id: resultGrid
                width: parent.width
                columns: root.width < Math.round(760 * root.uiScale) ? 1 : (root.width < Math.round(1050 * root.uiScale) ? 2 : 4)
                rowSpacing: Math.round(12 * root.uiScale)
                columnSpacing: Math.round(12 * root.uiScale)

                Repeater {
                    model: [
                        { "label": "glmark2 得分", "value": root.gpuCapabilityEnabled ? root.scoreText() : "--", "detail": root.textValue(root.benchmark, "finishedAt", "尚未完成"), "accent": theme.warning },
                        { "label": "本次耗时", "value": root.gpuCapabilityEnabled ? root.elapsedText() : "--", "detail": root.textValue(root.benchmark, "exitStatus", "尚未运行") + " · code " + String(root.numberValue(root.benchmark, "exitCode", -1)), "accent": theme.focus },
                        { "label": "GL 渲染器", "value": root.gpuCapabilityEnabled ? root.rendererText() : "--", "detail": root.gpuCapabilityEnabled ? root.binaryText() : "当前板型未启用", "accent": theme.accent },
                        { "label": "内嵌 GPU 预览", "value": root.gpuCapabilityEnabled ? root.previewOperatorStatusText() : "--", "detail": root.previewTargetText(), "accent": root.previewAccent() }
                    ]

                    delegate: Rectangle {
                        width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                        height: Math.round(126 * root.uiScale)
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
                            text: modelData.value
                            color: theme.inkStrong
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(modelData.label === "GL 渲染器" || modelData.label === "内嵌 GPU 预览" ? 17 * root.uiScale : 25 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Math.round(16 * root.uiScale)
                            anchors.rightMargin: Math.round(16 * root.uiScale)
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Math.round(14 * root.uiScale)
                            text: modelData.detail
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }
                }
            }

            Rectangle {
                id: embeddedPreviewBand
                objectName: "gpu-embedded-preview-card"
                width: parent.width
                height: root.previewBandHeight
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Text {
                    id: embeddedPreviewTitle
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * root.uiScale)
                    anchors.right: embeddedPreviewStatus.left
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(12 * root.uiScale)
                    text: "实时 GPU 图形预览"
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(14 * root.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    id: embeddedPreviewStatus
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: embeddedPreviewTitle.verticalCenter
                    width: Math.min(Math.round(parent.width * 0.58), implicitWidth)
                    text: root.previewRunning ? "实时渲染 · 点击停止后保留驱动趋势" : "不打开独立 Weston 窗口"
                    color: theme.muted
                    elide: Text.ElideLeft
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Math.round(11 * root.uiScale)
                }

                GpuPreviewSurface {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: embeddedPreviewTitle.bottom
                    anchors.bottom: parent.bottom
                    anchors.margins: Math.round(12 * root.uiScale)
                    anchors.topMargin: Math.round(10 * root.uiScale)
                    active: root.previewRunning && root.previewSurfaceReady
                    uiScale: root.uiScale
                }
            }

            Rectangle {
                id: gpuTelemetryBand
                objectName: "gpu-driver-status-chart"
                width: parent.width
                height: gpuTelemetryContent.height + Math.round(24 * root.uiScale)
                radius: Math.min(8, Math.round(8 * root.uiScale))
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    id: gpuTelemetryContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: Math.round(10 * root.uiScale)

                    Item {
                        width: parent.width
                        height: Math.round(24 * root.uiScale)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "GPU 驱动状态图"
                            color: theme.inkStrong
                            font.pixelSize: Math.round(14 * root.uiScale)
                            font.weight: Font.DemiBold
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.gpuGraphPeakText()
                            color: theme.muted
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.compactLayout ? 1 : 3
                        rowSpacing: Math.round(8 * root.uiScale)
                        columnSpacing: Math.round(8 * root.uiScale)

                        Repeater {
                            model: [
                                { "label": "当前驱动负载", "value": root.gpuLoadText(), "accent": theme.warning },
                                { "label": "GPU 核心频率", "value": root.gpuFrequencyText(), "accent": theme.focus },
                                { "label": "任务状态", "value": root.gpuTelemetryStateText(), "accent": root.benchmarkRunning || root.previewRunning ? theme.accent : theme.muted }
                            ]

                            delegate: Rectangle {
                                width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                                height: Math.round(62 * root.uiScale)
                                radius: Math.min(6, Math.round(6 * root.uiScale))
                                color: theme.surfaceSoft
                                border.color: theme.rule
                                border.width: 1

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.max(3, Math.round(4 * root.uiScale))
                                    radius: width / 2
                                    color: modelData.accent
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Math.round(12 * root.uiScale)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Math.round(12 * root.uiScale)
                                    anchors.top: parent.top
                                    anchors.topMargin: Math.round(8 * root.uiScale)
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
                                    anchors.bottomMargin: Math.round(8 * root.uiScale)
                                    text: modelData.value
                                    color: theme.ink
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(modelData.label === "任务状态" ? 11 * root.uiScale : 18 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: gpuLoadChart
                        width: parent.width
                        height: root.telemetryChartHeight
                        radius: Math.min(6, Math.round(6 * root.uiScale))
                        color: theme.surfaceSoft
                        border.color: theme.rule
                        border.width: 1
                        clip: true
                        readonly property int plotLeftMargin: Math.round(38 * root.uiScale)
                        readonly property int plotRightMargin: Math.round(12 * root.uiScale)
                        readonly property int plotTopMargin: Math.round(30 * root.uiScale)
                        readonly property int plotBottomMargin: Math.round(12 * root.uiScale)
                        readonly property real plotHeight: Math.max(1, height - plotTopMargin - plotBottomMargin)

                        Repeater {
                            model: [100, 50, 0]
                            Item {
                                width: parent.width
                                height: 1
                                y: gpuLoadChart.plotTopMargin
                                   + (1 - Number(modelData) / 100) * gpuLoadChart.plotHeight

                                Text {
                                    anchors.right: ruleLine.left
                                    anchors.rightMargin: Math.round(7 * root.uiScale)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: String(modelData)
                                    color: theme.inkFaint
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(9 * root.uiScale)
                                }

                                Rectangle {
                                    id: ruleLine
                                    anchors.left: parent.left
                                    anchors.leftMargin: gpuLoadChart.plotLeftMargin
                                    anchors.right: parent.right
                                    anchors.rightMargin: gpuLoadChart.plotRightMargin
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 1
                                    color: theme.rule
                                }
                            }
                        }

                        Text {
                            id: gpuLoadChartTitle
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(10 * root.uiScale)
                            anchors.right: gpuGraphSampleSummary.left
                            anchors.rightMargin: Math.round(10 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(7 * root.uiScale)
                            text: "Mali 驱动负载 / 任务期"
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                        }

                        Text {
                            id: gpuGraphSampleSummary
                            anchors.right: parent.right
                            anchors.rightMargin: Math.round(10 * root.uiScale)
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(7 * root.uiScale)
                            width: Math.min(Math.round(parent.width * 0.58), implicitWidth)
                            text: root.gpuGraphSampleLabel()
                            color: theme.muted
                            elide: Text.ElideLeft
                            horizontalAlignment: Text.AlignRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                        }

                        Row {
                            id: gpuLoadBars
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: gpuLoadChart.plotLeftMargin
                            anchors.rightMargin: gpuLoadChart.plotRightMargin
                            anchors.topMargin: gpuLoadChart.plotTopMargin
                            anchors.bottomMargin: gpuLoadChart.plotBottomMargin
                            spacing: Math.max(2, Math.round(3 * root.uiScale))
                            property var samples: root.gpuGraphSamples()
                            property int sampleCount: samples.length

                            Repeater {
                                model: gpuLoadBars.samples

                                delegate: Item {
                                    readonly property real sample: Number(modelData)
                                    width: Math.max(2, (gpuLoadBars.width
                                                        - gpuLoadBars.spacing * Math.max(0, gpuLoadBars.sampleCount - 1))
                                                       / Math.max(1, gpuLoadBars.sampleCount))
                                    height: gpuLoadBars.height

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.bottom
                                        width: Math.max(2, parent.width)
                                        height: parent.sample < 0 ? 2
                                               : Math.max(3, parent.height * Math.max(0, Math.min(100, parent.sample)) / 100)
                                        radius: Math.min(width / 2, Math.round(2 * root.uiScale))
                                        // Bar height already expresses utilization.  A high load is
                                        // expected during a benchmark, so it must not look like failure.
                                        color: parent.sample < 0 ? theme.rule : theme.focus
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: root.gpuLoadHistory.length === 0
                            text: root.gpuTaskRunning ? "等待第一个 Mali 驱动样本" : "任务开始后记录本次驱动负载"
                            color: theme.inkFaint
                            font.family: theme.bodyFont
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    Text {
                        width: parent.width
                        text: root.gpuTelemetryPath.length > 0
                              ? root.gpuTelemetryStateText() + " · 数据源 " + root.gpuTelemetryPath
                                + " · 仅在 GPU 任务期间每 0.7 秒采样；灰色短线表示读取断点，停止后保留本次趋势。"
                              : root.gpuTelemetryStateText()
                        color: root.gpuTelemetryAvailable ? theme.muted : theme.warning
                        wrapMode: Text.WordWrap
                        font.pixelSize: Math.round(11 * root.uiScale)
                        lineHeight: 1.2
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
                    spacing: Math.round(10 * root.uiScale)

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
                            color: root.previewRunning ? root.previewAccent() : root.stateAccent()
                        }

                        Text {
                            width: Math.max(0, parent.width - x)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.previewRunning ? root.previewOperatorStatusText() : root.stateText()
                            color: theme.muted
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(11 * root.uiScale)
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.width < Math.round(760 * root.uiScale) ? 1 : 2
                        rowSpacing: Math.round(8 * root.uiScale)
                        columnSpacing: Math.round(18 * root.uiScale)

                        Repeater {
                            model: [
                                { "label": "基准工具", "value": root.binaryText() },
                                { "label": "预览显示位置", "value": root.previewTargetText() }
                            ]

                            delegate: Row {
                                width: (parent.width - parent.columnSpacing * Math.max(0, parent.columns - 1)) / parent.columns
                                height: Math.round(22 * root.uiScale)
                                spacing: Math.round(8 * root.uiScale)

                                Text {
                                    text: modelData.label
                                    color: theme.muted
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: Math.max(0, parent.width - x)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.value
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                    font.pixelSize: Math.round(11 * root.uiScale)
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.errorText() !== "无"
                        text: root.errorText()
                        color: theme.danger
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }

                    Text {
                        width: parent.width
                        visible: root.previewWarningText().length > 0
                                 && root.previewWarningText() !== root.errorText()
                        text: root.previewWarningText()
                        color: theme.warning
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(11 * root.uiScale)
                    }
                }
            }
        }
    }
}
