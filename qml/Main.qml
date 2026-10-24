import QtQuick
import "Theme.js" as T
import "ControlDefaults.js" as CD

// Full-screen transparent overlay. The Quick Panel panel slides in from the left;
// the rest of the screen is dimmed and closes the panel on tap.
Item {
    id: root
    focus: true

    // Layout is designed for a 1080 px tall screen and scales from there.
    readonly property real s: height > 0 ? height / 1080 : 1
    readonly property real panelW: Math.min(width, 960 * s)

    // where the focus is: grid | side | edit
    property string area: "grid"
    property int gridIndex: 0
    property int sideIndex: 0           // 0 brightness, 1 volume, 2 power
    property int brightIndex: 0         // focused brightness bar (one per screen when extended)
    readonly property var screens: sys.displays
    property bool editing: false

    // brightness of the focused bar, in steps of d (falls back to the primary display)
    function adjustBrightness(d) {
        const n = screens.length
        if (n === 0) { sys.setBrightness(sys.brightness + d); return }
        const e = screens[Math.min(brightIndex, n - 1)]
        sys.setDisplayBrightness(e.id, e.brightness + d)
    }

    // ---------------- tile catalog ----------------
    // (tiles the desktop cannot do, e.g. Refresh Rate outside KDE, are left out: see Desktop.h)
    readonly property var catalog: allTiles.filter(t => desktop.unavailableTiles.indexOf(t.id) < 0)
    readonly property var allTiles: [
        { id: "opmode",     title: qsTr("Operating Mode"),    icon: "opmode" },
        { id: "ctrlmode",   title: qsTr("Control Mode"),      icon: "ctrlmode" },
        { id: "fan",        title: qsTr("Fan Curve"),         icon: "fan" },
        { id: "keyboard",   title: qsTr("Keyboard"),          icon: "keyboard" },
        { id: "monitor",    title: qsTr("Real-time Monitor"), icon: "monitor" },
        { id: "charge",     title: qsTr("Charge Limit"),      icon: "charge" },
        { id: "desktop",    title: qsTr("Show Desktop"),      icon: "desktop" },
        { id: "rgb",        title: qsTr("Lighting"),          icon: "rgb" },
        { id: "refresh",    title: qsTr("Refresh Rate"),      icon: "refresh" },
        { id: "display",    title: qsTr("Display Mode"),      icon: "display" },
        { id: "tdp",        title: qsTr("Manual TDP"),        icon: "sliders" },
        { id: "cpuclock",   title: qsTr("CPU Clock"),         icon: "cpu" },
        { id: "gpuclock",   title: qsTr("GPU Clock"),         icon: "gpu" },
        { id: "wifi",       title: qsTr("Wi-Fi"),             icon: "wifi" },
        { id: "bluetooth",  title: qsTr("Bluetooth"),         icon: "bluetooth" },
        { id: "screenshot", title: qsTr("Screenshot"),        icon: "screenshot" }
    ]
    function catalogEntry(id) {
        for (let i = 0; i < catalog.length; ++i) if (catalog[i].id === id) return catalog[i]
        return null
    }
    readonly property var tiles: config.tiles.filter(id => catalogEntry(id) !== null)

    // xpanel-daemon values and their display names
    readonly property var opModes: [
        { value: "silent",      label: qsTr("Silent") },
        { value: "performance", label: qsTr("Performance") },
        { value: "turbo",       label: qsTr("Turbo") },
        { value: "manual",      label: qsTr("Manual") }
    ]
    // Control Mode presets (as in Control Center): applied by xpanel-daemon's input layer
    readonly property var ctrlModes: [
        { value: "gamepad", label: qsTr("Gamepad") },
        { value: "desktop", label: qsTr("Desktop") }
    ]
    readonly property var chargeModes: [
        { value: 100, label: qsTr("Off") }, { value: 60, label: "60%" },
        { value: 80, label: "80%" }, { value: 90, label: "90%" }
    ]
    readonly property var rgbModes: [
        { value: "off", label: qsTr("Off") }, { value: "static", label: qsTr("Static") },
        { value: "breathing", label: qsTr("Breathing") }, { value: "dualbreathing", label: qsTr("Dual breathing") },
        { value: "colorcycle", label: qsTr("Color cycle") }, { value: "rainbow", label: qsTr("Rainbow") },
        { value: "strobe", label: qsTr("Strobe") }, { value: "battery", label: qsTr("Battery level") },
        { value: "followmode", label: qsTr("Follow mode") }
    ]
    // Display Mode: Ally screen only, external only, the same on both, or one desktop over both
    readonly property var displayModes: [
        { value: "handheld", label: qsTr("Handheld") }, { value: "external", label: qsTr("External") },
        { value: "duplicate", label: qsTr("Duplicate") }, { value: "extend", label: qsTr("Extend") }
    ]
    readonly property var fanModes: [
        { value: "auto", label: qsTr("Auto") }, { value: "quiet", label: qsTr("Quiet") },
        { value: "max", label: qsTr("Max") }, { value: "custom", label: qsTr("Custom") }
    ]
    // Custom joins the cycle once a curve was saved (Settings > Performance)
    function fanCycle() {
        const saved = !!device.value("fanCurve")
        return fanModes.filter(m => m.value !== "custom" || saved)
    }
    function labelOf(list, v) {
        for (let i = 0; i < list.length; ++i) if (list[i].value === v) return list[i]
        return null
    }

    // Display data of a tile. The extra arguments only make QML re-evaluate on change.
    function tileInfo(id, _rev, _r, _w, _b, _m, _c) {
        const off = device.connected ? "" : "—"
        switch (id) {
        case "opmode": {
            const e = labelOf(opModes, device.value("opmode"))
            const w = device.value("spl")
            return { value: e ? e.label : off, badge: e && w ? w + "W" : "" }
        }
        case "tdp": {
            const w = device.value("tdp")
            return { value: !device.connected ? off : device.value("tdpLock") ? qsTr("%1W locked").arg(w) : w + "W",
                     badge: device.value("opmode") === "manual" ? qsTr("ON") : "" }
        }
        case "ctrlmode": {
            const e = labelOf(ctrlModes, config.get("control.preset", "gamepad"))
            return { value: e ? e.label : qsTr("Gamepad") }
        }
        case "charge": {
            const e = labelOf(chargeModes, device.value("charge"))
            const v = device.value("charge")
            return { value: e ? e.label : (v ? v + "%" : off) }
        }
        case "rgb": {
            const e = labelOf(rgbModes, device.value("lighting"))
            return { value: e ? e.label : off }
        }
        case "fan": {
            const e = labelOf(fanModes, device.value("fan"))
            return { value: e ? e.label : off }
        }
        case "cpuclock":
        case "gpuclock":
            return device.connected ? popups.tileInfo(id === "cpuclock" ? "cpu" : "gpu") : { value: off }
        case "monitor":  return { value: config.monitorOsd ? qsTr("On") : qsTr("Off") }
        case "refresh":  return { value: sys.refreshRate > 0 ? sys.refreshRate + "Hz" : "" }
        case "display": {
            if (!sys.hasExternal) return { value: qsTr("Handheld only") }
            const e = labelOf(displayModes, sys.displayMode)
            return { value: e ? e.label : "" }
        }
        case "wifi":     return { value: sys.wifi ? qsTr("On") : qsTr("Off") }
        case "bluetooth":return { value: sys.bluetooth ? qsTr("On") : qsTr("Off") }
        }
        return { value: "" }
    }

    // ---------------- tile actions ----------------
    // Multi-value tiles change on the spot: each press moves to the next value.
    function cycle(key, list) {
        if (!device.connected) return
        const cur = device.value(key)
        const i = list.findIndex(m => m.value === cur)
        device.setValue(key, list[(i + 1) % list.length].value)
    }
    TilePopups { id: popups; chooser: chooser }
    function activateTile(id) {
        switch (id) {
        case "cpuclock": popups.openClock("cpu"); return
        case "gpuclock": popups.openClock("gpu"); return
        case "opmode":   cycle("opmode", opModes); return
        case "charge":   cycle("charge", chargeModes); return
        case "rgb":      cycle("lighting", rgbModes); return
        case "fan":      cycle("fan", fanCycle()); return
        case "tdp":      popups.openTdp(); return
        case "ctrlmode": {
            const i = ctrlModes.findIndex(m => m.value === config.get("control.preset", "gamepad"))
            config.set("control.preset", ctrlModes[(i + 1) % ctrlModes.length].value)
            return
        }
        case "keyboard":  panel.requestClose(); sys.openKeyboard(); return
        case "desktop":   panel.requestClose(); sys.showDesktop(); return
        case "screenshot":panel.requestClose(); sys.screenshot(); return
        case "monitor":   config.monitorOsd = !config.monitorOsd; return
        case "refresh":   sys.toggleRefreshRate(); return
        case "display": {
            if (!sys.hasExternal) return
            const i = displayModes.findIndex(m => m.value === sys.displayMode)
            sys.setDisplayMode(displayModes[(i + 1) % displayModes.length].value)
            return
        }
        case "wifi":      sys.setWifi(!sys.wifi); return
        case "bluetooth": sys.setBluetooth(!sys.bluetooth); return
        }
    }

    function openPowerMenu() {
        chooser.show(qsTr("Power"), [
            { label: qsTr("Sleep"), value: "suspend" },
            { label: qsTr("Restart"), value: "reboot" },
            { label: qsTr("Shut down"), value: "poweroff" },
            { label: qsTr("Log out"), value: "logout" },
            { label: qsTr("Quit XPanel"), value: "quit" }
        ].filter(it => it.value !== "logout" || desktop.logout), it => {
            panel.requestClose()
            if (it.value === "quit") Qt.callLater(Qt.quit)
            else sys.power(it.value)
        })
    }

    // ---------------- navigation (gamepad via xpanel-daemon, or keyboard) ----------------
    function act(a) {
        if (!panel.open) return
        if (chooser.visible) {
            if (a === "up") chooser.move(-1)
            else if (a === "down") chooser.move(1)
            else if (a === "left") chooser.adjust(-1)
            else if (a === "right") chooser.adjust(1)
            else if (a === "a") chooser.accept()
            else if (a === "b") chooser.hide()
            return
        }
        if (editing) {
            if (a === "up") editPanel.move(0, -1)
            else if (a === "down") editPanel.move(0, 1)
            else if (a === "left") editPanel.move(-1, 0)
            else if (a === "right") editPanel.move(1, 0)
            else if (a === "a") editPanel.accept()
            else if (a === "b" || a === "y") editing = false
            return
        }
        if (a === "b") { panel.requestClose(); return }
        if (a === "y") { editing = true; editPanel.index = 0; return }
        if (a === "x") { panel.openApp("settings"); return }
        if (a === "lb") { sys.setVolume(sys.volume - 5); return }
        if (a === "rb") { sys.setVolume(sys.volume + 5); return }
        if (area === "side") {
            if (a === "up") sideIndex = Math.max(0, sideIndex - 1)
            else if (a === "down") sideIndex = Math.min(2, sideIndex + 1)
            else if (a === "right" && sideIndex === 2) area = "grid"
            else if (a === "left" || a === "right") {
                const d = a === "right" ? 5 : -5
                if (sideIndex === 0) adjustBrightness(d)
                else if (sideIndex === 1) sys.setVolume(sys.volume + d)
            } else if (a === "a") {
                if (sideIndex === 1) sys.toggleMute()
                else if (sideIndex === 2) openPowerMenu()
                else if (sideIndex === 0 && screens.length > 1) brightIndex = (Math.min(brightIndex, screens.length - 1) + 1) % screens.length
                else area = "grid"
            }
            return
        }
        // grid
        const n = tiles.length
        const col = gridIndex % 3
        if (a === "left") { if (col === 0) area = "side"; else gridIndex-- }
        else if (a === "right") { if (col < 2 && gridIndex + 1 < n) gridIndex++ }
        else if (a === "up") { if (gridIndex >= 3) gridIndex -= 3 }
        else if (a === "down") { if (gridIndex + 3 < n) gridIndex += 3; else if (Math.floor(gridIndex / 3) < Math.floor((n - 1) / 3)) gridIndex = n - 1 }
        else if (a === "a" && tiles[gridIndex]) activateTile(tiles[gridIndex])
        gridFlick.ensureVisible(gridIndex)
    }

    Connections {
        target: panel
        function onGamepadAction(name) { root.act(name) }
        function onOpenChanged() {
            if (panel.open) {
                sys.refreshAll()
                root.forceActiveFocus()
                slideOut.stop(); slideIn.start()
            } else {
                chooser.hide(); root.editing = false
                slideIn.stop(); slideOut.start()
            }
        }
    }
    Binding { target: sys; property: "sampling"; value: panel.open || config.monitorOsd || panel.appOpen }

    // Send the resolved Control Mode settings to the input layer (on change and on connect).
    function sendControl() { device.setValue("control", CD.resolve((k, d) => config.get(k, d))) }
    Timer { id: controlDebounce; interval: 300; onTriggered: root.sendControl() }
    Connections { target: config; function onValuesChanged() { controlDebounce.restart() } }
    Connections { target: device; function onConnectedChanged() { if (device.connected) controlDebounce.restart() } }
    onTilesChanged: gridIndex = Math.min(gridIndex, Math.max(0, tiles.length - 1))

    Keys.onPressed: (e) => {
        const map = {
            [Qt.Key_Up]: "up", [Qt.Key_Down]: "down", [Qt.Key_Left]: "left", [Qt.Key_Right]: "right",
            [Qt.Key_Return]: "a", [Qt.Key_Enter]: "a", [Qt.Key_Space]: "a",
            [Qt.Key_Escape]: "b", [Qt.Key_Backspace]: "b", [Qt.Key_E]: "y",
            [Qt.Key_PageUp]: "lb", [Qt.Key_PageDown]: "rb"
        }
        if (map[e.key] !== undefined) { act(map[e.key]); e.accepted = true }
    }

    // ---------------- visuals ----------------
    property real slide: 0    // 0 hidden, 1 shown
    NumberAnimation { id: slideIn; target: root; property: "slide"; to: 1; duration: 200; easing.type: Easing.OutCubic }
    NumberAnimation { id: slideOut; target: root; property: "slide"; to: 0; duration: 160; easing.type: Easing.InCubic
        onFinished: panel.closeFinished() }

    // dim the rest of the screen; tap closes
    Rectangle {
        anchors.fill: parent
        color: "#000"
        opacity: 0.35 * root.slide
        MouseArea { anchors.fill: parent; enabled: panel.open; onClicked: panel.requestClose() }
    }

    EditPanel {
        id: editPanel
        s: root.s
        catalog: root.catalog
        active: root.editing
        visible: root.editing && root.slide > 0
        x: root.panelW + 30 * root.s
        y: 120 * root.s
        width: Math.min(root.width - x - 30 * root.s, 760 * root.s)
        height: root.height - 180 * root.s
    }

    Item {
        id: sheet
        width: root.panelW
        height: root.height
        x: -width * (1 - root.slide)
        clip: true

        MouseArea { anchors.fill: parent }   // keep taps from reaching the dim layer

        Rectangle { anchors.fill: parent; color: T.panel; opacity: 0.97 }
        // fine diagonal stripes, as in the original
        Canvas {
            anchors.fill: parent
            opacity: 0.06
            onPaint: {
                const c = getContext("2d")
                c.strokeStyle = "#ffffff"; c.lineWidth = 1
                const step = 7 * root.s
                for (let x = -height; x < width; x += step) {
                    c.beginPath(); c.moveTo(x, height); c.lineTo(x + height, 0); c.stroke()
                }
            }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#2a2a30" }

        // header
        Image {
            source: "qrc:/qml/icons/logo.svg"
            x: 48 * root.s; y: 38 * root.s
            width: 66 * root.s; height: 66 * root.s
            sourceSize: Qt.size(width * 2, height * 2)
        }
        Text {
            x: 144 * root.s; y: 40 * root.s
            text: qsTr("Quick Panel")
            color: T.text
            font.pixelSize: 42 * root.s
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 44 * root.s
            y: 52 * root.s
            spacing: 16 * root.s
            Text {
                visible: !device.connected
                text: qsTr("xpanel-daemon offline")
                color: T.warn; font.pixelSize: 22 * root.s
                anchors.verticalCenter: parent.verticalCenter
            }
            // the running game's profile is applied (restored when the game ends)
            Rectangle {
                visible: profiles.activeProfile
                anchors.verticalCenter: parent.verticalCenter
                width: profileText.width + 24 * root.s; height: 36 * root.s
                radius: height / 2
                color: "transparent"; border.color: T.accent; border.width: Math.max(1, 2 * root.s)
                Text {
                    id: profileText
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, 260 * root.s)
                    text: qsTr("Profile: %1").arg(profiles.activeTitle)
                    color: T.accent; font.pixelSize: 19 * root.s
                    elide: Text.ElideRight
                }
            }
            Text {
                text: sys.battery + "%" + (sys.charging ? " ⚡" : "")
                color: sys.battery <= 15 && !sys.charging ? T.warn : T.text
                font.pixelSize: 26 * root.s
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: sys.clock
                color: T.text; font.pixelSize: 26 * root.s
                anchors.verticalCenter: parent.verticalCenter
            }
            // gear: opens the full Control Center window
            Image {
                source: "qrc:/qml/icons/gear.svg"
                width: 34 * root.s; height: 34 * root.s
                sourceSize: Qt.size(width * 2, height * 2)
                anchors.verticalCenter: parent.verticalCenter
                opacity: gearMouse.pressed ? 0.6 : 1
                MouseArea { id: gearMouse; anchors.fill: parent; anchors.margins: -12 * root.s
                            onClicked: panel.openApp(appUpdate.available && !appUpdate.skipped ? "settings/update" : "settings") }
                // a new XPanel version is available
                Rectangle { visible: appUpdate.available && !appUpdate.skipped
                            width: 10 * root.s; height: width; radius: width / 2; color: T.accent
                            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: -2 * root.s }
            }
        }
        // edit corner (the small triangle at the top right)
        Item {
            anchors.right: parent.right; y: 0
            width: 60 * root.s; height: 60 * root.s
            Canvas {
                id: editCorner
                anchors.right: parent.right; anchors.rightMargin: 14 * root.s
                y: 12 * root.s
                width: 16 * root.s; height: 16 * root.s
                onPaint: { const c = getContext("2d"); c.fillStyle = root.editing ? T.accent : "#fff"
                           c.beginPath(); c.moveTo(0, 0); c.lineTo(width, 0); c.lineTo(width, height); c.fill() }
                Connections { target: root; function onEditingChanged() { editCorner.requestPaint() } }
            }
            MouseArea { anchors.fill: parent; onClicked: root.editing = !root.editing }
        }

        StatsStrip {
            visible: false
            s: root.s * 0.85
            x: 200 * root.s; y: 112 * root.s
        }

        // left column: brightness, volume, power
        // one bar per active screen (a single bar for Ally only / external only / mirrored)
        Repeater {
            model: Math.max(1, root.screens.length)   // a count, so the sliders survive value updates
            Item {
                id: bar
                readonly property var e: index < root.screens.length
                    ? root.screens[index] : { id: "", label: "", internal: true, brightness: sys.brightness }
                readonly property int n: Math.max(1, root.screens.length)
                readonly property real sc: n === 1 ? 1 : (n === 2 ? 0.62 : 0.5)
                readonly property real cx: (94 + (index - (n - 1) / 2) * 46) * root.s
                readonly property bool multi: n > 1
                VSlider {
                    s: root.s * bar.sc
                    x: bar.cx - width / 2; y: 148 * root.s
                    height: 300 * root.s
                    value: bar.e.brightness
                    minimum: 1
                    focused: root.area === "side" && root.sideIndex === 0 && !root.editing
                             && (!bar.multi || Math.min(root.brightIndex, bar.n - 1) === index)
                    onMoved: (v) => {
                        root.brightIndex = index
                        if (bar.e.id === "") sys.setBrightness(v)
                        else sys.setDisplayBrightness(bar.e.id, v)
                    }
                }
                Text {
                    visible: bar.multi
                    x: bar.cx - width / 2; y: 120 * root.s
                    width: 46 * root.s
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: bar.e.internal ? "Ally" : bar.e.label
                    color: root.area === "side" && root.sideIndex === 0 && Math.min(root.brightIndex, bar.n - 1) === index
                           ? T.accent : T.dim
                    font.pixelSize: 14 * root.s
                }
            }
        }
        Image {
            source: "qrc:/qml/icons/sun.svg"
            x: 70 * root.s; y: 462 * root.s
            width: 48 * root.s; height: 48 * root.s
            sourceSize: Qt.size(width * 2, height * 2)
        }
        VSlider {
            s: root.s
            x: 64 * root.s; y: 540 * root.s
            height: 300 * root.s
            value: sys.volume
            focused: root.area === "side" && root.sideIndex === 1 && !root.editing
            onMoved: (v) => sys.setVolume(v)
        }
        Image {
            source: "qrc:/qml/icons/" + (sys.muted ? "muted" : "volume") + ".svg"
            x: 70 * root.s; y: 856 * root.s
            width: 48 * root.s; height: 48 * root.s
            sourceSize: Qt.size(width * 2, height * 2)
            MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: sys.toggleMute() }
        }
        Rectangle { x: 48 * root.s; y: 936 * root.s; width: 92 * root.s; height: 1; color: "#3a3a40" }
        Item {
            x: 60 * root.s; y: 960 * root.s
            width: 68 * root.s; height: 68 * root.s
            Rectangle {
                anchors.fill: parent; radius: width / 2; color: "transparent"
                border.color: T.accent; border.width: 3 * root.s
                visible: root.area === "side" && root.sideIndex === 2 && !root.editing
            }
            Image {
                anchors.centerIn: parent
                source: "qrc:/qml/icons/power.svg"
                width: 46 * root.s; height: 46 * root.s
                sourceSize: Qt.size(width * 2, height * 2)
            }
            MouseArea { anchors.fill: parent; onClicked: root.openPowerMenu() }
        }

        // tile grid
        Flickable {
            id: gridFlick
            x: 188 * root.s; y: 140 * root.s
            width: parent.width - x - 20 * root.s
            height: parent.height - y - 10 * root.s
            clip: true
            contentHeight: tileGrid.height + 40 * root.s
            boundsBehavior: Flickable.StopAtBounds
            readonly property real tileW: 213 * root.s
            readonly property real tileH: 260 * root.s
            readonly property real gap: 31 * root.s
            function ensureVisible(i) {
                const top = Math.floor(i / 3) * (tileH + gap)
                const bottom = top + tileH + 20 * root.s
                if (top < contentY) contentY = Math.max(0, top - 10 * root.s)
                else if (bottom > contentY + height) contentY = bottom - height
            }
            Behavior on contentY { NumberAnimation { duration: 140 } }

            Grid {
                id: tileGrid
                x: 12 * root.s; y: 12 * root.s
                columns: 3
                spacing: gridFlick.gap
                Repeater {
                    model: root.tiles
                    Tile {
                        readonly property var entry: root.catalogEntry(modelData)
                        readonly property var info: root.tileInfo(modelData, device.rev, sys.refreshRate,
                                                                  sys.wifi, sys.bluetooth, config.monitorOsd,
                                                                  config.rev, sys.displayMode, sys.hasExternal)
                        width: gridFlick.tileW; height: gridFlick.tileH
                        s: root.s
                        icon: entry ? entry.icon : ""
                        title: entry ? entry.title : modelData
                        value: info.value || ""
                        badge: info.badge || ""
                        focused: root.area === "grid" && root.gridIndex === index && !root.editing
                        onClicked: { root.area = "grid"; root.gridIndex = index; root.activateTile(modelData) }
                    }
                }
            }
        }

        // hint bar
        Text {
            anchors.right: parent.right; anchors.rightMargin: 30 * root.s
            anchors.bottom: parent.bottom; anchors.bottomMargin: 14 * root.s
            text: qsTr("Ⓐ Select   Ⓑ Close   Ⓧ Control Center   Ⓨ Edit")
            color: T.dim; font.pixelSize: 20 * root.s
            visible: device.value("inputLayer") === "active"
        }

        Chooser {
            id: chooser
            s: root.s
            anchors.fill: parent
        }
    }
}
