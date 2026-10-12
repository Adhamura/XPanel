.pragma library

// Hotspots of the ROG Ally X line art used by ControllerMap.qml.
// Front, top and back numbers match the final art (viewBoxes moved to a 0 origin).
// Each view: viewBox size of its SVG. Each button: view, x, y (centre), w, h in viewBox units,
// round = circle/pill shape. Label placement (same units): lx, ly = label centre,
// la = "c" | "l" | "r" (anchor), narrow = two-line label, lead = draw a leader line.
// also = [{view, x, y, w, h, r, round, lx, ly, la, lead}]: more hotspots of the same button on other
// views (lx..lead optional: no label without lx). system = true: not mappable (Windows handles it).

var views = {
    front: { src: "../icons/allyx-front.svg", w: 1352, h: 572, title: "Front" },
    top:   { src: "../icons/allyx-top.svg",   w: 1156, h: 192, title: "Top" },
    back:  { src: "../icons/allyx-back.svg",  w: 378,  h: 160, title: "Back (as held, left stays left)" }
}

// Front and back: centres converted from the SVG viewBoxes (origin 0,0).
var buttons = {
    "View":        { view: "front", x: 258.8, y: 109.6, w: 32.8, h: 50.4, round: false, r: 4, lx: 300, ly: 100, la: "l", lead: true },
    "QAM":         { view: "front", x: 258.8, y: 161.0, w: 32.8, h: 50.4, round: false, r: 4, lx: 300, ly: 150, la: "l", lead: true },
    "Menu":        { view: "front", x: 1092.9, y: 109.6, w: 32.8, h: 50.4, round: false, r: 4, lx: 1050, ly: 75, la: "r", lead: true },
    "Armoury":     { view: "front", x: 1092.9, y: 161.0, w: 32.8, h: 50.4, round: false, r: 4, lx: 1050, ly: 125, la: "r", lead: true },
    "L3":          { view: "front", x: 116.3, y: 132.3, w: 134.3, h: 134.3, round: true, lx: 116, ly: 40 },
    "R3":          { view: "front", x: 1164.2, y: 257.0, w: 134.3, h: 134.3, round: true, lx: 1164, ly: 354 },
    "D-pad up":    { view: "front", x: 199.0, y: 204.8, w: 33.4, h: 37.5, round: false, r: 2, lx: 300, ly: 200, la: "l", lead: true },
    "D-pad down":  { view: "front", x: 199.0, y: 273.3, w: 33.4, h: 37.5, round: false, r: 2, lx: 199, ly: 330 },
    "D-pad left":  { view: "front", x: 164.8, y: 239.0, w: 37.5, h: 33.4, round: false, r: 2, lx: 75, ly: 239, narrow: true },
    "D-pad right": { view: "front", x: 233.3, y: 239.0, w: 37.5, h: 33.4, round: false, r: 2, lx: 300, ly: 255, la: "l", lead: true },
    "Y":           { view: "front", x: 1225.4, y: 72.9, w: 55.8, h: 55.8, round: true, lx: 1225, ly: 28 },
    "X":           { view: "front", x: 1177.6, y: 120.7, w: 55.8, h: 55.8, round: true, lx: 1040, ly: 22, la: "r", lead: true },
    "B":           { view: "front", x: 1273.2, y: 120.7, w: 55.8, h: 55.8, round: true, lx: 1300, ly: 215, narrow: true },
    "A":           { view: "front", x: 1225.4, y: 168.5, w: 55.8, h: 55.8, round: true, lx: 1050, ly: 215, la: "r", lead: true },

    // Back view: art is mirrored (matrix(-1 0 0 1 464 0), viewBox origin 43,12): x = 421 - X_art, y = Y_art - 12.
    // Seen from behind the bumper is the top strip of each shoulder, the trigger the wedge below it.
    "LT": { view: "top", x: 118.1, y: 55.4, w: 156.0, h: 90.1, round: false, r: 10, lx: 250, ly: 30, la: "l", lead: true,
            also: [{ view: "back", x: 32.5, y: 16, w: 41, h: 12, r: 3, lx: 76, ly: 42, la: "l", lead: true }] },
    "LB": { view: "top", x: 124.1, y: 113.8, w: 168.0, h: 57.4, round: false, r: 8, lx: 270, ly: 128, la: "l", lead: true,
            also: [{ view: "back", x: 38, y: 6, w: 30, h: 7, r: 2, lx: 76, ly: 28, la: "l", lead: true }] },
    "RT": { view: "top", x: 1037.9, y: 55.4, w: 156.0, h: 90.1, round: false, r: 10, lx: 906, ly: 30, la: "r", lead: true,
            also: [{ view: "back", x: 345.5, y: 16, w: 41, h: 12, r: 3, lx: 302, ly: 42, la: "r", lead: true }] },
    "RB": { view: "top", x: 1031.9, y: 113.8, w: 168.0, h: 57.4, round: false, r: 8, lx: 886, ly: 128, la: "r", lead: true,
            also: [{ view: "back", x: 340, y: 6, w: 30, h: 7, r: 2, lx: 302, ly: 28, la: "r", lead: true }] },

    // Top view system parts (viewBox origin 52.5,94): volume rocker and power button
    "Volume down": { view: "top", x: 424.8, y: 124.0, w: 37.4, h: 18.0, round: false, r: 3, system: true },
    "Volume up":   { view: "top", x: 479.6, y: 124.0, w: 37.4, h: 18.0, round: false, r: 3, system: true },
    "Power":       { view: "top", x: 892.9, y: 128.0, w: 55.1, h: 21.3, round: false, r: 4, system: true },

    "Left paddle":  { view: "back", x: 53.8,  y: 72.5, w: 35.5, h: 41.0, round: false, r: 4, lx: 54, ly: 112, la: "c" },
    "Right paddle": { view: "back", x: 324.3, y: 72.5, w: 35.5, h: 41.0, round: false, r: 4, lx: 324, ly: 112, la: "c" }
}

// Selection order for step().
var order = ["LB", "LT", "QAM", "View", "L3", "D-pad up", "D-pad left", "D-pad right", "D-pad down",
             "Y", "X", "B", "A", "Armoury", "Menu", "R3", "RB", "RT", "Left paddle", "Right paddle",
             "Volume down", "Volume up", "Power"]

// Stick caps for the test-mode dot (centre x, y and half sizes rx, ry in viewBox units).
// noY: top view shows the cap from the side, so only left / right movement is drawn.
var sticks = {
    front: [{ side: "l", x: 116.3, y: 132.3, rx: 67.15, ry: 67.15 },
            { side: "r", x: 1164.2, y: 257.0, rx: 67.15, ry: 67.15 }],
    top:   [{ side: "l", x: 100.55, y: 177.5, rx: 34.65, ry: 8.5, noY: true },
            { side: "r", x: 973.95, y: 177.5, rx: 34.65, ry: 8.5, noY: true }]
}

function isSystem(name) { return !!(buttons[name] && buttons[name].system) }

// true when the button has a hotspot on this view
function onView(name, view) {
    var b = buttons[name]
    if (!b) return false
    if (b.view === view) return true
    var a = b.also || []
    for (var i = 0; i < a.length; i++) if (a[i].view === view) return true
    return false
}

// Hotspot list of one view, each with its name (a button can appear on several views).
function list(view) {
    var out = []
    function add(n, b) {
        out.push({ n: n, x: b.x, y: b.y, w: b.w, h: b.h, round: !!b.round, r: b.r || 0,
                   lx: b.lx, ly: b.ly, la: b.la || "c", narrow: !!b.narrow, lead: !!b.lead,
                   hasLabel: b.lx !== undefined && !b.system })
    }
    for (var n in buttons) {
        var b = buttons[n]
        if (b.view === view) add(n, b)
        var a = b.also || []
        for (var i = 0; i < a.length; i++) if (a[i].view === view) add(n, a[i])
    }
    return out
}
