import QtQuick 2.12

Item {
    id: surface

    BoardLabTheme { id: theme }

    property string leftTitle: ""
    property string middleTitle: ""
    property string rightTitle: ""
    property Component leftContent
    property Component middleContent
    property Component rightContent
    property int pageIndex: 0
    // On compact displays a work surface becomes a tabbed sequence.  Most
    // tools begin with their configuration, while read-only/live tools can
    // opt into the result page so the first thing a customer sees is the
    // current board state instead of a control form.
    property int narrowStartPage: 0
    property string mainPageLabel: ""
    property string detailPageLabel: "状态"
    property string logPageLabel: "日志"
    property Component logContent
    property real uiScale: 1.0
    // BoardToolsApp embeds this surface directly in its body, while the
    // standalone applications place it below their own header. Keep the
    // original fill behavior for embedded tools and let standalone callers
    // opt out so external anchors are not overwritten.
    property bool fillParent: true

    readonly property bool hasDetailPage: rightTitle.length > 0 && rightContent !== null
    readonly property bool hasLogPage: logContent !== null
    readonly property bool portraitLayout: height > width
    // 1280x720 is a common customer desktop size.  The individual panes
    // scroll their own content, so keep the three-column workbench there and
    // only collapse after the usable pane height becomes genuinely cramped.
    readonly property bool narrowLayout: width < Math.round(1080 * uiScale)
                                         || height < Math.round(520 * uiScale)
                                         || portraitLayout
    readonly property bool showDetailInline: !narrowLayout && hasDetailPage
    readonly property int detailPageIndex: narrowLayout ? 2 : 1
    readonly property int logPageIndex: narrowLayout ? 3 : 2
    readonly property int visibleTabCount: narrowLayout
                                         ? 1 + 1 + (hasDetailPage ? 1 : 0) + (hasLogPage ? 1 : 0)
                                         : 0
    readonly property int narrowTabColumns: visibleTabCount <= 2
                                           ? Math.max(1, visibleTabCount)
                                           : (width < Math.round(520 * uiScale) ? 2 : visibleTabCount)
    readonly property int narrowTabRows: Math.ceil(visibleTabCount / Math.max(1, narrowTabColumns))
    readonly property string effectiveMainPageLabel: mainPageLabel.length > 0 ? mainPageLabel : (leftTitle.length > 0 ? leftTitle : "配置")
    readonly property int maximumContentWidth: Math.round(1760 * uiScale)

    Component {
        id: emptyLogContent
        BoardToolsLogPanel {
            uiScale: surface.uiScale
            text: "等待操作\n\n完成配置或执行后，最新状态会显示在这里。"
        }
    }

    onNarrowLayoutChanged: pageIndex = narrowLayout ? narrowStartPage : 0
    Component.onCompleted: pageIndex = narrowLayout ? narrowStartPage : 0
    anchors.fill: surface.fillParent ? parent : null

    Item {
        id: contentFrame
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(parent.width, surface.maximumContentWidth)
        x: Math.round((parent.width - width) / 2)
    }

    Grid {
        id: pageTabs
        parent: contentFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Math.round(theme.space12 * surface.uiScale)
        visible: surface.visibleTabCount > 1
        columns: surface.narrowTabColumns
        flow: Grid.LeftToRight
        height: visible
                ? tabHeight * surface.narrowTabRows + spacing * Math.max(0, surface.narrowTabRows - 1)
                : 0
        spacing: Math.round(theme.space8 * surface.uiScale)
        property int tabHeight: Math.max(44, Math.round(44 * surface.uiScale))
        property real tabWidth: Math.max(1, (width - spacing * Math.max(0, columns - 1)) / Math.max(1, columns))

        BoardToolsToolButtonLite {
            uiScale: surface.uiScale
            width: pageTabs.tabWidth
            text: surface.effectiveMainPageLabel
            checkedState: surface.pageIndex === 0
            onTriggered: surface.pageIndex = 0
        }
        BoardToolsToolButtonLite {
            uiScale: surface.uiScale
            visible: surface.narrowLayout
            width: pageTabs.tabWidth
            text: surface.middleTitle.length > 0 ? surface.middleTitle : "操作"
            checkedState: surface.pageIndex === 1
            onTriggered: surface.pageIndex = 1
        }
        BoardToolsToolButtonLite {
            uiScale: surface.uiScale
            visible: surface.hasDetailPage && !surface.showDetailInline
            width: pageTabs.tabWidth
            text: surface.detailPageLabel
            checkedState: surface.pageIndex === surface.detailPageIndex
            onTriggered: surface.pageIndex = surface.detailPageIndex
        }
        BoardToolsToolButtonLite {
            uiScale: surface.uiScale
            visible: surface.hasLogPage
            width: pageTabs.tabWidth
            text: surface.logPageLabel
            checkedState: surface.pageIndex === surface.logPageIndex
            onTriggered: surface.pageIndex = surface.logPageIndex
        }
    }

    Item {
        id: paneArea
        parent: contentFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 0
        anchors.topMargin: pageTabs.visible
                           ? pageTabs.height + Math.round(theme.space24 * surface.uiScale)
                           : 0
        clip: true

        BoardToolsWorkPane {
            id: leftPane
            visible: surface.narrowLayout ? surface.pageIndex === 0 : true
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: surface.narrowLayout ? parent.width : Math.max(Math.round(300 * surface.uiScale), Math.min(Math.round(390 * surface.uiScale), parent.width * 0.27))
            title: surface.leftTitle
            content: surface.leftContent
            uiScale: surface.uiScale
            loadContent: surface.visible && visible
        }

        BoardToolsWorkPane {
            id: middlePane
            visible: surface.narrowLayout ? surface.pageIndex === 1 : true
            anchors.left: surface.narrowLayout ? parent.left : leftPane.right
            anchors.leftMargin: surface.narrowLayout ? 0 : Math.round(theme.space16 * surface.uiScale)
            anchors.right: surface.showDetailInline ? inlineDetailColumn.left : parent.right
            anchors.rightMargin: surface.showDetailInline ? Math.round(theme.space16 * surface.uiScale) : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            title: surface.middleTitle
            content: surface.middleContent
            uiScale: surface.uiScale
            loadContent: surface.visible && visible
        }

        Item {
            id: inlineDetailColumn
            visible: surface.showDetailInline
            width: Math.max(Math.round(320 * surface.uiScale), Math.min(Math.round(430 * surface.uiScale), parent.width * 0.29))
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            BoardToolsWorkPane {
                id: inlineDetailPane
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: surface.hasLogPage
                        ? Math.max(Math.round(250 * surface.uiScale), (parent.height - Math.round(theme.space16 * surface.uiScale)) * 0.60)
                        : parent.height
                title: surface.rightTitle
                content: surface.rightContent
                uiScale: surface.uiScale
                loadContent: surface.visible && visible
            }

            BoardToolsWorkPane {
                id: inlineLogPane
                visible: surface.hasLogPage
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: inlineDetailPane.bottom
                anchors.topMargin: Math.round(theme.space16 * surface.uiScale)
                anchors.bottom: parent.bottom
                title: surface.logPageLabel
                content: surface.hasLogPage ? surface.logContent : emptyLogContent
                uiScale: surface.uiScale
                loadContent: surface.visible && visible
            }
        }

        BoardToolsWorkPane {
            id: detailPagePane
            visible: !surface.showDetailInline && surface.hasDetailPage && surface.pageIndex === surface.detailPageIndex
            anchors.fill: parent
            title: surface.rightTitle
            content: surface.rightContent
            uiScale: surface.uiScale
            loadContent: surface.visible && visible
        }

        BoardToolsWorkPane {
            id: logPagePane
            visible: !surface.showDetailInline && surface.hasLogPage && surface.pageIndex === surface.logPageIndex
            anchors.fill: parent
            title: surface.logPageLabel
            content: surface.logContent
            uiScale: surface.uiScale
            loadContent: surface.visible && visible
        }
    }
}
