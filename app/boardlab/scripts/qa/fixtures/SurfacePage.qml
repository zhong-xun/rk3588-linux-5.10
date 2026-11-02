import QtQuick 2.12

Item {
    id: root
    property real uiScale: 1
    property string standaloneTool: ""
    property string currentTool: ""
    property string editedValue: "initial"
    property int samples: 0
    property int activations: 0
    signal closed()
    signal toolRequested(string tool)
    onVisibleChanged: if (visible) activations++
    Timer {
        interval: 20
        running: root.visible
        repeat: true
        onTriggered: root.samples++
    }
}
