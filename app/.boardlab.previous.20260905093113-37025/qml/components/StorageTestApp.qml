import QtQuick 2.12
import QtQuick.Controls 2.12

// V2 Signal Workbench storage surface.
Item {
    id: root
    signal closed()

    property real uiScale: 1.0
    property bool benchmarkWriteConfirmed: false
    property string confirmedBenchmarkDeviceSignature: ""
    readonly property string storageDeviceSignature: root.deviceSignature()
    property string lastOperationText: "扫描后会显示可安全操作的分区；系统盘与 Swap 会被保护。"
    readonly property bool storageBusy: boardBackend.backendBusy
                                       && boardBackend.backendBusyOperation.indexOf("storage-") === 0

    BoardLabTheme { id: theme }

    function typeLabel(type) {
        if (type === "USB")
            return "U盘"
        if (type === "SD")
            return "SD"
        if (type === "EMMC")
            return "eMMC"
        if (type === "NVME")
            return "NVMe"
        if (type === "SATA")
            return "SATA"
        return "存储"
    }

    function typeAccent(type) {
        return theme.accent
    }

    function protectionText(device) {
        if (device.protected)
            return "系统保护"
        if (device.mounted && !device.managedMount)
            return "系统挂载"
        if (device.mounted)
            return "已挂载"
        if (device.canMount)
            return "可挂载"
        return "待检查"
    }

    function deviceSignature() {
        var entries = []
        for (var index = 0; index < boardBackend.storageDevices.length; ++index) {
            var device = boardBackend.storageDevices[index]
            entries.push(String(device.device || "") + "|"
                         + String(device.parentDevice || "") + "|"
                         + String(device.actualMountPoint || "") + "|"
                         + String(device.sizeBytes || 0))
        }
        entries.sort()
        return entries.join(";")
    }

    function clearBenchmarkConfirmation() {
        root.benchmarkWriteConfirmed = false
        root.confirmedBenchmarkDeviceSignature = ""
    }

    onStorageDeviceSignatureChanged: {
        if (root.benchmarkWriteConfirmed
                && root.confirmedBenchmarkDeviceSignature !== root.storageDeviceSignature)
            root.clearBenchmarkConfirmation()
    }

    onVisibleChanged: {
        if (!visible) {
            root.clearBenchmarkConfirmation()
            return
        }
        if (visible && !boardBackend.backendBusy) {
            lastOperationText = "正在扫描存储分区…"
            boardBackend.scanStorageDevicesAsync()
        }
    }

    Connections {
        target: boardBackend
        onOperationFinished: {
            if (operation.indexOf("storage-") === 0) {
                if (operation === "storage-scan") {
                    root.lastOperationText = success
                                             ? "已发现 " + (result.length > 0 ? result : boardBackend.storageDevices.length) + " 个存储设备"
                                             : "扫描失败：" + status
                } else {
                    root.lastOperationText = success ? status : "操作失败：" + status
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(124 * root.uiScale)
        color: theme.instrument
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: Math.round(30 * root.uiScale)
        anchors.right: headerActions.left
        anchors.rightMargin: Math.round(24 * root.uiScale)
        anchors.top: parent.top
        anchors.topMargin: Math.round(42 * root.uiScale)
        spacing: Math.round(4 * root.uiScale)

        Text {
            width: parent.width
            text: qsTr("存储设备测试")
            color: theme.instrumentInk
            font.family: theme.displayFont
            elide: Text.ElideRight
            font.pixelSize: Math.round(30 * root.uiScale)
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            text: root.storageBusy ? "正在执行：" + boardBackend.backendBusyOperation : root.lastOperationText
            color: root.storageBusy ? theme.info : theme.instrumentMuted
            font.family: theme.bodyFont
            elide: Text.ElideRight
            font.pixelSize: Math.round(13 * root.uiScale)
        }
    }

    Row {
        id: headerActions
        anchors.right: parent.right
        anchors.rightMargin: Math.round(30 * root.uiScale)
        anchors.top: parent.top
        anchors.topMargin: Math.round(52 * root.uiScale)
        spacing: Math.round(10 * root.uiScale)

        BoardToolsActionButton {
            uiScale: root.uiScale
            width: Math.round(112 * root.uiScale)
            height: Math.max(44, Math.round(44 * root.uiScale))
            text: root.storageBusy && boardBackend.backendBusyOperation === "storage-scan" ? qsTr("扫描中") : qsTr("刷新")
            variant: "primary"
            enabled: !boardBackend.backendBusy
            onClicked: {
                root.lastOperationText = "正在刷新存储分区…"
                boardBackend.scanStorageDevicesAsync()
            }
        }

        BoardToolsActionButton {
            uiScale: root.uiScale
            width: Math.round(120 * root.uiScale)
            height: Math.max(44, Math.round(44 * root.uiScale))
            text: qsTr("返回桌面")
            variant: "secondary"
            onClicked: {
                root.clearBenchmarkConfirmation()
                root.closed()
            }
        }
    }

    Flickable {
        anchors.left: parent.left
        anchors.leftMargin: Math.round(30 * root.uiScale)
        anchors.right: parent.right
        anchors.rightMargin: Math.round(30 * root.uiScale)
        anchors.top: parent.top
        anchors.topMargin: Math.round(124 * root.uiScale)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(48 * root.uiScale)
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: deviceColumn.height
        clip: true

        Column {
            id: deviceColumn
            width: parent.width
            spacing: Math.round(14 * root.uiScale)

            Rectangle {
                width: parent.width
                height: Math.round(72 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: root.benchmarkWriteConfirmed ? theme.readySoft : theme.surfaceSoft
                border.color: root.benchmarkWriteConfirmed ? theme.ready : theme.rule
                border.width: 1

                Item {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(16 * root.uiScale)
                    anchors.right: benchmarkConsent.left
                    anchors.rightMargin: Math.round(16 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(13 * root.uiScale)
                        text: "安全操作模式"
                        color: theme.ink
                        font.family: theme.bodyFont
                        font.pixelSize: Math.round(14 * root.uiScale)
                        font.weight: Font.Bold
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: Math.round(35 * root.uiScale)
                        text: "仅允许本平台管理的挂载点测速；测速会临时写入并删除 100 MiB 文件。系统盘、Swap 和其他程序的挂载不可操作。"
                        color: theme.muted
                        font.family: theme.bodyFont
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(12 * root.uiScale)
                    }
                }

                BoardToolsSwitchRow {
                    id: benchmarkConsent
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(12 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(266 * root.uiScale)
                    height: Math.max(44, Math.round(52 * root.uiScale))
                    uiScale: root.uiScale
                    label: "允许测速写入"
                    checked: root.benchmarkWriteConfirmed
                    accent: theme.warning
                    onText: "已确认"
                    offText: "未确认"
                    onToggled: {
                        root.benchmarkWriteConfirmed = checked
                        root.confirmedBenchmarkDeviceSignature = checked
                                ? root.storageDeviceSignature : ""
                    }
                }
            }

            Repeater {
                model: boardBackend.storageDevices

                Rectangle {
                    width: deviceColumn.width
                    height: Math.round(202 * root.uiScale)
                    radius: Math.round(theme.radiusPanel * root.uiScale)
                    color: modelData.protected ? theme.warningSoft : theme.surfaceRaised
                    border.color: modelData.protected ? theme.warning : theme.rule
                    border.width: modelData.protected ? 2 : 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: Math.round(18 * root.uiScale)
                        spacing: Math.round(18 * root.uiScale)

                        Rectangle {
                            width: Math.round(94 * root.uiScale)
                            height: width
                            radius: Math.round(theme.radiusPanel * root.uiScale)
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.typeAccent(modelData.type)

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: theme.withAlpha(theme.surfaceRaised, 0.16)
                            }

                            Text {
                                anchors.centerIn: parent
                                text: root.typeLabel(modelData.type)
                                color: theme.accentInk
                                font.family: theme.bodyFont
                                font.pixelSize: Math.round(20 * root.uiScale)
                                font.weight: Font.Bold
                            }
                        }

                        Column {
                            width: parent.width - Math.round(112 * root.uiScale) - parent.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Math.round(9 * root.uiScale)

                            Row {
                                width: parent.width
                                spacing: Math.round(10 * root.uiScale)

                                Text {
                                    width: Math.max(Math.round(160 * root.uiScale), parent.width - stateBadge.width - parent.spacing)
                                    text: modelData.device
                                    color: theme.ink
                                    font.family: theme.monoFont
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Math.round(18 * root.uiScale)
                                    font.weight: Font.Bold
                                }

                                Rectangle {
                                    id: stateBadge
                                    width: Math.round(92 * root.uiScale)
                                    height: Math.round(24 * root.uiScale)
                                    radius: height / 2
                                    color: modelData.protected ? theme.warningSoft : (modelData.mounted ? theme.readySoft : theme.infoSoft)
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.protectionText(modelData)
                                        color: modelData.protected ? theme.warning : (modelData.mounted ? theme.ready : theme.info)
                                        font.family: theme.bodyFont
                                        font.pixelSize: Math.round(11 * root.uiScale)
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                visible: modelData.label.length > 0 || modelData.model.length > 0
                                text: modelData.label.length > 0 ? "卷标：" + modelData.label : "设备：" + modelData.model
                                color: theme.muted
                                font.family: theme.bodyFont
                                elide: Text.ElideRight
                                font.pixelSize: Math.round(12 * root.uiScale)
                            }

                            Row {
                                spacing: Math.round(18 * root.uiScale)
                                Text {
                                    text: "容量  " + modelData.size
                                    color: theme.ink
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(13 * root.uiScale)
                                }
                                Text {
                                    text: "文件系统  " + modelData.filesystem
                                    color: modelData.filesystem === "未识别" ? theme.warning : theme.ink
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(13 * root.uiScale)
                                }
                                Text {
                                    visible: modelData.parentDevice.length > 0 && modelData.parentDevice !== modelData.device
                                    text: "磁盘  " + modelData.parentDevice
                                    color: theme.muted
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                }
                            }

                            Text {
                                width: parent.width
                                text: modelData.protected
                                      ? "此分区正在供系统使用，已禁止卸载与测速。"
                                      : (modelData.mounted
                                         ? (modelData.managedMount
                                            ? "挂载位置：" + modelData.actualMountPoint
                                            : "此分区由系统或其他程序挂载，平台不会卸载或测速。")
                                         : (modelData.canMount
                                            ? "建议挂载到：" + modelData.mountPointHint
                                            : "未识别可挂载文件系统；请确认分区与格式。"))
                                color: modelData.protected ? theme.warning : (modelData.mounted ? theme.ready : theme.muted)
                                font.family: theme.bodyFont
                                elide: Text.ElideMiddle
                                font.pixelSize: Math.round(12 * root.uiScale)
                            }

                            Row {
                                spacing: Math.round(10 * root.uiScale)

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: Math.round(112 * root.uiScale)
                                    height: Math.max(44, Math.round(44 * root.uiScale))
                                    text: modelData.protected ? qsTr("系统保护")
                                          : (modelData.mounted
                                             ? (modelData.canUnmount ? qsTr("安全卸载") : qsTr("系统管理"))
                                             : (modelData.canMount ? qsTr("挂载") : qsTr("不可挂载")))
                                    variant: modelData.mounted ? "stop" : "primary"
                                    enabled: !boardBackend.backendBusy && (modelData.mounted ? modelData.canUnmount : modelData.canMount)
                                    onClicked: {
                                        root.lastOperationText = modelData.mounted ? "正在卸载 " + modelData.device + "…"
                                                                                   : "正在挂载 " + modelData.device + "…"
                                        if (modelData.mounted)
                                            boardBackend.unmountStorageDeviceAsync(modelData.actualMountPoint)
                                        else
                                            boardBackend.mountStorageDeviceAsync(modelData.device, modelData.mountPointHint)
                                    }
                                }

                                BoardToolsActionButton {
                                    uiScale: root.uiScale
                                    width: Math.round(124 * root.uiScale)
                                    height: Math.max(44, Math.round(44 * root.uiScale))
                                    text: modelData.testing ? qsTr("测速中")
                                          : (!modelData.canBenchmark
                                             ? (modelData.mounted && !modelData.managedMount ? qsTr("系统管理")
                                                : (modelData.canMount ? qsTr("挂载后测速") : qsTr("测速不可用")))
                                             : (root.benchmarkWriteConfirmed ? qsTr("开始测速") : qsTr("确认后测速")))
                                    variant: "primary"
                                    accent: theme.warning
                                    enabled: modelData.canBenchmark && root.benchmarkWriteConfirmed
                                             && !modelData.testing && !boardBackend.backendBusy
                                    onClicked: {
                                        root.lastOperationText = "正在测速 " + modelData.device + "；会写入并删除临时文件…"
                                        boardBackend.testStorageSpeed(modelData.actualMountPoint)
                                        root.clearBenchmarkConfirmation()
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.readSpeed.length > 0 || modelData.writeSpeed.length > 0
                                    text: "读 " + modelData.readSpeed + "  ·  写 " + modelData.writeSpeed
                                    color: theme.info
                                    font.family: theme.monoFont
                                    font.pixelSize: Math.round(13 * root.uiScale)
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: !root.storageBusy && boardBackend.storageDevices.length === 0
                width: parent.width
                height: Math.round(200 * root.uiScale)
                text: qsTr("未检测到可识别的存储分区\n\n请插入U盘、TF卡或NVMe固态硬盘后点击刷新")
                color: theme.muted
                font.family: theme.bodyFont
                font.pixelSize: Math.round(16 * root.uiScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

}
