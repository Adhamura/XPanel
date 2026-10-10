import QtQuick
import "../Theme.js" as T

// Fan curve graph: 8 points, x = temperature in °C, y = fan speed in %.
//   Touch / mouse: drag a point. Moving a point up or down moves the points after / before it
//   along, so the curve never gets quieter as it gets hotter (the firmware insists on that).
//   Gamepad: A = edit, then left / right pick a point, up / down change its fan speed,
//   LB / RB its temperature; A or B = done. (Up / down / LB / RB reach the editor through navKey().)
// The change is announced with committed(curve) a moment after the last move.
Item {
    id: ed
    property var source: null        // [[°C, %] x 8] from the daemon
    property bool enabledEditor: true
    property string caption: "Fan curve"
    property string liveText: ""     // right of the caption: current temperature and fan speed
    property int liveTemp: 0         // marker line at this temperature (0 = none)
    signal committed(var curve)

    readonly property int minTemp: 20
    readonly property int maxTemp: 105
    readonly property int points: 8
    property var pts: [[40, 10], [50, 25], [60, 40], [70, 55], [78, 70], [85, 82], [90, 92], [95, 100]]
    property int sel: 0
    property bool editing: false
    property var backFn: null
    readonly property bool interacting: area.pressed || commitTimer.running

    function isCurve(c) {
        if (!c || c.length !== points) return false
        for (let i = 0; i < points; ++i) if (!c[i] || c[i].length !== 2) return false
        return true
    }
    // take the daemon's curve unless the user is in the middle of changing it
    function adopt() {
        if (!interacting && isCurve(source)) pts = source.map(p => [p[0], p[1]])
    }
    onSourceChanged: adopt()
    Component.onCompleted: adopt()

    // Move point i to (t °C, d %). Temperatures stay between the neighbours; the fan speed pushes
    // the other points along.
    function moveTo(i, t, d) {
        const a = pts.map(p => [p[0], p[1]])
        const lo = i > 0 ? a[i - 1][0] + 1 : minTemp
        const hi = i < points - 1 ? a[i + 1][0] - 1 : maxTemp
        a[i][0] = Math.max(lo, Math.min(hi, Math.round(t)))
        const duty = Math.max(0, Math.min(100, Math.round(d)))
        a[i][1] = duty
        for (let j = i + 1; j < points; ++j) if (a[j][1] < duty) a[j][1] = duty
        for (let j = i - 1; j >= 0; --j) if (a[j][1] > duty) a[j][1] = duty
        pts = a
    }
    function commitSoon() { commitTimer.restart() }
    Timer { id: commitTimer; interval: 350; onTriggered: ed.committed(ed.pts) }

    // ---------- gamepad / keyboard ----------
    function step(d) { if (enabledEditor) sel = Math.max(0, Math.min(points - 1, sel + d)) }
    function nudge(dt, dp) {
        moveTo(sel, pts[sel][0] + dt, pts[sel][1] + dp)
        commitSoon()
    }
    function enterEdit() {
        editing = true
        backFn = () => { ed.editing = false; ed.backFn = null }
        app.pushBack(backFn) // Ⓑ ends the editing
    }
    function leaveEdit() {
        editing = false
        if (backFn) {
            const st = app.backStack
            if (st.length && st[st.length - 1] === backFn) app.popBack()
            backFn = null
        }
    }
    function activate() {
        if (!enabledEditor) return
        if (editing) leaveEdit(); else enterEdit()
    }
    // up / down: fan speed, LB / RB: temperature, while editing
    function navKey(a) {
        if (!editing) return false
        if (a === "up") nudge(0, 5)
        else if (a === "down") nudge(0, -5)
        else if (a === "rb") nudge(1, 0)
        else if (a === "lb") nudge(-1, 0)
        else return false
        return true
    }
    onActiveFocusChanged: if (!activeFocus && editing) leaveEdit()
    activeFocusOnTab: enabledEditor
    Keys.onPressed: (e) => {
        if (!editing) return
        const names = {}
        names[Qt.Key_Up] = "up"; names[Qt.Key_Down] = "down"
        names[Qt.Key_PageUp] = "lb"; names[Qt.Key_PageDown] = "rb"
        if (names[e.key] !== undefined && navKey(names[e.key])) e.accepted = true
    }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()

    // ---------- look ----------
    implicitHeight: 430
    opacity: enabledEditor ? 1 : 0.4
    Rectangle { anchors.fill: parent; color: "#131318"; radius: 3 }
    Rectangle {
        anchors.fill: parent; radius: 3; color: "transparent"
        border.color: ed.editing ? "#ffffff" : T.accent; border.width: 3; visible: ed.activeFocus
    }

    Item {
        id: head
        x: 20; y: 12; width: parent.width - 40; height: 28
        Text { text: ed.caption; color: T.text; font.pixelSize: 19 }
        Text { anchors.right: parent.right; text: ed.liveText; color: T.dim; font.pixelSize: 16
               anchors.verticalCenter: parent.verticalCenter }
    }

    Item {
        id: graph
        x: 12; y: head.y + head.height + 6; width: parent.width - 24; height: parent.height - y - 52
        readonly property real padL: 52
        readonly property real padR: 20
        readonly property real padT: 22
        readonly property real padB: 32
        readonly property real plotW: width - padL - padR
        readonly property real plotH: height - padT - padB
        function xOf(t) { return padL + (t - ed.minTemp) / (ed.maxTemp - ed.minTemp) * plotW }
        function yOf(p) { return padT + (1 - p / 100) * plotH }
        function tOf(x) { return ed.minTemp + (x - padL) / plotW * (ed.maxTemp - ed.minTemp) }
        function pOf(y) { return (1 - (y - padT) / plotH) * 100 }

        Canvas {
            id: canvas
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: ed
                function onPtsChanged() { canvas.requestPaint() }
                function onLiveTempChanged() { canvas.requestPaint() }
            }
            onPaint: {
                const c = getContext("2d")
                c.clearRect(0, 0, width, height)
                c.font = "13px sans-serif"
                c.lineWidth = 1
                // grid and axes
                c.strokeStyle = "#26262d"; c.fillStyle = T.dim
                c.textAlign = "right"; c.textBaseline = "middle"
                for (let p = 0; p <= 100; p += 25) {
                    const y = Math.round(graph.yOf(p)) + 0.5
                    c.beginPath(); c.moveTo(graph.padL, y); c.lineTo(graph.padL + graph.plotW, y); c.stroke()
                    c.fillText(p + "%", graph.padL - 8, y)
                }
                c.textAlign = "center"; c.textBaseline = "top"
                for (let t = 20; t <= 100; t += 10) {
                    const x = Math.round(graph.xOf(t)) + 0.5
                    c.beginPath(); c.moveTo(x, graph.padT); c.lineTo(x, graph.padT + graph.plotH); c.stroke()
                    if (t % 20 === 0) c.fillText(t + "°C", x, graph.padT + graph.plotH + 8)
                }
                // the current temperature
                if (ed.liveTemp >= ed.minTemp && ed.liveTemp <= ed.maxTemp) {
                    const x = Math.round(graph.xOf(ed.liveTemp)) + 0.5
                    c.strokeStyle = "#8b8b92"; c.setLineDash([5, 5])
                    c.beginPath(); c.moveTo(x, graph.padT); c.lineTo(x, graph.padT + graph.plotH); c.stroke()
                    c.setLineDash([])
                }
                // curve: flat before the first and after the last point
                const a = ed.pts
                c.beginPath()
                c.moveTo(graph.xOf(ed.minTemp), graph.yOf(a[0][1]))
                for (let i = 0; i < a.length; ++i) c.lineTo(graph.xOf(a[i][0]), graph.yOf(a[i][1]))
                c.lineTo(graph.xOf(ed.maxTemp), graph.yOf(a[a.length - 1][1]))
                c.strokeStyle = T.accent; c.lineWidth = 3; c.lineJoin = "round"; c.stroke()
                c.lineTo(graph.xOf(ed.maxTemp), graph.yOf(0))
                c.lineTo(graph.xOf(ed.minTemp), graph.yOf(0))
                c.closePath()
                c.fillStyle = "rgba(34, 199, 232, 0.13)"; c.fill()
            }
        }

        Repeater {
            model: ed.points
            Rectangle {
                readonly property bool on: index === ed.sel
                x: graph.xOf(ed.pts[index][0]) - width / 2
                y: graph.yOf(ed.pts[index][1]) - height / 2
                width: on ? 30 : 22; height: width; radius: width / 2
                color: on ? (ed.editing ? "#ffffff" : T.accent) : "#e8e8ec"
                border.color: on ? "#ffffff" : "#8a8a94"; border.width: on ? 3 : 1
            }
        }
        // value of the picked point
        Rectangle {
            readonly property real px: graph.xOf(ed.pts[ed.sel][0])
            readonly property real py: graph.yOf(ed.pts[ed.sel][1])
            width: tip.implicitWidth + 20; height: 30; radius: 4
            color: "#1b3446"; border.color: T.accent
            x: Math.max(0, Math.min(graph.width - width, px - width / 2))
            y: py - 50 < 0 ? py + 24 : py - 50
            Text { id: tip; anchors.centerIn: parent; color: "#ffffff"; font.pixelSize: 16
                   text: ed.pts[ed.sel][0] + " °C  ·  " + ed.pts[ed.sel][1] + " %" }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            enabled: ed.enabledEditor
            preventStealing: true
            property bool dragging: false
            property real offX: 0
            property real offY: 0
            onPressed: (m) => {
                ed.forceActiveFocus()
                let best = -1, bestD = 52 * 52
                for (let i = 0; i < ed.points; ++i) {
                    const dx = m.x - graph.xOf(ed.pts[i][0]), dy = m.y - graph.yOf(ed.pts[i][1])
                    const d = dx * dx + dy * dy
                    if (d < bestD) { bestD = d; best = i }
                }
                dragging = best >= 0
                if (dragging) {
                    ed.sel = best
                    offX = m.x - graph.xOf(ed.pts[best][0])
                    offY = m.y - graph.yOf(ed.pts[best][1])
                }
            }
            onPositionChanged: (m) => {
                if (pressed && dragging) ed.moveTo(ed.sel, graph.tOf(m.x - offX), graph.pOf(m.y - offY))
            }
            onReleased: { if (dragging) ed.commitSoon(); dragging = false }
            onCanceled: { if (dragging) ed.commitSoon(); dragging = false }
        }
    }

    Text {
        x: 20; anchors.bottom: parent.bottom; anchors.bottomMargin: 14; width: parent.width - 40
        wrapMode: Text.WordWrap; color: ed.editing ? T.accent : T.dim; font.pixelSize: 15
        text: ed.editing ? "◀ ▶ point    ▲ ▼ fan speed    LB / RB temperature    Ⓐ or Ⓑ done"
                         : "Drag the points. With a gamepad: Ⓐ to edit the curve."
    }
}
