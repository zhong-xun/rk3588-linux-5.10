import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: root

    BoardLabTheme { id: theme }

    property string appTitle: "App"
    property real uiScale: 1.0
    readonly property string boardLabel: boardProfile.displayName.length > 0
                                             ? boardProfile.displayName : "Linux Board"
    readonly property string boardFamilyLabel: boardProfile.socFamily.length > 0
                                                   ? boardProfile.socFamily : "Linux"
    property var profile: buildProfile(appTitle)
    signal closed()

    function practicalProfile(title) {
        var name = title.length > 0 ? root.boardLabel + " 工作区" : root.boardLabel
        return {
            "title": name,
            "iconName": "circuit-board",
            "kind": "list",
            "accent": theme.accent,
            "eyebrow": "北京迅为电子 / BOARDLAB",
            "headline": name,
            "summary": "这里保留板端状态卡片和现场运维信息，内容聚焦设置、独立测试应用和交付验证。",
            "metrics": ["全屏", "中文", root.boardFamilyLabel],
            "actions": ["刷新状态", "留存记录", "关闭"],
            "items": [
                {"iconName": "chart-no-axes-combined", "title": "板端状态", "subtitle": "CPU、内存、温度和网络状态来自本机后端。", "meta": "实时"},
                {"iconName": "circuit-board", "title": "现场验收", "subtitle": "GPIO、UART、CAN、4G、音频等实际操作分别进入独立测试应用。", "meta": "测试"},
                {"iconName": "arrow-left", "title": "工程入口", "subtitle": "主页直接进入各个独立工具，关闭后回到桌面继续验收。", "meta": "交互"}
            ]
        }
    }

    function buildProfile(title) {
        return practicalProfile(title)
    }

    function profileIconName() {
        return root.profile && root.profile.iconName && root.profile.iconName.length > 0
                ? root.profile.iconName : "circuit-board"
    }

    function actionIconName(action) {
        if (action === "刷新状态")
            return "refresh-cw"
        if (action === "留存记录")
            return "hard-drive"
        if (action === "关闭")
            return "x"
        return "chevron-right"
    }

    Rectangle {
        anchors.fill: parent
        color: theme.canvas
    }

    StatusBar {
        id: appStatus
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        active: root.visible
        darkText: true
        uiScale: root.uiScale
    }

    Rectangle {
        id: header
        anchors.top: appStatus.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(72 * root.uiScale)
        color: theme.surfaceRaised
        border.color: theme.rule
        border.width: 1

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: Math.round(22 * root.uiScale)
            anchors.right: doneButton.left
            anchors.rightMargin: Math.round(12 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            text: root.profile.title
            color: theme.inkStrong
            elide: Text.ElideRight
            font.family: theme.displayFont
            font.pixelSize: Math.round(24 * root.uiScale)
            font.weight: Font.Bold
        }

        BoardToolsActionButton {
            id: doneButton
            anchors.right: parent.right
            anchors.rightMargin: Math.round(18 * root.uiScale)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(44, Math.round(44 * root.uiScale))
            height: Math.round(44 * root.uiScale)
            touchTarget: Math.round(44 * root.uiScale)
            uiScale: root.uiScale
            text: ""
            iconName: "x"
            accessibleName: "关闭工作区"
            variant: "secondary"
            onClicked: root.closed()
        }
    }

    Flickable {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: contentColumn.height + Math.round(32 * root.uiScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentColumn
            x: Math.round(18 * root.uiScale)
            y: Math.round(18 * root.uiScale)
            width: Math.max(1, parent.width - Math.round(36 * root.uiScale))
            spacing: Math.round(14 * root.uiScale)

            Rectangle {
                width: parent.width
                height: Math.round(188 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.instrument
                border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.max(3, Math.round(4 * root.uiScale))
                    color: root.profile.accent
                }

                Rectangle {
                    width: Math.round(62 * root.uiScale)
                    height: width
                    radius: Math.round(theme.radiusControl * root.uiScale)
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(18 * root.uiScale)
                    color: theme.withAlpha(theme.instrumentInk, 0.10)

                    LucideIcon {
                        anchors.centerIn: parent
                        width: Math.round(30 * root.uiScale)
                        height: width
                        name: root.profileIconName()
                        color: root.profile.accent
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(96 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(20 * root.uiScale)
                    spacing: Math.round(5 * root.uiScale)

                    Text {
                        width: parent.width
                        text: root.profile.eyebrow
                        color: theme.instrumentMuted
                        elide: Text.ElideRight
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(11 * root.uiScale)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: root.profile.headline
                        color: theme.instrumentInk
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.family: theme.displayFont
                        font.pixelSize: Math.round(22 * root.uiScale)
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(104 * root.uiScale)
                    text: root.profile.summary
                    color: theme.instrumentMuted
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(13 * root.uiScale)
                    lineHeight: 1.12
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(18 * root.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(18 * root.uiScale)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Math.round(14 * root.uiScale)
                    spacing: Math.round(8 * root.uiScale)

                    Repeater {
                        model: root.profile.metrics

                        Rectangle {
                            width: Math.max(Math.round(70 * root.uiScale), metricLabel.implicitWidth + Math.round(22 * root.uiScale))
                            height: Math.round(28 * root.uiScale)
                            radius: Math.round(theme.radiusControl * root.uiScale)
                            color: theme.withAlpha(theme.instrumentInk, 0.10)

                            Text {
                                id: metricLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: theme.instrumentInk
                                font.family: theme.monoFont
                                font.pixelSize: Math.round(11 * root.uiScale)
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Grid {
                id: actionGrid
                width: parent.width
                columns: root.width < Math.round(560 * root.uiScale) ? 1 : 3
                rowSpacing: Math.round(8 * root.uiScale)
                columnSpacing: Math.round(8 * root.uiScale)

                Repeater {
                    model: root.profile.actions

                    BoardToolsActionButton {
                        width: (actionGrid.width - actionGrid.columnSpacing * (actionGrid.columns - 1)) / actionGrid.columns
                        uiScale: root.uiScale
                        touchTarget: Math.round(44 * root.uiScale)
                        text: modelData
                        iconName: root.actionIconName(modelData)
                        accessibleName: modelData
                        variant: modelData === "刷新状态" ? "primary" : "secondary"
                        onClicked: {
                            if (modelData === "关闭")
                                root.closed()
                        }
                    }
                }
            }

            Rectangle {
                visible: root.profile.kind === "gallery"
                width: parent.width
                height: Math.round(160 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Grid {
                    anchors.fill: parent
                    anchors.margins: Math.round(12 * root.uiScale)
                    columns: 3
                    rowSpacing: Math.round(8 * root.uiScale)
                    columnSpacing: Math.round(8 * root.uiScale)

                    Repeater {
                        model: [theme.accent, theme.ready, theme.warning, theme.danger, theme.focus, theme.info]

                        Rectangle {
                            width: (parent.width - parent.columnSpacing * 2) / 3
                            height: (parent.height - parent.rowSpacing) / 2
                            radius: Math.round(theme.radiusControl * root.uiScale)
                            color: modelData
                        }
                    }
                }
            }

            Rectangle {
                visible: root.profile.kind === "camera"
                width: parent.width
                height: Math.round(184 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.instrument
                border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                border.width: 1
                clip: true

                Grid {
                    anchors.fill: parent
                    columns: 3
                    rows: 3

                    Repeater {
                        model: 9

                        Rectangle {
                            width: parent.width / 3
                            height: parent.height / 3
                            color: "transparent"
                            border.color: theme.withAlpha(theme.instrumentInk, 0.16)
                            border.width: 1
                        }
                    }
                }

                Rectangle {
                    width: Math.round(92 * root.uiScale)
                    height: Math.round(64 * root.uiScale)
                    radius: Math.round(theme.radiusControl * root.uiScale)
                    anchors.centerIn: parent
                    color: "transparent"
                    border.color: theme.instrumentInk
                    border.width: 2
                    opacity: 0.88
                }

                Rectangle {
                    width: Math.round(62 * root.uiScale)
                    height: width
                    radius: width / 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Math.round(12 * root.uiScale)
                    color: theme.instrumentInk
                    border.color: theme.withAlpha(theme.instrumentInk, 0.5)
                    border.width: 4
                }
            }

            Rectangle {
                visible: root.profile.kind === "map"
                width: parent.width
                height: Math.round(156 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1
                clip: true

                Repeater {
                    model: [
                        {"x": 18, "y": 96, "w": 150, "h": 18, "c": theme.readySoft},
                        {"x": 82, "y": 34, "w": 190, "h": 18, "c": theme.infoSoft},
                        {"x": 150, "y": 84, "w": 160, "h": 18, "c": theme.warningSoft}
                    ]

                    Rectangle {
                        x: Math.round(modelData.x * root.uiScale)
                        y: Math.round(modelData.y * root.uiScale)
                        width: Math.round(modelData.w * root.uiScale)
                        height: Math.round(modelData.h * root.uiScale)
                        radius: height / 2
                        rotation: index === 0 ? -18 : (index === 1 ? 14 : -8)
                        color: modelData.c
                    }
                }

                Repeater {
                    model: [
                        {"x": 62, "y": 68, "label": "A"},
                        {"x": 168, "y": 52, "label": "B"},
                        {"x": 252, "y": 104, "label": "C"}
                    ]

                    Rectangle {
                        x: Math.round(modelData.x * root.uiScale)
                        y: Math.round(modelData.y * root.uiScale)
                        width: Math.round(34 * root.uiScale)
                        height: width
                        radius: width / 2
                        color: root.profile.accent

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: theme.accentInk
                            font.family: theme.monoFont
                            font.pixelSize: Math.round(14 * root.uiScale)
                            font.weight: Font.Bold
                        }
                    }
                }
            }

            Rectangle {
                visible: root.profile.kind === "music" || root.profile.kind === "weather" || root.profile.kind === "clock"
                width: parent.width
                height: Math.round(112 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: Math.round(16 * root.uiScale)
                    spacing: Math.round(14 * root.uiScale)

                    Rectangle {
                        width: Math.round(78 * root.uiScale)
                        height: width
                        radius: Math.round(theme.radiusControl * root.uiScale)
                        color: theme.accentSoft

                        LucideIcon {
                            anchors.centerIn: parent
                            width: Math.round(30 * root.uiScale)
                            height: width
                            name: root.profileIconName()
                            color: root.profile.accent
                        }
                    }

                    Column {
                        width: parent.width - Math.round(92 * root.uiScale)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Math.round(5 * root.uiScale)

                        Text {
                            width: parent.width
                            text: root.profile.headline
                            color: theme.inkStrong
                            elide: Text.ElideRight
                            font.family: theme.displayFont
                            font.pixelSize: Math.round(20 * root.uiScale)
                            font.weight: Font.Bold
                        }

                        Text {
                            width: parent.width
                            text: root.profile.summary
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.family: theme.bodyFont
                            font.pixelSize: Math.round(13 * root.uiScale)
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: itemColumn.height + Math.round(24 * root.uiScale)
                radius: Math.round(theme.radiusPanel * root.uiScale)
                color: theme.surfaceRaised
                border.color: theme.rule
                border.width: 1

                Column {
                    id: itemColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Math.round(12 * root.uiScale)
                    spacing: 0

                    Repeater {
                        model: root.profile.items

                        Item {
                            width: parent.width
                            height: Math.round(68 * root.uiScale)

                            Rectangle {
                                width: Math.round(38 * root.uiScale)
                                height: width
                                radius: Math.round(theme.radiusControl * root.uiScale)
                                anchors.left: parent.left
                                anchors.leftMargin: Math.round(2 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                color: theme.withAlpha(root.profile.accent, 0.12)

                                LucideIcon {
                                    anchors.centerIn: parent
                                    width: Math.round(20 * root.uiScale)
                                    height: width
                                    name: modelData.iconName && modelData.iconName.length > 0
                                          ? modelData.iconName : "circuit-board"
                                    color: root.profile.accent
                                }
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: Math.round(52 * root.uiScale)
                                anchors.right: metaLabel.left
                                anchors.rightMargin: Math.round(10 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Math.round(3 * root.uiScale)

                                Text {
                                    width: parent.width
                                    text: modelData.title
                                    color: theme.inkStrong
                                    elide: Text.ElideRight
                                    font.family: theme.displayFont
                                    font.pixelSize: Math.round(15 * root.uiScale)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.subtitle
                                    color: theme.muted
                                    elide: Text.ElideRight
                                    font.family: theme.bodyFont
                                    font.pixelSize: Math.round(12 * root.uiScale)
                                }
                            }

                            Text {
                                id: metaLabel
                                anchors.right: parent.right
                                anchors.rightMargin: Math.round(2 * root.uiScale)
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.round(52 * root.uiScale)
                                horizontalAlignment: Text.AlignRight
                                text: modelData.meta
                                color: theme.inkFaint
                                elide: Text.ElideRight
                                font.family: theme.monoFont
                                font.pixelSize: Math.round(11 * root.uiScale)
                            }

                            Rectangle {
                                visible: index < root.profile.items.length - 1
                                anchors.left: parent.left
                                anchors.leftMargin: Math.round(52 * root.uiScale)
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: theme.rule
                            }
                        }
                    }
                }
            }
        }
    }
}
