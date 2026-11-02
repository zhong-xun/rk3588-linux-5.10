import QtQuick 2.12

Loader {
    id: root
    property url pageSource
    property bool selected: false
    property real uiScale: 1.0
    property string toolName: ""
    signal closed()
    signal toolRequested(string tool)

    active: false
    asynchronous: true
    visible: selected && status === Loader.Ready
    enabled: selected && status === Loader.Ready

    function open() {
        if (source.toString().length > 0)
            return
        var properties = { "uiScale": root.uiScale }
        if (root.toolName.length > 0)
            properties.standaloneTool = root.toolName
        active = true
        setSource(pageSource, properties)
    }

    function retry() {
        source = ""
        open()
    }

    function updateTool() {
        if (item && toolName.length > 0) {
            item.standaloneTool = toolName
            item.currentTool = toolName
        }
    }

    onSelectedChanged: if (selected) open()
    onToolNameChanged: updateTool()
    Component.onCompleted: if (selected) open()
    onLoaded: {
        updateTool()
        if (selected)
            item.forceActiveFocus()
    }

    // Retain visited pages so form input and completed test logs survive a
    // return to the directory. Hidden pages still receive visibility changes.
    Connections {
        target: root.item
        ignoreUnknownSignals: true
        onClosed: root.closed()
        onToolRequested: root.toolRequested(tool)
    }
}
