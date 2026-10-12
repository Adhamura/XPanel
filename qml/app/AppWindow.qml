import QtQuick
import QtQuick.Window
import "../Theme.js" as T

// The full Control Center window: Game Library, Control Mode, Media Gallery, User Center, Settings.
Item {
    id: app
    width: 1600; height: 900
    focus: true

    readonly property var tabs: [
        { id: "library",  title: qsTr("Game Library") },
        { id: "control",  title: qsTr("Control Mode") },
        { id: "gallery",  title: qsTr("Media Gallery") },
        { id: "user",     title: qsTr("User Center") },
        { id: "settings", title: qsTr("Settings") }
    ]
    property string tab: "library"
    property string settingsPage: "general"
    readonly property real s: Math.max(0.8, Math.min(width / 1600, height / 900))

    // ---------- open on a page (tray menu, --app page) ----------
    function openPage(p) {
        if (!p) return
        const parts = p.split("/")
        tab = parts[0]
        if (parts[0] === "settings" && parts[1]) settingsPage = parts[1]
    }
    Connections {
        target: panel
        function onAppPageChanged() { app.openPage(panel.appPage) }
        function onAppOpenChanged() {
            if (!panel.appOpen) return
            app.openPage(panel.appPage)
            if (config.get("personal.launchAnimation", true)) splash.play()
            Qt.callLater(app.focusFirst)
        }
        function onAppAction(name) { app.act(name) }
    }

    // ---------- back handlers (dialogs push one) ----------
    property var backStack: []
    function pushBack(fn) { backStack = backStack.concat([fn]) }
    function popBack() { const s = backStack.slice(); s.pop(); backStack = s }
    function back() {
        if (backStack.length) { const fn = backStack[backStack.length - 1]; popBack(); fn(); return }
        if (tab === "settings" && settingsLoader.item && settingsLoader.item.inContent && settingsLoader.item.inContent()) {
            settingsLoader.item.focusNav(); return
        }
        panel.closeApp()
    }

    // ---------- notifications (User Center) ----------
    ListModel { id: notes }
    property alias notifications: notes
    function note(text) {
        notes.insert(0, { text: text, time: Qt.formatTime(new Date(), "HH:mm") })
        if (notes.count > 100) notes.remove(100)
    }
    property var lastState: ({})
    Connections {
        target: device
        function onStateChanged() {
            const names = { opmode: qsTr("Operating Mode"), charge: qsTr("Charge limit"), lighting: qsTr("Lighting"),
                            fan: qsTr("Fan curve"), eco: qsTr("Eco Mode"), cpuBoost: qsTr("CPU Boost"), gpuClock: qsTr("GPU clock") }
            const st = {}
            for (const k in names) {
                const v = device.value(k)
                st[k] = v
                if (app.lastState[k] !== undefined && app.lastState[k] !== v)
                    app.note(qsTr("%1 → %2").arg(names[k]).arg(v))
            }
            app.lastState = st
        }
        function onConnectedChanged() { app.note(device.connected ? qsTr("xpanel-daemon connected") : qsTr("xpanel-daemon disconnected")) }
        function onErrorChanged() { if (device.error) app.note(qsTr("xpanel-daemon: %1").arg(device.error)) }
    }
    Connections { target: library; function onLaunched(id) { app.note(qsTr("Started %1").arg(id)) } }
    Connections { target: appUpdate; function onUpdateFound(version) { app.note(qsTr("XPanel %1 is available: Settings › Update Center").arg(version)) } }

    // ---------- gamepad / keyboard navigation ----------
    function focusFirst() {
        const w = app.Window.window
        if (!w) return
        // Settings: focus the entry of the open sub-page (focusing an entry selects its page)
        if (tab === "settings" && settingsLoader.item) { settingsLoader.item.focusNav(); return }
        let it = pageArea.nextItemInFocusChain(true)
        if (it) it.forceActiveFocus()
    }
    function moveFocus(forward) {
        const w = app.Window.window
        const cur = w ? w.activeFocusItem : null
        let it = cur ? cur.nextItemInFocusChain(forward) : pageArea.nextItemInFocusChain(true)
        if (it) it.forceActiveFocus()
    }
    function switchTab(d) {
        let i = tabs.findIndex(t => t.id === tab)
        i = (i + d + tabs.length) % tabs.length
        tab = tabs[i].id
        backStack = []
        Qt.callLater(focusFirst)
    }
    function act(a) {
        const w = app.Window.window
        const it = w ? w.activeFocusItem : null
        // a text field keeps arrows and A for typing (on-screen keyboard); B leaves it
        const typing = it && (it instanceof TextInput || it instanceof TextEdit)
        // a control that is being edited may take the keys that otherwise move or switch (fan curve graph)
        if (it && typeof it.navKey === "function" && it.navKey(a)) return
        if (a === "lb") return switchTab(-1)
        if (a === "rb") return switchTab(1)
        if (a === "b") { if (typing) { moveFocus(true); return } return back() }
        if (a === "up") return moveFocus(false)
        if (a === "down") return moveFocus(true)
        if (a === "left" || a === "right") {
            const d = a === "right" ? 1 : -1
            if (it && typeof it.step === "function") it.step(d)
            else moveFocus(d > 0)
            return
        }
        if (a === "a" && it && typeof it.activate === "function") it.activate()
        if (a === "y" && it && typeof it.secondary === "function") it.secondary()
    }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Escape) { back(); e.accepted = true }
        else if (e.key === Qt.Key_PageUp) { switchTab(-1); e.accepted = true }
        else if (e.key === Qt.Key_PageDown) { switchTab(1); e.accepted = true }
        else if (e.key === Qt.Key_Up) { moveFocus(false); e.accepted = true }
        else if (e.key === Qt.Key_Down) { moveFocus(true); e.accepted = true }
        else if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) { act(e.key === Qt.Key_Right ? "right" : "left"); e.accepted = true }
        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) { act("a"); e.accepted = true }
    }

    // ---------- look ----------
    Rectangle { anchors.fill: parent; color: "#0b0b0e" }
    Canvas {
        anchors.fill: parent
        opacity: config.get("personal.stripes", true) ? 0.045 : 0
        onPaint: {
            const c = getContext("2d"); c.strokeStyle = "#ffffff"; c.lineWidth = 1
            for (let x = -height; x < width; x += 7) { c.beginPath(); c.moveTo(x, height); c.lineTo(x + height, 0); c.stroke() }
        }
        onWidthChanged: requestPaint()
    }

    // top bar
    Item {
        id: topBar
        width: parent.width; height: 84
        Image { source: "qrc:/qml/icons/logo.svg"; x: 28; y: 18; width: 52; height: 52; sourceSize: Qt.size(104, 104) }
        Row {
            x: 104; height: parent.height
            spacing: 6
            Repeater {
                model: app.tabs
                Item {
                    readonly property bool sel: app.tab === modelData.id
                    width: modelData.id === "settings" ? 64 : label.implicitWidth + 44
                    height: parent.height
                    Text { id: label; visible: modelData.id !== "settings"; anchors.centerIn: parent
                           text: modelData.title; font.pixelSize: 22; font.bold: parent.sel
                           color: parent.sel ? "#ffffff" : T.dim }
                    Image { visible: modelData.id === "settings"; anchors.centerIn: parent
                            source: "qrc:/qml/icons/gear.svg"; width: 34; height: 34; sourceSize: Qt.size(68, 68)
                            opacity: parent.sel ? 1 : 0.6 }
                    Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 10; width: parent.width - 20
                                anchors.horizontalCenter: parent.horizontalCenter; height: 4; color: T.accent; visible: parent.sel }
                    MouseArea { anchors.fill: parent; onClicked: { app.tab = modelData.id; app.backStack = []; Qt.callLater(app.focusFirst) } }
                }
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
            spacing: 22
            Text { text: qsTr("LB / RB  switch"); color: "#5d5d66"; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
            Text { text: sys.battery + "%" + (sys.charging ? " ⚡" : ""); color: T.text; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
            Text { text: sys.clock; color: T.text; font.pixelSize: 20; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: "✕"; color: T.text; font.pixelSize: 26; anchors.verticalCenter: parent.verticalCenter
                MouseArea { anchors.fill: parent; anchors.margins: -12; onClicked: panel.closeApp() }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#26262d" }
    }

    // pages
    Item {
        id: pageArea
        anchors.top: topBar.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: hints.top
        Loader { anchors.fill: parent; active: app.tab === "library"; source: "LibraryPage.qml" }
        Loader { anchors.fill: parent; active: app.tab === "control"; source: "ControlPage.qml" }
        Loader { anchors.fill: parent; active: app.tab === "gallery"; source: "GalleryPage.qml" }
        Loader { anchors.fill: parent; active: app.tab === "user"; source: "UserPage.qml" }
        Loader { id: settingsLoader; anchors.fill: parent; active: app.tab === "settings"; source: "SettingsPage.qml" }
    }

    // gamepad hints
    Rectangle {
        id: hints
        anchors.bottom: parent.bottom; width: parent.width; height: 44; color: "#0e0e12"
        Rectangle { width: parent.width; height: 1; color: "#26262d" }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Ⓐ Select    Ⓑ Back    ◀ ▶ Change    ▲ ▼ Move    LB / RB Section")
            color: T.dim; font.pixelSize: 16
        }
    }

    // launch animation
    Rectangle {
        id: splash
        anchors.fill: parent; color: "#050507"; visible: opacity > 0; opacity: 0
        function play() { anim.restart() }
        Image { id: splashLogo; anchors.centerIn: parent; source: "qrc:/qml/icons/logo.svg"
                width: 220; height: 220; sourceSize: Qt.size(440, 440) }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: splashLogo.bottom; anchors.topMargin: 20
                    width: 260 * splash.bar; height: 4; color: T.accent }
        property real bar: 0
        SequentialAnimation {
            id: anim
            PropertyAction { target: splash; property: "opacity"; value: 1 }
            PropertyAction { target: splash; property: "bar"; value: 0 }
            ParallelAnimation {
                NumberAnimation { target: splash; property: "bar"; to: 1; duration: 650; easing.type: Easing.OutCubic }
                NumberAnimation { target: splashLogo; property: "scale"; from: 0.85; to: 1; duration: 650 }
            }
            NumberAnimation { target: splash; property: "opacity"; to: 0; duration: 250 }
        }
    }
}
