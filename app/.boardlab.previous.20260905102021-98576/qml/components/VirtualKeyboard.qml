import QtQuick 2.12
import QtQuick.Controls 2.12

// Touch-oriented on-screen keyboard for kiosk boards without a physical keyboard.
// Pure QML: works on Buildroot/Weston, Ubuntu, and Debian without Qt Virtual Keyboard.
Item {
    id: root

    BoardLabTheme { id: theme }

    property real uiScale: 1.0
    property Item target: null
    property bool passwordMode: false

    readonly property int keyHeight: Math.round(52 * uiScale)
    readonly property int keyRadius: Math.min(8, Math.round(8 * uiScale))
    readonly property int keyGap: Math.round(6 * uiScale)
    readonly property int sideMargin: Math.round(10 * uiScale)
    readonly property int panelHeight: Math.round(292 * uiScale)

    signal hideRequested()
    signal enterPressed()

    property bool shiftLatched: false
    property bool shiftOnce: false
    property int layoutIndex: 0 // 0=letters, 1=numbers/symbols, 2=more symbols

    width: parent ? parent.width : 800
    height: panelHeight
    visible: false
    z: 10000

    function activeEditable() {
        if (!root.target)
            return null
        if (root.target.cursorPosition === undefined)
            return null
        return root.target
    }

    function insertText(value) {
        var field = root.activeEditable()
        if (!field || value.length === 0)
            return
        var pos = Number(field.cursorPosition)
        if (isNaN(pos) || pos < 0)
            pos = String(field.text || "").length

        if (field.selectedText && String(field.selectedText).length > 0
                && field.selectionStart !== undefined && field.selectionEnd !== undefined) {
            var selStart = Math.min(field.selectionStart, field.selectionEnd)
            var selEnd = Math.max(field.selectionStart, field.selectionEnd)
            if (field.remove !== undefined)
                field.remove(selStart, selEnd)
            else
                field.text = String(field.text).substring(0, selStart) + String(field.text).substring(selEnd)
            pos = selStart
            if (field.cursorPosition !== undefined)
                field.cursorPosition = pos
        }

        if (field.insert !== undefined) {
            field.insert(pos, value)
        } else {
            var current = String(field.text || "")
            field.text = current.substring(0, pos) + value + current.substring(pos)
            if (field.cursorPosition !== undefined)
                field.cursorPosition = pos + value.length
        }

        if (root.shiftOnce && !root.shiftLatched)
            root.shiftOnce = false
    }

    function backspace() {
        var field = root.activeEditable()
        if (!field)
            return
        var pos = Number(field.cursorPosition)
        if (isNaN(pos))
            pos = 0

        if (field.selectedText && String(field.selectedText).length > 0
                && field.selectionStart !== undefined && field.selectionEnd !== undefined) {
            var start = Math.min(field.selectionStart, field.selectionEnd)
            var end = Math.max(field.selectionStart, field.selectionEnd)
            if (field.remove !== undefined)
                field.remove(start, end)
            else
                field.text = String(field.text).substring(0, start) + String(field.text).substring(end)
            field.cursorPosition = start
            return
        }
        if (pos <= 0)
            return
        if (field.remove !== undefined)
            field.remove(pos - 1, pos)
        else
            field.text = String(field.text).substring(0, pos - 1) + String(field.text).substring(pos)
        field.cursorPosition = pos - 1
    }

    function pressKey(token) {
        if (token === "backspace") {
            root.backspace()
            return
        }
        if (token === "space") {
            root.insertText(" ")
            return
        }
        if (token === "enter") {
            root.enterPressed()
            root.hideRequested()
            return
        }
        if (token === "shift") {
            if (root.shiftLatched) {
                root.shiftLatched = false
                root.shiftOnce = false
            } else if (root.shiftOnce) {
                root.shiftLatched = true
                root.shiftOnce = false
            } else {
                root.shiftOnce = true
            }
            return
        }
        if (token === "mode") {
            root.layoutIndex = (root.layoutIndex + 1) % 3
            root.shiftOnce = false
            root.shiftLatched = false
            return
        }
        if (token === "hide") {
            root.hideRequested()
            return
        }
        var ch = token
        if (root.layoutIndex === 0) {
            var upper = root.shiftLatched || root.shiftOnce
            ch = upper ? token.toUpperCase() : token.toLowerCase()
        }
        root.insertText(ch)
    }

    function rowTokens(index) {
        if (root.layoutIndex === 1) {
            if (index === 0) return ["1","2","3","4","5","6","7","8","9","0"]
            if (index === 1) return ["@","#","$","%","&","*","-","+","(",")"]
            return ["mode","!", "\"", "'", ":", ";", "/", "?", "backspace"]
        }
        if (root.layoutIndex === 2) {
            if (index === 0) return ["~","`","|","_","^","=","{","}","[","]"]
            if (index === 1) return ["\\","<",">",",",".",";",":","/","?","$"]
            return ["mode","#","%","*","+","-","@","&","backspace"]
        }
        if (index === 0) return ["q","w","e","r","t","y","u","i","o","p"]
        if (index === 1) return ["a","s","d","f","g","h","j","k","l"]
        return ["shift","z","x","c","v","b","n","m","backspace"]
    }

    function displayLabel(token) {
        if (token === "shift")
            return root.shiftLatched ? "CAPS" : (root.shiftOnce ? "Shift" : "shift")
        if (token === "backspace")
            return "删除"
        if (token === "mode")
            return root.layoutIndex === 0 ? "123" : (root.layoutIndex === 1 ? "#+=" : "ABC")
        if (token === "space")
            return "空格"
        if (token === "enter")
            return "完成"
        if (token === "hide")
            return "收起"
        if (root.layoutIndex === 0 && (root.shiftLatched || root.shiftOnce))
            return token.toUpperCase()
        return token
    }

    function keyWeight(token) {
        if (token === "space")
            return 4.8
        if (token === "shift" || token === "backspace" || token === "enter"
                || token === "mode" || token === "hide")
            return 1.5
        return 1.0
    }

    function rowTotalWeight(tokens) {
        var sum = 0
        for (var i = 0; i < tokens.length; ++i)
            sum += keyWeight(tokens[i])
        return Math.max(sum, 0.01)
    }

    function keyWidthForRow(rowWidth, tokens, token) {
        var gaps = Math.max(tokens.length - 1, 0) * root.keyGap
        var usable = Math.max(rowWidth - gaps, tokens.length)
        return Math.max(1, Math.floor(usable * keyWeight(token) / rowTotalWeight(tokens)))
    }

    function iconNameForToken(token) {
        if (token === "shift")
            return "arrow-up"
        if (token === "backspace")
            return "delete"
        if (token === "enter")
            return "corner-down-left"
        if (token === "hide")
            return "chevron-down"
        return ""
    }

    function accessibleLabel(token) {
        if (token === "shift")
            return "切换大小写"
        if (token === "backspace")
            return "退格"
        if (token === "mode")
            return "切换符号键盘"
        if (token === "enter")
            return "完成"
        if (token === "hide")
            return "收起键盘"
        if (token === "space")
            return "空格"
        return root.displayLabel(token)
    }

    Rectangle {
        anchors.fill: parent
        color: theme.instrument
        border.color: theme.withAlpha(theme.instrumentInk, 0.18)
        border.width: 1

        Column {
            id: keyColumn
            anchors.fill: parent
            anchors.margins: root.sideMargin
            spacing: root.keyGap

            Item {
                width: parent.width
                height: Math.round(26 * root.uiScale)
                Text {
                    anchors.centerIn: parent
                    text: root.passwordMode ? "安全键盘 · Wi-Fi 密码" : "触摸键盘"
                    color: root.passwordMode ? theme.warning : theme.instrumentMuted
                    font.family: theme.monoFont
                    font.pixelSize: Math.round(12 * root.uiScale)
                }
            }

            Repeater {
                model: 3
                delegate: Row {
                    id: letterRow
                    property var tokens: root.rowTokens(index)
                    width: keyColumn.width
                    height: root.keyHeight
                    spacing: root.keyGap

                    Repeater {
                        model: letterRow.tokens
                        delegate: Rectangle {
                            id: keyButton
                            property string token: String(modelData)
                            readonly property string lucideName: root.iconNameForToken(token)
                            width: root.keyWidthForRow(letterRow.width, letterRow.tokens, token)
                            height: root.keyHeight
                            radius: root.keyRadius
                            color: keyTap.pressed ? theme.accent : (token === "shift" || token === "backspace" || token === "mode" ? theme.withAlpha(theme.instrumentInk, 0.14) : theme.instrumentRaised)
                            border.color: keyTap.pressed ? theme.withAlpha(theme.accentInk, 0.70) : theme.withAlpha(theme.instrumentInk, 0.18)
                            border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: root.accessibleLabel(token)

                            Behavior on color { ColorAnimation { duration: theme.durationFast } }

                            Item {
                                anchors.centerIn: parent
                                width: parent.width - Math.round(8 * root.uiScale)
                                height: parent.height

                                LucideIcon {
                                    anchors.centerIn: parent
                                    width: Math.round(20 * root.uiScale)
                                    height: width
                                    visible: keyButton.lucideName.length > 0
                                    name: keyButton.lucideName
                                    color: theme.instrumentInk
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: keyButton.lucideName.length === 0
                                    text: root.displayLabel(keyButton.token)
                                    color: theme.instrumentInk
                                    font.family: keyButton.token.length === 1 ? theme.monoFont : theme.bodyFont
                                    font.pixelSize: Math.round((keyButton.token.length === 1 ? 18 : 13) * root.uiScale)
                                    font.bold: keyButton.token.length === 1
                                }
                            }

                            MouseArea {
                                id: keyTap
                                anchors.fill: parent
                                hoverEnabled: true
                                onPressed: {
                                    mouse.accepted = true
                                    root.pressKey(parent.token)
                                }
                            }

                            ToolTip.visible: keyTap.containsMouse && keyButton.lucideName.length > 0
                            ToolTip.delay: 800
                            ToolTip.text: root.accessibleLabel(keyButton.token)
                        }
                    }
                }
            }

            Row {
                id: bottomRow
                property var tokens: ["mode", "space", "enter", "hide"]
                width: keyColumn.width
                height: root.keyHeight
                spacing: root.keyGap

                Repeater {
                    model: bottomRow.tokens
                    delegate: Rectangle {
                        id: bottomKeyButton
                        property string token: String(modelData)
                        readonly property string lucideName: root.iconNameForToken(token)
                        width: root.keyWidthForRow(bottomRow.width, bottomRow.tokens, token)
                        height: root.keyHeight
                        radius: root.keyRadius
                        color: bottomTap.pressed ? theme.accentHover : (token === "enter" ? theme.accent : theme.withAlpha(theme.instrumentInk, 0.14))
                        border.color: bottomTap.pressed ? theme.withAlpha(theme.accentInk, 0.70) : theme.withAlpha(theme.instrumentInk, 0.18)
                        border.width: 1
                        Accessible.role: Accessible.Button
                        Accessible.name: root.accessibleLabel(token)

                        Behavior on color { ColorAnimation { duration: theme.durationFast } }

                        Item {
                            anchors.centerIn: parent
                            width: parent.width - Math.round(8 * root.uiScale)
                            height: parent.height

                            LucideIcon {
                                anchors.centerIn: parent
                                width: Math.round(20 * root.uiScale)
                                height: width
                                visible: bottomKeyButton.lucideName.length > 0
                                name: bottomKeyButton.lucideName
                                color: theme.accentInk
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: bottomKeyButton.lucideName.length === 0
                                text: root.displayLabel(bottomKeyButton.token)
                                color: theme.instrumentInk
                                font.family: theme.bodyFont
                                font.pixelSize: Math.round(15 * root.uiScale)
                                font.bold: bottomKeyButton.token === "enter"
                            }
                        }

                        MouseArea {
                            id: bottomTap
                            anchors.fill: parent
                            hoverEnabled: true
                            onPressed: {
                                mouse.accepted = true
                                root.pressKey(parent.token)
                            }
                        }

                        ToolTip.visible: bottomTap.containsMouse && bottomKeyButton.lucideName.length > 0
                        ToolTip.delay: 800
                        ToolTip.text: root.accessibleLabel(bottomKeyButton.token)
                    }
                }
            }
        }
    }
}
