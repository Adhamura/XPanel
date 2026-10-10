import QtQuick
import "../Theme.js" as T

// Settings > General: Personalization, Device Info, Device storage, About, Quick Panel layout.
CcPage {
    id: page
    readonly property var info: deviceInfo.info()
    property var diag: []
    function gb(b) { return (b / 1024 / 1024 / 1024).toFixed(1) + " GB" }

    CcSection { title: "Personalization" }
    CcRow { title: "Launch animation"; description: "Short logo animation when Control Center opens."
        CcSwitch { checked: (config.rev, config.get("personal.launchAnimation", true)); onToggled: (v) => config.set("personal.launchAnimation", v) } }
    CcRow { title: "Background pattern"; description: "Fine diagonal lines behind the pages (applies on next open)."
        CcSwitch { checked: (config.rev, config.get("personal.stripes", true)); onToggled: (v) => config.set("personal.stripes", v) } }

    CcSection { title: "Quick Panel Layout" }
    CcRow { title: "Tiles"; description: "Choose and order the Quick Panel tiles."
        CcButton { text: "Edit Quick Panel"; onClicked: { panel.closeApp(); panel.requestOpen() } } }
    CcRow { title: "Open the Quick Panel with"
        CcChoice { options: [{ value: "left_button", label: "Left menu button" }, { value: "right_button", label: "Right menu button" },
                             { value: "none", label: "Tray only" }]
                   current: config.button; onPicked: (v) => config.button = v } }

    CcSection { title: "Device Info" }
    Repeater {
        model: [["Model", page.info.vendor + " " + page.info.model], ["Processor", page.info.cpu + " (" + page.info.cores + " threads)"],
                ["Graphics", page.info.gpu], ["Memory", page.info.ram], ["Display", page.info.display],
                ["BIOS", page.info.bios + "  (" + page.info.biosDate + ")"],
                ["Serial number", device.value("serial") || "shown when xpanel-daemon runs"],
                ["System", page.info.os], ["Kernel", page.info.kernel]]
        CcRow { title: modelData[0]; Text { text: modelData[1] || "—"; color: T.text; font.pixelSize: 18 } }
    }

    CcSection { title: "Device storage" }
    Repeater {
        model: deviceInfo.storage()
        CcRow {
            title: modelData.name
            description: modelData.device + "  ·  " + modelData.fs + "  ·  " + page.gb(modelData.free) + " free of " + page.gb(modelData.total)
            Rectangle {
                width: 360; height: 14; radius: 7; color: "#2a2a33"
                Rectangle { width: parent.width * (1 - modelData.free / modelData.total); height: parent.height; radius: 7
                            color: modelData.free / modelData.total < 0.1 ? T.warn : T.accent }
            }
        }
    }

    CcSection { title: "About" }
    CcRow { title: "XPanel"; description: "Quick Panel and Control Center for the ROG Ally on Linux"
        Text { text: "Version " + appVersion; color: T.text; font.pixelSize: 18 } }
    CcRow { title: "Service"; description: "xpanel-daemon (TDP, fan, charge limit, lighting)"
        Text { text: device.connected ? "Running" : "Not running"; color: device.connected ? T.text : T.warn; font.pixelSize: 18 } }
    CcRow { title: "Input layer"; description: "XPanel's own controller handling (in xpanel-daemon)."
        Text { text: device.value("inputLayer") || "not running"; color: device.value("inputLayer") === "active" ? "#5fd38d" : T.warn; font.pixelSize: 18 } }
    CcRow { title: "App Diagnostics"; description: "Checks the services and tools XPanel uses."
        CcButton { text: "Run"; onClicked: page.diag = tweaks.diagnostics() } }
    Repeater {
        model: page.diag
        CcRow { title: (modelData.ok ? "✓  " : "✕  ") + modelData.name; description: modelData.detail
            Text { text: modelData.ok ? "OK" : "Problem"; color: modelData.ok ? "#5fd38d" : T.warn; font.pixelSize: 18 } }
    }
}
