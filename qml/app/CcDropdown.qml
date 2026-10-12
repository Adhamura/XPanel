import QtQuick
import "../Theme.js" as T

// Dropdown: options [{value, label}]. Closed: A opens the list, left / right pick the previous / next
// option. Open: up / down move, A picks, B closes (navKey takes the keys while the list is open).
Item {
    id: dd
    property var options: []
    property var current
    property bool enabledChoice: true
    property int maxRows: 8
    signal picked(var value)
    readonly property int index: {
        for (let i = 0; i < options.length; ++i) if (options[i].value === current) return i
        return -1
    }
    property bool open: false
    property int hover: 0

    function step(d) {
        if (!enabledChoice || options.length === 0) return
        const i = Math.max(0, Math.min(options.length - 1, (index < 0 ? 0 : index) + d))
        if (i !== index) picked(options[i].value)
    }
    function activate() {
        if (!enabledChoice || options.length === 0) return
        if (open) { choose(hover); return }
        hover = Math.max(0, index)
        open = true
    }
    function choose(i) {
        open = false
        if (i >= 0 && i < options.length && i !== index) picked(options[i].value)
    }
    function moveHover(d) {
        hover = Math.max(0, Math.min(options.length - 1, hover + d))
        list.positionViewAtIndex(hover, ListView.Contain)
    }
    // gamepad keys while the list is open (AppWindow.act asks navKey first)
    function navKey(a) {
        if (!open) return false
        if (a === "up" || a === "left") moveHover(-1)
        else if (a === "down" || a === "right") moveHover(1)
        else if (a === "a") choose(hover)
        else if (a === "b") open = false
        return true
    }

    activeFocusOnTab: enabledChoice
    implicitWidth: 220; implicitHeight: 44
    opacity: enabledChoice ? 1 : 0.4
    onActiveFocusChanged: if (!activeFocus) open = false

    Rectangle {
        anchors.fill: parent; anchors.margins: -5; radius: 4
        color: "transparent"; border.color: T.accent; border.width: 3; visible: dd.activeFocus
    }
    Rectangle {
        anchors.fill: parent; radius: 3
        color: dd.open ? "#1b3446" : "#1a1a20"; border.color: dd.open ? T.accent : "#34343d"
        Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
               text: dd.index >= 0 ? dd.options[dd.index].label : ""; color: "#ffffff"; font.pixelSize: 17 }
        Text { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
               text: dd.open ? "▲" : "▼"; color: T.dim; font.pixelSize: 13 }
        MouseArea { anchors.fill: parent
                    onClicked: { dd.forceActiveFocus(); if (dd.open) dd.open = false; else dd.activate() } }
    }

    // the list floats over the page: parented to the window's content item so rows below do not cover it
    Rectangle {
        id: popup
        parent: dd.Window.window ? dd.Window.window.contentItem : dd
        visible: dd.open
        z: 1000
        width: dd.width
        height: Math.min(dd.options.length, dd.maxRows) * 40 + 8
        radius: 3; color: "#15151a"; border.color: T.accent
        onVisibleChanged: if (visible) {
            const p = dd.mapToItem(parent, 0, dd.height + 4)
            const room = parent.height - p.y - 8
            x = p.x
            y = room >= height ? p.y : dd.mapToItem(parent, 0, -height - 4).y // open upwards near the bottom
            list.positionViewAtIndex(dd.hover, ListView.Center)
        }
        CcScrollBar { flickable: list }
        ListView {
            id: list
            anchors.fill: parent; anchors.margins: 4
            clip: true
            model: dd.options
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                width: ListView.view.width; height: 40; radius: 2
                color: index === dd.hover ? "#1b3446" : "transparent"
                border.color: index === dd.hover ? T.accent : "transparent"
                Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                       text: modelData.label; font.pixelSize: 17
                       color: index === dd.index ? T.accent : index === dd.hover ? "#ffffff" : T.text }
                MouseArea { anchors.fill: parent; hoverEnabled: true
                            onEntered: dd.hover = index
                            onClicked: dd.choose(index) }
            }
        }
    }

    Keys.onLeftPressed: (e) => { if (open) moveHover(-1); else step(-1) }
    Keys.onRightPressed: (e) => { if (open) moveHover(1); else step(1) }
    Keys.onUpPressed: (e) => { if (open) moveHover(-1); else e.accepted = false }
    Keys.onDownPressed: (e) => { if (open) moveHover(1); else e.accepted = false }
    Keys.onEscapePressed: (e) => { if (open) open = false; else e.accepted = false }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
}
