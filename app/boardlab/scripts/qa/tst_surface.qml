import QtQuick 2.12
import QtTest 1.2
import "../../qml/components"

Item {
    width: 800
    height: 480
    Component {
        id: surfaceComponent
        AppSurface {
            width: 800
            height: 480
            pageSource: Qt.resolvedUrl("fixtures/SurfacePage.qml")
        }
    }
    SignalSpy { id: closedSpy; signalName: "closed" }
    TestCase {
        name: "PageLifecycle"
        when: windowShown

        function test_lazy_load_retain_and_hide() {
            var surface = createTemporaryObject(surfaceComponent, parent)
            verify(surface.item === null)
            compare(surface.status, Loader.Null)
            surface.selected = true
            tryCompare(surface, "status", Loader.Ready)
            var page = surface.item
            compare(page.activations, 1)
            page.editedValue = "retained input"
            tryVerify(function() { return page.samples > 0 })
            surface.selected = false
            var samples = page.samples
            wait(80)
            compare(page.samples, samples)
            surface.selected = true
            compare(surface.item, page)
            compare(surface.item.editedValue, "retained input")
            compare(page.activations, 2)
            tryVerify(function() { return page.samples > samples })
        }

        function test_cancel_while_loading_and_reopen() {
            var surface = createTemporaryObject(surfaceComponent, parent)
            surface.selected = true
            surface.selected = false
            tryCompare(surface, "status", Loader.Ready)
            verify(!surface.item.visible)
            compare(surface.item.samples, 0)
            surface.selected = true
            verify(surface.item.visible)
        }

        function test_tool_changes_and_close_signal() {
            var surface = createTemporaryObject(surfaceComponent, parent, { "toolName": "GPIO" })
            surface.selected = true
            tryCompare(surface, "status", Loader.Ready)
            compare(surface.item.standaloneTool, "GPIO")
            surface.toolName = "UART"
            compare(surface.item.currentTool, "UART")
            closedSpy.target = surface
            closedSpy.clear()
            surface.item.closed()
            compare(closedSpy.count, 1)
        }
    }
}
