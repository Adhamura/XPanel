import QtQuick
import "../Theme.js" as T

// Settings > Performance: Operating Mode, manual power limits, Eco Mode, GPU, fan.
CcPage {
    id: page
    readonly property bool manual: device.value("opmode") === "manual"

    CcSection { title: qsTr("Operating Mode"); note: device.connected ? "" : device.error ? qsTr("No access to xpanel-daemon: %1").arg(device.error)
                                                                       : qsTr("xpanel-daemon is not running: install it with  %1").arg(device.installHint) }
    // Operating Mode watts of this model (from xpanel-daemon; Ally X values until it answers)
    readonly property var modeWatts: (device.rev, device.value("modeWatts") || { silent: 13, performance: 17, turbo: 25, turboAc: 30 })

    CcRow { title: qsTr("Mode")
            description: qsTr("Silent %1 W · Performance %2 W · Turbo %3 W (%4 W on the charger) · Manual")
                         .arg(page.modeWatts.silent).arg(page.modeWatts.performance).arg(page.modeWatts.turbo).arg(page.modeWatts.turboAc)
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "silent", label: qsTr("Silent") }, { value: "performance", label: qsTr("Performance") },
                             { value: "turbo", label: qsTr("Turbo") }, { value: "manual", label: qsTr("Manual") }]
                   current: (device.rev, device.value("opmode")); onPicked: (v) => device.setValue("opmode", v) } }
    CcRow { title: qsTr("Now"); description: qsTr("Applied power limits (SPL / SPPT / FPPT) and platform profile")
        Text { text: (device.rev, !device.connected ? "—"
                     : device.value("spl") + " / " + device.value("sppt") + " / " + device.value("fppt") + " W  ·  " + device.value("profile"))
               color: T.accent; font.pixelSize: 18 } }

    CcSection { title: qsTr("Manual"); note: qsTr("Used in Manual mode. SPPT / FPPT at 0 W = SPL + 5 W / + 10 W.") }
    CcRow { title: qsTr("SPL — sustained power")
        CcSlider { width: 460; from: device.value("tdpMin") || 7; to: device.value("tdpMax") || 30; unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdp") || 15)
                   onMoved: (v) => { device.setValue("tdp", v); if (!page.manual) device.setValue("opmode", "manual") } } }
    CcRow { title: qsTr("SPPT — slow boost")
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("spptMax") || 43); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpSlow") || 0); onMoved: (v) => device.setValue("tdpSlow", v) } }
    CcRow { title: qsTr("FPPT — fast boost")
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("fpptMax") || 53); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpFast") || 0); onMoved: (v) => device.setValue("tdpFast", v) } }

    CcSection { title: qsTr("Eco Mode") }
    CcRow { title: qsTr("Eco Mode"); description: qsTr("Saves battery: CPU energy preference \"power\" and no CPU boost.")
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("eco")); onToggled: (v) => device.setValue("eco", v) } }

    CcSection { title: qsTr("GPU Settings") }
    CcRow { title: qsTr("GPU clock"); description: qsTr("Auto lets the driver decide. A limit saves power and heat.")
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: qsTr("Auto") }, { value: "limit", label: qsTr("Limit") }]
                   current: (device.rev, device.value("gpuClock") > 0 ? "limit" : "auto")
                   onPicked: (v) => device.setValue("gpuClock", v === "auto" ? 0 : Math.round((device.value("gpuMax") || 2700) * 0.75)) } }
    CcRow { title: qsTr("GPU clock limit"); visible: device.value("gpuClock") > 0
        CcSlider { width: 460; from: device.value("gpuMin") || 800; to: device.value("gpuMax") || 2700; stepSize: 50; unit: " MHz"
                   value: (device.rev, device.value("gpuClock") || 0); onMoved: (v) => device.setValue("gpuClock", v) } }

    CcSection { title: qsTr("Fan"); note: page.fanCustom ? qsTr("Custom curve: 8 points of fan speed over temperature. Changes apply a moment after you let go.") : "" }
    CcRow { title: qsTr("Fan Mode")
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: qsTr("Auto") }, { value: "quiet", label: qsTr("Quiet") }, { value: "max", label: qsTr("Max") },
                             { value: "custom", label: qsTr("Custom") }]
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

    CcRow { title: qsTr("Start from"); visible: page.fanCustom; description: qsTr("Copies a preset into the curve you are editing.")
        Row {
            spacing: 8
            CcButton { text: qsTr("Quiet"); enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("quiet")) }
            CcButton { text: qsTr("Balanced"); enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("balanced")) }
            CcButton { text: qsTr("Performance"); enabledButton: device.connected; onClicked: page.saveCurve(page.presetCurve("performance")) }
        } }
    CcRow { title: qsTr("Separate GPU fan curve"); visible: page.fanCustom
            description: qsTr("Off: one curve for both fans (CPU and GPU).")
        CcSwitch { enabledSwitch: device.connected; checked: page.separate; onToggled: (v) => page.setSeparate(v) } }
    CcRow { title: qsTr("Edit curve"); visible: page.fanCustom && page.separate
        CcChoice { options: [{ value: "cpu", label: qsTr("CPU fan") }, { value: "gpu", label: qsTr("GPU fan") }]
                   current: page.which; onPicked: (v) => page.which = v } }
    FanCurveEditor {
        width: parent.width; visible: page.fanCustom
        enabledEditor: device.connected
        caption: !page.separate ? qsTr("Fan curve (both fans)") : (page.which === "gpu" ? qsTr("GPU fan curve") : qsTr("CPU fan curve"))
        liveTemp: page.editedKey === "gpu" ? sys.gpuTemp : sys.cpuTemp
        liveText: page.editedKey === "gpu" ? qsTr("GPU %1 °C   ·   Fan %2 rpm").arg(sys.gpuTemp).arg(sys.fanRpm)
                                           : qsTr("CPU %1 °C   ·   Fan %2 rpm").arg(sys.cpuTemp).arg(sys.fanRpm)
        source: page.fanCurve ? page.fanCurve[page.editedKey] || page.fanCurve.cpu : null
        onCommitted: (curve) => page.saveCurve(curve)
    }

    CcSection { title: qsTr("Live") }
    CcRow { title: "CPU / GPU"
        Text { text: sys.cpuLoad + "% " + sys.cpuTemp + "°C   ·   " + sys.gpuLoad + "% " + sys.gpuTemp + "°C"; color: T.text; font.pixelSize: 18 } }
    CcRow { title: qsTr("Power / Fan")
        Text { text: qsTr("SoC %1 W   ·   Battery %2 W   ·   Fan %3 rpm").arg(sys.socWatts.toFixed(1)).arg(sys.batteryWatts.toFixed(1)).arg(sys.fanRpm)
               color: T.text; font.pixelSize: 18 } }
}
