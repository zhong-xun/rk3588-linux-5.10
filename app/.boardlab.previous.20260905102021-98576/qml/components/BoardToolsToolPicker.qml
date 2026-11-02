import QtQuick 2.12
import QtQuick.Controls 2.12
import "BoardLabCapabilityMap.js" as BoardLabCapabilities

Item {
    id: picker

    BoardLabTheme { id: theme }

    property real uiScale: 1.0
    property string currentTool: ""
    property var profile: null
    signal toolRequested(string tool)

    readonly property real effectiveUiScale: Math.max(1.0, uiScale)
    readonly property bool compactLayout: parent && parent.width < Math.round(720 * effectiveUiScale)
    readonly property int viewportMargin: Math.round(16 * effectiveUiScale)
    readonly property int popupGap: Math.round(8 * effectiveUiScale)
    readonly property int popupMinimumHeight: Math.round(220 * effectiveUiScale)
    readonly property var connectionTools: picker.filteredTools([
        {"tool": "Ethernet", "iconName": "network", "label": "以太网"},
        {"tool": "UART", "iconName": "usb", "label": "串口 / 485"},
        {"tool": "CAN", "iconName": "network", "label": "CAN"},
        {"tool": "Cellular", "iconName": "radio-tower", "label": "4G / 5G"}
    ])
    readonly property var visionTools: picker.filteredTools([
        {"tool": "Camera", "iconName": "camera", "label": "摄像头"},
        {"tool": "HDMIIN", "iconName": "camera", "label": "HDMI 输入"},
        {"tool": "Backlight", "iconName": "sun-medium", "label": "背光"}
    ])
    readonly property var controlTools: picker.filteredTools([
        {"tool": "GPIO", "iconName": "circuit-board", "label": "GPIO"},
        {"tool": "Music", "iconName": "music-2", "label": "音乐"},
        {"tool": "Buzzer", "iconName": "volume-2", "label": "蜂鸣器"},
        {"tool": "Fan", "iconName": "fan", "label": "风扇"},
        {"tool": "LED", "iconName": "lightbulb", "label": "LED"},
        {"tool": "Watchdog", "iconName": "timer", "label": "看门狗"},
        {"tool": "Files", "iconName": "folder", "label": "文件"}
    ])
    readonly property bool hasConnectionTools: connectionTools.length > 0
    readonly property bool hasVisionTools: visionTools.length > 0
    readonly property bool hasControlTools: controlTools.length > 0
    readonly property int visibleGroupCount: (hasConnectionTools ? 1 : 0)
                                         + (hasVisionTools ? 1 : 0)
                                         + (hasControlTools ? 1 : 0)
    readonly property bool hasAvailableTools: visibleGroupCount > 0

    width: Math.round((compactLayout ? 92 : 126) * effectiveUiScale)
    height: Math.max(44, Math.round(44 * effectiveUiScale))

    function showMenu() {
        if (picker.hasAvailableTools)
            toolMenu.open()
    }

    function hideMenu() {
        toolMenu.close()
    }

    function filteredTools(entries) {
        var visibleTools = []
        for (var index = 0; index < entries.length; ++index) {
            var entry = entries[index]
            if (entry && BoardLabCapabilities.toolEnabled(entry.tool, picker.profile))
                visibleTools.push(entry)
        }
        return visibleTools
    }

    Component {
        id: toolOption

        Button {
            id: option

            property var entry: modelData
            readonly property bool selected: picker.currentTool === (entry && entry.tool ? entry.tool : "")

            width: parent ? parent.width : 0
            height: Math.max(44, Math.round(44 * picker.effectiveUiScale))
            text: entry && entry.label ? entry.label : ""
            activeFocusOnTab: enabled
            hoverEnabled: enabled
            font.family: theme.bodyFont
            font.pixelSize: Math.round(12 * picker.effectiveUiScale)
            font.weight: Font.DemiBold
            scale: down ? 0.985 : 1.0

            Behavior on scale { NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic } }

            onClicked: {
                var nextTool = entry && entry.tool ? entry.tool : ""
                toolMenu.close()
                if (nextTool.length > 0 && nextTool !== picker.currentTool)
                    picker.toolRequested(nextTool)
            }

            contentItem: Item {
                LucideIcon {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(12 * picker.effectiveUiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(18 * picker.effectiveUiScale)
                    height: width
                    name: option.entry && option.entry.iconName ? option.entry.iconName : ""
                    color: option.selected ? theme.accent : theme.inkFaint
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(42 * picker.effectiveUiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(12 * picker.effectiveUiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: option.text
                    color: option.selected ? theme.inkStrong : theme.muted
                    elide: Text.ElideRight
                    font: option.font
                }
            }

            background: Rectangle {
                radius: Math.round(theme.radiusControl * picker.effectiveUiScale)
                border.width: option.activeFocus ? 2 : 1
                border.color: option.activeFocus ? theme.focus
                                                    : (option.selected ? theme.withAlpha(theme.accent, 0.52)
                                                                       : (option.hovered ? theme.ruleStrong : theme.rule))
                color: option.selected ? theme.accentSoft
                                       : (option.hovered ? theme.surfaceSoft : theme.surfaceRaised)
            }
        }
    }

    BoardToolsActionButton {
        id: trigger

        anchors.fill: parent
        visible: picker.hasAvailableTools
        enabled: picker.hasAvailableTools
        uiScale: picker.effectiveUiScale
        touchTarget: picker.height
        text: toolMenu.visible ? (picker.compactLayout ? "收起" : "收起清单")
                               : (picker.compactLayout ? "工具" : "工具清单")
        iconName: "menu"
        accent: theme.accent
        variant: "primary"
        onClicked: toolMenu.visible ? picker.hideMenu() : picker.showMenu()
    }

    Popup {
        id: toolMenu

        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        readonly property bool compactMenu: width < Math.round(520 * picker.effectiveUiScale)
        readonly property point triggerOrigin: parent ? picker.mapToItem(parent, 0, 0) : Qt.point(0, 0)
        readonly property int popupAvailableHeight: parent ? Math.max(1, parent.height - picker.viewportMargin * 2)
                                                          : Math.round(620 * picker.effectiveUiScale)
        readonly property int availableBelow: parent ? Math.max(0, parent.height - triggerOrigin.y - picker.height - picker.popupGap - picker.viewportMargin)
                                                   : popupAvailableHeight
        readonly property int availableAbove: parent ? Math.max(0, triggerOrigin.y - picker.popupGap - picker.viewportMargin)
                                                   : popupAvailableHeight
        readonly property bool opensAbove: availableBelow < height && availableAbove > availableBelow

        width: parent ? Math.max(1, Math.min(Math.round(640 * picker.effectiveUiScale), parent.width - picker.viewportMargin * 2))
                      : Math.round(640 * picker.effectiveUiScale)
        height: Math.min(popupAvailableHeight, Math.max(picker.popupMinimumHeight, menuContent.idealHeight))
        x: parent ? Math.max(picker.viewportMargin,
                             Math.min(triggerOrigin.x + picker.width - width, parent.width - width - picker.viewportMargin))
                  : 0
        y: {
            if (!parent)
                return picker.height + picker.popupGap
            var targetY = opensAbove ? triggerOrigin.y - height - picker.popupGap
                                     : triggerOrigin.y + picker.height + picker.popupGap
            return Math.max(picker.viewportMargin, Math.min(targetY, parent.height - height - picker.viewportMargin))
        }

        background: Rectangle {
            radius: Math.round(theme.radiusPanel * picker.effectiveUiScale)
            color: theme.surfaceRaised
            border.color: theme.rule
            border.width: 1
        }

        contentItem: Item {
            id: menuContent

            readonly property int contentPadding: Math.round(14 * picker.effectiveUiScale)
            readonly property int sectionGap: Math.round(10 * picker.effectiveUiScale)
            readonly property int idealHeight: contentPadding * 2
                                               + titleRow.height
                                               + sectionGap
                                               + toolGroups.implicitHeight
                                               + sectionGap
                                               + footerNote.implicitHeight

            Item {
                id: titleRow

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: menuContent.contentPadding
                height: Math.round(24 * picker.effectiveUiScale)

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: toolMenu.compactMenu ? 0 : Math.round(196 * picker.effectiveUiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "切换硬件工具"
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(14 * picker.effectiveUiScale)
                    font.weight: Font.Bold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !toolMenu.compactMenu
                    width: Math.round(188 * picker.effectiveUiScale)
                    text: "仅切换工作台，不执行测试"
                    color: theme.muted
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(10 * picker.effectiveUiScale)
                }
            }

            Flickable {
                id: toolsFlick

                anchors.left: parent.left
                anchors.leftMargin: menuContent.contentPadding
                anchors.right: parent.right
                anchors.rightMargin: menuContent.contentPadding
                anchors.top: titleRow.bottom
                anchors.topMargin: menuContent.sectionGap
                anchors.bottom: footerNote.top
                anchors.bottomMargin: menuContent.sectionGap
                clip: true
                contentWidth: width
                contentHeight: toolGroups.height
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds

                Grid {
                    id: toolGroups

                    readonly property int activeColumns: toolMenu.compactMenu ? 1 : Math.max(1, picker.visibleGroupCount)
                    readonly property real columnWidth: (width - columnSpacing * Math.max(0, activeColumns - 1)) / activeColumns

                    width: toolsFlick.width
                    height: implicitHeight
                    columns: activeColumns
                    columnSpacing: Math.round(8 * picker.effectiveUiScale)
                    rowSpacing: Math.round(12 * picker.effectiveUiScale)

                    Column {
                        visible: picker.hasConnectionTools
                        width: visible ? toolGroups.columnWidth : 0
                        spacing: Math.round(6 * picker.effectiveUiScale)

                        Text {
                            width: parent.width
                            text: "CONNECTION"
                            color: theme.inkFaint
                            elide: Text.ElideRight
                            font.family: theme.monoFont
                            font.pixelSize: Math.round(10 * picker.effectiveUiScale)
                            font.weight: Font.Bold
                        }

                        Repeater { model: picker.connectionTools; delegate: toolOption }
                    }

                    Column {
                        visible: picker.hasVisionTools
                        width: visible ? toolGroups.columnWidth : 0
                        spacing: Math.round(6 * picker.effectiveUiScale)

                        Text {
                            width: parent.width
                            text: "VISION"
                            color: theme.inkFaint
                            elide: Text.ElideRight
                            font.family: theme.monoFont
                            font.pixelSize: Math.round(10 * picker.effectiveUiScale)
                            font.weight: Font.Bold
                        }

                        Repeater { model: picker.visionTools; delegate: toolOption }
                    }

                    Column {
                        visible: picker.hasControlTools
                        width: visible ? toolGroups.columnWidth : 0
                        spacing: Math.round(6 * picker.effectiveUiScale)

                        Text {
                            width: parent.width
                            text: "CONTROL"
                            color: theme.inkFaint
                            elide: Text.ElideRight
                            font.family: theme.monoFont
                            font.pixelSize: Math.round(10 * picker.effectiveUiScale)
                            font.weight: Font.Bold
                        }

                        Repeater { model: picker.controlTools; delegate: toolOption }
                    }
                }

                ScrollIndicator.vertical: ScrollIndicator {
                    active: toolsFlick.moving || toolsFlick.dragging
                    width: Math.max(2, Math.min(3, Math.round(2 * picker.effectiveUiScale)))
                }
            }

            Text {
                id: footerNote

                anchors.left: parent.left
                anchors.leftMargin: menuContent.contentPadding
                anchors.right: parent.right
                anchors.rightMargin: menuContent.contentPadding
                anchors.bottom: parent.bottom
                anchors.bottomMargin: menuContent.contentPadding
                text: "存储与局域网控制台仍从“返回硬件工具”进入。"
                color: theme.muted
                wrapMode: Text.WordWrap
                font.family: theme.bodyFont
                font.pixelSize: Math.round(10 * picker.effectiveUiScale)
            }
        }
    }
}
