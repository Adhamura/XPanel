import QtQuick
import "Theme.js" as T

// One line of live sensor values (the Real-time Monitor).
Row {
    id: st
    property real s: 1
    spacing: 22 * s
    component Stat: Row {
        property string label
        property string val
        spacing: 7 * st.s
        Text { text: label; color: T.dim; font.pixelSize: 20 * st.s; anchors.baseline: v.baseline }
        Text { id: v; text: val; color: T.text; font.pixelSize: 24 * st.s; font.bold: true }
    }
    function mhz(v) { return v > 0 ? "  " + v + " MHz" : "" }
    Stat { label: "CPU"; val: sys.cpuLoad + "%  " + sys.cpuTemp + "°" + mhz(sys.cpuMhz) }
    Stat { label: "GPU"; val: sys.gpuLoad + "%  " + sys.gpuTemp + "°" + mhz(sys.gpuMhz) }
    Stat { label: "SoC"; val: sys.socWatts.toFixed(1) + "W" }
    Stat { label: qsTr("FAN"); val: sys.fanRpm + "" }
    Stat { label: "RAM"; val: (sys.ramUsedMb / 1024).toFixed(1) + "G" }
    Stat { label: qsTr("BAT"); val: (sys.charging ? "+" : "-") + sys.batteryWatts.toFixed(1) + "W" }
}
