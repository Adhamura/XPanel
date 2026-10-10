import QtQuick
import "../Theme.js" as T
import "../ControlDefaults.js" as CD

// Control Mode: Gamepad / Desktop presets, key mapping (primary + secondary while a rear
// paddle is held), button repeat, sticks, triggers, vibration, gyro, external controllers.
// Saved under "control.<preset>.*"; XPanel's own input layer applies them.
Item {
    id: page
    property int rev: config.rev
    property string editing: config.get("control.preset", "gamepad")

    function key(k) { return "control." + editing + "." + k }
    function val(k, def) { return (rev, config.get(key(k), defaults(k, def))) }
    function put(k, v) { config.set(key(k), v) }

    // ---------- functions a button can have ----------
    readonly property var categories: [
        { id: "pad", label: "Gamepad", items: ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "L3", "R3",
                                               "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Guide"] },
        { id: "mouse", label: "Mouse", items: ["Left click", "Right click", "Middle click", "Wheel up", "Wheel down", "Back", "Forward"] },
        { id: "key", label: "Keyboard", items: ["Esc", "Enter", "Space", "Tab", "Backspace", "Shift", "Ctrl", "Alt", "Meta",
                                                "Up", "Down", "Left", "Right", "Page Up", "Page Down", "Home", "End",
                                                "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                                                "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
                                                "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
                                                "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"] },
        { id: "numpad", label: "Numpad", items: ["Num 0", "Num 1", "Num 2", "Num 3", "Num 4", "Num 5", "Num 6", "Num 7",
                                                 "Num 8", "Num 9", "Num +", "Num -", "Num *", "Num /", "Num Enter", "Num ."] },
        { id: "combo", label: "Combination", items: ["Alt+Tab", "Alt+F4", "Ctrl+C", "Ctrl+V", "Ctrl+Z", "Ctrl+S",
                                                     "Meta+D (desktop)", "Screenshot", "On-screen keyboard", "Volume up", "Volume down", "Mute"] },
        { id: "none", label: "Disabled", items: ["Disabled"] }
    ]
    readonly property var buttons: CD.buttons

    // Preset defaults (shared with what is sent to the input layer)
    function defaults(k, def) { return CD.defaultFor(editing, k, def) }
    function label(v) { return v ? v.split(":")[1] : "" }

    CcPage {
        anchors.fill: parent
        Row {
            spacing: 16
            Text { text: "Control Mode"; color: T.text; font.pixelSize: 32; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            CcChoice {
                options: [{ value: "gamepad", label: "Gamepad Mode" }, { value: "desktop", label: "Desktop Mode" }]
                current: page.editing
                onPicked: (v) => config.set("control.preset", v)
            }
            CcButton { text: "Reset this mode"; onClicked: {
                    const keys = Object.keys(config.group("control." + page.editing))
                    for (const k of keys) config.set("control." + page.editing + "." + k, undefined) } }
        }
        Rectangle {
            width: parent.width; height: noteText.implicitHeight + 24; color: "#1a1408"; border.color: "#6b5212"; radius: 3
            Text {
                id: noteText
                x: 16; y: 12; width: parent.width - 32; wrapMode: Text.WordWrap
                color: "#e8d9a8"; font.pixelSize: 15
                text: device.value("inputLayer") === "active"
                      ? "XPanel's input layer applies these settings live to the Ally controller (virtual Xbox 360 pad + keyboard / mouse)."
                      : "XPanel's input layer is not active (" + (device.value("inputLayer") || "xpanel-daemon not running") +
                        "). The settings are saved and apply when it runs."
            }
        }

        // ----- key mapping -----
        CcSection { title: "Key Mapping"; note: "Primary button: normal press. Secondary function: while a rear paddle is held." }
        Repeater {
            model: page.buttons
            CcRow {
                title: modelData
                Row {
                    spacing: 10
                    CcButton { width: 230; text: "Primary: " + page.label(page.val("map." + modelData + ".primary", ""))
                               onClicked: picker.show(modelData, "primary") }
                    CcButton { width: 230; text: "Secondary: " + page.label(page.val("map." + modelData + ".secondary", ""))
                               onClicked: picker.show(modelData, "secondary") }
                }
            }
        }

        // ----- button repeat -----
        CcSection { title: "Button repeat" }
        CcRow { title: "Hold to repeat"; description: "A held button sends its function again and again."
            CcSwitch { checked: page.val("repeat.on", false); onToggled: (v) => page.put("repeat.on", v) } }
        CcRow { title: "Repeat interval"
            CcSlider { width: 420; from: 30; to: 500; stepSize: 10; unit: " ms"; value: page.val("repeat.ms", 100)
                       enabledSlider: page.val("repeat.on", false); onMoved: (v) => page.put("repeat.ms", v) } }

        // ----- sticks -----
        Repeater {
            model: [{ id: "lstick", name: "Left stick" }, { id: "rstick", name: "Right stick" }]
            Column {
                width: parent.width; spacing: 10
                CcSection { title: modelData.name }
                CcRow { title: "Stick function"; visible: page.editing === "desktop"
                    CcChoice { options: [{ value: "stick", label: "Stick" }, { value: "mouse", label: "Mouse" }, { value: "scroll", label: "Scroll" }]
                               current: page.val(modelData.id + ".mode", "stick"); onPicked: (v) => page.put(modelData.id + ".mode", v) } }
                CcRow { title: "Sensitivity"; visible: page.editing === "desktop"
                    CcSlider { width: 420; from: 5; to: 100; unit: " %"; value: page.val(modelData.id + ".sens", 50)
                               onMoved: (v) => page.put(modelData.id + ".sens", v) } }
                CcRow { title: "Dead zone (inner)"; description: "Movement below this is ignored."
                    CcSlider { width: 420; from: 0; to: 40; unit: " %"; value: page.val(modelData.id + ".deadzone", 5)
                               onMoved: (v) => page.put(modelData.id + ".deadzone", v) } }
                CcRow { title: "Outer threshold"; description: "From here on the stick counts as fully pushed."
                    CcSlider { width: 420; from: 60; to: 100; unit: " %"; value: page.val(modelData.id + ".outer", 98)
                               onMoved: (v) => page.put(modelData.id + ".outer", v) } }
                CcRow { title: "Response curve"
                    CcChoice { options: [{ value: "linear", label: "Linear" }, { value: "precise", label: "Precise" },
                                         { value: "aggressive", label: "Aggressive" }, { value: "instant", label: "Instant" }]
                               current: page.val(modelData.id + ".curve", "linear"); onPicked: (v) => page.put(modelData.id + ".curve", v) } }
                CcRow { title: "Anti-deadzone"; description: "Adds a small push so games with their own dead zone react sooner."
                    CcSlider { width: 420; from: 0; to: 30; unit: " %"; value: page.val(modelData.id + ".anti", 0)
                               onMoved: (v) => page.put(modelData.id + ".anti", v) } }
            }
        }

        // ----- triggers -----
        CcSection { title: "Trigger" }
        CcRow { title: "Mirror triggers"; description: "Use the left trigger settings for both triggers."
            CcSwitch { checked: page.val("trigger.mirror", true); onToggled: (v) => page.put("trigger.mirror", v) } }
        Repeater {
            model: [{ id: "lt", name: "Left trigger" }, { id: "rt", name: "Right trigger" }]
            Column {
                width: parent.width; spacing: 10
                readonly property bool locked: modelData.id === "rt" && page.val("trigger.mirror", true)
                CcRow { title: modelData.name + " — start"; description: "Pull needed before the trigger reacts."
                    CcSlider { width: 420; from: 0; to: 50; unit: " %"; enabledSlider: !parent.parent.parent.locked
                               value: page.val(modelData.id + ".start", 5); onMoved: (v) => page.put(modelData.id + ".start", v) } }
                CcRow { title: modelData.name + " — full at"; description: "Pull at which the trigger counts as fully pressed."
                    CcSlider { width: 420; from: 50; to: 100; unit: " %"; enabledSlider: !parent.parent.parent.locked
                               value: page.val(modelData.id + ".end", 96); onMoved: (v) => page.put(modelData.id + ".end", v) } }
            }
        }

        // ----- vibration -----
        CcSection { title: "Vibration" }
        CcRow { title: "Left side"
            CcSlider { width: 420; from: 0; to: 100; stepSize: 5; unit: " %"; value: page.val("vibration.left", 100)
                       onMoved: (v) => page.put("vibration.left", v) } }
        CcRow { title: "Right side"
            CcSlider { width: 420; from: 0; to: 100; unit: " %"; value: page.val("vibration.right", 100)
                       onMoved: (v) => page.put("vibration.right", v) } }

        // ----- gyro -----
        CcSection { title: "Gyro" }
        CcRow { title: "Note"; description: "Gyro is saved but not applied yet: reading the Ally's motion sensor comes in a later step."; Item { width: 1; height: 1 } }
        CcRow { title: "Gyro function"
            CcChoice { options: [{ value: "off", label: "Off" }, { value: "mouse", label: "Mouse" }, { value: "rstick", label: "Right stick" }]
                       current: page.val("gyro.mode", "off"); onPicked: (v) => page.put("gyro.mode", v) } }
        CcRow { title: "Horizontal sensitivity"
            CcSlider { width: 420; from: 10; to: 300; stepSize: 5; unit: " %"; value: page.val("gyro.sensX", 100)
                       enabledSlider: page.val("gyro.mode", "off") !== "off"; onMoved: (v) => page.put("gyro.sensX", v) } }
        CcRow { title: "Vertical sensitivity"
            CcSlider { width: 420; from: 10; to: 300; stepSize: 5; unit: " %"; value: page.val("gyro.sensY", 100)
                       enabledSlider: page.val("gyro.mode", "off") !== "off"; onMoved: (v) => page.put("gyro.sensY", v) } }
        CcRow { title: "Only while a button is held"; description: "Gyro works only while LT is held (aiming)."
            CcSwitch { checked: page.val("gyro.ratchet", false); enabledSwitch: page.val("gyro.mode", "off") !== "off"
                       onToggled: (v) => page.put("gyro.ratchet", v) } }

        // ----- external controller -----
        CcSection { title: "External Controller"; note: "Controllers connected by USB or Bluetooth." }
        Repeater {
            id: ext
            model: deviceInfo.controllers()
            CcRow { title: modelData.name; description: modelData.bus + "  ·  " + modelData.id
                CcButton { text: "Refresh"; onClicked: ext.model = deviceInfo.controllers() } }
        }
        CcRow { visible: ext.count === 0; title: "No external controller connected"
            CcButton { text: "Refresh"; onClicked: ext.model = deviceInfo.controllers() } }
    }

    // ---------- function picker ----------
    Rectangle {
        id: picker
        property string button
        property string which
        property string cat: "pad"
        anchors.fill: parent; color: "#f2050507"; visible: false
        function show(b, w) {
            button = b; which = w
            const cur = page.val("map." + b + "." + w, "")
            cat = cur ? cur.split(":")[0] : "pad"
            visible = true
            app.pushBack(() => picker.visible = false)
            Qt.callLater(() => catChoice.forceActiveFocus())
        }
        function pick(v) { page.put("map." + button + "." + which, v); visible = false; app.popBack() }
        MouseArea { anchors.fill: parent }
        CcPage {
            anchors.fill: parent
            Text { text: picker.button + " — " + (picker.which === "primary" ? "primary button" : "secondary function")
                   color: T.text; font.pixelSize: 30; font.bold: true }
            CcChoice { id: catChoice; options: page.categories.map(c => ({ value: c.id, label: c.label }))
                       current: picker.cat; onPicked: (v) => picker.cat = v }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: (page.categories.find(c => c.id === picker.cat) || { items: [] }).items
                    CcButton {
                        width: 180; text: modelData
                        primary: page.val("map." + picker.button + "." + picker.which, "") === picker.cat + ":" + modelData
                        onClicked: picker.pick(picker.cat + ":" + modelData)
                    }
                }
            }
            CcButton { text: "Cancel"; onClicked: { picker.visible = false; app.popBack() } }
        }
    }
}
