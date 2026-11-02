import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.12
import "components"
import "components/BoardLabCapabilityMap.js" as BoardLabCapabilities

ApplicationWindow {
    id: window
    BoardLabTheme { id: theme }
    width: kioskMode ? 1920 : Math.min(1280, Math.max(320, Screen.desktopAvailableWidth))
    height: kioskMode ? 1080 : Math.min(800, Math.max(240, Screen.desktopAvailableHeight - 40))
    minimumWidth: 320
    minimumHeight: 240
    visible: true
    color: theme.canvas
    title: "迅为开发板测试平台"
    visibility: kioskMode ? Window.FullScreen : Window.Windowed

    property string activeApp: ""
    property bool applicationExitInProgress: false
    property int startupHomePage: isNaN(Number(startupHomePageArgument)) ? 0 : Math.max(0, Number(startupHomePageArgument))
    property Item virtualKeyboardTarget: null
    property bool virtualKeyboardEnabled: typeof boardLabVirtualKeyboardEnabled === "boolean"
                                          ? boardLabVirtualKeyboardEnabled : kioskMode
    onVirtualKeyboardEnabledChanged: if (!virtualKeyboardEnabled) hideVirtualKeyboard()
    readonly property string normalizedStartupPreviewSource: String(startupPreviewSource || "").trim().toLowerCase()
    property int startupPreviewAttempts: 0
    readonly property real uiScale: 1.0
    readonly property var currentSurface: surfaceForApp(activeApp)
    readonly property var demoView: demoLoader.item
    property string lastOpenedApp: ""

    function isHardwareDemoApp(title) {
        return title.indexOf("Demo:") === 0 || title.indexOf("Tool:") === 0
    }

    function hardwareDemoToolName(title) {
        return isHardwareDemoApp(title) ? title.substring(5) : ""
    }

    function surfaceForApp(title) {
        if (isHardwareDemoApp(title))
            return demoLoader
        var surfaces = {
            "Storage": storageLoader, "Diagnostics": diagnosticsLoader,
            "Environment": environmentLoader, "Keys": keysLoader,
            "GPU": gpuLoader, "Monitor": monitorLoader,
            "NPU YOLO": npuLoader, "WiFi": wifiLoader,
            "RK Desk": deskLoader, "Qt Web": webLoader
        }
        return surfaces[title] || null
    }

    function appRouteEnabled(title) {
        if (!surfaceForApp(title))
            return false
        if (isHardwareDemoApp(title)
                && BoardLabCapabilities.capabilityForTool(hardwareDemoToolName(title)).length === 0)
            return false
        return BoardLabCapabilities.appEnabled(title, boardProfile)
    }

    function prepareCurrentPage() {
        hideVirtualKeyboard()
        var page = currentSurface ? currentSurface.item : null
        if (!page)
            return
        if (isHardwareDemoApp(activeApp)) {
            page.prepareForNavigation()
            if (activeApp === "Demo:Music" || activeApp === "Demo:Buzzer"
                    || activeApp === "Tool:Music" || activeApp === "Tool:Buzzer")
                boardBackend.runAudioOperationAsync("stop-all", -1)
        } else if (activeApp === "Keys") {
            page.stopMonitoring("已离开按键检测，监听已停止")
        } else if (activeApp === "GPU") {
            page.stopBenchmarkForExit()
        } else if (activeApp === "NPU YOLO") {
            page.stopDemoForExit()
        } else if (activeApp === "WiFi") {
            page.resetTransientInput()
        }
    }

    function showHome() {
        startupPreviewDelay.stop()
        prepareCurrentPage()
        activeApp = ""
        Qt.callLater(function() { home.focusApp(window.lastOpenedApp) })
    }

    function openApp(title) {
        if (applicationExitInProgress || !appRouteEnabled(title) || title === activeApp)
            return
        startupPreviewDelay.stop()
        prepareCurrentPage()
        lastOpenedApp = title
        activeApp = title
    }

    function quitApplication() {
        if (applicationExitInProgress)
            return
        applicationExitInProgress = true
        startupOpenDelay.stop()
        startupPreviewDelay.stop()
        prepareCurrentPage()
        Qt.quit()
    }

    onClosing: {
        if (!applicationExitInProgress) {
            close.accepted = false
            quitApplication()
        }
    }

    function showVirtualKeyboard(targetItem) {
        if (!virtualKeyboardEnabled || !targetItem)
            return
        virtualKeyboardTarget = targetItem
        virtualKeyboard.visible = true
        Qt.callLater(function() { window.revealVirtualKeyboardTarget(targetItem) })
    }

    function hideVirtualKeyboard() {
        virtualKeyboard.visible = false
        virtualKeyboardTarget = null
    }

    function revealVirtualKeyboardTarget(targetItem) {
        if (!targetItem || !virtualKeyboard.visible)
            return
        var scroller = targetItem.parent
        while (scroller) {
            if (scroller.contentItem !== undefined && scroller.contentY !== undefined
                    && scroller.contentHeight !== undefined) {
                var top = targetItem.mapToItem(scroller.contentItem, 0, 0).y
                var next = scroller.contentY
                if (top + targetItem.height > next + scroller.height)
                    next = top + targetItem.height - scroller.height
                else if (top < next)
                    next = top
                scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), next))
                return
            }
            scroller = scroller.parent
        }
    }

    Component.onCompleted: {
        if (startupApp.length > 0 || startupTool.length > 0 || normalizedStartupPreviewSource.length > 0) {
            lockScreen.locked = false
            startupOpenDelay.start()
        }
    }

    Timer {
        id: startupOpenDelay
        interval: 180
        onTriggered: {
            if (window.normalizedStartupPreviewSource === "camera") {
                window.openApp("Demo:Camera")
                startupPreviewDelay.start()
            } else if (window.normalizedStartupPreviewSource === "hdmiin") {
                window.openApp("Demo:HDMIIN")
                startupPreviewDelay.start()
            } else if (startupTool.length > 0) {
                window.openApp("Demo:" + startupTool)
            } else {
                window.openApp(startupApp)
            }
        }
    }

    // Wait for the asynchronous V4L2 discovery and the relevant QML Loader.
    // This test-only route does not supply a resolution, format, or framerate:
    // CameraPreviewSurface stays on the same automatic GStreamer negotiation
    // path that an operator uses from the product UI.
    Timer {
        id: startupPreviewDelay
        interval: 250
        repeat: true
        onTriggered: {
            if (window.startupPreviewAttempts >= 24) {
                stop()
                return
            }
            window.startupPreviewAttempts += 1

            if (!demoView || !demoView.visible)
                return

            if (window.normalizedStartupPreviewSource === "camera") {
                if (demoView.activeTool !== "Camera")
                    return
                demoView.ensureCameraDeviceSelection()
                var cameraDevice = demoView.selectedCameraDevice()
                if (cameraDevice.length === 0)
                    return
                demoView.cameraMode = demoView.selectedCameraMode()
                demoView.cameraStateText = "正在打开实时预览: " + demoView.cameraMode + " / " + cameraDevice
                demoView.cameraPreviewRequested = true
                demoView.appendBoundedLog("cameraLog", "START visual-QA automatic GStreamer appsink preview "
                                          + demoView.cameraMode + " " + cameraDevice + "\n")
                stop()
            } else if (window.normalizedStartupPreviewSource === "hdmiin") {
                if (demoView.activeTool !== "HDMIIN")
                    return
                var hdmiDevice = demoView.selectedHdmiInDevice()
                if (hdmiDevice.length === 0)
                    return
                demoView.hdmiInStateText = "正在打开 HDMI 实时预览: " + hdmiDevice
                demoView.hdmiInPreviewRequested = true
                demoView.appendBoundedLog("hdmiInLog", "START visual-QA automatic GStreamer appsink HDMIIN preview "
                                          + hdmiDevice + "\n")
                stop()
            } else {
                stop()
            }
        }
    }

    Item {
        id: screen
        anchors.fill: parent
        anchors.bottomMargin: virtualKeyboard.visible ? virtualKeyboard.height : 0
        clip: true

        HomeScreen {
            id: home
            anchors.fill: parent
            uiScale: window.uiScale
            startupPage: window.startupHomePage
            active: window.activeApp.length === 0 && !lockScreen.locked
            keyboardEnabled: window.virtualKeyboardEnabled
            onKeyboardModeRequested: window.virtualKeyboardEnabled = enabled
            onAppRequested: window.openApp(title)
            onQuitRequested: window.quitApplication()
        }

        AppSurface {
            id: demoLoader
            objectName: "surface-demo"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/BoardToolsApp.qml")
            selected: window.isHardwareDemoApp(window.activeApp)
            toolName: window.hardwareDemoToolName(window.activeApp)
            onClosed: window.showHome()
            onToolRequested: window.openApp("Demo:" + tool)
        }
        AppSurface {
            id: storageLoader
            objectName: "surface-storage"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/StorageTestApp.qml")
            selected: window.activeApp === "Storage"
            onClosed: window.showHome()
        }
        AppSurface {
            id: diagnosticsLoader
            objectName: "surface-diagnostics"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/DiagnosticsApp.qml")
            selected: window.activeApp === "Diagnostics"
            onClosed: window.showHome()
        }
        AppSurface {
            id: environmentLoader
            objectName: "surface-environment"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/EnvironmentTestApp.qml")
            selected: window.activeApp === "Environment"
            onClosed: window.showHome()
        }
        AppSurface {
            id: keysLoader
            objectName: "surface-keys"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/KeyTestApp.qml")
            selected: window.activeApp === "Keys"
            onClosed: window.showHome()
        }
        AppSurface {
            id: gpuLoader
            objectName: "surface-gpu"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/GpuTestApp.qml")
            selected: window.activeApp === "GPU"
            onClosed: window.showHome()
        }
        AppSurface {
            id: monitorLoader
            objectName: "surface-monitor"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/ResourceMonitorApp.qml")
            selected: window.activeApp === "Monitor"
            onClosed: window.showHome()
        }
        AppSurface {
            id: npuLoader
            objectName: "surface-npu"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/NpuYoloDemoApp.qml")
            selected: window.activeApp === "NPU YOLO"
            onClosed: window.showHome()
        }
        AppSurface {
            id: wifiLoader
            objectName: "surface-wifi"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/WifiTestApp.qml")
            selected: window.activeApp === "WiFi"
            onClosed: window.showHome()
        }
        AppSurface {
            id: deskLoader
            objectName: "surface-desk"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/RkDeskApp.qml")
            selected: window.activeApp === "RK Desk"
            onClosed: window.showHome()
            onToolRequested: window.openApp("Demo:" + tool)
        }
        AppSurface {
            id: webLoader
            objectName: "surface-web"
            anchors.fill: parent
            pageSource: Qt.resolvedUrl("components/WebConsoleApp.qml")
            selected: window.activeApp === "Qt Web"
            onClosed: window.showHome()
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(320, parent.width - 32)
            spacing: 16
            visible: window.currentSurface !== null && window.currentSurface.status !== Loader.Ready

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: parent.visible && window.currentSurface !== null
                         && window.currentSurface.status === Loader.Loading
                visible: running
                width: 48
                height: 48
            }
            Text {
                width: parent.width
                text: window.currentSurface && window.currentSurface.status === Loader.Error ? "页面加载失败" : "正在打开工具"
                color: theme.ink
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
            }
            BoardToolsActionButton {
                width: parent.width
                text: "重试"
                visible: window.currentSurface !== null && window.currentSurface.status === Loader.Error
                onClicked: window.currentSurface.retry()
            }
            BoardToolsActionButton {
                width: parent.width
                text: "返回目录"
                variant: "secondary"
                iconName: "arrow-left"
                onClicked: window.showHome()
            }
        }

        LockScreen {
            id: lockScreen
            anchors.fill: parent
            uiScale: window.uiScale
            locked: !startUnlocked
        }
    }

    VirtualKeyboard {
        id: virtualKeyboard
        objectName: "boardlab-virtual-keyboard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        uiScale: window.uiScale
        target: window.virtualKeyboardTarget
        passwordMode: target !== null && target.echoMode !== undefined && Number(target.echoMode) === 2
        onHideRequested: window.hideVirtualKeyboard()
        onEnterPressed: {
            var field = window.virtualKeyboardTarget
            if (!field || (field.acceptableInput !== undefined && !field.acceptableInput))
                return
            if (field.accepted !== undefined)
                field.accepted()
            if (window.virtualKeyboardTarget === field && field.editingFinished !== undefined)
                field.editingFinished()
        }
    }

    Shortcut {
        sequence: "Alt+Left"
        enabled: window.activeApp.length > 0 && !virtualKeyboard.visible
        onActivated: window.showHome()
    }
    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (virtualKeyboard.visible)
                window.hideVirtualKeyboard()
            else if (window.activeApp.length > 0)
                window.showHome()
        }
    }
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.quitApplication()
    }
}
