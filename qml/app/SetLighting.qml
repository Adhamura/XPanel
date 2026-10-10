import QtQuick
import "../Theme.js" as T

// Settings > Lighting: effect, colour, brightness, speed of the joystick rings.
CcPage {
    id: page
    readonly property string color: (device.rev, device.value("lightColor") || "#00c8ff")
    readonly property string effect: (device.rev, device.value("lighting") || "static")
    readonly property bool usesColor: effect === "static" || effect === "breathing"
    readonly property var presets: ["#ff0014", "#ff6a00", "#ffd000", "#3cff00", "#00c8ff", "#0040ff", "#9600ff", "#ff00b4", "#ffffff"]
    function hueToHex(h) {
        const c = Qt.hsva(h / 360, 1, 1, 1)
        const x = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + x(c.r) + x(c.g) + x(c.b)
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
                border.color: page.effect === "off" ? "#2a2a33" : (page.usesColor ? page.color : Qt.hsva(hueAnim.h / 360, 1, 1, 1))
                opacity: page.effect === "breathing" ? breath.o : 1
            }
        }
        QtObject { id: hueAnim; property real h: 0; NumberAnimation on h { from: 0; to: 360; duration: 3000; loops: Animation.Infinite } }
        QtObject { id: breath; property real o: 1; SequentialAnimation on o { loops: Animation.Infinite
                NumberAnimation { to: 0.15; duration: 1200 } NumberAnimation { to: 1; duration: 1200 } } }
    }
    CcRow { title: "Effect"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: "off", label: "Off" }, { value: "static", label: "Static" }, { value: "breathing", label: "Breathing" },
                             { value: "colorcycle", label: "Color cycle" }, { value: "rainbow", label: "Rainbow" }]
                   current: page.effect; onPicked: (v) => device.setValue("lighting", v) } }
    CcRow { title: "Color"; visible: page.usesColor
        Row {
            spacing: 8
            Repeater {
                model: page.presets
                Rectangle {
                    width: 40; height: 40; radius: 20; color: modelData
                    border.color: activeFocus ? T.accent : (page.color.toLowerCase() === modelData ? "#ffffff" : "#000000")
                    border.width: activeFocus || page.color.toLowerCase() === modelData ? 4 : 1
                    activeFocusOnTab: true
                    function activate() { device.setValue("lightColor", modelData) }
                    MouseArea { anchors.fill: parent; onClicked: parent.activate() }
                }
            }
        }
    }
    CcRow { title: "Hue"; visible: page.usesColor; description: "Any color of the rainbow."
        CcSlider { width: 460; from: 0; to: 359; stepSize: 5; unit: "°"; value: Qt.color(page.color).hsvHue * 360
                   onMoved: (v) => device.setValue("lightColor", page.hueToHex(v)) } }
    CcRow { title: "Brightness"
        CcChoice { enabledChoice: device.connected && page.effect !== "off"
                   options: [{ value: 1, label: "Low" }, { value: 2, label: "Medium" }, { value: 3, label: "High" }]
                   current: (device.rev, device.value("lightBrightness") || 3); onPicked: (v) => device.setValue("lightBrightness", v) } }
    CcRow { title: "Speed"; visible: page.effect !== "static" && page.effect !== "off"
        CcChoice { options: [{ value: "low", label: "Slow" }, { value: "medium", label: "Medium" }, { value: "high", label: "Fast" }]
                   current: (device.rev, device.value("lightSpeed") || "medium"); onPicked: (v) => device.setValue("lightSpeed", v) } }
}
