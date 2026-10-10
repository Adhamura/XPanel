import QtQuick
import "../Theme.js" as T

// Settings > Performance: Operating Mode, manual power limits, Eco Mode, GPU, fan.
CcPage {
    id: page
    readonly property bool manual: device.value("opmode") === "manual"

    CcSection { title: "Operating Mode"; note: device.connected ? "" : device.error ? "No access to xpanel-daemon: " + device.error
                                                                       : "xpanel-daemon is not running: install it with  sudo scripts/install-daemon.sh" }
    CcRow { title: "Mode"; description: "Silent 13 W · Performance 17 W · Turbo 25 W (30 W on the charger) · Manual"
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
        CcSlider { width: 460; from: 0; to: 43; unit: " W"; enabledSlider: device.connected
                   value: (device.rev, device.value("tdpSlow") || 0); onMoved: (v) => device.setValue("tdpSlow", v) } }
    CcRow { title: "FPPT — fast boost"
        CcSlider { width: 460; from: 0; to: 53; unit: " W"; enabledSlider: device.connected
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

    CcSection { title: "Fan" }
    CcRow { title: "Fan curve"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "auto", label: "Auto" }, { value: "quiet", label: "Quiet" }, { value: "max", label: "Max" }]
                   current: (device.rev, device.value("fan")); onPicked: (v) => device.setValue("fan", v) } }

    CcSection { title: "Live" }
    CcRow { title: "CPU / GPU"
        Text { text: sys.cpuLoad + "% " + sys.cpuTemp + "°C   ·   " + sys.gpuLoad + "% " + sys.gpuTemp + "°C"; color: T.text; font.pixelSize: 18 } }
    CcRow { title: "Power / Fan"
        Text { text: "SoC " + sys.socWatts.toFixed(1) + " W   ·   Battery " + sys.batteryWatts.toFixed(1) + " W   ·   Fan " + sys.fanRpm + " rpm"
               color: T.text; font.pixelSize: 18 } }
}
