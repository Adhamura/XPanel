import QtQuick
import "Theme.js" as T

// Vertical slider in the left column (brightness, volume).
Item {
    id: sl
    property real s: 1
    property int value: 50          // 0..100
    property int minimum: 0
    property bool focused: false
    signal moved(int v)

    width: 60 * s

    readonly property real knob: 56 * s
    readonly property real travel: height - knob
    readonly property real knobY: travel * (1 - (value - 0) / 100)

    // track
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 12 * sl.s
        y: sl.knob / 2; height: sl.travel
        color: "#3a3a40"
        radius: width / 2
    }
    // fill (below the knob)
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 6 * sl.s
        y: sl.knobY + sl.knob / 2
        height: sl.travel - sl.knobY
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#d9f2ff" }
            GradientStop { position: 1; color: "#1565ff" }
        }
    }
    // knob
    Rectangle {
        x: (sl.width - width) / 2
        y: sl.knobY
        width: sl.knob; height: sl.knob
        radius: width / 2
        color: "#d6d6da"
        border.color: sl.focused ? T.accent : "#9a9aa0"
        border.width: sl.focused ? 4 * sl.s : 1
        Column {
            anchors.centerIn: parent
            spacing: 4 * sl.s
            Repeater {
                model: 3
                Rectangle { width: 18 * sl.s; height: 2 * sl.s; color: "#9a9aa0" }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -10 * sl.s
        preventStealing: true
        function apply(my) {
            const y = my - 10 * sl.s - sl.knob / 2
            const v = Math.round(100 * (1 - y / sl.travel))
            sl.moved(Math.max(sl.minimum, Math.min(100, v)))
        }
        onPressed: (m) => apply(m.y)
        onPositionChanged: (m) => { if (pressed) apply(m.y) }
    }
}
