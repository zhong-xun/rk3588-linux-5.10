import QtQuick 2.12
import BoardLab 1.0

// Shared embedded preview surface for camera and RK3588 HDMI-RX.
//
// Do not use Qt Multimedia Camera/VideoOutput here.  On the Buildroot
// Qt 5.15 stack that path loads CameraBin, which rejects the valid RK3588
// HDMI-RX multi-planar V4L2 node before it can negotiate a frame.  The C++
// GstVideoPreview item instead owns a small v4l2src -> appsink pipeline and
// paints its converted frames directly into this Qt Quick item.
Item {
    id: preview

    BoardLabTheme { id: theme }

    property string devicePath: ""
    property bool requested: false
    property real uiScale: 1.0
    property string sourceTitle: "摄像头"
    // Fullscreen uses the inline surface as a frame provider.  Keeping the
    // capture pipeline in one item prevents a second open of /dev/videoN.
    property var mirrorSource: null
    property bool mirrorOnly: false
    // The source pipeline keeps running while fullscreen mirrors its frames,
    // but the covered inline QQuickPaintedItem need not be rasterized too.
    property bool presentationSuspended: false
    property alias previewBackend: gstPreview

    readonly property bool running: gstPreview.running
    readonly property bool active: gstPreview.active
    readonly property string statusText: gstPreview.statusText
    readonly property string detailText: gstPreview.detailText
    readonly property color stateAccent: preview.active ? theme.ready
                                        : (preview.requested ? theme.warning : theme.inkFaint)

    implicitHeight: Math.max(Math.round(246 * uiScale), Math.round(width * 9 / 16))
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * preview.uiScale))
        color: theme.instrument
    }

    GstVideoPreview {
        id: gstPreview
        anchors.fill: parent
        visible: !preview.presentationSuspended
        devicePath: preview.devicePath
        requested: preview.requested && (!preview.mirrorOnly || preview.mirrorSource !== null)
        sourceTitle: preview.sourceTitle
        frameSource: preview.mirrorSource
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * preview.uiScale))
        color: theme.withAlpha(theme.instrument, 0)
        border.color: preview.stateAccent
        border.width: 1
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * preview.uiScale))
        visible: !preview.requested || !preview.active
        color: theme.instrument
        opacity: preview.requested ? 0.52 : 1.0

        Column {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Math.round(40 * preview.uiScale))
            spacing: Math.round(8 * preview.uiScale)

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: preview.requested ? preview.statusText : "实时画面将在这里显示"
                color: theme.instrumentInk
                font.family: theme.displayFont
                font.pixelSize: Math.round(17 * preview.uiScale)
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: preview.devicePath.length > 0 ? preview.devicePath : "等待" + preview.sourceTitle + "节点"
                color: theme.instrumentMuted
                font.family: theme.monoFont
                font.pixelSize: Math.round(12 * preview.uiScale)
                elide: Text.ElideMiddle
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.round(42 * preview.uiScale)
        radius: Math.max(theme.radiusControl, Math.round(theme.radiusPanel * preview.uiScale))
        color: theme.instrumentRaised

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: preview.stateAccent
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(14 * preview.uiScale)
            anchors.right: parent.right
            anchors.rightMargin: Math.round(14 * preview.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            text: preview.statusText
            color: preview.active ? theme.instrumentInk : theme.instrumentMuted
            elide: Text.ElideRight
            font.family: theme.bodyFont
            font.pixelSize: Math.round(12 * preview.uiScale)
            font.weight: Font.DemiBold
        }
    }
}
