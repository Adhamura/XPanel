import QtQuick
import QtQuick.Window
import "../Theme.js" as T

// Settings: left list of sub-pages, content on the right.
Item {
    id: page
    readonly property var pages: [
        { id: "general",     title: qsTr("General") },
        { id: "performance", title: qsTr("Performance") },
        { id: "update",      title: qsTr("Update Center") },
        { id: "calibration", title: qsTr("Calibration") },
        { id: "lighting",    title: qsTr("Lighting") },
        { id: "audio",       title: qsTr("Audio") },
        { id: "display",     title: qsTr("Display") },
        { id: "connection",  title: qsTr("Connection") },
        { id: "shortcuts",   title: qsTr("Keyboard Shortcuts") },
        { id: "feedback",    title: qsTr("Feedback") },
        { id: "battery",     title: qsTr("Battery Care") }
    ]
    readonly property var files: ({ general: "SetGeneral.qml", performance: "SetPerformance.qml", update: "SetUpdate.qml",
        calibration: "SetCalibration.qml", lighting: "SetLighting.qml", audio: "SetAudio.qml",
        display: "SetDisplay.qml", connection: "SetConnection.qml", shortcuts: "SetShortcuts.qml",
        feedback: "SetFeedback.qml", battery: "SetBattery.qml" })

    function inContent() {
        const w = page.Window.window, it = w ? w.activeFocusItem : null
        let p = it
        while (p && p !== content) p = p.parent
        return !!p
    }
    function focusNav() { navRepeater.itemAt(Math.max(0, pages.findIndex(p => p.id === app.settingsPage))).forceActiveFocus() }

    Rectangle {
        id: nav
        width: 330; height: parent.height; color: "#0e0e12"
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#24242a" }
        Column {
            y: 18; width: parent.width
            Repeater {
                id: navRepeater
                model: page.pages
                Rectangle {
                    readonly property bool sel: app.settingsPage === modelData.id
                    width: nav.width - 1; height: 56
                    color: activeFocus ? "#1b2a36" : (sel ? "#17171d" : "transparent")
                    activeFocusOnTab: true
                    // moving onto an entry shows its page; A / right enters it
                    onActiveFocusChanged: if (activeFocus) app.settingsPage = modelData.id
                    function activate() { content.item && content.item.nextItemInFocusChain(true).forceActiveFocus() }
                    function step(d) { if (d > 0) activate() }
                    Rectangle { width: 4; height: parent.height; color: T.accent; visible: parent.sel }
                    Rectangle { anchors.fill: parent; color: "transparent"; border.color: T.accent; border.width: 3; visible: parent.activeFocus }
                    Text { x: 28; anchors.verticalCenter: parent.verticalCenter; text: modelData.title
                           color: parent.sel ? "#ffffff" : T.dim; font.pixelSize: 19; font.bold: parent.sel }
                    MouseArea { anchors.fill: parent; onClicked: { parent.forceActiveFocus(); app.settingsPage = modelData.id } }
                }
            }
        }
    }
    Loader {
        id: content
        anchors.left: nav.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        source: page.files[app.settingsPage] || "SetGeneral.qml"
    }
}
