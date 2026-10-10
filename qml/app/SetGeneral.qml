import QtQuick
import "../Theme.js" as T

// Settings > General: Personalization, Device Info, Device storage, About, Quick Panel layout.
Item {
    id: root
    CcPage {
        id: page
        anchors.fill: parent
        readonly property var info: deviceInfo.info()
        property var diag: []
        function gb(b) { return (b / 1024 / 1024 / 1024).toFixed(1) + " GB" }
        // "System default" is translated; the languages' own names (i18n.languages: English and every
        // translation in this build) are not.
        readonly property var languages: [{ code: "", name: qsTr("System default") }].concat(i18n.languages)
        readonly property string languageName: {
            const l = languages.find(x => x.code === i18n.language)
            return l ? l.name : i18n.language
        }

        CcSection { title: qsTr("Personalization") }
        CcRow { title: qsTr("Language"); visible: i18n.languages.length > 1
            description: qsTr("Applies right away. Japanese, Korean and Chinese need a CJK font on the system.")
            CcButton { id: langBtn; text: page.languageName; onClicked: langPicker.show() } }
        CcRow { title: qsTr("Launch animation"); description: qsTr("Short logo animation when Control Center opens.")
            CcSwitch { checked: (config.rev, config.get("personal.launchAnimation", true)); onToggled: (v) => config.set("personal.launchAnimation", v) } }
        CcRow { title: qsTr("Background pattern"); description: qsTr("Fine diagonal lines behind the pages (applies on next open).")
            CcSwitch { checked: (config.rev, config.get("personal.stripes", true)); onToggled: (v) => config.set("personal.stripes", v) } }

        CcSection { title: qsTr("Quick Panel Layout") }
        CcRow { title: qsTr("Tiles"); description: qsTr("Choose and order the Quick Panel tiles.")
            CcButton { text: qsTr("Edit Quick Panel"); onClicked: { panel.closeApp(); panel.requestOpen() } } }
        CcRow { title: qsTr("Open the Quick Panel with")
            CcChoice { options: [{ value: "left_button", label: qsTr("Left menu button") }, { value: "right_button", label: qsTr("Right menu button") },
                                 { value: "none", label: qsTr("Tray only") }]
                       current: config.button; onPicked: (v) => config.button = v } }

        CcSection { title: qsTr("Device Info") }
        Repeater {
            model: [[qsTr("Model"), (device.rev, device.value("model") && device.value("model") !== "unknown"
                                           ? device.value("modelName") + "  (" + page.info.model + ")" : page.info.vendor + " " + page.info.model)],
                    [qsTr("Processor"), qsTr("%1 (%2 threads)").arg(page.info.cpu).arg(page.info.cores)],
                    [qsTr("Graphics"), page.info.gpu], [qsTr("Memory"), page.info.ram], [qsTr("Display"), page.info.display],
                    ["BIOS", page.info.bios + "  (" + page.info.biosDate + ")"],
                    [qsTr("Serial number"), device.value("serial") || qsTr("shown when xpanel-daemon runs")],
                    [qsTr("System"), page.info.os], [qsTr("Kernel"), page.info.kernel]]
            CcRow { title: modelData[0]; Text { text: modelData[1] || "—"; color: T.text; font.pixelSize: 18 } }
        }

        CcSection { title: qsTr("Device storage") }
        Repeater {
            model: deviceInfo.storage()
            CcRow {
                title: modelData.name
                description: qsTr("%1  ·  %2  ·  %3 free of %4").arg(modelData.device).arg(modelData.fs).arg(page.gb(modelData.free)).arg(page.gb(modelData.total))
                Rectangle {
                    width: 360; height: 14; radius: 7; color: "#2a2a33"
                    Rectangle { width: parent.width * (1 - modelData.free / modelData.total); height: parent.height; radius: 7
                                color: modelData.free / modelData.total < 0.1 ? T.warn : T.accent }
                }
            }
        }

        CcSection { title: qsTr("About") }
        CcRow { title: "XPanel"; description: qsTr("Quick Panel and Control Center for the ROG Ally on Linux")
            Text { text: qsTr("Version %1").arg(appVersion); color: T.text; font.pixelSize: 18 } }
        CcRow { title: qsTr("Service"); description: device.error ? qsTr("No access: %1").arg(device.error) : qsTr("xpanel-daemon (TDP, fan, charge limit, lighting)")
            Text { text: device.connected ? qsTr("Running") : device.error ? qsTr("No access") : qsTr("Not running"); color: device.connected ? T.text : T.warn; font.pixelSize: 18 } }
        CcRow { title: qsTr("Input layer"); description: qsTr("XPanel's own controller handling (in xpanel-daemon).")
            Text { text: device.value("inputLayer") || qsTr("not running"); color: device.value("inputLayer") === "active" ? "#5fd38d" : T.warn; font.pixelSize: 18 } }
        CcRow { title: qsTr("App Diagnostics"); description: qsTr("Checks the services and tools XPanel uses.")
            CcButton { text: qsTr("Run"); onClicked: page.diag = tweaks.diagnostics() } }
        Repeater {
            model: page.diag
            CcRow { title: (modelData.ok ? "✓  " : "✕  ") + modelData.name; description: modelData.detail
                Text { text: modelData.ok ? qsTr("OK") : qsTr("Problem"); color: modelData.ok ? "#5fd38d" : T.warn; font.pixelSize: 18 } }
        }
    }

    // ---------- language picker ----------
    Rectangle {
        id: langPicker
        anchors.fill: parent; color: "#f2050507"; visible: false
        function show() {
            visible = true
            app.pushBack(() => langPicker.hide())
            Qt.callLater(() => langFlow.focusCurrent())
        }
        function hide() { visible = false; Qt.callLater(() => langBtn.forceActiveFocus()) }
        // The page is translated the moment the language changes (the buttons below are rebuilt):
        // close the picker first, change the language afterwards.
        function choose(code) { hide(); app.popBack(); Qt.callLater(() => { i18n.language = code }) }
        MouseArea { anchors.fill: parent }
        CcPage {
            anchors.fill: parent
            Text { text: qsTr("Language"); color: T.text; font.pixelSize: 32; font.bold: true }
            Text { text: qsTr("Japanese, Korean and Chinese need a CJK font on the system (for example Noto Sans CJK).")
                   color: T.dim; font.pixelSize: 16; width: parent.width; wrapMode: Text.WordWrap }
            Flow {
                id: langFlow
                width: parent.width; spacing: 10
                function focusCurrent() {
                    for (let i = 0; i < children.length; ++i)
                        if (children[i].primary === true) { children[i].forceActiveFocus(); return }
                }
                Repeater {
                    model: page.languages
                    CcButton {
                        width: 250; text: modelData.name
                        primary: i18n.language === modelData.code
                        onClicked: langPicker.choose(modelData.code)
                    }
                }
            }
            CcButton { text: qsTr("Cancel"); onClicked: { langPicker.hide(); app.popBack() } }
        }
    }
}
