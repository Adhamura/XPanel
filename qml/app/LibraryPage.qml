import QtQuick
import "../Theme.js" as T

// Game Library: Favorites / Game Platforms, search, grid or list, Manage Library,
// and Game Options (a per-game profile applied when the game is started from here).
Item {
    id: page
    property string view: "platforms"     // favorites | platforms
    property string platform: "All"
    property string search: ""
    property bool listView: config.get("library.listView", false)
    property int rev: config.rev
    readonly property var favorites: (rev, config.get("library.favorites", []))

    readonly property var shown: {
        const q = search.toLowerCase()
        return library.games.filter(g =>
            (view === "favorites" ? favorites.indexOf(g.id) >= 0 : (platform === "All" || g.platform === platform))
            && (!q || g.title.toLowerCase().indexOf(q) >= 0))
    }
    function toggleFavorite(id) {
        const f = favorites.slice()
        const i = f.indexOf(id)
        if (i >= 0) f.splice(i, 1); else f.push(id)
        config.set("library.favorites", f)
    }
    // per-game profile: applied by `profiles` when the game starts, restored when it ends
    function profile(id) { return config.get("game." + id, { opmode: "", control: "", tdp: 0, fan: "" }) }
    function setProfile(id, key, value) { const p = profile(id); p[key] = value; config.set("game." + id, p) }
    function play(g) {
        profiles.play(g.id)
        panel.closeApp()
    }

    // ---------- toolbar ----------
    Row {
        id: bar
        x: 32; y: 20
        spacing: 12
        CcChoice {
            options: [{ value: "favorites", label: "★ Favorites" }, { value: "platforms", label: "Game Platforms" }]
            current: page.view
            onPicked: (v) => page.view = v
        }
        CcChoice {
            visible: page.view === "platforms"
            options: [{ value: "All", label: "All" }, { value: "Steam", label: "Steam" }, { value: "Desktop", label: "Installed apps" }]
            current: page.platform
            onPicked: (v) => page.platform = v
        }
        CcField { width: 300; placeholder: "Search…"; text: page.search; onTextChanged: page.search = text }
        CcButton { text: page.listView ? "Grid view" : "List view"; onClicked: config.set("library.listView", !page.listView) }
        CcButton { text: "Manage Library"; onClicked: manage.show() }
        CcButton { text: "Rescan"; onClicked: library.rescan() }
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 32; y: 32
        text: page.shown.length + " games"; color: T.dim; font.pixelSize: 16
    }

    // ---------- games ----------
    GridView {
        id: grid
        anchors.top: bar.bottom; anchors.topMargin: 20
        anchors.left: parent.left; anchors.leftMargin: 32
        anchors.right: parent.right; anchors.bottom: parent.bottom
        clip: true
        model: page.shown
        readonly property real cw: page.listView ? width - 32 : 230
        cellWidth: page.listView ? width : cw + 24
        cellHeight: page.listView ? 96 : 360
        keyNavigationEnabled: false
        delegate: Item {
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                id: card
                readonly property bool fav: page.favorites.indexOf(modelData.id) >= 0
                width: grid.cw; height: grid.cellHeight - 24
                color: "#15151b"; radius: 4
                border.color: activeFocus ? T.accent : "#2c2c33"; border.width: activeFocus ? 4 : 1
                activeFocusOnTab: true
                function activate() { details.show(modelData) }
                function secondary() { page.toggleFavorite(modelData.id) }
                onActiveFocusChanged: if (activeFocus) grid.positionViewAtIndex(index, GridView.Contain)
                Image {
                    id: cover
                    visible: !!modelData.cover
                    x: 4; y: 4
                    width: page.listView ? 56 : parent.width - 8
                    height: page.listView ? parent.height - 8 : (parent.width - 8) * 1.5
                    source: modelData.cover; fillMode: Image.PreserveAspectCrop; asynchronous: true
                }
                Rectangle { // no cover: icon + title tile
                    visible: !modelData.cover
                    x: 4; y: 4; width: cover.width; height: cover.height; color: "#1d1d25"
                    Image { anchors.centerIn: parent; width: Math.min(parent.width, parent.height) * 0.45; height: width
                            source: modelData.icon ? "image://theme/" + modelData.icon : ""; sourceSize: Qt.size(128, 128) }
                }
                Text {
                    x: page.listView ? 80 : 10
                    y: page.listView ? 18 : cover.height + 12
                    width: parent.width - x - 12
                    text: modelData.title; color: T.text; font.pixelSize: 17; elide: Text.ElideRight
                }
                Text {
                    x: page.listView ? 80 : 10
                    y: page.listView ? 48 : cover.height + 34
                    text: modelData.platform + (card.fav ? "   ★" : ""); color: card.fav ? T.accent : T.dim; font.pixelSize: 14
                }
                MouseArea { anchors.fill: parent; onClicked: { card.forceActiveFocus(); card.activate() } }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: page.shown.length === 0
            text: page.view === "favorites" ? "No favorites yet.\nOpen a game and press Y or ★ to add it."
                                            : "No games found.\nSteam games appear after you install them; other games: Manage Library."
            horizontalAlignment: Text.AlignHCenter; color: T.dim; font.pixelSize: 20
        }
    }

    // ---------- game details + Game Options ----------
    Rectangle {
        id: details
        property var game: null
        anchors.fill: parent; color: "#e6050507"; visible: !!game
        function show(g) { game = g; app.pushBack(() => details.hide()); Qt.callLater(() => playBtn.forceActiveFocus()) }
        function hide() { game = null; grid.forceActiveFocus() }
        MouseArea { anchors.fill: parent }
        Image { anchors.fill: parent; source: details.game ? details.game.hero : ""; fillMode: Image.PreserveAspectCrop; opacity: 0.25 }
        CcPage {
            anchors.fill: parent
            visible: !!details.game
            Text { text: details.game ? details.game.title : ""; color: T.text; font.pixelSize: 38; font.bold: true }
            Text { text: details.game ? details.game.platform + "  ·  " + details.game.id : ""; color: T.dim; font.pixelSize: 16 }
            Row {
                spacing: 12
                CcButton { id: playBtn; text: "▶  Play"; primary: true; onClicked: page.play(details.game) }
                CcButton { text: details.game && page.favorites.indexOf(details.game.id) >= 0 ? "★ Remove favorite" : "☆ Add favorite"
                           onClicked: page.toggleFavorite(details.game.id) }
                CcButton { text: "Remove from library"; danger: true
                           onClicked: { library.removeGame(details.game.id); details.hide(); app.popBack() } }
                CcButton { text: "Back"; onClicked: { details.hide(); app.popBack() } }
            }
            CcSection { title: "Game Options"
                        note: "Applied while the game runs; your previous settings come back when it ends. Settings you change during the game are kept." }
            CcRow {
                visible: !!details.game && details.game.platform === "Steam"
                title: "Use this profile when the game starts from Steam"
                description: "Off: only when you start it with Play here."
                CcSwitch {
                    checked: details.game ? (config.rev, page.profile(details.game.id).enabled !== false) : true
                    onToggled: (v) => page.setProfile(details.game.id, "enabled", v)
                }
            }
            CcRow {
                title: "Operating Mode"
                CcChoice {
                    options: [{ value: "", label: "Keep" }, { value: "silent", label: "Silent" }, { value: "performance", label: "Performance" },
                              { value: "turbo", label: "Turbo" }, { value: "manual", label: "Manual" }]
                    current: details.game ? (config.rev, page.profile(details.game.id).opmode) : ""
                    onPicked: (v) => page.setProfile(details.game.id, "opmode", v)
                }
            }
            CcRow {
                title: "Manual TDP"
                description: "Used when the Operating Mode above is Manual."
                CcSlider {
                    width: 420; from: 7; to: 30; unit: " W"
                    value: details.game ? (config.rev, page.profile(details.game.id).tdp || 15) : 15
                    onMoved: (v) => page.setProfile(details.game.id, "tdp", v)
                }
            }
            CcRow {
                title: "Fan Mode"
                CcChoice {
                    options: [{ value: "", label: "Keep" }, { value: "auto", label: "Auto" }, { value: "quiet", label: "Quiet" },
                              { value: "max", label: "Max" }]
                    current: details.game ? (config.rev, page.profile(details.game.id).fan || "") : ""
                    onPicked: (v) => page.setProfile(details.game.id, "fan", v)
                }
            }
            CcRow {
                title: "Control Mode"
                CcChoice {
                    options: [{ value: "", label: "Keep" }, { value: "gamepad", label: "Gamepad" }, { value: "desktop", label: "Desktop" }]
                    current: details.game ? (config.rev, page.profile(details.game.id).control) : ""
                    onPicked: (v) => page.setProfile(details.game.id, "control", v)
                }
            }
        }
    }

    // ---------- Manage Library: add installed apps ----------
    Rectangle {
        id: manage
        anchors.fill: parent; color: "#f2050507"; visible: false
        property var apps: []
        function show() { apps = library.installedApps(); visible = true; app.pushBack(() => manage.hide()); Qt.callLater(() => closeBtn.forceActiveFocus()) }
        function hide() { visible = false; grid.forceActiveFocus() }
        MouseArea { anchors.fill: parent }
        CcPage {
            anchors.fill: parent
            Row {
                spacing: 16
                Text { text: "Manage Library"; color: T.text; font.pixelSize: 32; font.bold: true }
                CcButton { id: closeBtn; text: "Done"; onClicked: { manage.hide(); app.popBack() } }
            }
            Text { text: "Add installed apps to the library (games are added by themselves)."; color: T.dim; font.pixelSize: 16 }
            Repeater {
                model: manage.apps
                CcRow {
                    readonly property bool inLib: library.games.some(g => g.id === "app:" + modelData.desktopId)
                    title: modelData.title
                    description: modelData.desktopId + (modelData.isGame ? "  ·  game" : "")
                    CcButton {
                        text: parent.parent.inLib ? "In library" : "Add"
                        enabledButton: !parent.parent.inLib
                        onClicked: library.addApp(modelData.desktopId)
                    }
                }
            }
        }
    }
}
