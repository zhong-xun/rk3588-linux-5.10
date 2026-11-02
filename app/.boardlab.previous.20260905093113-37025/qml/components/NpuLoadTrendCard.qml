import QtQuick 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    property real uiScale: 1.0
    property int coreIndex: 0
    property string coreLabel: "NPU 0"
    property string coreDetail: "核心 0"
    property real percent: -1
    property var samples: []
    property bool available: false
    property string unavailableText: "当前未取得核心负载"
    property color accent: theme.focus
    property bool compact: false
    // Keep the Qt trace aligned with the service-owned history limit.
    property int historyWindowSize: 60

    objectName: "npu-load-chart-" + String(coreIndex)
    // The surrounding Grid controls the final height, while this remains a
    // sensible fallback for direct use.
    implicitHeight: Math.round((compact ? 172 : 190) * root.uiScale)
    height: implicitHeight

    readonly property bool hasLiveValue: root.available && root.validPercent(root.percent)
    readonly property int validSampleCount: root.countValidSamples()
    readonly property bool hasTrend: root.available && root.validSampleCount >= 2
    readonly property bool hasData: root.available && (root.hasLiveValue || root.validSampleCount > 0)
    readonly property int visibleStartIndex: Math.max(0, (root.samples ? root.samples.length : 0)
                                                      - root.historyWindowSize)
    readonly property int visibleSampleCount: Math.min(root.historyWindowSize,
                                                       root.samples ? root.samples.length : 0)

    function validPercent(value) {
        var numeric = Number(value)
        return !isNaN(numeric) && numeric >= 0 && numeric <= 100
    }

    function normalizedPercent(value) {
        return Math.max(0, Math.min(100, Number(value)))
    }

    function accentRgba(opacity) {
        return root.colorRgba(root.accent, opacity)
    }

    function colorRgba(color, opacity) {
        return "rgba(" + String(Math.round(color.r * 255)) + ", "
                + String(Math.round(color.g * 255)) + ", "
                + String(Math.round(color.b * 255)) + ", "
                + String(opacity) + ")"
    }

    function sampleValue(index) {
        if (!root.samples || index < 0 || index >= root.samples.length)
            return -1
        var value = Number(root.samples[index])
        return root.validPercent(value) ? value : -1
    }

    function countValidSamples() {
        if (!root.samples)
            return 0
        var count = 0
        for (var index = root.visibleStartIndex; index < root.samples.length; ++index) {
            if (root.validPercent(root.samples[index]))
                ++count
        }
        return count
    }

    function currentValueText() {
        if (!root.hasLiveValue)
            return "--"
        return String(Math.round(root.normalizedPercent(root.percent))) + "%"
    }

    function emptyText() {
        if (!root.available)
            return root.unavailableText.length > 0 ? root.unavailableText : "RKNPU 负载不可读"
        return "等待核心 " + String(root.coreIndex) + " 的采样数据"
    }

    function samplingStateText() {
        if (!root.available)
            return "遥测不可用"
        if (root.hasLiveValue)
            return "实时采样"
        return "等待采样"
    }

    function historyText() {
        if (root.visibleSampleCount <= 0)
            return "等待样本"
        return "最近 " + String(root.visibleSampleCount) + " 个采样点"
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(8, Math.round(8 * root.uiScale))
        color: root.hasData ? theme.surfaceRaised : theme.surfaceSoft
        border.color: root.hasData ? root.accentRgba(0.32) : theme.rule
        border.width: 1

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.max(3, Math.round(3 * root.uiScale))
            radius: parent.radius
            color: root.hasData ? root.accent : theme.ruleStrong
        }

        Rectangle {
            id: accentMark
            anchors.left: parent.left
            anchors.leftMargin: Math.round(12 * root.uiScale)
            anchors.top: parent.top
            anchors.topMargin: Math.round(17 * root.uiScale)
            width: Math.max(5, Math.round(6 * root.uiScale))
            height: width
            radius: width / 2
            color: root.hasData ? root.accent : theme.inkFaint
        }

        Text {
            id: titleLabel
            anchors.left: accentMark.right
            anchors.leftMargin: Math.round(7 * root.uiScale)
            anchors.right: currentValue.left
            anchors.rightMargin: Math.round(6 * root.uiScale)
            anchors.top: parent.top
            anchors.topMargin: Math.round(12 * root.uiScale)
            text: root.coreLabel
            color: theme.ink
            elide: Text.ElideRight
            font.pixelSize: Math.round(11 * root.uiScale)
            font.weight: Font.DemiBold
        }

        Text {
            id: coreDetailLabel
            anchors.left: titleLabel.left
            anchors.right: titleLabel.right
            anchors.top: titleLabel.bottom
            anchors.topMargin: Math.round(1 * root.uiScale)
            text: root.coreDetail.length > 0 ? root.coreDetail : "独立核心负载"
            color: theme.muted
            elide: Text.ElideRight
            font.pixelSize: Math.round(10 * root.uiScale)
        }

        Text {
            id: currentValue
            anchors.right: parent.right
            anchors.rightMargin: Math.round(12 * root.uiScale)
            anchors.top: parent.top
            anchors.topMargin: Math.round(11 * root.uiScale)
            text: root.currentValueText()
            color: root.hasLiveValue ? root.accent : theme.inkFaint
            horizontalAlignment: Text.AlignRight
            font.pixelSize: Math.round((root.compact ? 22 : 25) * root.uiScale)
            font.weight: Font.Bold
        }

        Text {
            id: samplingState
            anchors.right: currentValue.right
            anchors.top: currentValue.bottom
            anchors.topMargin: Math.round(1 * root.uiScale)
            text: root.samplingStateText()
            color: root.hasData ? root.accent : theme.inkFaint
            horizontalAlignment: Text.AlignRight
            font.pixelSize: Math.round(9 * root.uiScale)
        }

        Item {
            id: plotArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Math.round(10 * root.uiScale)
            anchors.rightMargin: Math.round(10 * root.uiScale)
            anchors.top: coreDetailLabel.bottom
            anchors.topMargin: Math.round(10 * root.uiScale)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.round(9 * root.uiScale)

            Canvas {
                id: trendCanvas
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                contextType: "2d"
                antialiasing: true

                function requestRedraw() {
                    requestPaint()
                }

                onPaint: {
                    var context = getContext("2d")
                    var canvasWidth = width
                    var canvasHeight = height
                    context.clearRect(0, 0, canvasWidth, canvasHeight)
                    if (canvasWidth <= 1 || canvasHeight <= 1)
                        return

                    var labelWidth = Math.max(24, Math.round(25 * root.uiScale))
                    var plotRightPadding = Math.max(4, Math.round(6 * root.uiScale))
                    var plotTop = Math.max(7, Math.round(8 * root.uiScale))
                    var plotBottomPadding = Math.max(15, Math.round(17 * root.uiScale))
                    var plotLeft = labelWidth
                    var plotRight = canvasWidth - plotRightPadding
                    var plotWidth = Math.max(1, plotRight - plotLeft)
                    var plotHeight = Math.max(1, canvasHeight - plotTop - plotBottomPadding)
                    var plotBottom = plotTop + plotHeight

                    context.font = Math.max(8, Math.round(8 * root.uiScale)) + "px monospace"
                    context.textBaseline = "middle"
                    context.fillStyle = root.hasData
                            ? root.colorRgba(theme.muted, 1)
                            : root.colorRgba(theme.inkFaint, 1)
                    context.fillText("100", 0, plotTop + 1)
                    context.fillText("50", Math.max(4, labelWidth - Math.round(12 * root.uiScale)),
                                     plotTop + plotHeight / 2)
                    context.fillText("0", Math.max(9, labelWidth - Math.round(7 * root.uiScale)), plotBottom)
                    context.textBaseline = "alphabetic"
                    context.fillText("历史", plotLeft, canvasHeight - Math.max(2, Math.round(3 * root.uiScale)))
                    context.textAlign = "right"
                    context.fillText("当前", plotRight, canvasHeight - Math.max(2, Math.round(3 * root.uiScale)))
                    context.textAlign = "left"

                    context.lineWidth = 1
                    for (var gridIndex = 0; gridIndex <= 2; ++gridIndex) {
                        var gridY = plotTop + plotHeight * gridIndex / 2
                        context.strokeStyle = gridIndex === 1
                                ? (root.hasData ? root.accentRgba(0.16) : root.colorRgba(theme.ruleStrong, 0.16))
                                : (root.hasData ? root.colorRgba(theme.ruleStrong, 0.20) : root.colorRgba(theme.ruleStrong, 0.15))
                        context.beginPath()
                        context.moveTo(plotLeft, Math.round(gridY) + 0.5)
                        context.lineTo(plotRight, Math.round(gridY) + 0.5)
                        context.stroke()
                    }

                    var total = root.samples ? root.samples.length : 0
                    if (!root.hasTrend || root.visibleSampleCount < 2)
                        return

                    var segment = []
                    var latestValidPoint = null
                    function flushSegment() {
                        if (segment.length < 2) {
                            segment = []
                            return
                        }

                        var gradient = context.createLinearGradient(0, plotTop, 0, plotBottom)
                        gradient.addColorStop(0, root.accentRgba(0.30))
                        gradient.addColorStop(0.62, root.accentRgba(0.11))
                        gradient.addColorStop(1, root.accentRgba(0.01))

                        context.beginPath()
                        context.moveTo(segment[0].x, plotBottom)
                        for (var fillIndex = 0; fillIndex < segment.length; ++fillIndex)
                            context.lineTo(segment[fillIndex].x, segment[fillIndex].y)
                        context.lineTo(segment[segment.length - 1].x, plotBottom)
                        context.closePath()
                        context.fillStyle = gradient
                        context.fill()

                        context.beginPath()
                        context.moveTo(segment[0].x, segment[0].y)
                        for (var lineIndex = 1; lineIndex < segment.length; ++lineIndex)
                            context.lineTo(segment[lineIndex].x, segment[lineIndex].y)
                        context.strokeStyle = root.accentRgba(1)
                        context.lineWidth = Math.max(1.5, 2 * root.uiScale)
                        context.lineJoin = "round"
                        context.lineCap = "round"
                        context.stroke()
                        segment = []
                    }

                    for (var index = root.visibleStartIndex; index < total; ++index) {
                        var value = root.sampleValue(index)
                        if (!root.validPercent(value)) {
                            flushSegment()
                            continue
                        }
                        // The Web dashboard fills the plotting span as soon as
                        // samples arrive. Do the same here: sparse history is
                        // immediately legible instead of clustering at right.
                        var localIndex = index - root.visibleStartIndex
                        var x = plotLeft + localIndex * plotWidth
                                / Math.max(1, root.visibleSampleCount - 1)
                        var y = plotBottom - root.normalizedPercent(value) * plotHeight / 100
                        latestValidPoint = { "x": x, "y": y }
                        segment.push(latestValidPoint)
                    }
                    flushSegment()

                    if (latestValidPoint !== null) {
                        context.beginPath()
                        context.fillStyle = root.accentRgba(0.20)
                        context.arc(latestValidPoint.x, latestValidPoint.y,
                                    Math.max(4.8, 5.6 * root.uiScale), 0, Math.PI * 2, false)
                        context.fill()
                        context.beginPath()
                        context.fillStyle = root.colorRgba(theme.surfaceRaised, 1)
                        context.arc(latestValidPoint.x, latestValidPoint.y,
                                    Math.max(3.2, 3.8 * root.uiScale), 0, Math.PI * 2, false)
                        context.fill()
                        context.beginPath()
                        context.fillStyle = root.accent
                        context.arc(latestValidPoint.x, latestValidPoint.y,
                                    Math.max(1.9, 2.3 * root.uiScale), 0, Math.PI * 2, false)
                        context.fill()
                    }
                }

                Component.onCompleted: requestRedraw()
                onWidthChanged: requestRedraw()
                onHeightChanged: requestRedraw()
                onVisibleChanged: requestRedraw()
            }

            Text {
                anchors.left: trendCanvas.left
                anchors.right: trendCanvas.right
                anchors.verticalCenter: trendCanvas.verticalCenter
                anchors.leftMargin: Math.round(28 * root.uiScale)
                anchors.rightMargin: Math.round(4 * root.uiScale)
                visible: !root.hasTrend
                text: root.hasData ? "正在积累趋势数据" : root.emptyText()
                color: root.hasData ? theme.inkFaint : theme.muted
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.pixelSize: Math.round(10 * root.uiScale)
            }

            Text {
                anchors.left: trendCanvas.left
                anchors.leftMargin: Math.round(28 * root.uiScale)
                anchors.right: trendCanvas.right
                anchors.rightMargin: Math.round(5 * root.uiScale)
                anchors.bottom: trendCanvas.bottom
                anchors.bottomMargin: Math.round(1 * root.uiScale)
                visible: root.hasTrend
                text: root.historyText()
                color: theme.muted
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.round(8 * root.uiScale)
            }
        }
    }

    onSamplesChanged: trendCanvas.requestRedraw()
    onPercentChanged: trendCanvas.requestRedraw()
    onAvailableChanged: trendCanvas.requestRedraw()
    onAccentChanged: trendCanvas.requestRedraw()
}
