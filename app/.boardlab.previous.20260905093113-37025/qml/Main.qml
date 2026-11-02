import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.12
import "components"
import "components/BoardLabCapabilityMap.js" as BoardLabCapabilities

ApplicationWindow {
    id: window
    BoardLabTheme { id: theme }
    width: kioskMode ? 1920 : 1280
    height: kioskMode ? 1080 : 800
    visible: true
    color: theme.canvas
    title: "迅为开发板测试平台"
    visibility: kioskMode ? Window.FullScreen : Window.Windowed

    property string activeApp: ""
    property string activeDemoTool: ""
    // Keep the explicit UI exit path idempotent.  A touch screen can deliver
    // a second tap while the process is already leaving the event loop.
    property bool applicationExitInProgress: false
    property int startupHomePage: isNaN(Number(startupHomePageArgument)) ? 0 : Math.max(0, Number(startupHomePageArgument))
    // Visual QA may start a source without injecting pointer events into the
    // customer's Weston session.  This is intentionally empty for normal
    // product launches, so ordinary camera and HDMI-IN previews remain fully
    // user initiated and let Qt Multimedia negotiate their capture caps.
    readonly property string normalizedStartupPreviewSource: String(startupPreviewSource || "").trim().toLowerCase()
    property int startupPreviewAttempts: 0
    // Shared on-screen keyboard host for touch kiosks without a physical keyboard.
    property Item virtualKeyboardTarget: null
    // The bundled fixed YOLOv8 workload is available only on validated
    // RK3562, RK3568, RK3576, and RK3588-family product profiles. Keep
    // command-line startup routing in step with the filtered launcher so
    // --open-app cannot bypass the board-profile safety boundary.
    readonly property bool npuYoloDemoEnabled: boardProfile.hasCapability("npuYoloDemo")
    // Workbench geometry follows this window, not the host desktop.  The
    // upper bound deliberately stays at one: 4K earns more readable context,
    // not a globally inflated interface. Compact layouts switch composition
    // before reducing their visual scale.
    readonly property real uiScale: Math.max(0.9, Math.min(1.0,
                                                           Math.min(width / 1024.0,
                                                                    height / 600.0)))

    function isHardwareDemoApp(title) {
        return title.indexOf("Demo:") === 0 || title.indexOf("Tool:") === 0
    }

    function hardwareDemoToolName(title) {
        if (title.indexOf("Demo:") === 0)
            return title.substring(5)
        if (title.indexOf("Tool:") === 0)
            return title.substring(5)
        return ""
    }

    function appDisplayName(title) {
        if (isHardwareDemoApp(title))
            return demoView.toolDisplayName(hardwareDemoToolName(title))
        var names = {
            "Storage": "存储",
            "Diagnostics": "系统诊断",
            "Environment": "温湿度检测",
            "Keys": "按键检测",
            "GPU": "GPU 测试",
            "Monitor": "资源监控",
            "NPU YOLO": "YOLOv8 NPU",
            "WiFi": "Wi-Fi",
            "RK Desk": "RK 桌面",
            "Qt Web": "扫码控制"
        }
        return names[title] || title
    }

    function appSubtitle(title) {
        if (isHardwareDemoApp(title))
            return "独立测试应用 · " + demoView.toolSubtitle(hardwareDemoToolName(title))
        if (title === "Storage")
            return "U盘 TF卡 NVME测速"
        if (title === "Diagnostics")
            return "RTC 与 NPU 只读状态"
        if (title === "Environment")
            return "环境传感器实时采样"
        if (title === "Keys")
            return "板载按键事件监听"
        if (title === "GPU")
            return "glmark2-es 固定离屏基准"
        if (title === "Monitor")
            return "CPU、内存、GPU 与 NPU 实时状态"
        if (title === "NPU YOLO")
            return "YOLOv8 内置 MP4 目标检测示例"
        if (title === "WiFi")
            return "NetworkManager 状态、扫描与连接"
        if (title === "RK Desk")
            return "板端状态与工程入口"
        if (title === "Qt Web")
            return "扫码打开局域网 Web 控制台"
        return (boardProfile.displayName.length > 0 ? boardProfile.displayName : "开发板") + " 工作区"
    }

    function showVirtualKeyboard(targetItem) {
        if (!targetItem)
            return
        virtualKeyboardTarget = targetItem
        virtualKeyboard.visible = true
        // Run after the dock has been laid out so the visible viewport is
        // calculated against its actual height rather than the prior frame.
        Qt.callLater(function() { window.revealVirtualKeyboardTarget(targetItem) })
    }

    function hideVirtualKeyboard() {
        virtualKeyboard.visible = false
        // Keep the field value, but never retain a stale item from a page that
        // is being hidden or destroyed.
        virtualKeyboardTarget = null
    }

    function revealVirtualKeyboardTarget(targetItem) {
        if (!targetItem || !virtualKeyboard.visible)
            return

        var scroller = targetItem.parent
        while (scroller) {
            if (scroller.contentItem !== undefined
                    && scroller.contentY !== undefined
                    && scroller.contentHeight !== undefined) {
                var scrollerTop = scroller.mapToItem(window.contentItem, 0, 0).y
                var visibleBottom = Math.min(scrollerTop + scroller.height,
                                             window.height - virtualKeyboard.height - Math.round(12 * window.uiScale))
                var visibleHeight = visibleBottom - scrollerTop
                if (visibleHeight > 32) {
                    var itemTop = targetItem.mapToItem(scroller.contentItem, 0, 0).y
                    var itemBottom = itemTop + targetItem.height
                    var topInView = itemTop - scroller.contentY
                    var bottomInView = itemBottom - scroller.contentY
                    var nextContentY = scroller.contentY
                    if (bottomInView > visibleHeight)
                        nextContentY += bottomInView - visibleHeight
                    else if (topInView < Math.round(12 * window.uiScale))
                        nextContentY += topInView - Math.round(12 * window.uiScale)
                    var maximumContentY = Math.max(0, scroller.contentHeight - scroller.height)
                    scroller.contentY = Math.max(0, Math.min(maximumContentY, nextContentY))
                }
                return
            }
            scroller = scroller.parent
        }
    }

    function closeApp(title) {
        if (title.length === 0)
            return
        hideVirtualKeyboard()
        if (title === "Demo:Music" || title === "Demo:Buzzer")
            boardBackend.runAudioOperationAsync("stop-all", -1)
        if (title === "Keys")
            keyTestView.stopMonitoring("已停止按键监听")
        if (title === "GPU")
            gpuView.stopBenchmarkForExit()
        if (title === "NPU YOLO")
            npuYoloView.stopDemoForExit()
        if (title === "WiFi")
            wifiView.resetTransientInput()
        if (activeApp === title)
            showHome()
    }

    function quitApplication() {
        if (applicationExitInProgress)
            return

        applicationExitInProgress = true
        stopCloseTimers()
        hideVirtualKeyboard()

        // End active hardware work before the GUI event loop goes away.  The
        // backend owns the processes, so the calls are safe even when a task
        // has just completed between the tap and this handler.
        if (activeApp === "Demo:Music" || activeApp === "Demo:Buzzer")
            boardBackend.runAudioOperationAsync("stop-all", -1)
        if (activeApp === "Keys")
            keyTestView.stopMonitoring("已退出迅为开发板测试平台，按键监听已停止")
        if (activeApp === "GPU")
            gpuView.stopBenchmarkForExit()
        if (activeApp === "NPU YOLO")
            npuYoloView.stopDemoForExit()
        if (activeApp === "WiFi")
            wifiView.resetTransientInput()
        if (window.isHardwareDemoApp(activeApp))
            demoView.prepareForNavigation()

        Qt.quit()
    }

    function stopCloseTimers() {
        closeDelay.stop()
        demoCloseDelay.stop()
        storageCloseDelay.stop()
        diagnosticsCloseDelay.stop()
        environmentCloseDelay.stop()
        keyTestCloseDelay.stop()
        gpuCloseDelay.stop()
        monitorCloseDelay.stop()
        npuYoloCloseDelay.stop()
        wifiCloseDelay.stop()
        rkDeskCloseDelay.stop()
        webCloseDelay.stop()
    }

    function targetSurface(title) {
        if (isHardwareDemoApp(title))
            return "demo"
        if (title === "Storage")
            return "storage"
        if (title === "Diagnostics")
            return "diagnostics"
        if (title === "Environment")
            return "environment"
        if (title === "Keys")
            return "keys"
        if (title === "GPU")
            return "gpu"
        if (title === "Monitor")
            return "monitor"
        if (title === "NPU YOLO")
            return "npu"
        if (title === "WiFi")
            return "wifi"
        if (title === "RK Desk")
            return "rkdesk"
        if (title === "Qt Web")
            return "web"
        return "mock"
    }

    function hideInactiveAppSurfaces(title) {
        var target = targetSurface(title)
        if (target !== "mock") {
            appView.visible = false
            appView.opacity = 0
            appView.scale = 0.92
        }
        if (target !== "demo") {
            demoView.visible = false
            demoView.opacity = 0
            demoView.scale = 0.92
        }
        if (target !== "storage") {
            storageView.visible = false
            storageView.opacity = 0
            storageView.scale = 0.92
        }
        if (target !== "diagnostics") {
            diagnosticsView.visible = false
            diagnosticsView.opacity = 0
            diagnosticsView.scale = 0.92
        }
        if (target !== "environment") {
            environmentView.visible = false
            environmentView.opacity = 0
            environmentView.scale = 0.92
        }
        if (target !== "keys") {
            keyTestView.visible = false
            keyTestView.opacity = 0
            keyTestView.scale = 0.92
        }
        if (target !== "gpu") {
            gpuView.visible = false
            gpuView.opacity = 0
            gpuView.scale = 0.92
        }
        if (target !== "monitor") {
            resourceMonitorView.visible = false
            resourceMonitorView.opacity = 0
            resourceMonitorView.scale = 0.92
        }
        if (target !== "npu") {
            npuYoloView.visible = false
            npuYoloView.opacity = 0
            npuYoloView.scale = 0.92
        }
        if (target !== "wifi") {
            wifiView.visible = false
            wifiView.opacity = 0
            wifiView.scale = 0.92
        }
        if (target !== "rkdesk") {
            rkDeskView.visible = false
            rkDeskView.opacity = 0
            rkDeskView.scale = 0.92
        }
        if (target !== "web") {
            webView.visible = false
            webView.opacity = 0
            webView.scale = 0.92
        }
    }

    function showHome() {
        stopCloseTimers()
        hideVirtualKeyboard()
        if (window.isHardwareDemoApp(activeApp))
            demoView.prepareForNavigation()
        if (activeApp === "Demo:Music" || activeApp === "Demo:Buzzer") {
            boardBackend.runAudioOperationAsync("stop-all", -1)
        }
        if (activeApp === "Keys")
            keyTestView.stopMonitoring("已离开按键检测，监听已停止")
        if (activeApp === "GPU")
            gpuView.stopBenchmarkForExit()
        if (activeApp === "NPU YOLO")
            npuYoloView.stopDemoForExit()
        if (activeApp === "WiFi")
            wifiView.resetTransientInput()
        appView.opacity = 0
        appView.scale = 0.92
        demoView.opacity = 0
        demoView.scale = 0.92
        storageView.opacity = 0
        storageView.scale = 0.92
        diagnosticsView.opacity = 0
        diagnosticsView.scale = 0.92
        environmentView.opacity = 0
        environmentView.scale = 0.92
        keyTestView.opacity = 0
        keyTestView.scale = 0.92
        gpuView.opacity = 0
        gpuView.scale = 0.92
        resourceMonitorView.opacity = 0
        resourceMonitorView.scale = 0.92
        npuYoloView.opacity = 0
        npuYoloView.scale = 0.92
        wifiView.opacity = 0
        wifiView.scale = 0.92
        rkDeskView.opacity = 0
        rkDeskView.scale = 0.92
        webView.opacity = 0
        webView.scale = 0.92
        activeApp = ""
        closeDelay.start()
        demoCloseDelay.start()
        storageCloseDelay.start()
        diagnosticsCloseDelay.start()
        environmentCloseDelay.start()
        keyTestCloseDelay.start()
        gpuCloseDelay.start()
        monitorCloseDelay.start()
        npuYoloCloseDelay.start()
        wifiCloseDelay.start()
        rkDeskCloseDelay.start()
        webCloseDelay.start()
    }

    Component.onCompleted: {
        if (startupApp.length > 0 || startupTool.length > 0
                || normalizedStartupPreviewSource.length > 0) {
            lockScreen.locked = false
            startupOpenDelay.start()
        }
    }

    function appRouteEnabled(title) {
        return window.isKnownAppRoute(title)
                && (title !== "NPU YOLO" || window.npuYoloDemoEnabled)
                && BoardLabCapabilities.appEnabled(title, boardProfile)
    }

    function isKnownAppRoute(title) {
        if (isHardwareDemoApp(title))
            return BoardLabCapabilities.capabilityForTool(hardwareDemoToolName(title)).length > 0

        return ["Storage", "Diagnostics", "Environment", "Keys", "GPU",
                "Monitor", "NPU YOLO", "WiFi", "RK Desk", "Qt Web"].indexOf(title) >= 0
    }

    function openApp(title) {
        if (!window.appRouteEnabled(title))
            return

        stopCloseTimers()
        hideVirtualKeyboard()
        if (window.isHardwareDemoApp(activeApp) && title !== activeApp)
            demoView.prepareForNavigation()
        if (title !== "Demo:Music" && title !== "Demo:Buzzer"
                && (activeApp === "Demo:Music" || activeApp === "Demo:Buzzer")) {
            boardBackend.runAudioOperationAsync("stop-all", -1)
        }
        if (title !== "Keys" && activeApp === "Keys")
            keyTestView.stopMonitoring("已离开按键检测，监听已停止")
        if (title !== "GPU" && activeApp === "GPU")
            gpuView.stopBenchmarkForExit()
        if (title !== "NPU YOLO" && activeApp === "NPU YOLO")
            npuYoloView.stopDemoForExit()
        if (title !== "WiFi" && activeApp === "WiFi")
            wifiView.resetTransientInput()
        hideInactiveAppSurfaces(title)
        activeApp = title
        if (isHardwareDemoApp(title)) {
            activeDemoTool = hardwareDemoToolName(title)
            demoView.standaloneTool = activeDemoTool
            demoView.currentTool = activeDemoTool
            demoView.visible = true
            demoView.opacity = 1
            demoView.scale = 1
        } else if (title === "Storage") {
            storageView.visible = true
            storageView.opacity = 1
            storageView.scale = 1
        } else if (title === "Diagnostics") {
            diagnosticsView.visible = true
            diagnosticsView.opacity = 1
            diagnosticsView.scale = 1
        } else if (title === "Environment") {
            environmentView.visible = true
            environmentView.opacity = 1
            environmentView.scale = 1
        } else if (title === "Keys") {
            keyTestView.visible = true
            keyTestView.opacity = 1
            keyTestView.scale = 1
        } else if (title === "GPU") {
            gpuView.visible = true
            gpuView.opacity = 1
            gpuView.scale = 1
        } else if (title === "Monitor") {
            resourceMonitorView.visible = true
            resourceMonitorView.opacity = 1
            resourceMonitorView.scale = 1
        } else if (title === "NPU YOLO") {
            npuYoloView.visible = true
            npuYoloView.opacity = 1
            npuYoloView.scale = 1
        } else if (title === "WiFi") {
            wifiView.visible = true
            wifiView.opacity = 1
            wifiView.scale = 1
        } else if (title === "RK Desk") {
            rkDeskView.visible = true
            rkDeskView.opacity = 1
            rkDeskView.scale = 1
        } else if (title === "Qt Web") {
            webView.visible = true
            webView.opacity = 1
            webView.scale = 1
        } else {
            appView.visible = true
            appView.opacity = 1
            appView.scale = 1
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

            if (!demoView.visible)
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

            Rectangle {
                id: deviceFrame
                anchors.fill: parent
                scale: 1.0
                transformOrigin: Item.Center
                radius: 0
        color: theme.canvas
        border.color: theme.rule
        border.width: 0

        Rectangle {
            id: screen
            anchors.fill: parent
            anchors.margins: 0
            radius: 0
            color: theme.canvas
            clip: true

            HomeScreen {
                id: home
                anchors.fill: parent
                uiScale: window.uiScale
                startupPage: window.startupHomePage
                active: window.activeApp.length === 0
                        && !lockScreen.locked
                onAppRequested: window.openApp(title)
                onQuitRequested: window.quitApplication()
            }

            MockApp {
                id: appView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                appTitle: activeApp
                onClosed: {
                    window.showHome()
                }
                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: closeDelay
                    interval: 190
                    onTriggered: appView.visible = false
                }
            }

            BoardToolsApp {
                id: demoView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: {
                    window.showHome()
                    home.showTools()
                }
                onToolRequested: {
                    window.openApp("Demo:" + tool)
                }

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: demoCloseDelay
                    interval: 190
                    onTriggered: demoView.visible = false
                }
            }

            StorageTestApp {
                id: storageView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: {
                    window.showHome()
                }

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: storageCloseDelay
                    interval: 190
                    onTriggered: storageView.visible = false
                }
            }

            DiagnosticsApp {
                id: diagnosticsView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: diagnosticsCloseDelay
                    interval: 190
                    onTriggered: diagnosticsView.visible = false
                }
            }

            EnvironmentTestApp {
                id: environmentView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: environmentCloseDelay
                    interval: 190
                    onTriggered: environmentView.visible = false
                }
            }

            KeyTestApp {
                id: keyTestView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: keyTestCloseDelay
                    interval: 190
                    onTriggered: keyTestView.visible = false
                }
            }

            GpuTestApp {
                id: gpuView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: gpuCloseDelay
                    interval: 190
                    onTriggered: gpuView.visible = false
                }
            }

            ResourceMonitorApp {
                id: resourceMonitorView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: monitorCloseDelay
                    interval: 190
                    onTriggered: resourceMonitorView.visible = false
                }
            }

            NpuYoloDemoApp {
                id: npuYoloView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: npuYoloCloseDelay
                    interval: 190
                    onTriggered: npuYoloView.visible = false
                }
            }

            WifiTestApp {
                id: wifiView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: window.showHome()

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: wifiCloseDelay
                    interval: 190
                    onTriggered: wifiView.visible = false
                }
            }

            RkDeskApp {
                id: rkDeskView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: {
                    window.showHome()
                }
                onToolRequested: {
                    window.openApp("Demo:" + tool)
                }

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: rkDeskCloseDelay
                    interval: 190
                    onTriggered: rkDeskView.visible = false
                }
            }

            WebConsoleApp {
                id: webView
                anchors.fill: parent
                uiScale: window.uiScale
                visible: false
                enabled: visible && opacity > 0.01
                opacity: 0
                scale: 0.96
                onClosed: {
                    window.showHome()
                }

                Behavior on opacity {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }
                Behavior on scale {
                    NumberAnimation { duration: theme.motionDuration(180); easing.type: Easing.OutCubic }
                }

                Timer {
                    id: webCloseDelay
                    interval: 190
                    onTriggered: webView.visible = false
                }
            }

            LockScreen {
                id: lockScreen
                anchors.fill: parent
                uiScale: window.uiScale
                locked: !startUnlocked
            }

        }
    }

    // Bottom docked on-screen keyboard for touch panels (Wi-Fi password, IP, APN, …).
    VirtualKeyboard {
        id: virtualKeyboard
        objectName: "boardlab-virtual-keyboard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        uiScale: window.uiScale
        target: window.virtualKeyboardTarget
        passwordMode: {
            if (!window.virtualKeyboardTarget)
                return false
            // TextField.echoMode === TextInput.Password
            return window.virtualKeyboardTarget.echoMode !== undefined
                && Number(window.virtualKeyboardTarget.echoMode) === 2
        }
        onHideRequested: window.hideVirtualKeyboard()
        onEnterPressed: {
            // Password fields often connect on editingFinished; force it when present.
            if (window.virtualKeyboardTarget && window.virtualKeyboardTarget.editingFinished !== undefined)
                window.virtualKeyboardTarget.editingFinished()
        }
    }

    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (virtualKeyboard.visible) {
                window.hideVirtualKeyboard()
                return
            }
            if (window.activeApp.length > 0)
                window.showHome()
            else
                window.quitApplication()
        }
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.quitApplication()
    }

}
