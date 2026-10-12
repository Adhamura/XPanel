import QtQuick
import QtQuick.Shapes
import "../Theme.js" as T
import "ControllerMapHotspots.js" as Hot

// ROG Ally X key map: ONE line-art view at a time (front, top, back). Changing `view` flips the device
// like a real turn (rotation around X or Y, swapped at 90 degrees). Every hotspot is hoverable and
// selectable. Art: qml/icons/allyx-*.svg. Hotspots: ControllerMapHotspots.js.
// Gamepad (AppWindow.act): navKey() moves the selection to the nearest part in that direction,
// activate() emits picked(selected).
Item {
    id: cm
    property var pressed: ({})
    property var labels: ({})
    property string selected: ""
    property string view: "front"          // front | top | back
    property bool testing: false           // test mode: draw stick dots and trigger fills
    property var axes: ({})                // lx ly rx ry (-1..1, y up), lt rt (0..1)
    property string hoverName: ""          // a button is hovered on any view: all its hotspots glow
    signal picked(string name)

    readonly property var order: Hot.order
    readonly property real stageH: width * Hot.views.front.h / Hot.views.front.w + 12
    implicitHeight: stageH
    activeFocusOnTab: true

    function viewOf(name) { return Hot.buttons[name] ? Hot.buttons[name].view : "" }
    function isSystem(name) { return Hot.isSystem(name) }
    function axisValue(k) { const v = axes[k]; return (testing && typeof v === "number") ? v : 0 }
    onSelectedChanged: {
        const v = viewOf(selected)
        if (v !== "" && !Hot.onView(selected, view)) view = v   // e.g. a pressed paddle flips to the back
    }
    onViewChanged: {
        if (selected !== "" && !Hot.onView(selected, view)) selected = ""
        flip.start()
    }

    // nearest part in a direction (screen axes), inside the current view
    function neighbour(dir) {
        const list = Hot.list(view)
        if (list.length === 0) return ""
        let cur = null
        for (const p of list) if (p.n === selected) cur = p
        if (!cur) {
            for (const n of order) if (Hot.onView(n, view)) return n
            return list[0].n
        }
        let best = "", bestScore = 1e18
        for (const p of list) {
            if (p.n === cur.n) continue
            const dx = p.x - cur.x, dy = p.y - cur.y
            let along, across
            if (dir === "right") { along = dx; across = Math.abs(dy) }
            else if (dir === "left") { along = -dx; across = Math.abs(dy) }
            else if (dir === "down") { along = dy; across = Math.abs(dx) }
            else { along = -dy; across = Math.abs(dx) }
            if (along < 1) continue
            const score = along + 2 * across
            if (score < bestScore) { bestScore = score; best = p.n }
        }
        return best
    }
    function navKey(a) {
        if (a !== "up" && a !== "down" && a !== "left" && a !== "right") return false
        const first = !(selected !== "" && Hot.onView(selected, view))
        const n = neighbour(a)
        if (n !== "") { selected = n; return true }
        if (first) return false
        return a === "left" || a === "right"      // up / down past the edge leave the map
    }
    function step(d) { navKey(d > 0 ? "right" : "left") }
    function activate() {
        if (selected === "" || !Hot.onView(selected, view)) navKey("right")
        if (selected !== "") picked(selected)
    }

    Rectangle {
        anchors.fill: parent; anchors.margins: -4; radius: 6
        color: "transparent"; border.color: T.accent; border.width: 3; visible: cm.activeFocus
    }

    // ---------- flip animation ----------
    Item {
        id: stage
        width: cm.width; height: cm.stageH
        property string shown: "front"
        property real sv: 1
        property real ax: 0
        property real ay: 1
        property real dir: 1
        transform: [
            Rotation { id: rot; origin.x: stage.width / 2; origin.y: stage.height / 2
                       axis.x: stage.ax; axis.y: stage.ay; axis.z: 0; angle: 0 },
            Scale { origin.x: stage.width / 2; origin.y: stage.height / 2; xScale: stage.sv; yScale: stage.sv }
        ]
        Component.onCompleted: shown = cm.view

        Panel { view: stage.shown }
    }

    SequentialAnimation {
        id: flip
        function start() {
            if (running) complete()
            if (stage.shown === cm.view) return
            const fb = (stage.shown === "front" && cm.view === "back") || (stage.shown === "back" && cm.view === "front")
            stage.ax = fb ? 0 : 1; stage.ay = fb ? 1 : 0
            const fwd = (stage.shown === "front") || (stage.shown === "top" && cm.view === "back")
            stage.dir = fwd ? 1 : -1
            restart()
        }
        ParallelAnimation {
            NumberAnimation { target: rot; property: "angle"; to: 90 * stage.dir; duration: 190; easing.type: Easing.InCubic }
            NumberAnimation { target: stage; property: "sv"; to: 0.92; duration: 190; easing.type: Easing.InOutCubic }
        }
        ScriptAction { script: { stage.shown = cm.view; rot.angle = -90 * stage.dir } }
        ParallelAnimation {
            NumberAnimation { target: rot; property: "angle"; to: 0; duration: 190; easing.type: Easing.OutCubic }
            NumberAnimation { target: stage; property: "sv"; to: 1; duration: 190; easing.type: Easing.InOutCubic }
        }
    }

    component Panel: Item {
        id: pn
        property string view: "front"
        readonly property var vw: Hot.views[view]
        readonly property var parts: Hot.list(view)
        readonly property real pw: Math.min(cm.width, (cm.stageH - 12) * vw.w / vw.h)
        readonly property real u: pw / vw.w
        width: pw; height: vw.h * u
        x: (cm.width - pw) / 2; y: (cm.stageH - height) / 2

        Image {
            anchors.fill: parent
            source: pn.vw.src
            sourceSize.width: Math.ceil(pn.pw * 2); sourceSize.height: Math.ceil(pn.height * 2)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        // leader lines
        Repeater {
            model: pn.parts
            Shape {
                id: ld
                required property var modelData
                visible: ld.modelData.lead && ld.modelData.hasLabel
                ShapePath {
                    strokeColor: "#6a6a74"; strokeWidth: 1; fillColor: "transparent"
                    startX: ld.modelData.x * pn.u; startY: ld.modelData.y * pn.u
                    PathLine { x: ld.modelData.lx * pn.u; y: ld.modelData.ly * pn.u }
                }
            }
        }

        // hotspots: faint outline, hover glow, pressed glow, selection ring
        Repeater {
            model: pn.parts
            Item {
                id: btn
                required property var modelData
                readonly property string nm: modelData.n
                readonly property bool isDown: cm.pressed[nm] === true
                readonly property bool isSel: cm.selected === nm
                readonly property bool isHover: ma.containsMouse || cm.hoverName === nm
                objectName: nm
                x: (modelData.x - modelData.w / 2) * pn.u
                y: (modelData.y - modelData.h / 2) * pn.u
                width: modelData.w * pn.u; height: modelData.h * pn.u
                readonly property real trig: nm === "LT" ? cm.axisValue("lt") : (nm === "RT" ? cm.axisValue("rt") : 0)
                readonly property real rad: modelData.round ? Math.min(width, height) / 2 : Math.max(2, modelData.r * pn.u)

                Rectangle {
                    anchors.fill: parent; anchors.margins: -5; radius: btn.rad + 5
                    color: T.accent; opacity: btn.isDown ? 0.35 : (btn.isHover ? 0.12 : 0)
                }
                Rectangle {
                    anchors.fill: parent; radius: btn.rad
                    color: btn.isDown ? Qt.rgba(0.133, 0.78, 0.91, 0.6) : "transparent"
                    border.color: (btn.isDown || btn.isHover) ? T.accent : "#5a5a66"
                    border.width: btn.isDown ? 3 : (btn.isHover ? 2 : 1)
                }
                Rectangle {   // trigger pressure (test mode): fills from the bottom
                    visible: btn.trig > 0.01
                    x: 0; width: parent.width; height: parent.height * btn.trig; y: parent.height - height
                    radius: Math.min(btn.rad, height / 2)
                    color: T.accent; opacity: 0.55
                }
                Rectangle {
                    anchors.fill: parent; anchors.margins: -5; radius: btn.rad + 5
                    color: "transparent"; border.color: T.accent; border.width: 3; visible: btn.isSel
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true
                    onClicked: { cm.forceActiveFocus(); cm.selected = btn.nm }
                    onContainsMouseChanged: {
                        if (containsMouse) cm.hoverName = btn.nm
                        else if (cm.hoverName === btn.nm) cm.hoverName = ""
                    }
                }
            }
        }

        // stick position dots (test mode)
        Repeater {
            model: cm.testing ? (Hot.sticks[pn.view] || []) : []
            Rectangle {
                id: dot
                required property var modelData
                readonly property real dr: 0.4 * Math.min(modelData.rx, modelData.ry)
                readonly property real sx: cm.axisValue(modelData.side === "l" ? "lx" : "rx")
                readonly property real sy: cm.axisValue(modelData.side === "l" ? "ly" : "ry")
                width: dr * 2 * pn.u; height: width; radius: width / 2
                x: (modelData.x + sx * (modelData.rx - dr)) * pn.u - width / 2
                y: (modelData.y - (modelData.noY ? 0 : sy) * (modelData.ry - dr)) * pn.u - height / 2
                color: T.accent; border.color: "#0b0b0f"; border.width: 1
            }
        }

        // labels
        Repeater {
            model: pn.parts
            Rectangle {
                id: lb
                required property var modelData
                visible: modelData.hasLabel
                readonly property string nm: modelData.n
                readonly property bool isDown: cm.pressed[nm] === true
                readonly property bool isSel: cm.selected === nm
                readonly property string shown: {
                    const v = cm.labels[nm]
                    return (v === undefined || v === "") ? "-" : String(v)
                }
                readonly property real maxW: modelData.narrow ? 62 : 130
                width: Math.min(maxW, tx.implicitWidth) + 12
                height: modelData.narrow ? 40 : 24
                radius: 4
                color: isSel ? "#1b3446" : "#101015"
                border.color: (isSel || isDown || lma.containsMouse) ? T.accent : "#2a2a32"; border.width: 1
                x: {
                    const cx = modelData.lx * pn.u
                    return modelData.la === "l" ? cx : (modelData.la === "r" ? cx - width : cx - width / 2)
                }
                y: modelData.ly * pn.u - height / 2
                Text {
                    id: tx
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, lb.maxW)
                    text: lb.shown
                    color: lb.isDown ? T.accent : (lb.shown === "-" ? T.dim : T.text)
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: modelData.narrow ? Text.Wrap : Text.NoWrap
                    maximumLineCount: modelData.narrow ? 2 : 1
                    elide: Text.ElideRight
                }
                MouseArea { id: lma; anchors.fill: parent; hoverEnabled: true
                            onClicked: { cm.forceActiveFocus(); cm.selected = lb.nm } }
            }
        }
    }

    Keys.onLeftPressed: (e) => { if (!navKey("left")) e.accepted = false }
    Keys.onRightPressed: (e) => { if (!navKey("right")) e.accepted = false }
    Keys.onUpPressed: (e) => { if (!navKey("up")) e.accepted = false }
    Keys.onDownPressed: (e) => { if (!navKey("down")) e.accepted = false }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
}
