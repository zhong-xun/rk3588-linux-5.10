import QtQuick 2.12
import QtQuick.Controls 2.12

// V2 Signal Workbench diagnostics surface.
Item {
    id: root
    signal closed()
    property real uiScale: 1.0

    BoardLabTheme { id: theme }

    function value(map, key, fallback) {
        if (!map || map[key] === undefined || map[key] === null || String(map[key]).length === 0)
            return fallback
        return String(map[key])
    }

    function stateText(map) {
        if (map && map.available)
            return "可读取"
        if (map && map.authorizationRequired)
            return "需要授权"
        return "未发现"
    }

    function stateColor(map) {
        if (map && map.available)
            return theme.ready
        if (map && map.authorizationRequired)
            return theme.warning
        return theme.inkFaint
    }

    function itemCount(items, unit) {
        return (items ? items.length : 0) + " " + unit
    }

    function firstValue(items, key, fallback) {
        if (!items || items.length === 0)
            return fallback
        var value = items[0][key]
        return value === undefined || value === null || String(value).length === 0 ? fallback : String(value)
    }

    function joinedValues(items, formatter, fallback) {
        if (!items || items.length === 0)
            return fallback
        var values = []
        for (var i = 0; i < items.length; ++i)
            values.push(formatter(items[i]))
        return values.join("；")
    }

    Rectangle { anchors.fill: parent; color: theme.canvas }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(92 * root.uiScale)
        color: theme.instrument
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Math.round(30 * root.uiScale)
        spacing: Math.round(14 * root.uiScale)

        Row {
            width: parent.width
            height: Math.round(48 * root.uiScale)

            Column {
                width: parent.width - actions.width - Math.round(20 * root.uiScale)
                spacing: Math.round(3 * root.uiScale)
                Text { text: "系统诊断"; color: theme.instrumentInk; font.family: theme.displayFont; font.pixelSize: Math.round(30 * root.uiScale); font.weight: Font.Bold }
                Text { text: "仅读取板端时钟与 RKNPU 状态，不修改时间或电源模式"; color: theme.instrumentMuted; font.family: theme.bodyFont; font.pixelSize: Math.round(13 * root.uiScale) }
            }

            Row {
                id: actions
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(10 * root.uiScale)
                BoardToolsActionButton {
                    uiScale: root.uiScale; width: Math.round(108 * root.uiScale); height: Math.max(44, Math.round(44 * root.uiScale))
                    text: "重新读取"; variant: "primary"
                    onClicked: boardBackend.refresh()
                }
                BoardToolsActionButton {
                    uiScale: root.uiScale; width: Math.round(108 * root.uiScale); height: Math.max(44, Math.round(44 * root.uiScale))
                    text: "返回桌面"; variant: "secondary"
                    onClicked: root.closed()
                }
            }
        }

        Rectangle {
            id: outputCard
            width: parent.width; height: Math.round(126 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
            color: theme.surfaceRaised; border.color: theme.rule; border.width: 1
            Column { anchors.fill: parent; anchors.margins: Math.round(16 * root.uiScale); spacing: Math.round(6 * root.uiScale)
                Row { width: parent.width
                    Text { text: "显示输出"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(18 * root.uiScale); font.weight: Font.DemiBold; width: parent.width - outputCount.width }
                    Rectangle { id: outputCount; width: Math.round(82 * root.uiScale); height: Math.round(24 * root.uiScale); radius: height / 2; color: theme.ready
                        Text { anchors.centerIn: parent; text: root.itemCount(boardBackend.displayOutputs, "接口"); color: theme.inkStrong; font.family: theme.bodyFont; font.pixelSize: Math.round(10 * root.uiScale); font.weight: Font.Bold }
                    }
                }
                Text { text: root.firstValue(boardBackend.displayOutputs, "connector", "未发现 DRM 输出"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(16 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                Text { text: root.joinedValues(boardBackend.displayOutputs, function(item) { return String(item.connector || "DRM") + " · " + String(item.status || "unknown") }, "仅读取 connector 状态；不会切换显示模式"); color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale); elide: Text.ElideRight; width: parent.width }
            }
        }

        Rectangle {
            width: parent.width; height: Math.round(76 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
            color: theme.surfaceSoft; border.color: theme.rule; border.width: 1
            Row {
                anchors.fill: parent; anchors.margins: Math.round(16 * root.uiScale); spacing: Math.round(30 * root.uiScale)
                Column { width: Math.round(250 * root.uiScale); spacing: 4
                    Text { text: "当前板型"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: boardProfile.displayName + " · " + boardProfile.socFamily; color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(17 * root.uiScale); font.weight: Font.DemiBold }
                }
                Column { width: Math.round(390 * root.uiScale); spacing: 4
                    Text { text: "设备树"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: boardProfile.detectionSource + " · " + (boardProfile.deviceTreeModel || "未读取模型"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(13 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                }
            }
        }

        Row {
            width: parent.width; spacing: Math.round(14 * root.uiScale)

            Rectangle {
                width: (parent.width - parent.spacing) / 2; height: Math.round(262 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised; border.color: theme.rule; border.width: 1
                Column {
                    anchors.fill: parent; anchors.margins: Math.round(18 * root.uiScale); spacing: Math.round(12 * root.uiScale)
                    Row { width: parent.width
                        Text { text: "RTC 硬件时钟"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(20 * root.uiScale); font.weight: Font.DemiBold; width: parent.width - rtcState.width }
                        Rectangle { id: rtcState; width: Math.round(82 * root.uiScale); height: Math.round(26 * root.uiScale); radius: height / 2; color: root.stateColor(boardBackend.rtcStatus)
                            Text { anchors.centerIn: parent; text: root.stateText(boardBackend.rtcStatus); color: theme.inkStrong; font.family: theme.bodyFont; font.pixelSize: Math.round(11 * root.uiScale); font.weight: Font.Bold }
                        }
                    }
                    Text { text: "系统 UTC"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: root.value(boardBackend.rtcStatus, "systemUtc", "等待读取"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(17 * root.uiScale) }
                    Text { text: "硬件 UTC"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: root.value(boardBackend.rtcStatus, "hardwareUtc", "未授权或不可读"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(16 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                    Text { text: root.value(boardBackend.rtcStatus, "status", "等待读取"); color: root.stateColor(boardBackend.rtcStatus); font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale); wrapMode: Text.Wrap; width: parent.width }
                }
            }

            Rectangle {
                width: (parent.width - parent.spacing) / 2; height: Math.round(262 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised; border.color: theme.rule; border.width: 1
                Column {
                    anchors.fill: parent; anchors.margins: Math.round(18 * root.uiScale); spacing: Math.round(12 * root.uiScale)
                    Row { width: parent.width
                        Text { text: "RKNPU 状态"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(20 * root.uiScale); font.weight: Font.DemiBold; width: parent.width - npuState.width }
                        Rectangle { id: npuState; width: Math.round(82 * root.uiScale); height: Math.round(26 * root.uiScale); radius: height / 2; color: root.stateColor(boardBackend.npuStatus)
                            Text { anchors.centerIn: parent; text: root.stateText(boardBackend.npuStatus); color: theme.inkStrong; font.family: theme.bodyFont; font.pixelSize: Math.round(11 * root.uiScale); font.weight: Font.Bold }
                        }
                    }
                    Text { text: "驱动版本"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: root.value(boardBackend.npuStatus, "version", "未授权或不可读"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(16 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                    Text { text: "负载 / 电源"; color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale) }
                    Text { text: root.value(boardBackend.npuStatus, "load", "--") + "  ·  " + root.value(boardBackend.npuStatus, "power", "--"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(15 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                    Text { text: root.value(boardBackend.npuStatus, "status", "等待读取"); color: root.stateColor(boardBackend.npuStatus); font.family: theme.bodyFont; font.pixelSize: Math.round(12 * root.uiScale); wrapMode: Text.Wrap; width: parent.width }
                }
            }
        }

        Row {
            width: parent.width; spacing: Math.round(14 * root.uiScale)
            Rectangle {
                width: (parent.width - parent.spacing) / 2; height: Math.round(126 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised; border.color: theme.rule; border.width: 1
                Column { anchors.fill: parent; anchors.margins: Math.round(16 * root.uiScale); spacing: Math.round(6 * root.uiScale)
                    Row { width: parent.width
                        Text { text: "Wi-Fi 状态"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(18 * root.uiScale); font.weight: Font.DemiBold; width: parent.width - wifiState.width }
                        Rectangle { id: wifiState; width: Math.round(82 * root.uiScale); height: Math.round(24 * root.uiScale); radius: height / 2; color: root.stateColor(boardBackend.wifiStatus)
                            Text { anchors.centerIn: parent; text: root.stateText(boardBackend.wifiStatus); color: theme.inkStrong; font.family: theme.bodyFont; font.pixelSize: Math.round(10 * root.uiScale); font.weight: Font.Bold }
                        }
                    }
                    Text { text: root.value(boardBackend.wifiStatus, "device", "未发现") + " · " + root.value(boardBackend.wifiStatus, "state", "--"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(16 * root.uiScale) }
                    Text { text: root.value(boardBackend.wifiStatus, "connection", "未连接"); color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(13 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                }
            }
            Rectangle {
                width: (parent.width - parent.spacing) / 2; height: Math.round(126 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised; border.color: theme.rule; border.width: 1
                Column { anchors.fill: parent; anchors.margins: Math.round(16 * root.uiScale); spacing: Math.round(6 * root.uiScale)
                    Row { width: parent.width
                        Text { text: "蓝牙状态"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(18 * root.uiScale); font.weight: Font.DemiBold; width: parent.width - bluetoothState.width }
                        Rectangle { id: bluetoothState; width: Math.round(82 * root.uiScale); height: Math.round(24 * root.uiScale); radius: height / 2; color: root.stateColor(boardBackend.bluetoothStatus)
                            Text { anchors.centerIn: parent; text: root.stateText(boardBackend.bluetoothStatus); color: theme.inkStrong; font.family: theme.bodyFont; font.pixelSize: Math.round(10 * root.uiScale); font.weight: Font.Bold }
                        }
                    }
                    Text { text: root.value(boardBackend.bluetoothStatus, "controller", "未发现"); color: theme.ink; font.family: theme.monoFont; font.pixelSize: Math.round(16 * root.uiScale) }
                    Text { text: boardBackend.bluetoothStatus && boardBackend.bluetoothStatus.powered ? "控制器已开启；未执行扫描或配对" : root.value(boardBackend.bluetoothStatus, "status", "等待读取"); color: theme.muted; font.family: theme.bodyFont; font.pixelSize: Math.round(13 * root.uiScale); elide: Text.ElideRight; width: parent.width }
                }
            }
        }

        Rectangle {
            width: parent.width; height: Math.round(64 * root.uiScale); radius: Math.round(theme.radiusPanel * root.uiScale)
            color: theme.warningSoft; border.color: theme.warning; border.width: 1
            Text { anchors.fill: parent; anchors.margins: Math.round(14 * root.uiScale); text: "无线诊断不会扫描、联网、创建热点或配对。需要修改 Wi-Fi、蓝牙、RTC 或 NPU 时，应由客户在明确确认后使用单独的授权工具。"; color: theme.ink; font.family: theme.bodyFont; font.pixelSize: Math.round(13 * root.uiScale); wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter }
        }
    }
}
