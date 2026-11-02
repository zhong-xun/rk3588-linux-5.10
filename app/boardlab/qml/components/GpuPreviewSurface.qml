import QtQuick 2.12

// A deliberately self-contained Qt Quick render target for the GPU page.
// It stays inside the BoardLab scene graph: no secondary Weston/X11 window is
// created. Updating the shader uniform every frame makes the effect a useful
// live Mali workload while ResourceMonitor samples the driver devfreq load.
Item {
    id: root
    objectName: "gpu-embedded-preview-surface"

    BoardLabTheme { id: theme }

    property bool active: false
    property real uiScale: 1.0
    property real phase: 0.0

    clip: true

    Timer {
        id: renderClock
        interval: 16
        repeat: true
        running: root.active && root.visible
        onTriggered: root.phase = (root.phase + 0.018) % 1000.0
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(7, Math.round(7 * root.uiScale))
        color: theme.instrument
    }

    // Keep a scene-graph animation underneath the shader.  Some diagnostic
    // launchers deliberately select Qt's software/offscreen renderer, where
    // ShaderEffect may be transparent or unavailable.  The operator still
    // gets a visibly live preview in that case; on the normal Wayland/Mali
    // path both this layer and the shader are composed by Qt Quick.
    Item {
        id: sceneGraphFallback
        anchors.fill: parent
        visible: root.active
        clip: true

        Repeater {
            model: 5

            Rectangle {
                readonly property real sweep: (root.phase * 0.58 + index * 0.22) % 1.0
                width: Math.max(18, Math.round((26 + sweep * 238) * root.uiScale))
                height: width
                radius: width / 2
                x: Math.round(parent.width * 0.52 - width / 2)
                y: Math.round(parent.height * 0.50 - height / 2)
                color: "transparent"
                border.width: Math.max(1, Math.round(2 * root.uiScale))
                border.color: index % 2 === 0 ? theme.focus : theme.accent
                opacity: Math.max(0.05, 0.76 - sweep * 0.70)
            }
        }

        Repeater {
            model: 3

            Rectangle {
                readonly property real offset: (root.phase * 0.19 + index * 0.33) % 1.0
                width: Math.max(28, Math.round(parent.width * 0.23 * root.uiScale))
                height: Math.max(2, Math.round(2 * root.uiScale))
                radius: height / 2
                x: Math.round(-width + (parent.width + width * 2) * offset)
                y: Math.round(parent.height * (0.26 + index * 0.22))
                color: index === 1 ? theme.accent : theme.focus
                opacity: 0.58
            }
        }

        Rectangle {
            width: Math.max(2, Math.round(2 * root.uiScale))
            height: parent.height
            x: Math.round((parent.width - width) * ((root.phase * 0.12) % 1.0))
            color: theme.instrumentInk
            opacity: 0.36
        }
    }

    ShaderEffect {
        id: shaderPreview
        anchors.fill: parent
        visible: root.active
        opacity: 0.62
        property real phase: root.phase

        fragmentShader: "
            #ifdef GL_ES
            precision mediump float;
            #endif

            uniform lowp float qt_Opacity;
            uniform highp float phase;
            varying highp vec2 qt_TexCoord0;

            void main() {
                highp vec2 uv = qt_TexCoord0;
                highp vec2 p = uv * 2.0 - 1.0;
                p.x *= 1.65;
                highp float radius = length(p);
                highp float angle = atan(p.y, p.x);
                highp float rings = sin(radius * 22.0 - phase * 5.4);
                highp float sweep = sin(angle * 6.0 + phase * 2.1);
                highp float halo = smoothstep(1.15, 0.08, radius);
                highp float core = smoothstep(0.34, 0.02, radius);
                highp vec3 base = vec3(0.067, 0.086, 0.071);
                highp vec3 cyan = vec3(0.153, 0.561, 0.765);
                highp vec3 moss = vec3(0.173, 0.525, 0.365);
                highp float ringMask = smoothstep(0.20, 0.94, rings * 0.5 + 0.5);
                highp float sweepMask = smoothstep(0.35, 0.96, sweep * 0.5 + 0.5);
                highp vec3 color = base;
                color += cyan * ringMask * halo * 0.72;
                color += moss * sweepMask * halo * 0.48;
                color += vec3(0.957, 0.973, 0.945) * core * 0.82;
                color += vec3(0.10, 0.14, 0.11) * (1.0 - halo) * 0.45;
                gl_FragColor = vec4(color, 1.0) * qt_Opacity;
            }
        "
    }

    Item {
        anchors.fill: parent
        visible: !root.active

        Repeater {
            model: 3

            Rectangle {
                width: Math.round((70 - index * 14) * root.uiScale)
                height: width
                radius: width / 2
                x: (parent.width - width) / 2 + (index - 1) * Math.round(18 * root.uiScale)
                y: (parent.height - height) / 2 + (index - 1) * Math.round(10 * root.uiScale)
                color: "transparent"
                border.width: Math.max(1, Math.round(2 * root.uiScale))
                border.color: index === 0 ? theme.focus : theme.instrumentRaised
                opacity: 0.72 - index * 0.15
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: Math.round(58 * root.uiScale)
            text: "启动后将在此窗口实时渲染"
            color: theme.instrumentMuted
            font.pixelSize: Math.round(11 * root.uiScale)
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Math.round(10 * root.uiScale)
        width: previewBadgeRow.width + Math.round(16 * root.uiScale)
        height: Math.round(24 * root.uiScale)
        radius: height / 2
        color: root.active ? theme.instrumentRaised : theme.instrument
        border.color: root.active ? theme.focus : theme.withAlpha(theme.instrumentInk, 0.24)
        border.width: 1

        Row {
            id: previewBadgeRow
            anchors.centerIn: parent
            spacing: Math.round(6 * root.uiScale)

            Rectangle {
                width: Math.max(5, Math.round(6 * root.uiScale))
                height: width
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: root.active ? theme.accent : theme.instrumentMuted
            }

            Text {
                text: root.active ? "GPU 实时渲染中" : "内嵌预览待启动"
                color: theme.instrumentInk
                font.pixelSize: Math.round(10 * root.uiScale)
                font.weight: Font.DemiBold
            }
        }
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Math.round(10 * root.uiScale)
        text: root.active ? "Qt Quick ShaderEffect · Mali 驱动负载同步采样" : "Qt GPU 测试页内嵌窗口"
        color: root.active ? theme.instrumentInk : theme.instrumentMuted
        font.pixelSize: Math.round(10 * root.uiScale)
    }
}
