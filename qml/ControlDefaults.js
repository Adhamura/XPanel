.pragma library

// Control Mode defaults and the "resolved" settings sent to xpanel-daemon's input layer.
// Keys are the same as on the Control Mode page: control.<preset>.<key>.

var buttons = ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "L3", "R3",
               "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left paddle", "Right paddle"]

var desktopMap = {
    "A": "mouse:Left click", "B": "mouse:Right click", "X": "key:Enter", "Y": "key:Esc",
    "LB": "combo:Alt+Tab", "RB": "mouse:Middle click", "LT": "key:Shift", "RT": "mouse:Left click",
    "View": "combo:On-screen keyboard", "Menu": "key:Meta", "D-pad up": "key:Up", "D-pad down": "key:Down",
    "D-pad left": "key:Left", "D-pad right": "key:Right", "L3": "key:Tab", "R3": "mouse:Middle click",
    "Left paddle": "none:Disabled", "Right paddle": "none:Disabled"
}

// Default for one key of a preset; `def` is the page's own default for plain settings.
function defaultFor(preset, k, def) {
    if (k.indexOf("map.") === 0) {
        var parts = k.split(".")              // map.<button>.<primary|secondary>
        var b = parts[1], which = parts[2]
        if (which === "secondary") return "none:Disabled"
        if (preset === "desktop") return desktopMap[b] || "none:Disabled"
        if (b === "Left paddle" || b === "Right paddle") return "none:Disabled"
        return "pad:" + b
    }
    if (preset === "desktop") {
        var d = { "lstick.mode": "mouse", "rstick.mode": "scroll", "lstick.sens": 60, "rstick.sens": 50 }
        if (d[k] !== undefined) return d[k]
    }
    var g = { "lstick.mode": "stick", "rstick.mode": "stick", "lstick.sens": 50, "rstick.sens": 50,
              "lstick.deadzone": 5, "rstick.deadzone": 5, "lstick.outer": 98, "rstick.outer": 98,
              "lstick.curve": "linear", "rstick.curve": "linear", "lstick.anti": 0, "rstick.anti": 0,
              "repeat.on": false, "repeat.ms": 100, "trigger.mirror": true,
              "lt.start": 5, "lt.end": 96, "rt.start": 5, "rt.end": 96,
              "vibration.left": 100, "vibration.right": 100,
              "gyro.mode": "off", "gyro.sensX": 100, "gyro.sensY": 100, "gyro.ratchet": false,
              "gyro.axis": "yaw", "gyro.invertX": false, "gyro.invertY": false }
    return g[k] !== undefined ? g[k] : def
}

// Everything the input layer needs, with defaults filled in. get(key, fallback) reads the config.
function resolve(get) {
    var out = { preset: get("control.preset", "gamepad") }
    var presets = ["gamepad", "desktop"]
    for (var i = 0; i < presets.length; ++i) {
        var p = presets[i]
        var v = function (k) { return get("control." + p + "." + k, defaultFor(p, k, undefined)) }
        var map = {}
        for (var j = 0; j < buttons.length; ++j) {
            var b = buttons[j]
            map[b] = { primary: v("map." + b + ".primary"), secondary: v("map." + b + ".secondary") }
        }
        var stick = function (s) {
            return { mode: v(s + ".mode"), sens: v(s + ".sens"), deadzone: v(s + ".deadzone"),
                     outer: v(s + ".outer"), curve: v(s + ".curve"), anti: v(s + ".anti") }
        }
        out[p] = {
            map: map,
            repeat: { on: v("repeat.on"), ms: v("repeat.ms") },
            lstick: stick("lstick"), rstick: stick("rstick"),
            trigger: { mirror: v("trigger.mirror") },
            lt: { start: v("lt.start"), end: v("lt.end") }, rt: { start: v("rt.start"), end: v("rt.end") },
            vibration: { left: v("vibration.left"), right: v("vibration.right") },
            gyro: { mode: v("gyro.mode"), sensX: v("gyro.sensX"), sensY: v("gyro.sensY"), ratchet: v("gyro.ratchet"),
                    axis: v("gyro.axis"), invertX: v("gyro.invertX"), invertY: v("gyro.invertY") }
        }
    }
    return out
}
