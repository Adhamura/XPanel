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
    readonly property bool tdpLock: (device.rev, !!device.value("tdpLock"))
    CcRow { title: qsTr("Lock boost to SPL"); description: qsTr("SPPT and FPPT equal SPL: the chip never draws more than the set watts.")
        CcSwitch { enabledSwitch: device.connected; checked: page.tdpLock; onToggled: (v) => device.setValue("tdpLock", v) } }
    CcRow { title: qsTr("SPPT — slow boost"); visible: !page.tdpLock
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("spptMax") || 43); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpSlow") || 0); onMoved: (v) => device.setValue("tdpSlow", v) } }
    CcRow { title: qsTr("FPPT — fast boost"); visible: !page.tdpLock
        CcSlider { width: 460; from: 0; to: (device.rev, device.value("fpptMax") || 53); unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpFast") || 0); onMoved: (v) => device.setValue("tdpFast", v) } }

    CcSection { title: qsTr("Eco Mode") }
    CcRow { title: qsTr("Eco Mode"); description: qsTr("Saves battery: CPU energy preference \"power\" and no CPU boost.")
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("eco")); onToggled: (v) => device.setValue("eco", v) } }

    CcRow { title: qsTr("CPU Boost"); description: qsTr("Short bursts above the base clock for games and heavy tasks. Off saves power.")
        CcSwitch { enabledSwitch: device.connected && !(device.rev, !!device.value("eco"))
                   checked: (device.rev, device.value("cpuBoost") !== false && !device.value("eco"))
                   onToggled: (v) => {
                       device.setValue("cpuBoost", v)
                       if (!v) device.setValue("cpuBoostMode", "off")
                       else if (device.value("cpuBoostMode") === "off") device.setValue("cpuBoostMode", "aggressive")
                   } } }

    CcRow { title: qsTr("CPU Boost Mode")
            description: qsTr("Off · Enabled · Aggressive · Efficient Aggressive frequency scaling behavior.")
            visible: !(device.rev, !!device.value("eco"))
        CcChoice { enabledChoice: device.connected && !(device.rev, !!device.value("eco"))
                   options: [{ value: "off", label: qsTr("Off") },
                             { value: "enabled", label: qsTr("Enabled") },
                             { value: "aggressive", label: qsTr("Aggressive") },
                             { value: "efficient", label: qsTr("Efficient") }]
                   current: (device.rev, device.value("cpuBoost") === false ? "off" : (device.value("cpuBoostMode") || "aggressive"))
                   onPicked: (v) => {
                       device.setValue("cpuBoostMode", v)
                       device.setValue("cpuBoost", v !== "off")
                   } } }

    CcSection { title: qsTr("GPU Settings") }
    // Memory Assigned to GPU (UMA frame buffer): the daemon reports it when the firmware has it
    // (Windows: ASUS WMI; Linux: asus-armoury apu_mem)
    readonly property bool gpuMemShown: (device.rev, !!device.value("gpuMemSupported")) || Qt.platform.os === "windows"
    function gpuMemOptions() {
        const mbs = device.value("gpuMemOptions") || []
        if (!mbs || mbs.length === 0) {
            return [0, 1024, 2048, 3072, 4096, 5120, 6144, 7168, 8192, 9216, 10240, 11264, 12288, 13312, 14336, 15360, 16384].map(mb => ({
                value: mb,
                label: mb === 0 ? qsTr("Auto") : qsTr("%1 GB").arg(mb / 1024)
            }))
        }
        return mbs.map(mb => ({ value: mb,
                                label: mb === 0 ? qsTr("Auto") : (mb % 1024 === 0 ? qsTr("%1 GB").arg(mb / 1024) : qsTr("%1 MB").arg(mb)) }))
    }
    CcRow { title: qsTr("Memory Assigned to GPU"); visible: page.gpuMemShown
            description: qsTr("Video memory reserved for the GPU. Takes effect after a restart.")
        CcDropdown { enabledChoice: device.connected
                   options: (device.rev, page.gpuMemOptions())
                   current: (device.rev, device.value("gpuMemory") !== undefined ? device.value("gpuMemory") : 6144)
                   onPicked: (v) => device.setValue("gpuMemory", v) } }
    CcRow { title: qsTr("Restart required"); visible: page.gpuMemShown && (device.rev, !!device.value("gpuMemPending"))
            description: Qt.platform.os === "windows" ? qsTr("Restart Windows to apply the new GPU memory size.")
                                                      : qsTr("Restart the Ally to apply the new GPU memory size.") }

    // AMD Advanced Graphics Options (RSR, AFMF, Anti-Lag, Boost, Chill)
    Item {
        width: parent ? parent.width : 600
        implicitHeight: Math.max(amdHeaderCol.implicitHeight, optBtn.implicitHeight) + 16
        Column {
            id: amdHeaderCol
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Row {
                spacing: 10
                Rectangle { width: 4; height: 24; color: T.accent; anchors.verticalCenter: parent.verticalCenter }
                Text { text: qsTr("AMD Advanced Graphics Options"); color: T.text; font.pixelSize: 22; font.bold: true; font.italic: true }
            }
            Text { text: qsTr("Driver features for resolution upscaling, frame generation, and latency."); color: T.dim; font.pixelSize: 15 }
        }
        CcButton {
            id: optBtn
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Optimize All")
            onClicked: sys.optimizeAmdGraphics()
        }
    }
    CcRow {
        title: qsTr("Radeon™ Super Resolution (RSR)")
        description: qsTr("In-driver upscaler using FSR algorithms to boost frame rates for games rendered at lower resolutions.")
        CcSwitch {
            checked: sys.amdRsr
            onToggled: (v) => sys.setAmdRsr(v)
        }
    }
    CcRow {
        title: qsTr("AMD Fluid Motion Frames")
        description: qsTr("Frame generation technology designed to boost frame rates and smooth movement.")
        CcSwitch {
            checked: sys.amdAfmf
            onToggled: (v) => sys.setAmdAfmf(v)
        }
    }
    CcRow {
        title: qsTr("Radeon™ Anti-Lag")
        description: qsTr("Controls pace of CPU work to ensure it does not get ahead of the GPU, reducing input latency.")
        CcSwitch {
            checked: sys.amdAntiLag
            onToggled: (v) => sys.setAmdAntiLag(v)
        }
    }
    CcRow {
        title: qsTr("Radeon™ Boost")
        description: qsTr("Dynamically reduces resolution during motion to improve responsiveness and frame rates.")
        CcSwitch {
            checked: sys.amdBoost
            onToggled: (v) => sys.setAmdBoost(v)
        }
    }
    CcRow {
        title: qsTr("Radeon™ Chill")
        description: qsTr("Power-saving feature that dynamically regulates frame rate based on in-game movement.")
        CcSwitch {
            checked: sys.amdChill
            onToggled: (v) => sys.setAmdChill(v)
        }
    }

    readonly property bool gpuFixedOnly: (device.rev, !!device.value("gpuFixedOnly"))
    CcRow { visible: !!device.value("gpuMax") || !desktop.windows; title: qsTr("GPU clock")
            description: page.gpuFixedOnly ? qsTr("Auto lets the driver decide. Fixed holds one clock, also at idle. Auto comes back after a restart.")
                                           : qsTr("Auto lets the driver decide. A limit saves power and heat.")
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: qsTr("Auto") }, { value: "limit", label: page.gpuFixedOnly ? qsTr("Fixed") : qsTr("Limit") }]
                   current: (device.rev, device.value("gpuClock") > 0 || device.value("gpuClockMin") > 0 ? "limit" : "auto")
                   onPicked: (v) => { if (v === "auto") device.setValue("gpuClockMin", 0)
                                      device.setValue("gpuClock", v === "auto" ? 0 : Math.round((device.value("gpuMax") || 2700) * 0.75)) } } }
    CcRow { title: qsTr("Restart required"); visible: (device.rev, page.gpuFixedOnly && !device.value("gpuClock") && !device.value("gpuClockMin") && !!device.value("gpuForcedBoot"))
            description: qsTr("Restart Windows to give the GPU clock back to the driver.") }
    // Windows (fixed only): the Quick Panel tile may store the clock as the minimum (the daemon takes max, else min).
    CcRow { title: page.gpuFixedOnly ? qsTr("GPU fixed clock") : qsTr("GPU clock limit")
            visible: (device.rev, (device.value("gpuClock") > 0 || (page.gpuFixedOnly && device.value("gpuClockMin") > 0)) && !!device.value("gpuMax"))
        CcSlider { width: 460; from: device.value("gpuMin") || 800; to: device.value("gpuMax") || 2700; stepSize: 50; unit: " MHz"
                   value: (device.rev, device.value("gpuClock") || (page.gpuFixedOnly ? device.value("gpuClockMin") : 0) || 0)
                   onMoved: (v) => { device.setValue("gpuClock", v)
                                     if (page.gpuFixedOnly && device.value("gpuClockMin")) device.setValue("gpuClockMin", 0) } } }
    CcRow { visible: (!!device.value("gpuMax") || !desktop.windows) && !page.gpuFixedOnly; title: qsTr("GPU minimum clock");
            description: qsTr("Keeps the GPU from dropping below this clock. Uses more power at idle.")
        CcSlider { width: 460; from: device.value("gpuMin") || 800; to: device.value("gpuMax") || 2700; stepSize: 50; unit: " MHz"
                   enabledSlider: device.connected
                   value: (device.rev, device.value("gpuClockMin") || device.value("gpuMin") || 800)
                   onMoved: (v) => device.setValue("gpuClockMin", v <= (device.value("gpuMin") || 800) ? 0 : v) } }

    CcSection { title: qsTr("CPU clock");
                note: qsTr("Limits for every core. A lower maximum keeps the CPU cooler.") }
    CcRow { title: qsTr("CPU maximum clock");
        CcSlider { width: 460; from: device.value("cpuMin") || 400; to: device.value("cpuMax") || 5100; stepSize: 100; unit: " MHz"
                   enabledSlider: device.connected
                   value: (device.rev, device.value("cpuClock") || device.value("cpuMax") || 5100)
                   onMoved: (v) => device.setValue("cpuClock", v >= (device.value("cpuMax") || 5100) ? 0 : v) } }
    CcRow { title: qsTr("CPU minimum clock");
        CcSlider { width: 460; from: device.value("cpuMin") || 400; to: device.value("cpuMax") || 5100; stepSize: 100; unit: " MHz"
                   enabledSlider: device.connected
                   value: (device.rev, device.value("cpuClockMin") || device.value("cpuMin") || 400)
                   onMoved: (v) => device.setValue("cpuClockMin", v <= (device.value("cpuMin") || 400) ? 0 : v) } }

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
