import QtQuick
import "../Theme.js" as T

// Settings > Lighting: effect, colour, brightness, speed of the joystick rings.
CcPage {
    id: page
    readonly property string color: (device.rev, device.value("lightColor") || "#00c8ff")
    readonly property string color2: (device.rev, device.value("lightColor2") || "#ff2d55")
    readonly property string effect: (device.rev, device.value("lighting") || "static")
    readonly property string opmode: (device.rev, device.value("opmode") || "performance")
    // effects that use the colour; Follow Operating Mode only in Manual mode
    readonly property bool usesColor: ["static", "breathing", "dualbreathing", "strobe", "followmode"].indexOf(effect) >= 0
    readonly property bool animated: ["off", "static", "battery", "followmode"].indexOf(effect) < 0
    function hueToHex(h) {
        const c = Qt.hsva(h / 360, 1, 1, 1)
        const x = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + x(c.r) + x(c.g) + x(c.b)
    }
    // the colours xpanel-daemon makes of the battery level and the Operating Mode (daemon/Aura.h)
    function batteryColor(pct) {
        const p = Math.max(0, Math.min(100, pct))
        return Qt.rgba(p < 50 ? 1 : (100 - p) / 50, p < 50 ? p / 50 : 1, 0, 1)
    }
    function modeColor(mode) {
        if (mode === "silent") return Qt.color("#0040ff")
        if (mode === "performance") return Qt.color("#ffffff")
        if (mode === "turbo") return Qt.color("#ff0014")
        return Qt.color(page.color)
    }
    function mix(a, b, t) { return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1) }
    // colour of the rings in the preview right now
    readonly property color shown: {
        switch (effect) {
        case "off": return "#2a2a33"
        case "battery": return batteryColor(sys.battery)
        case "followmode": return modeColor(opmode)
        case "dualbreathing": return mix(Qt.color(color), Qt.color(color2), breath.o)
        case "colorcycle": case "rainbow": return Qt.hsva(hueAnim.h / 360, 1, 1, 1)
        default: return color
        }
    }

    CcSection { title: "Lighting"; note: device.connected ? "" : device.error ? "No access to xpanel-daemon: " + device.error : "xpanel-daemon is not running." }
    Rectangle { // preview of the two rings
        width: parent.width; height: 120; color: "#101014"; radius: 4
        Repeater {
            model: 2
            Rectangle {
                x: index === 0 ? parent.width * 0.3 - 40 : parent.width * 0.7 - 40; y: 20
                width: 80; height: 80; radius: 40; color: "transparent"
                border.width: 10
                border.color: page.shown
                opacity: page.effect === "breathing" ? breath.o * 0.85 + 0.15
                       : (page.effect === "strobe" ? (strobe.on ? 1 : 0.12) : 1)
            }
        }
        QtObject { id: hueAnim; property real h: 0; NumberAnimation on h { from: 0; to: 360; duration: 3000; loops: Animation.Infinite } }
        // 0 -> 1 -> 0: breathing in / out
        QtObject { id: breath; property real o: 1; SequentialAnimation on o { loops: Animation.Infinite
                NumberAnimation { to: 0; duration: 1200 } NumberAnimation { to: 1; duration: 1200 } } }
        QtObject { id: strobe; property bool on: true }
        Timer { interval: 180; repeat: true; running: page.effect === "strobe"; onTriggered: strobe.on = !strobe.on }
    }
    CcRow { title: "Effect"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "off", label: "Off" }, { value: "static", label: "Static" }, { value: "breathing", label: "Breathing" },
                             { value: "dualbreathing", label: "Dual breathing" }, { value: "colorcycle", label: "Color cycle" },
                             { value: "rainbow", label: "Rainbow" }, { value: "strobe", label: "Strobe" }]
                   current: page.effect; onPicked: (v) => device.setValue("lighting", v) } }
    CcRow { title: "Follow the system"
           description: "Battery level: red when empty, green when full. Operating Mode: blue Silent, white Performance, red Turbo, your color in Manual."
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "battery", label: "Battery level" }, { value: "followmode", label: "Operating Mode" }]
                   current: page.effect; onPicked: (v) => device.setValue("lighting", v) } }
    CcRow { title: "Color"; visible: page.usesColor
            description: page.effect === "followmode" ? "Used in Manual mode." : ""
        CcSwatches { current: page.color; onPicked: (c) => device.setValue("lightColor", c) } }
    CcRow { title: "Hue"; visible: page.usesColor; description: "Any color of the rainbow."
        CcSlider { width: 460; from: 0; to: 359; stepSize: 5; unit: "°"; value: Qt.color(page.color).hsvHue * 360
                   onMoved: (v) => device.setValue("lightColor", page.hueToHex(v)) } }
    CcRow { title: "Second color"; visible: page.effect === "dualbreathing"; description: "The light breathes between the two colors."
        CcSwatches { current: page.color2; onPicked: (c) => device.setValue("lightColor2", c) } }
    CcRow { title: "Second hue"; visible: page.effect === "dualbreathing"
        CcSlider { width: 460; from: 0; to: 359; stepSize: 5; unit: "°"; value: Qt.color(page.color2).hsvHue * 360
                   onMoved: (v) => device.setValue("lightColor2", page.hueToHex(v)) } }
    CcRow { title: "Direction"; visible: page.effect === "rainbow"; description: "Which way the rainbow runs. Up and down may look the same on the rings."
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "right", label: "Right" }, { value: "left", label: "Left" },
                             { value: "up", label: "Up" }, { value: "down", label: "Down" }]
                   current: (device.rev, device.value("lightDirection") || "right"); onPicked: (v) => device.setValue("lightDirection", v) } }
    CcRow { title: "Brightness"
        CcChoice { enabledChoice: device.connected && page.effect !== "off"
                   options: [{ value: 1, label: "Low" }, { value: 2, label: "Medium" }, { value: 3, label: "High" }]
                   current: (device.rev, device.value("lightBrightness") || 3); onPicked: (v) => device.setValue("lightBrightness", v) } }
    CcRow { title: "Speed"; visible: page.animated
        CcChoice { options: [{ value: "low", label: "Slow" }, { value: "medium", label: "Medium" }, { value: "high", label: "Fast" }]
                   current: (device.rev, device.value("lightSpeed") || "medium"); onPicked: (v) => device.setValue("lightSpeed", v) } }
}
