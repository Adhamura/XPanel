import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1600; height: 960
    visible: true
    color: "#0b0b0e"
    title: "XPanel Button Mapper — ROG Ally X"

    // ---------- state ----------
    property int selected: -1
    property bool holdMode: false
    property bool rowFirst: false     // true after a click on a list row: the next picture click labels that row
    property string armed: ""         // control clicked on the picture, waiting for its input
    property double armedKeepUntil: 0 // sticks / analog triggers: keep collecting axes for a moment
    property var assigned: ({})
    property var active: ({})
    property int rev: 0
    property string statusText: "Press Start. The probe stops InputPlumber while it records."

    ListModel { id: rows }

    readonly property var controlNames: ({
        A: "A", B: "B", X: "X", Y: "Y",
        DPadUp: "D-pad up", DPadDown: "D-pad down", DPadLeft: "D-pad left", DPadRight: "D-pad right",
        LeftStick: "Left stick", RightStick: "Right stick", View: "View", Menu: "Menu",
        LeftMenu: "Left menu button", RightMenu: "Right menu button",
        LB: "LB", RB: "RB", LT: "LT", RT: "RT", Power: "Power", VolumeUp: "Volume +",
        VolumeDown: "Volume −", LeftPaddle: "Left paddle", RightPaddle: "Right paddle",
        Ignore: "Ignore (noise)"
    })

    function rowKey(ev) {
        if (ev.kind === "hid") {
            // vendor reports: the command byte matters; other report ids stream
            const vendor = ev.id === "5a" || ev.id === "5d"
            return ev.src + "|hid|" + ev.id + (vendor ? "|" + ev.data.substr(0, 8) : "")
        }
        return ev.src + "|" + ev.kind + "|" + ev.code
    }
    function findRow(key) {
        for (let i = 0; i < rows.count; ++i) if (rows.get(i).key === key) return i
        return -1
    }
    function controlText(r) {
        if (!r.control) return ""
        let t = controlNames[r.control] || r.control
        if (r.kind === "abs" && (r.control === "LeftStick" || r.control === "RightStick"))
            t += " axis " + r.name.replace("ABS_", "")
        else if (r.control === "LeftStick") t = "L3 (stick click)"
        else if (r.control === "RightStick") t = "R3 (stick click)"
        else if (r.kind === "abs" && (r.control === "LT" || r.control === "RT")) t += " (analog)"
        if (r.variant === "hold") t += " — hold"
        return t
    }
    function rebuildAssigned() {
        const a = {}
        for (let i = 0; i < rows.count; ++i) { const c = rows.get(i).control; if (c) a[c] = true }
        assigned = a
        rev++
    }
    function isPressed(ev) {
        if (ev.kind === "key") return ev.value !== 0
        if (ev.kind === "abs") {
            const mid = (ev.min + ev.max) / 2, half = Math.max(1, (ev.max - ev.min) / 2)
            // triggers rest at min, sticks rest in the middle
            const rest = ev.min >= 0 ? ev.min : mid
            return Math.abs(ev.value - rest) > half * 0.35
        }
        return true
    }
    readonly property var analogControls: ["LeftStick", "RightStick", "LT", "RT"]
    // Picture click: label the row the user picked in the list, or wait for the next input.
    function picked(control) {
        if (rowFirst && selected >= 0) { assign(control); rowFirst = false; return }
        armed = armed === control ? "" : control
        armedKeepUntil = 0
        statusText = armed ? "Now press / move  " + (controlNames[armed] || armed) + "  on the Ally" : "Cancelled."
    }
    // A new input while a control is armed: that input belongs to the armed control.
    function learn(i, ev) {
        if (!armed || ev.kind === "hid" || !isPressed(ev)) return false
        const analog = analogControls.indexOf(armed) >= 0
        if (armedKeepUntil > 0 && (Date.now() > armedKeepUntil || ev.kind !== "abs")) {
            armed = ""; armedKeepUntil = 0
            return false
        }
        selected = i
        assign(armed)
        if (analog && ev.kind === "abs") {
            if (armedKeepUntil === 0) armedKeepUntil = Date.now() + 2000
            statusText += "  (move the other axis too)"
        } else {
            armed = ""
        }
        return true
    }
    function resetMap() {
        for (let i = 0; i < rows.count; ++i) { rows.setProperty(i, "control", ""); rows.setProperty(i, "variant", "") }
        armed = ""; rebuildAssigned()
        statusText = "All labels removed. Click a control on the picture, then press it on the Ally."
    }
    function assign(control) {
        if (selected < 0 || selected >= rows.count) { statusText = "Select an input in the list first."; return }
        rows.setProperty(selected, "control", control === "Clear" ? "" : control)
        rows.setProperty(selected, "variant", control === "Clear" || control === "Ignore" ? "" : (holdMode ? "hold" : "press"))
        rebuildAssigned()
        statusText = control === "Clear" ? "Cleared." : "Assigned: " + controlText(rows.get(selected))
    }
    function save() {
        const out = []
        for (let i = 0; i < rows.count; ++i) {
            const r = rows.get(i)
            if (!r.control) continue
            out.push({ control: r.control, variant: r.variant, kind: r.kind, src: r.src, dev: r.dev,
                       code: r.code, name: r.name, hidId: r.hidId, hidPrefix: r.hidPrefix,
                       min: r.min, max: r.max, sample: r.data })
        }
        backend.save(out)
    }

    Component.onCompleted: {
        // show what was mapped before, so the session can continue
        for (const m of backend.load()) {
            const ev = { kind: m.kind, src: m.src, code: m.code, id: m.hidId, data: m.hidPrefix || "" }
            rows.append({ key: rowKey(ev), src: m.src, dev: m.dev, kind: m.kind, code: m.code || 0,
                          name: m.name || ("report " + m.hidId), value: 0, min: m.min || 0, max: m.max || 0,
                          data: m.sample || "", hidId: m.hidId || "", hidPrefix: m.hidPrefix || "",
                          count: 0, control: m.control, variant: m.variant || "", last: 0 })
        }
        rebuildAssigned()
    }

    Connections {
        target: backend
        function onStatus(text) { win.statusText = text }
        function onInput(ev) {
            const key = win.rowKey(ev)
            let i = win.findRow(key)
            const label = ev.kind === "hid" ? "report " + ev.id + (ev.id === "5a" || ev.id === "5d" ? " " + ev.data.substr(3, 5) : "") : ev.name
            if (i < 0) {
                rows.append({ key: key, src: ev.src, dev: ev.dev, kind: ev.kind, code: ev.code || 0, name: label,
                              value: ev.value || 0, min: ev.min || 0, max: ev.max || 0, data: ev.data || "",
                              hidId: ev.id || "", hidPrefix: ev.kind === "hid" ? ev.data.substr(0, 8) : "",
                              count: 0, control: "", variant: "", last: 0 })
                i = rows.count - 1
            }
            rows.setProperty(i, "count", rows.get(i).count + 1)
            rows.setProperty(i, "value", ev.value || 0)
            rows.setProperty(i, "last", Date.now())
            if (ev.kind === "hid") rows.setProperty(i, "data", ev.data)
            if (win.learn(i, ev)) { list.positionViewAtIndex(i, ListView.Contain); return }
            const r = rows.get(i)
            if (r.control) {
                const a = win.active
                a[r.control] = win.isPressed(ev)
                win.active = a
                win.rev++
            }
        }
    }

    // ---------- UI ----------
    component Btn: Rectangle {
        property string text
        property bool on: false
        signal clicked()
        width: label.implicitWidth + 28; height: 36; radius: 4
        color: on ? "#1f6f99" : (ma.pressed ? "#33333d" : "#22222a")
        border.color: on ? "#1fb6ff" : "#44444f"
        Text { id: label; anchors.centerIn: parent; text: parent.text; color: "#f2f2f2"; font.pixelSize: 15 }
        MouseArea { id: ma; anchors.fill: parent; onClicked: parent.clicked() }
    }

    Row {
        id: topBar
        x: 16; y: 12; spacing: 10
        Text { text: "Button Mapper"; color: "#f2f2f2"; font.pixelSize: 24; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
        Item { width: 12; height: 1 }
        Btn { text: backend.running ? "Stop recording" : "Start recording"; on: backend.running
              onClicked: backend.running ? backend.stop() : backend.start() }
        Btn { text: "Assign as: " + (win.holdMode ? "HOLD" : "press"); on: win.holdMode; onClicked: win.holdMode = !win.holdMode }
        Btn { text: "Reset map"; onClicked: win.resetMap() }
        Btn { text: "Ignore"; onClicked: win.assign("Ignore") }
        Btn { text: "Clear"; onClicked: win.assign("Clear") }
        Btn { text: "Save"; onClicked: win.save() }
        Btn { text: "Forget unmapped"; onClicked: {
                for (let i = rows.count - 1; i >= 0; --i) if (!rows.get(i).control) rows.remove(i)
                win.selected = -1 } }
    }
    Text {
        x: 16; y: topBar.y + topBar.height + 8
        width: parent.width - 32
        text: win.statusText + "    •    file: " + backend.mapPath
        color: "#9a9aa4"; font.pixelSize: 14; elide: Text.ElideRight
    }

    // input list
    Rectangle {
        id: listBox
        x: 16; y: 96; width: 560; height: parent.height - y - 16
        color: "#111116"; border.color: "#2c2c31"
        Text { x: 12; y: 8; text: "Inputs seen (" + rows.count + ")  ·  click a row to fix one label"
               color: "#9a9aa4"; font.pixelSize: 13 }
        ListView {
            id: list
            x: 6; y: 32; width: parent.width - 12; height: parent.height - 38
            clip: true; spacing: 4
            model: rows
            delegate: Rectangle {
                id: rowItem
                width: list.width; height: 58
                readonly property bool sel: index === win.selected
                readonly property bool fresh: (Date.now() - model.last) < 600
                color: sel ? "#1b2a36" : "#16161c"
                border.color: sel ? "#1fb6ff" : (model.control ? "#2b5a73" : "#2c2c31")
                border.width: sel ? 2 : 1
                Rectangle { // activity flash
                    width: 6; height: parent.height; color: "#ffb020"
                    opacity: 0
                    SequentialAnimation on opacity { id: flash; running: false
                        NumberAnimation { to: 1; duration: 40 } NumberAnimation { to: 0; duration: 500 } }
                }
                property int seen: model.count
                onSeenChanged: flash.restart()
                Column {
                    x: 14; y: 6; width: parent.width - 150
                    Text { text: model.name + "   ·   " + model.kind + (model.kind === "hid" ? "" : " " + model.code)
                           color: "#f2f2f2"; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; width: parent.width }
                    Text { text: model.src + "  ·  " + model.dev + (model.kind === "hid" ? "  ·  " + model.data : "")
                           color: "#8b8b92"; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width }
                    Text { text: model.control ? "→ " + win.controlText(model) : "not mapped"
                           color: model.control ? "#1fb6ff" : "#5d5d66"; font.pixelSize: 13 }
                }
                Column {
                    anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                    Text { anchors.right: parent.right; text: "×" + model.count; color: "#9a9aa4"; font.pixelSize: 14 }
                    Text { anchors.right: parent.right; visible: model.kind !== "hid"
                           text: model.kind === "abs" ? String(model.value) : (model.value ? "down" : "up")
                           color: model.value ? "#ffb020" : "#5d5d66"; font.pixelSize: 13 }
                }
                MouseArea { anchors.fill: parent; onClicked: { win.selected = index; win.rowFirst = true; win.armed = ""
                    win.statusText = "Row selected: now click its control on the picture." } }
            }
        }
    }

    // pictures
    Column {
        x: listBox.x + listBox.width + 16; y: 96
        width: parent.width - x - 16
        spacing: 10
        readonly property string selControl: win.selected >= 0 && win.selected < rows.count ? rows.get(win.selected).control : ""
        Rectangle {
            width: parent.width; height: 44; radius: 4
            color: win.armed ? "#3a2a08" : "#16161c"
            border.color: win.armed ? "#ffb020" : "#2c2c31"
            Text {
                anchors.centerIn: parent
                text: !backend.running ? "Click  Start recording  first"
                      : win.armed ? "Now press / move:  " + (win.controlNames[win.armed] || win.armed) + "   (click it again to cancel)"
                      : "1. Click a control on the picture    2. Press it on the Ally"
                color: win.armed ? "#ffffff" : "#c8c8d0"; font.pixelSize: 20; font.bold: !!win.armed
            }
        }
        Text { text: "FRONT  (top edge above: LT / LB, RT / RB, volume, power)"; color: "#9a9aa4"; font.pixelSize: 14 }
        AllyView {
            side: "front"
            width: parent.width; height: (win.height - 200) * 0.56
            assigned: win.assigned; active: win.active; rev: win.rev
            highlight: parent.selControl
            armed: win.armed
            onPicked: (c) => win.picked(c)
        }
        Text { text: "BACK  (seen from behind: the left paddle is on the right)"; color: "#9a9aa4"; font.pixelSize: 14 }
        AllyView {
            side: "back"
            width: parent.width; height: (win.height - 200) * 0.40
            assigned: win.assigned; active: win.active; rev: win.rev
            highlight: parent.selControl
            armed: win.armed
            onPicked: (c) => win.picked(c)
        }
    }

    onClosing: backend.stop()
}
