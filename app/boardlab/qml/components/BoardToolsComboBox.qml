import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Window 2.12

ComboBox {
    id: control
    BoardLabTheme { id: theme }
    property color accent: theme.accent
    // Callers provide the owning window's scale. Never infer it from Screen:
    // a framed compact window may live on a 4K desktop.
    property real uiScale: 1.0
    property int baseTouchTarget: 46
    property int baseDelegateHeight: 48
    property int popupZ: 10000000
    property int popupVisibleRows: 7
    property int popupViewportMargin: Math.round(16 * effectiveUiScale)
    property int popupMinimumWidth: Math.round(360 * effectiveUiScale)
    property int popupMaximumWidth: Math.round(880 * effectiveUiScale)
    property bool openOnReady: false
    property int openDelayMs: 650
    property int openAttempts: 0
    readonly property var popupHost: Overlay.overlay ? Overlay.overlay : (control.Window.window && control.Window.window.contentItem ? control.Window.window.contentItem : control)
    readonly property real effectiveUiScale: Math.max(1.0, uiScale)
    readonly property int scaledTouchTarget: Math.round(baseTouchTarget * effectiveUiScale)
    readonly property int scaledDelegateHeight: Math.round(baseDelegateHeight * effectiveUiScale)
    readonly property int popupDelegateHeight: Math.round(baseDelegateHeight * effectiveUiScale)
    readonly property int fieldHorizontalPadding: Math.round(14 * effectiveUiScale)
    readonly property int arrowAreaWidth: Math.round(36 * effectiveUiScale)
    readonly property int textPixelSize: Math.round(13 * effectiveUiScale)
    readonly property int popupRowSpacing: Math.round(2 * effectiveUiScale)
    readonly property int popupGap: Math.round(6 * effectiveUiScale)
    readonly property int popupPadding: Math.round(6 * effectiveUiScale)
    readonly property int popupMinimumRows: Math.min(Math.max(1, count), 4)
    readonly property int popupComfortRows: Math.min(Math.max(1, count), popupVisibleRows)
    readonly property int delegateFontPixelSize: Math.round(13 * effectiveUiScale)
    readonly property int minimumPopupHeight: popupMinimumRows * popupDelegateHeight
                                              + Math.max(0, popupMinimumRows - 1) * popupRowSpacing
                                              + popupPadding * 2
    readonly property int comfortablePopupHeight: popupComfortRows * popupDelegateHeight
                                                  + Math.max(0, popupComfortRows - 1) * popupRowSpacing
                                                  + popupPadding * 2
    readonly property int popupMaximumHeight: Math.max(scaledDelegateHeight + popupPadding * 2,
                                                       Math.round(380 * effectiveUiScale))
    readonly property int popupMaxHeight: popupMaximumHeight

    implicitHeight: Math.max(44, scaledTouchTarget)
    height: Math.max(44, scaledTouchTarget)
    scale: control.down || control.pressed ? 0.985 : 1.0
    Behavior on scale { NumberAnimation { duration: theme.motionDuration(80); easing.type: Easing.OutCubic } }
    activeFocusOnTab: enabled
    hoverEnabled: enabled
    font.pixelSize: textPixelSize
    font.weight: Font.DemiBold
    font.family: theme.bodyFont

    function openPopupForVisualQa() {
        if (!openOnReady || !enabled || count <= 0)
            return
        if (!visible && openAttempts < 8) {
            openAttempts += 1
            openOnReadyTimer.restart()
            return
        }
        forceActiveFocus()
        popup.open()
    }

    function textElideMode(value) {
        return String(value).indexOf("/") >= 0 ? Text.ElideMiddle : Text.ElideRight
    }

    onOpenOnReadyChanged: {
        openAttempts = 0
        if (openOnReady)
            openOnReadyTimer.restart()
    }

    Component.onCompleted: {
        if (openOnReady)
            openOnReadyTimer.restart()
    }

    Timer {
        id: openOnReadyTimer
        interval: control.openDelayMs
        repeat: false
        onTriggered: control.openPopupForVisualQa()
    }

    Item {
        objectName: control.objectName.length > 0 ? control.objectName + "-popup" : ""
        visible: control.popup.visible
        width: visible ? 1 : 0
        height: visible ? 1 : 0
    }

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.leftMargin: control.fieldHorizontalPadding
            anchors.right: parent.right
            anchors.rightMargin: control.arrowAreaWidth
            anchors.verticalCenter: parent.verticalCenter
            text: control.displayText
            color: control.enabled ? theme.ink : theme.inkFaint
            elide: control.textElideMode(control.displayText)
            font: control.font
            verticalAlignment: Text.AlignVCenter
        }
    }

    indicator: Item {
        width: control.arrowAreaWidth
        height: control.height
        x: control.width - width
        y: 0

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: Math.round(22 * control.effectiveUiScale)
            color: theme.rule
        }

        LucideIcon {
            anchors.centerIn: parent
            color: control.enabled ? theme.muted : theme.inkFaint
            width: Math.round(18 * control.effectiveUiScale)
            height: width
            name: "chevron-down"
        }
    }

    background: Rectangle {
        border.color: control.popup.visible || control.activeFocus ? theme.focus : theme.ruleStrong
        border.width: control.popup.visible || control.activeFocus ? 2 : 1
        radius: Math.round(theme.radiusControl * control.effectiveUiScale)
        color: control.enabled ? (control.hovered ? theme.surfaceSoft : theme.surfaceRaised) : theme.canvasAlt
    }

    delegate: ItemDelegate {
        id: optionDelegate
        objectName: control.objectName.length > 0 ? control.objectName + "-delegate-" + index : ""
        function optionText() {
            var text = control.textAt(index)
            if (text && text.length > 0)
                return text
            if (control.textRole.length > 0) {
                if (typeof model !== "undefined" && model[control.textRole] !== undefined)
                    return String(model[control.textRole])
                if (typeof modelData !== "undefined" && modelData !== null && modelData[control.textRole] !== undefined)
                    return String(modelData[control.textRole])
            }
            if (typeof model !== "undefined" && model.display !== undefined)
                return String(model.display)
            if (typeof modelData !== "undefined" && modelData !== null)
                return String(modelData)
            return control.textAt(index)
        }
        function optionDetailText() {
            if (typeof model !== "undefined") {
                var details = []
                if (model.path !== undefined && String(model.path).length > 0)
                    details.push(String(model.path))
                if (model.group !== undefined && String(model.group).length > 0)
                    details.push(String(model.group))
                if (model.name !== undefined && String(model.name).length > 0 && details.indexOf(String(model.name)) < 0)
                    details.push(String(model.name))
                if (details.length > 0)
                    return details.join(" / ")
            }
            return ""
        }
        width: control.popup.width
        height: Math.max(control.popupDelegateHeight, optionDelegate.optionDetailText().length > 0 ? Math.round(60 * control.effectiveUiScale) : control.popupDelegateHeight)
        hoverEnabled: true
        highlighted: control.highlightedIndex === index

        contentItem: Column {
            anchors.left: parent.left
            anchors.leftMargin: control.fieldHorizontalPadding
            anchors.right: parent.right
            anchors.rightMargin: control.fieldHorizontalPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(2 * control.effectiveUiScale)

            Text {
                width: parent.width
                text: optionDelegate.optionText()
                color: optionDelegate.highlighted ? theme.accentInk
                                                   : (control.currentIndex === index ? control.accent : theme.ink)
                elide: control.textElideMode(optionDelegate.optionText())
                font.pixelSize: control.delegateFontPixelSize
                font.weight: optionDelegate.highlighted || control.currentIndex === index ? Font.DemiBold : Font.Normal
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                width: parent.width
                visible: optionDelegate.optionDetailText().length > 0
                text: optionDelegate.optionDetailText()
                color: optionDelegate.highlighted ? theme.withAlpha(theme.accentInk, 0.78) : theme.muted
                elide: Text.ElideMiddle
                font.pixelSize: Math.max(11, Math.round(11 * control.effectiveUiScale))
                verticalAlignment: Text.AlignVCenter
            }
        }

        background: Rectangle {
            radius: Math.round(theme.radiusControl * control.effectiveUiScale)
            color: optionDelegate.highlighted ? control.accent
                                               : (control.currentIndex === index ? theme.withAlpha(control.accent, 0.12)
                                                                                 : (optionDelegate.hovered ? theme.surfaceSoft : theme.surfaceRaised))
            border.color: control.currentIndex === index && !optionDelegate.highlighted ? theme.withAlpha(control.accent, 0.52) : theme.rule
            border.width: control.currentIndex === index && !optionDelegate.highlighted ? 1 : 0
        }
    }

    popup: Popup {
        id: popupPanel
        objectName: control.objectName.length > 0 ? control.objectName + "-popup-panel" : ""
        parent: control.popupHost
        readonly property point popupPanelControlOrigin: parent ? control.mapToItem(parent, 0, 0) : Qt.point(0, 0)
        readonly property int popupPanelSafeMinimumHeight: control.popupDelegateHeight + topPadding + bottomPadding
        readonly property int popupPanelRowCount: control.delegateModel ? control.count : 0
        readonly property int popupPanelDesiredHeight: popupPanelRowCount * control.popupDelegateHeight
                                                       + Math.max(0, popupPanelRowCount - 1) * control.popupRowSpacing
                                                       + topPadding + bottomPadding
        readonly property int popupPanelAvailableBelow: Math.max(popupPanelSafeMinimumHeight,
                                                                 parent ? parent.height - popupPanelControlOrigin.y - control.height - control.popupGap - control.popupViewportMargin
                                                                        : control.popupMaxHeight)
        readonly property int popupPanelAvailableAbove: Math.max(popupPanelSafeMinimumHeight,
                                                                 parent ? popupPanelControlOrigin.y - control.popupGap - control.popupViewportMargin
                                                                        : control.popupMaxHeight)
        readonly property bool popupPanelOpenAbove: popupPanelAvailableBelow < control.minimumPopupHeight && popupPanelAvailableAbove > popupPanelAvailableBelow
        readonly property int popupPanelAvailableHeight: popupPanelOpenAbove ? popupPanelAvailableAbove : popupPanelAvailableBelow
        readonly property int popupPanelViewportHeight: parent ? Math.max(popupPanelSafeMinimumHeight,
                                                                          parent.height - control.popupViewportMargin * 2)
                                                               : control.popupMaximumHeight
        readonly property real popupPanelTargetY: popupPanelOpenAbove
                                                  ? popupPanelControlOrigin.y - height - control.popupGap
                                                  : popupPanelControlOrigin.y + control.height + control.popupGap
        readonly property real popupPanelMaxY: parent
                                               ? Math.max(control.popupViewportMargin,
                                                          parent.height - height - control.popupViewportMargin)
                                               : 0
        x: parent ? Math.max(control.popupViewportMargin,
                             Math.min(popupPanelControlOrigin.x, parent.width - width - control.popupViewportMargin))
                  : 0
        y: parent ? Math.max(control.popupViewportMargin,
                             Math.min(popupPanelMaxY, popupPanelTargetY))
                  : control.height + control.popupGap
        width: parent ? Math.max(1,
                                 Math.min(Math.max(control.width, control.popupMinimumWidth),
                                          Math.min(control.popupMaximumWidth,
                                                   parent.width - control.popupViewportMargin * 2)))
                      : control.width
        height: Math.min(control.comfortablePopupHeight,
                         Math.max(popupPanelSafeMinimumHeight,
                                  Math.min(Math.max(control.minimumPopupHeight, popupPanelDesiredHeight),
                                           Math.min(control.popupMaximumHeight,
                                                    Math.min(popupPanelAvailableHeight, popupPanelViewportHeight)))))
        implicitHeight: height
        padding: control.popupPadding
        z: control.popupZ
        modal: true
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ListView {
            id: popupList
            objectName: control.objectName.length > 0 ? control.objectName + "-popup-list" : ""
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: control.popupRowSpacing
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {
                active: popupList.moving || popupList.dragging
                width: Math.max(2, Math.min(3, Math.round(2 * control.effectiveUiScale)))
            }
        }

        background: Rectangle {
            color: theme.surfaceRaised
            border.color: theme.rule
            border.width: 1
            radius: Math.round(theme.radiusPanel * control.effectiveUiScale)
        }
    }
}
