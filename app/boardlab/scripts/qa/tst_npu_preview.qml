import QtQuick 2.12
import QtTest 1.2
import "../../qml/components"

Item {
    width: 800
    height: 480
    property var npuYoloDemo: demo
    QtObject {
        id: demo
        property var status: ({})
    }
    Component {
        id: pageComponent
        NpuYoloDemoApp { width: 800; height: 480 }
    }
    TestCase {
        name: "NpuPreviewLifecycle"
        when: windowShown

        function test_hidden_page_skips_frames_and_resumes_latest() {
            var preview = Qt.resolvedUrl("../../assets/icons/boardlab.svg").toString()
            demo.status = { "previewUrl": preview, "previewSerial": 1 }
            var page = createTemporaryObject(pageComponent, parent)
            verify(page !== null)
            tryCompare(page, "hasPreviewFrame", true)
            verify(page.displayedPreviewSource.indexOf("qmlPreviewSerial=1") >= 0)
            page.visible = false
            compare(page.hasPreviewFrame, false)
            demo.status = { "previewUrl": preview, "previewSerial": 2 }
            wait(100)
            compare(page.displayedPreviewSource, "")
            compare(page.loadingPreviewSource, "")
            page.visible = true
            tryCompare(page, "hasPreviewFrame", true)
            verify(page.displayedPreviewSource.indexOf("qmlPreviewSerial=2") >= 0)
        }
    }
}
