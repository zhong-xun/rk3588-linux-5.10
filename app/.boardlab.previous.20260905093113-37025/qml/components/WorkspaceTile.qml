import QtQuick 2.12
import QtQuick.Controls 2.12

Item {
    id: workspaceTile

    BoardLabTheme { id: theme }

    property string title: ""
    property string iconName: ""
    property string detail: ""
    property string indexLabel: ""
    property color topColor: theme.accent
    property real uiScale: 1.0
    signal triggered()

    readonly property bool wideCard: width >= Math.round(560 * uiScale)
    readonly property bool tallCard: height >= Math.round(220 * uiScale)

    height: Math.round(112 * uiScale)
    scale: press.down ? 0.992 : 1.0
    Accessible.role: Accessible.Button
    Accessible.name: workspaceTile.title
    Accessible.description: workspaceTile.detail

    Behavior on scale {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    Button {
        id: press
        anchors.fill: parent
        activeFocusOnTab: workspaceTile.enabled
        hoverEnabled: workspaceTile.enabled
        Accessible.name: workspaceTile.title
        Accessible.description: workspaceTile.detail
        onClicked: workspaceTile.triggered()

        background: Rectangle {
            radius: Math.round(theme.radiusControl * workspaceTile.uiScale)
            color: press.down ? theme.surfaceSoft : (press.hovered ? theme.surfaceRaised : theme.surface)
            border.color: press.activeFocus ? theme.focus : (press.hovered ? theme.ruleStrong : theme.rule)
            border.width: press.activeFocus ? 2 : 1

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(3, Math.round(4 * workspaceTile.uiScale))
                color: workspaceTile.topColor
            }

            Behavior on color {
                ColorAnimation { duration: theme.durationFast }
            }
            Behavior on border.color {
                ColorAnimation { duration: theme.durationFast }
            }
        }

        contentItem: Item {
            width: press.availableWidth
            height: press.availableHeight

            Rectangle {
                id: codeCell
                visible: !workspaceTile.tallCard
                anchors.left: parent.left
                anchors.leftMargin: Math.round(18 * workspaceTile.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(42 * workspaceTile.uiScale)
                height: width
                radius: Math.round(theme.radiusControl * workspaceTile.uiScale)
                color: theme.withAlpha(workspaceTile.topColor, 0.14)
                border.color: theme.withAlpha(workspaceTile.topColor, 0.30)
                border.width: 1

                LucideIcon {
                    anchors.centerIn: parent
                    width: Math.round(21 * workspaceTile.uiScale)
                    height: width
                    name: workspaceTile.iconName
                    color: workspaceTile.topColor
                }
            }

            Column {
                visible: !workspaceTile.tallCard
                anchors.left: codeCell.right
                anchors.leftMargin: Math.round(14 * workspaceTile.uiScale)
                anchors.right: actionMark.left
                anchors.rightMargin: Math.round(14 * workspaceTile.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(4 * workspaceTile.uiScale)

                Text {
                    width: parent.width
                    text: workspaceTile.title
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.family: theme.displayFont
                    font.pixelSize: Math.round(16 * workspaceTile.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    text: workspaceTile.detail
                    color: theme.muted
                    elide: Text.ElideRight
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(12 * workspaceTile.uiScale)
                }
            }

            Item {
                id: actionMark
                visible: !workspaceTile.tallCard
                anchors.right: parent.right
                anchors.rightMargin: Math.round(16 * workspaceTile.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                width: workspaceTile.wideCard ? Math.round(64 * workspaceTile.uiScale)
                                              : Math.round(20 * workspaceTile.uiScale)
                height: Math.round(48 * workspaceTile.uiScale)

                Rectangle {
                    visible: workspaceTile.wideCard
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: theme.rule
                }

                Text {
                    visible: workspaceTile.wideCard && workspaceTile.indexLabel.length > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(2 * workspaceTile.uiScale)
                    text: workspaceTile.indexLabel
                    color: theme.inkFaint
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(10 * workspaceTile.uiScale)
                    font.weight: Font.DemiBold
                }

                LucideIcon {
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: Math.round(15 * workspaceTile.uiScale)
                    height: width
                    name: "chevron-right"
                    color: press.hovered || press.activeFocus ? workspaceTile.topColor : theme.muted
                }
            }

            Item {
                id: tallCardContent
                visible: workspaceTile.tallCard
                anchors.fill: parent

                Rectangle {
                    id: tallCodeCell
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(20 * workspaceTile.uiScale)
                    width: Math.round(48 * workspaceTile.uiScale)
                    height: width
                    radius: Math.round(theme.radiusControl * workspaceTile.uiScale)
                    color: theme.withAlpha(workspaceTile.topColor, 0.14)
                    border.color: theme.withAlpha(workspaceTile.topColor, 0.30)
                    border.width: 1

                    LucideIcon {
                        anchors.centerIn: parent
                        width: Math.round(24 * workspaceTile.uiScale)
                        height: width
                        name: workspaceTile.iconName
                        color: workspaceTile.topColor
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.top: parent.top
                    anchors.topMargin: Math.round(24 * workspaceTile.uiScale)
                    spacing: Math.round(9 * workspaceTile.uiScale)

                    Text {
                        visible: workspaceTile.indexLabel.length > 0
                        text: workspaceTile.indexLabel
                        color: theme.inkFaint
                        font.family: theme.monoFont
                        font.pixelSize: Math.round(10 * workspaceTile.uiScale)
                        font.weight: Font.DemiBold
                    }

                    LucideIcon {
                        width: Math.round(16 * workspaceTile.uiScale)
                        height: width
                        name: "chevron-right"
                        color: press.hovered || press.activeFocus ? workspaceTile.topColor : theme.muted
                    }
                }

                Text {
                    id: tallCardTitle
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.top: tallCodeCell.bottom
                    anchors.topMargin: Math.round(18 * workspaceTile.uiScale)
                    text: workspaceTile.title
                    color: theme.inkStrong
                    elide: Text.ElideRight
                    font.family: theme.displayFont
                    font.pixelSize: Math.round(20 * workspaceTile.uiScale)
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.right: parent.right
                    anchors.rightMargin: Math.round(20 * workspaceTile.uiScale)
                    anchors.top: tallCardTitle.bottom
                    anchors.topMargin: Math.round(10 * workspaceTile.uiScale)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Math.round(20 * workspaceTile.uiScale)
                    text: workspaceTile.detail
                    color: theme.muted
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignBottom
                    font.family: theme.bodyFont
                    font.pixelSize: Math.round(12 * workspaceTile.uiScale)
                    lineHeight: 1.25
                }
            }
        }
    }
}
