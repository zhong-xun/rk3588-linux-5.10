import QtQuick 2.12
import QtTest 1.2
import "../../qml/components"

Item {
    width: 800
    height: 480
    property bool kioskMode: false
    property var boardProfile: profile
    property var boardBackend: backend
    property var systemInfo: system
    property var localWebServer: web

    QtObject {
        id: profile
        property string displayName: "QA Board"
        property string socFamily: "RK3588"
        property string detectionSource: "fixture"
        property string configurationError: ""
        property var denied: []
        function hasCapability(name) { return denied.indexOf(name.toLowerCase()) < 0 }
    }
    QtObject { id: backend; property string statusText: "Ready" }
    QtObject {
        id: system
        property bool cpuUsageValid: true
        property real cpuUsage: 5
        property real memoryUsage: 10
        property real diskUsage: 20
    }
    QtObject { id: web; property bool running: false }
    HomeScreen { id: home; anchors.fill: parent }

    SignalSpy { id: requested; target: home; signalName: "appRequested" }
    TestCase {
        name: "HomeWorkflows"
        when: windowShown

        function init() {
            profile.denied = []
            home.searchQuery = ""
            home.selectedCategory = 0
            findChild(home, "home-tool-scroller").contentY = 0
            requested.clear()
            tryCompare(home, "availableToolCount", 22)
        }

        function test_search_open_and_clear() {
            var search = findChild(home, "home-tool-search")
            mouseClick(search)
            keyClick(Qt.Key_W)
            keyClick(Qt.Key_I)
            keyClick(Qt.Key_F)
            keyClick(Qt.Key_I)
            tryCompare(home, "searchQuery", "wifi")
            compare(home.visibleAppModel.length, 1)
            keyClick(Qt.Key_Return)
            compare(requested.count, 1)
            compare(requested.signalArguments[0][0], "WiFi")
            mouseClick(findChild(home, "home-clear-filter"))
            compare(home.searchQuery, "")
            compare(home.visibleAppModel.length, 22)
        }

        function test_category_and_empty_state() {
            var category = findChild(home, "home-category-filter")
            mouseClick(category)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(home, "selectedCategory", 1)
            home.searchQuery = "camera"
            compare(home.visibleAppModel.length, 0)
            home.searchQuery = "gpio"
            compare(home.visibleAppModel.length, 1)
        }

        function test_profile_gate_survives_search() {
            profile.denied = ["gpio", "npuyolodemo"]
            tryCompare(home, "availableToolCount", 20)
            home.searchQuery = "gpio"
            compare(home.visibleAppModel.length, 0)
            home.searchQuery = "YOLO"
            compare(home.visibleAppModel.length, 0)
        }

        function test_keyboard_reveals_offscreen_tool() {
            var scroller = findChild(home, "home-tool-scroller")
            scroller.contentY = 0
            var search = findChild(home, "home-tool-search")
            search.forceActiveFocus()
            for (var i = 0; i < 22; ++i)
                keyClick(Qt.Key_Tab)
            verify(scroller.contentY > 0)
            home.searchQuery = "wifi"
            compare(scroller.contentY, 0)
        }

        function test_restore_launcher_focus() {
            var scroller = findChild(home, "home-tool-scroller")
            scroller.contentY = 0
            home.focusApp("Qt Web")
            verify(scroller.contentY > 0)
            keyClick(Qt.Key_Space)
            compare(requested.signalArguments[0][0], "Qt Web")
        }
    }
}
