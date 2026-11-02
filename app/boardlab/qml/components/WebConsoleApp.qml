import QtQuick 2.12
import QtQuick.Controls 2.12

// V2 Signal Workbench LAN console surface.
Item {
    id: root
    objectName: "web-console-app"
    signal closed()

    property real uiScale: 1.0
    BoardLabTheme { id: theme }
    readonly property bool webReady: localWebServer.running && localWebServer.url.length > 0
    readonly property bool localhostBridge: localWebServer.bindAddress === "127.0.0.1"
                                       || localWebServer.bindAddress === "::1"
                                       || localWebServer.bindAddress === "localhost"
    readonly property bool compactLayout: width < Math.round(640 * root.uiScale)
    readonly property int contentMargin: Math.round((compactLayout ? 18 : 24) * root.uiScale)

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.round(120 * root.uiScale)
        color: theme.instrument
    }

    StatusBar {
        id: appStatus
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        active: root.visible
        darkText: false
        uiScale: root.uiScale
    }

    BoardToolsActionButton {
        z: 2
        anchors.top: appStatus.bottom
        anchors.topMargin: Math.round(18 * root.uiScale)
        anchors.right: parent.right
        anchors.rightMargin: Math.round(24 * root.uiScale)
        uiScale: root.uiScale
        width: Math.round(112 * root.uiScale)
        height: Math.max(44, Math.round(44 * root.uiScale))
        text: "关闭"
        variant: "secondary"
        onClicked: root.closed()
    }

    Flickable {
        id: webScroller
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: appStatus.bottom
        anchors.topMargin: Math.round(8 * root.uiScale)
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: webContent.y + webContent.implicitHeight + root.contentMargin
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        ScrollIndicator.vertical: ScrollIndicator {
            active: webScroller.moving || webScroller.dragging
        }

        Column {
            id: webContent
            x: Math.round((webScroller.width - width) / 2)
            y: Math.max(root.contentMargin, Math.round((webScroller.height - implicitHeight) / 2))
            width: Math.min(webScroller.width - root.contentMargin * 2, Math.round(760 * root.uiScale))
            spacing: Math.round(18 * root.uiScale)

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "手机扫码进入 Web 控制台"
                color: theme.ink
                font.family: theme.displayFont
                font.pixelSize: Math.round((root.compactLayout ? 26 : 31) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "请将手机或电脑与开发板连接到同一局域网，然后扫描二维码"
                color: theme.muted
                font.family: theme.bodyFont
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(14 * root.uiScale)
            }

            Rectangle {
                visible: root.webReady
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, Math.round(280 * root.uiScale))
                height: width
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Image {
                    objectName: "web-qr-image"
                    anchors.fill: parent
                    anchors.margins: Math.round(18 * root.uiScale)
                    source: root.webReady ? ("image://qr/" + encodeURIComponent(localWebServer.url)) : ""
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.round(480 * root.uiScale)
                    sourceSize.height: Math.round(480 * root.uiScale)
                }
            }

            Text {
                objectName: "web-lan-url"
                visible: root.webReady
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: localWebServer.url
                color: root.localhostBridge ? theme.danger : theme.accent
                font.family: theme.monoFont
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Math.round((root.compactLayout ? 18 : 22) * root.uiScale)
                font.weight: Font.Bold
            }

            Text {
                visible: root.webReady && localWebServer.statusText.length > 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: localWebServer.statusText
                color: theme.muted
                font.family: theme.bodyFont
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(13 * root.uiScale)
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, Math.round(430 * root.uiScale))
                spacing: Math.round(10 * root.uiScale)

                Rectangle {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.round(56 * root.uiScale)
                    radius: Math.round(theme.radiusPanel * root.uiScale)
                    color: theme.surfaceRaised
                    border.color: theme.rule
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - Math.round(12 * root.uiScale)
                        spacing: Math.round(3 * root.uiScale)
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "访问 Token"
                            color: theme.muted
                            font.family: theme.bodyFont
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "出厂默认 topeet"
                            color: theme.ink
                            font.family: theme.monoFont
                            elide: Text.ElideRight
                            font.pixelSize: Math.round((root.compactLayout ? 11 : 13) * root.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Rectangle {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.round(56 * root.uiScale)
                    radius: Math.round(theme.radiusPanel * root.uiScale)
                    color: theme.warningSoft
                    border.color: theme.warning
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - Math.round(12 * root.uiScale)
                        spacing: Math.round(3 * root.uiScale)
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "安全提示"
                            color: theme.warning
                            font.family: theme.bodyFont
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(10 * root.uiScale)
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "仅在可信局域网使用"
                            color: theme.ink
                            font.family: theme.bodyFont
                            elide: Text.ElideRight
                            font.pixelSize: Math.round((root.compactLayout ? 11 : 13) * root.uiScale)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }
}
