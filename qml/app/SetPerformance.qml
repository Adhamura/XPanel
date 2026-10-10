import QtQuick
import "../Theme.js" as T

// Settings > Performance: Operating Mode, manual power limits, Eco Mode, GPU, fan.
CcPage {
    id: page
    readonly property bool manual: device.value("opmode") === "manual"

    CcSection { title: "Operating Mode"; note: device.connected ? "" : device.error ? "No access to xpanel-daemon: " + device.error
                                                                       : "xpanel-daemon is not running: install it with  " + device.installHint }
    // Operating Mode watts of this model (from xpanel-daemon; Ally X values until it answers)
    readonly property var modeWatts: (device.rev, device.value("modeWatts") || { silent: 13, performance: 17, turbo: 25, turboAc: 30 })

    CcRow { title: "Mode"
            description: "Silent " + page.modeWatts.silent + " W · Performance " + page.modeWatts.performance + " W · Turbo "
                         + page.modeWatts.turbo + " W (" + page.modeWatts.turboAc + " W on the charger) · Manual"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "silent", label: "Silent" }, { value: "performance", label: "Performance" },
                             { value: "turbo", label: "Turbo" }, { value: "manual", label: "Manual" }]
                   current: (device.rev, device.value("opmode")); onPicked: (v) => device.setValue("opmode", v) } }
    CcRow { title: "Now"; description: "Applied power limits (SPL / SPPT / FPPT) and platform profile"
        Text { text: (device.rev, !device.connected ? "—"
                     : device.value("spl") + " / " + device.value("sppt") + " / " + device.value("fppt") + " W  ·  " + device.value("profile"))
               color: T.accent; font.pixelSize: 18 } }

    CcSection { title: "Manual"; note: "Used in Manual mode. SPPT / FPPT at 0 W = SPL + 5 W / + 10 W." }
    CcRow { title: "SPL — sustained power"
        CcSlider { width: 460; from: device.value("tdpMin") || 7; to: device.value("tdpMax") || 30; unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdp") || 15)
                   onMoved: (v) => { device.setValue("tdp", v); if (!page.manual) device.setValue("opmode", "manual") } } }
    CcRow { title: "SPPT — slow boost"
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("spptMax") || 43); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpSlow") || 0); onMoved: (v) => device.setValue("tdpSlow", v) } }
    CcRow { title: "FPPT — fast boost"
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("fpptMax") || 53); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpFast") || 0); onMoved: (v) => device.setValue("tdpFast", v) } }

    CcSection { title: "Eco Mode" }
    CcRow { title: "Eco Mode"; description: "Saves battery: CPU energy preference \"power\" and no CPU boost."
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("eco")); onToggled: (v) => device.setValue("eco", v) } }

    CcSection { title: "GPU Settings" }
    CcRow { title: "GPU clock"; description: "Auto lets the driver decide. A limit saves power and heat."
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: "Auto" }, { value: "limit", label: "Limit" }]
                   current: (device.rev, device.value("gpuClock") > 0 ? "limit" : "auto")
                   onPicked: (v) => device.setValue("gpuClock", v === "auto" ? 0 : Math.round((device.value("gpuMax") || 2700) * 0.75)) } }
    CcRow { title: "GPU clock limit"; visible: device.value("gpuClock") > 0
        CcSlider { width: 460; from: device.value("gpuMin") || 800; to: device.value("gpuMax") || 2700; stepSize: 50; unit: " MHz"
                   value: (device.rev, device.value("gpuClock") || 0); onMoved: (v) => device.setValue("gpuClock", v) } }

    CcSection { title: "Fan"; note: page.fanCustom ? "Custom curve: 8 points of fan speed over temperature. Changes apply a moment after you let go." : "" }
    CcRow { title: "Fan Mode"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: "Auto" }, { value: "quiet", label: "Quiet" }, { value: "max", label: "Max" },
                             { value: "custom", label: "Custom" }]
                   current: (device.rev, device.value("fan")); onPicked: (v) => page.pickFan(v) } }

    // ---- custom curve ----
    readonly property bool fanCustom: (device.rev, device.value("fan") === "custom")
    readonly property var fanCurve: (device.rev, device.value("fanCurve") || null)
    readonly property bool separate: !!(fanCurve && fanCurve.gpu)
    property string which: "cpu"                       // the curve the editor shows when they differ
    readonly property string editedKey: separate ? which : "cpu"
    // Presets: fan speed in % at 40 50 60 70 78 85 90 95 °C. Quiet is the built-in Quiet curve.
    readonly property var presetTemps: [40, 50, 60, 70, 78, 85, 90, 95]
    readonly property var presets: ({ quiet: [0, 8, 16, 25, 37, 51, 67, 82],
                                      balanced: [10, 25, 40, 55, 70, 82, 92, 100],
                                      performance: [25, 40, 55, 70, 82, 92, 100, 100] })
    function presetCurve(name) { return presetTemps.map((t, i) => [t, presets[name][i]]) }
    function pickFan(v) {
        // the first time Custom is picked there is no curve yet: start from Balanced
        if (v === "custom" && !fanCurve) device.setValue("fanCurve", { cpu: presetCurve("balanced") })
        device.setValue("fan", v)
    }
    function saveCurve(pts) {
        const o = { cpu: fanCurve ? fanCurve.cpu : presetCurve("balanced") }
        if (separate) o.gpu = fanCurve.gpu
        o[editedKey] = pts
        device.setValue("fanCurve", o)
    }
    function setSeparate(on) {
        const cpu = fanCurve ? fanCurve.cpu : presetCurve("balanced")
        which = "cpu"
        device.setValue("fanCurve", on ? { cpu: cpu, gpu: cpu.map(p => [p[0], p[1]]) } : { cpu: cpu })
    }

    CcRow { title: "Start from"; visible: page.fanCustom; description: "Copies a preset into the curve you are editing."
        Row {
            spacing: 8
            CcButton { text: "Quiet"; enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("quiet")) }
            CcButton { text: "Balanced"; enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("balanced")) }
            CcButton { text: "Performance"; enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("performance")) }
        } }
    CcRow { title: "Separate GPU fan curve"; visible: page.fanCustom
            description: "Off: one curve for both fans (CPU and GPU)."
        CcSwitch { enabledSwitch: device.connected; checked: page.separate; onToggled: (v) => page.setSeparate(v) } }
    CcRow { title: "Edit curve"; visible: page.fanCustom && page.separate
        CcChoice { options: [{ value: "cpu", label: "CPU fan" }, { value: "gpu", label: "GPU fan" }]
                   current: page.which; onPicked: (v) => page.which = v } }
    FanCurveEditor {
        width: parent.width; visible: page.fanCustom
        enabledEditor: device.connected
        caption: !page.separate ? "Fan curve (both fans)" : (page.which === "gpu" ? "GPU fan curve" : "CPU fan curve")
        liveTemp: page.editedKey === "gpu" ? sys.gpuTemp : sys.cpuTemp
        liveText: (page.editedKey === "gpu" ? "GPU " + sys.gpuTemp : "CPU " + sys.cpuTemp) + " °C   ·   Fan " + sys.fanRpm + " rpm"
        source: page.fanCurve ? page.fanCurve[page.editedKey] || page.fanCurve.cpu : null
        onCommitted: (curve) => page.saveCurve(curve)
    }

    CcSection { title: "Live" }
    CcRow { title: "CPU / GPU"
        Text { text: sys.cpuLoad + "% " + sys.cpuTemp + "°C   ·   " + sys.gpuLoad + "% " + sys.gpuTemp + "°C"; color: T.text; font.pixelSize: 18 } }
    CcRow { title: "Power / Fan"
        Text { text: "SoC " + sys.socWatts.toFixed(1) + " W   ·   Battery " + sys.batteryWatts.toFixed(1) + " W   ·   Fan " + sys.fanRpm + " rpm"
               color: T.text; font.pixelSize: 18 } }
}
