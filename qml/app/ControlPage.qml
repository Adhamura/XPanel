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

    // Button test: the daemon sends every logical button that goes down while this is on
    property bool testing: false
    property string lastPressed: ""
    property bool showAll: false
    property var axes: ({})
    onTestingChanged: { device.setButtonTest(testing); if (!testing) { clearHeld(); axes = ({}) } }
    onVisibleChanged: { testing = visible; if (!visible) clearHeld() }        // the test runs only while this page is shown
    Component.onCompleted: testing = visible
    Component.onDestruction: device.setButtonTest(false)

    // Test mode: the map glows for every button held right now (the daemon sends press and release).
    // The two menu buttons send press and release 1 ms apart, so they stay lit for 300 ms.
    property var pressedMap: ({})
    property var menuUntil: ({})
    readonly property var menuButtons: ["QAM", "Armoury"]
    function heldText() {
        const names = []
        for (const k of ctrlMap.order) if (pressedMap[k] === true) names.push(itemLabel(k))
        return names.join(" + ")
    }
    readonly property bool selIsSystem: ctrlMap.isSystem(ctrlMap.selected)
    readonly property bool noPad: ((device.rev, device.value("inputPad")) || "") === ""
    function clearHeld() { pressedMap = ({}); menuUntil = ({}); menuTimer.stop() }
    Connections {
        target: device
        function onPressed(name) {
            if (!page.testing) return
            page.lastPressed = name
            const m = Object.assign({}, page.pressedMap); m[name] = true
            page.pressedMap = m
            if (page.menuButtons.indexOf(name) >= 0) {
                page.menuUntil[name] = Date.now() + 300
                menuTimer.restart()
            }
            ctrlMap.selected = name
        }
        function onAxes(values) { if (page.testing) page.axes = values }
        function onReleased(name) {
            if (!page.testing || page.menuButtons.indexOf(name) >= 0) return
            const m = Object.assign({}, page.pressedMap); delete m[name]
            page.pressedMap = m
        }
    }
    Timer {
        id: menuTimer; interval: 50; repeat: true
        onTriggered: {
            const now = Date.now(), m = Object.assign({}, page.pressedMap); let any = false
            for (const k of page.menuButtons) {
                if (m[k] !== true) continue
                if (page.menuUntil[k] > now) any = true; else delete m[k]
            }
            page.pressedMap = m
            if (!any) stop()
        }
    }
    // "Open with Ally button": config.button says which Ally menu button opens the Quick Panel
    // (the other one opens Control Center). "none" = tray only.
    function menuButtonLabel(side) {
        const b = (config.rev, config.button)
        if (b === side) return qsTr("Quick Panel")
        if (b === "left_button" || b === "right_button") return qsTr("Control Center")
        return qsTr("Nothing")
    }
    function menuButtonSet(side, v) {
        const other = side === "left_button" ? "right_button" : "left_button"
        config.button = v === "qp" ? side : (v === "cc" ? other : "none")
    }
    readonly property bool selIsMenu: ctrlMap.selected === "QAM" || ctrlMap.selected === "Armoury"
    readonly property bool selIsPaddle: ctrlMap.selected === "Left paddle" || ctrlMap.selected === "Right paddle"
    function functionOptions(cat) {
        const c = categories.find(x => x.id === cat)
        return c ? c.items.map(it => ({ value: cat + ":" + it, label: itemLabel(it) })) : []
    }
    function catOf(v) { return v ? v.split(":")[0] : "pad" }
    // short text of the primary function of every button, for the map
    readonly property var mapLabels: {
        const o = {}
        for (const b of CD.buttons) o[b] = label(val("map." + b + ".primary", ""))
        o["QAM"] = menuButtonLabel("left_button")
        o["Armoury"] = menuButtonLabel("right_button")
        return o
    }

    // ---------- functions a button can have ----------
    readonly property var categories: [
        { id: "pad", label: qsTr("Gamepad"), items: ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "View", "Menu", "L3", "R3",
                                               "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Guide"] },
        { id: "mouse", label: qsTr("Mouse"), items: ["Left click", "Right click", "Middle click", "Wheel up", "Wheel down", "Back", "Forward"] },
        { id: "key", label: qsTr("Keyboard"), items: ["Esc", "Enter", "Space", "Tab", "Backspace", "Shift", "Ctrl", "Alt", "Meta",
                                                "Up", "Down", "Left", "Right", "Page Up", "Page Down", "Home", "End",
                                                "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                                                "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
                                                "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
                                                "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"] },
        { id: "numpad", label: qsTr("Numpad"), items: ["Num 0", "Num 1", "Num 2", "Num 3", "Num 4", "Num 5", "Num 6", "Num 7",
                                                 "Num 8", "Num 9", "Num +", "Num -", "Num *", "Num /", "Num Enter", "Num ."] },
        { id: "combo", label: qsTr("Combination"), items: ["Alt+Tab", "Alt+F4", "Ctrl+C", "Ctrl+V", "Ctrl+Z", "Ctrl+S",
                                                     "Meta+D (desktop)", "Screenshot", "On-screen keyboard", "Volume up", "Volume down", "Mute"] },
        { id: "none", label: qsTr("Disabled"), items: ["Disabled"] }
    ]
    readonly property var buttons: CD.buttons

    // Preset defaults (shared with what is sent to the input layer)
    function defaults(k, def) { return CD.defaultFor(editing, k, def) }
    function label(v) { return v ? itemLabel(v.split(":")[1]) : "" }

    // Display names of the functions and buttons. The ids ("Left click", "D-pad up", ...) are what is
    // stored in the config and sent to xpanel-daemon: never translate those, only what is shown.
    readonly property var itemNames: ({
        "D-pad up": qsTr("D-pad up"), "D-pad down": qsTr("D-pad down"),
        "D-pad left": qsTr("D-pad left"), "D-pad right": qsTr("D-pad right"),
        "Left paddle": qsTr("Left paddle"), "Right paddle": qsTr("Right paddle"), "Power": qsTr("Power"),
        "Left click": qsTr("Left click"), "Right click": qsTr("Right click"), "Middle click": qsTr("Middle click"),
        "Wheel up": qsTr("Wheel up"), "Wheel down": qsTr("Wheel down"), "Back": qsTr("Back"), "Forward": qsTr("Forward"),
        "Enter": qsTr("Enter"), "Space": qsTr("Space"), "Backspace": qsTr("Backspace"),
        "Shift": qsTr("Shift"), "Ctrl": qsTr("Ctrl"),
        "Up": qsTr("Up"), "Down": qsTr("Down"), "Left": qsTr("Left"), "Right": qsTr("Right"),
        "Page Up": qsTr("Page Up"), "Page Down": qsTr("Page Down"), "Home": qsTr("Home"), "End": qsTr("End"),
        "Num Enter": qsTr("Num Enter"),
        "Meta+D (desktop)": qsTr("Meta+D (desktop)"), "Screenshot": qsTr("Screenshot"),
        "On-screen keyboard": qsTr("On-screen keyboard"), "Volume up": qsTr("Volume up"),
        "QAM": qsTr("QAM"), "Armoury": qsTr("Armoury"),
        "Volume down": qsTr("Volume down"), "Mute": qsTr("Mute"), "Disabled": qsTr("Disabled")
    })
    function itemLabel(id) { return itemNames[id] || id }

    // Gyro: state from xpanel-daemon ("no device" | "ready" | "active" | "busy")
    readonly property bool gyroOn: val("gyro.mode", "off") !== "off"
    readonly property string gyroState: (device.rev, device.value("gyro") || "")
    readonly property string calibration: {
        const r = (device.rev, device.value("gyroCalibration") || "")
        return ({ "running": qsTr("measuring…"), "done": qsTr("done"), "moved": qsTr("the Ally moved, try again"),
                  "busy": qsTr("the motion sensor is busy"), "no data": qsTr("no data from the motion sensor"),
                  "no device": qsTr("no motion sensor") })[r] || r
    }
    readonly property string gyroLabel: ({ "active": qsTr("Active"), "ready": qsTr("Ready"), "busy": qsTr("Busy"),
                                           "no device": qsTr("Not found") })[gyroState] || qsTr("Not available")
    readonly property string gyroNote: ({
        "active": qsTr("Reading the motion sensor now."),
        "ready": qsTr("Starts when Gyro function is on and a game has the controller."),
        "busy": qsTr("Another program (Handheld Daemon, InputPlumber) is using the motion sensor."),
        "no device": desktop.windows ? qsTr("No motion sensor found.") : qsTr("No motion sensor found. It needs the kernel's bmi323 IIO driver.")
    })[gyroState] || (desktop.windows ? qsTr("Needs xpanel-daemon.") : qsTr("Needs xpanel-daemon (Linux)."))

    CcPage {
        anchors.fill: parent
        Row {
            spacing: 16
            Text { text: qsTr("Control Mode"); color: T.text; font.pixelSize: 32; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            CcChoice {
                options: [{ value: "gamepad", label: qsTr("Gamepad Mode") }, { value: "desktop", label: qsTr("Desktop Mode") }]
                current: page.editing
                onPicked: (v) => config.set("control.preset", v)
            }
            CcButton { text: qsTr("Reset this mode"); onClicked: {
                    const keys = Object.keys(config.group("control." + page.editing))
                    for (const k of keys) config.set("control." + page.editing + "." + k, undefined) } }
        }
        Rectangle {
            width: parent.width; height: noteText.implicitHeight + 24; color: "#1a1408"; border.color: "#6b5212"; radius: 3
            Text {
                id: noteText
                x: 16; y: 12; width: parent.width - 32; wrapMode: Text.WordWrap
                color: "#e8d9a8"; font.pixelSize: 15
                text: (device.rev, device.value("inputLayer")) === "active"
                      ? ((String(device.value("inputMode") || "").indexOf("virtual pad active") >= 0)
                         ? qsTr("Virtual pad active: games may see two controllers. Plain button remaps are programmed into the controller itself and need no virtual pad.")
                         : qsTr("XPanel's input layer applies these settings live to the Ally controller (button remaps are programmed into the controller; a virtual Xbox 360 pad is used only for what the controller cannot do)."))
                      : qsTr("XPanel's input layer is not active (%1). The settings are saved and apply when it runs.")
                            .arg(device.value("inputLayer") || qsTr("xpanel-daemon not running"))
            }
        }

        // ----- button test -----
        CcSection { title: qsTr("Button test"); note: qsTr("Press any button on the Ally, including the back paddles.") }
        Flow {
            id: mapFlow
            width: parent.width; spacing: 20
            readonly property bool sideBySide: width >= 900
            ControllerMap {
                id: ctrlMap
                width: mapFlow.sideBySide ? mapFlow.width - 360 : mapFlow.width
                height: implicitHeight
                pressed: page.pressedMap
                testing: page.testing
                axes: page.axes
                labels: page.mapLabels
                onPicked: (name) => (page.selIsSystem ? ctrlMap : (page.selIsMenu ? menuDd : catP)).forceActiveFocus()
            }
            Column {
                id: side
                width: mapFlow.sideBySide ? 340 : mapFlow.width
                spacing: 10
                Rectangle {   // the one Test mode control
                    width: parent.width; height: 52; radius: 3
                    color: page.testing ? T.accent : "#1c1c22"
                    border.color: tmArea.containsMouse || activeFocus ? "#ffffff" : (page.testing ? T.accent : "#3a3a44"); border.width: 2
                    activeFocusOnTab: true
                    function toggle() { page.lastPressed = ""; page.testing = !page.testing }
                    Text { anchors.centerIn: parent; font.pixelSize: 20; font.bold: true
                           text: page.testing ? qsTr("Test mode: on") : qsTr("Test mode: off")
                           color: page.testing ? "#06222b" : T.text }
                    MouseArea { id: tmArea; anchors.fill: parent; hoverEnabled: true; onClicked: { parent.forceActiveFocus(); parent.toggle() } }
                    Keys.onReturnPressed: toggle()
                    Keys.onSpacePressed: toggle()
                }
                Text { visible: page.testing; width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 15
                       text: qsTr("Held: %1").arg(page.heldText() || qsTr("none")) }
                Text { visible: !page.testing; width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 15
                       text: qsTr("Turn on to see the buttons, sticks and triggers you use.") }
                Text { visible: page.testing && page.noPad; width: parent.width; wrapMode: Text.WordWrap; color: T.warn; font.pixelSize: 15
                       text: qsTr("No gamepad input from Windows. Check that the Ally's Control Mode is Gamepad.") }
                CcChoice {
                    options: [{ value: "front", label: qsTr("Front") }, { value: "top", label: qsTr("Top") },
                              { value: "back", label: qsTr("Back") }]
                    current: ctrlMap.view; onPicked: (v) => ctrlMap.view = v
                }
                Text { text: ctrlMap.selected !== "" ? (ctrlMap.selected === "QAM" ? qsTr("Command Center button")
                                                      : ctrlMap.selected === "Armoury" ? qsTr("Armoury Crate button") : page.itemLabel(ctrlMap.selected))
                                                    : qsTr("Pick a button")
                       color: T.text; font.pixelSize: 28; font.bold: true; width: parent.width; elide: Text.ElideRight }
                Text { visible: ctrlMap.selected === ""; width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 15
                       text: qsTr("Click a button on the drawing, or move with the D-pad and press A.") }

                // volume rocker and power button: not mappable
                Text {
                    visible: page.selIsSystem; width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 15
                    text: qsTr("System button: Windows handles it (volume / power). It cannot be remapped.")
                }

                // QAM / Armoury: what the Ally menu button opens
                Column {
                    visible: page.selIsMenu; width: parent.width; spacing: 10
                    Text { text: qsTr("Open with Ally button"); color: T.dim; font.pixelSize: 15 }
                    CcDropdown {
                        id: menuDd; width: parent.width
                        readonly property string side: ctrlMap.selected === "Armoury" ? "right_button" : "left_button"
                        options: [{ value: "qp", label: qsTr("Quick Panel") }, { value: "cc", label: qsTr("Control Center") },
                                  { value: "none", label: qsTr("Nothing") }]
                        current: { const b = (config.rev, config.button)
                                   return b === side ? "qp" : (b === "none" ? "none" : "cc") }
                        onPicked: (v) => page.menuButtonSet(side, v)
                    }
                    Text { width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 14
                           text: qsTr("This button opens the chosen panel. The other Ally menu button opens the other one. Nothing: tray only.") }
                }

                // normal buttons: primary and second function
                Column {
                    id: mapCol
                    visible: ctrlMap.selected !== "" && !page.selIsMenu && !page.selIsSystem; width: parent.width; spacing: 10
                    readonly property string pk: "map." + ctrlMap.selected + ".primary"
                    readonly property string sk: "map." + ctrlMap.selected + ".secondary"
                    readonly property string pv: page.val(pk, "")
                    readonly property string sv: page.val(sk, "")
                    Text { text: qsTr("Primary button"); color: T.dim; font.pixelSize: 15 }
                    CcDropdown {
                        id: catP; width: parent.width
                        options: page.categories.map(c => ({ value: c.id, label: c.label }))
                        current: page.catOf(mapCol.pv)
                        onPicked: (v) => page.put(mapCol.pk, v + ":" + page.categories.find(c => c.id === v).items[0])
                    }
                    CcDropdown {
                        width: parent.width
                        options: page.functionOptions(page.catOf(mapCol.pv)); current: mapCol.pv
                        onPicked: (v) => page.put(mapCol.pk, v)
                    }
                    Text { visible: !page.selIsPaddle; text: qsTr("Second function (while a rear paddle is held)")
                           color: T.dim; font.pixelSize: 15; width: parent.width; wrapMode: Text.WordWrap }
                    CcDropdown {
                        visible: !page.selIsPaddle; width: parent.width
                        options: page.categories.map(c => ({ value: c.id, label: c.label }))
                        current: page.catOf(mapCol.sv)
                        onPicked: (v) => page.put(mapCol.sk, v + ":" + page.categories.find(c => c.id === v).items[0])
                    }
                    CcDropdown {
                        visible: !page.selIsPaddle; width: parent.width
                        options: page.functionOptions(page.catOf(mapCol.sv)); current: mapCol.sv
                        onPicked: (v) => page.put(mapCol.sk, v)
                    }
                    CcButton { text: qsTr("Reset to default"); onClicked: { page.put(mapCol.pk, undefined); page.put(mapCol.sk, undefined) } }
                }
            }
        }
        CcRow { title: qsTr("Swap back paddles"); description: qsTr("Use this if the left and right back paddles are the wrong way round.")
            CcSwitch { checked: (page.rev, config.get("control.swapPaddles", false))
                       onToggled: (v) => config.set("control.swapPaddles", v) } }

        // ----- key mapping -----
        CcSection { title: qsTr("All buttons"); note: qsTr("Primary button: normal press. Secondary function: while a rear paddle is held.") }
        CcButton { text: page.showAll ? qsTr("Hide the list") : qsTr("Show all buttons"); onClicked: page.showAll = !page.showAll }
        Column {
            visible: page.showAll; width: parent.width; spacing: 10
        Repeater {
            model: page.buttons
            CcRow {
                title: page.itemLabel(modelData)
                Row {
                    spacing: 10
                    CcButton { width: 230; text: qsTr("Primary: %1").arg(page.label(page.val("map." + modelData + ".primary", "")))
                               onClicked: picker.show(modelData, "primary") }
                    CcButton { width: 230; text: qsTr("Secondary: %1").arg(page.label(page.val("map." + modelData + ".secondary", "")))
                               onClicked: picker.show(modelData, "secondary") }
                }
            }
        }
        }

        // ----- button repeat -----
        CcSection { title: qsTr("Button repeat") }
        CcRow { title: qsTr("Hold to repeat"); description: qsTr("A held button sends its function again and again.")
            CcSwitch { checked: page.val("repeat.on", false); onToggled: (v) => page.put("repeat.on", v) } }
        CcRow { title: qsTr("Repeat interval")
            CcSlider { width: 420; from: 30; to: 500; stepSize: 10; unit: " ms"; value: page.val("repeat.ms", 100)
                       enabledSlider: page.val("repeat.on", false); onMoved: (v) => page.put("repeat.ms", v) } }

        // ----- sticks -----
        Repeater {
            model: [{ id: "lstick", name: qsTr("Left stick") }, { id: "rstick", name: qsTr("Right stick") }]
            Column {
                width: parent.width; spacing: 10
                CcSection { title: modelData.name }
                CcRow { title: qsTr("Stick function"); visible: page.editing === "desktop"
                    CcChoice { options: [{ value: "stick", label: qsTr("Stick") }, { value: "mouse", label: qsTr("Mouse") }, { value: "scroll", label: qsTr("Scroll") }]
                               current: page.val(modelData.id + ".mode", "stick"); onPicked: (v) => page.put(modelData.id + ".mode", v) } }
                CcRow { title: qsTr("Sensitivity"); visible: page.editing === "desktop"
                    CcSlider { width: 420; from: 5; to: 100; unit: " %"; value: page.val(modelData.id + ".sens", 50)
                               onMoved: (v) => page.put(modelData.id + ".sens", v) } }
                CcRow { title: qsTr("Dead zone (inner)"); description: qsTr("Movement below this is ignored.")
                    CcSlider { width: 420; from: 0; to: 40; unit: " %"; value: page.val(modelData.id + ".deadzone", 5)
                               onMoved: (v) => page.put(modelData.id + ".deadzone", v) } }
                CcRow { title: qsTr("Outer threshold"); description: qsTr("From here on the stick counts as fully pushed.")
                    CcSlider { width: 420; from: 60; to: 100; unit: " %"; value: page.val(modelData.id + ".outer", 98)
                               onMoved: (v) => page.put(modelData.id + ".outer", v) } }
                CcRow { title: qsTr("Response curve")
                    CcChoice { options: [{ value: "linear", label: qsTr("Linear") }, { value: "precise", label: qsTr("Precise") },
                                         { value: "aggressive", label: qsTr("Aggressive") }, { value: "instant", label: qsTr("Instant") }]
                               current: page.val(modelData.id + ".curve", "linear"); onPicked: (v) => page.put(modelData.id + ".curve", v) } }
                CcRow { title: qsTr("Anti-deadzone"); description: qsTr("Adds a small push so games with their own dead zone react sooner.")
                    CcSlider { width: 420; from: 0; to: 30; unit: " %"; value: page.val(modelData.id + ".anti", 0)
                               onMoved: (v) => page.put(modelData.id + ".anti", v) } }
            }
        }

        // ----- triggers -----
        CcSection { title: qsTr("Trigger") }
        CcRow { title: qsTr("Mirror triggers"); description: qsTr("Use the left trigger settings for both triggers.")
            CcSwitch { checked: page.val("trigger.mirror", true); onToggled: (v) => page.put("trigger.mirror", v) } }
        Repeater {
            model: [{ id: "lt", name: qsTr("Left trigger") }, { id: "rt", name: qsTr("Right trigger") }]
            Column {
                width: parent.width; spacing: 10
                readonly property bool locked: modelData.id === "rt" && page.val("trigger.mirror", true)
                CcRow { title: qsTr("%1 — start").arg(modelData.name); description: qsTr("Pull needed before the trigger reacts.")
                    CcSlider { width: 420; from: 0; to: 50; unit: " %"; enabledSlider: !parent.parent.parent.locked
                               value: page.val(modelData.id + ".start", 5); onMoved: (v) => page.put(modelData.id + ".start", v) } }
                CcRow { title: qsTr("%1 — full at").arg(modelData.name); description: qsTr("Pull at which the trigger counts as fully pressed.")
                    CcSlider { width: 420; from: 50; to: 100; unit: " %"; enabledSlider: !parent.parent.parent.locked
                               value: page.val(modelData.id + ".end", 96); onMoved: (v) => page.put(modelData.id + ".end", v) } }
            }
        }

        // ----- vibration -----
        CcSection { title: qsTr("Vibration") }
        CcRow { title: qsTr("Vibration"); description: qsTr("Rumble from games, for all presets. Also on the Quick Panel's Vibration tile.")
            CcSwitch { id: vibSwitch; checked: (page.rev, config.get("control.vibration", true))
                       onToggled: (v) => config.set("control.vibration", v) } }
        CcRow { title: qsTr("Left side")
            CcSlider { width: 420; from: 0; to: 100; stepSize: 5; unit: " %"; enabledSlider: vibSwitch.checked
                       value: page.val("vibration.left", 100); onMoved: (v) => page.put("vibration.left", v) } }
        CcRow { title: qsTr("Right side")
            CcSlider { width: 420; from: 0; to: 100; unit: " %"; enabledSlider: vibSwitch.checked
                       value: page.val("vibration.right", 100); onMoved: (v) => page.put("vibration.right", v) } }

        // ----- gyro -----
        CcSection { title: qsTr("Gyro"); note: qsTr("The Ally's motion sensor as mouse or right stick. It runs only while a game has the controller (not while XPanel is open).") }
        CcRow { title: qsTr("Motion sensor"); description: page.gyroNote
            Text { text: page.gyroLabel; color: page.gyroState === "active" || page.gyroState === "ready" ? "#5fd38d" : T.warn
                   font.pixelSize: 18 } }
        CcRow { title: qsTr("Gyro function")
            CcChoice { options: [{ value: "off", label: qsTr("Off") }, { value: "mouse", label: qsTr("Mouse") }, { value: "rstick", label: qsTr("Right stick") }]
                       current: page.val("gyro.mode", "off"); onPicked: (v) => page.put("gyro.mode", v) } }
        CcRow { title: qsTr("Turning axis"); description: qsTr("Yaw: turn the Ally left / right. Roll: tilt it like a steering wheel.")
            CcChoice { options: [{ value: "yaw", label: qsTr("Yaw") }, { value: "roll", label: qsTr("Roll") }]
                       enabledChoice: page.gyroOn
                       current: page.val("gyro.axis", "yaw"); onPicked: (v) => page.put("gyro.axis", v) } }
        CcRow { title: qsTr("Horizontal sensitivity")
            CcSlider { width: 420; from: 10; to: 300; stepSize: 5; unit: " %"; value: page.val("gyro.sensX", 100)
                       enabledSlider: page.gyroOn; onMoved: (v) => page.put("gyro.sensX", v) } }
        CcRow { title: qsTr("Vertical sensitivity")
            CcSlider { width: 420; from: 10; to: 300; stepSize: 5; unit: " %"; value: page.val("gyro.sensY", 100)
                       enabledSlider: page.gyroOn; onMoved: (v) => page.put("gyro.sensY", v) } }
        CcRow { title: qsTr("Invert horizontal"); description: qsTr("Use this if the aim moves the wrong way left / right.")
            CcSwitch { checked: page.val("gyro.invertX", false); enabledSwitch: page.gyroOn
                       onToggled: (v) => page.put("gyro.invertX", v) } }
        CcRow { title: qsTr("Invert vertical"); description: qsTr("Use this if the aim moves the wrong way up / down.")
            CcSwitch { checked: page.val("gyro.invertY", false); enabledSwitch: page.gyroOn
                       onToggled: (v) => page.put("gyro.invertY", v) } }
        CcRow { title: qsTr("Only while a button is held"); description: qsTr("Gyro works only while LT is held (aiming).")
            CcSwitch { checked: page.val("gyro.ratchet", false); enabledSwitch: page.gyroOn
                       onToggled: (v) => page.put("gyro.ratchet", v) } }
        CcRow { title: qsTr("Calibrate"); description: qsTr("If the aim drifts: put the Ally on a flat table, do not touch it, and press Calibrate.")
                                   + (page.calibration ? "  " + qsTr("Last result: %1.").arg(page.calibration) : "")
            CcButton { text: qsTr("Calibrate"); enabledButton: page.gyroState === "ready" || page.gyroState === "active"
                       onClicked: device.calibrateGyro() } }

        // ----- external controller -----
        CcSection { title: qsTr("External Controller"); note: qsTr("Controllers connected by USB or Bluetooth.") }
        Repeater {
            id: ext
            model: deviceInfo.controllers()
            CcRow { title: modelData.name; description: modelData.bus + "  ·  " + modelData.id
                CcButton { text: qsTr("Refresh"); onClicked: ext.model = deviceInfo.controllers() } }
        }
        CcRow { visible: ext.count === 0; title: qsTr("No external controller connected")
            CcButton { text: qsTr("Refresh"); onClicked: ext.model = deviceInfo.controllers() } }
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
            Text { text: (picker.which === "primary" ? qsTr("%1 — primary button") : qsTr("%1 — secondary function")).arg(page.itemLabel(picker.button))
                   color: T.text; font.pixelSize: 30; font.bold: true }
            CcChoice { id: catChoice; options: page.categories.map(c => ({ value: c.id, label: c.label }))
                       current: picker.cat; onPicked: (v) => picker.cat = v }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: (page.categories.find(c => c.id === picker.cat) || { items: [] }).items
                    CcButton {
                        width: 180; text: page.itemLabel(modelData)
                        primary: page.val("map." + picker.button + "." + picker.which, "") === picker.cat + ":" + modelData
                        onClicked: picker.pick(picker.cat + ":" + modelData)
                    }
                }
            }
            CcButton { text: qsTr("Cancel"); onClicked: { picker.visible = false; app.popBack() } }
        }
    }
}
