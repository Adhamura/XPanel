import QtQuick
import "Theme.js" as T

// Pop-up list for a tile with several choices.
// items: [{label, sub, value, selected}] or {stepper: true, label, value, min, max, step, unit}
Item {
    id: ch
    property real s: 1
    property string title
    property var items: []
    property int index: 0
    property var onPick: null        // function(item)
    property var onStep: null        // function(item, newValue)
    visible: false

    function show(t, list, pick, step) {
        title = t; items = list; onPick = pick; onStep = step || null
        index = 0
        for (let i = 0; i < list.length; ++i)
            if (list[i].selected) { index = i; break }
        visible = true
    }
    function hide() { visible = false }
    function move(d) { index = Math.max(0, Math.min(items.length - 1, index + d)) }
    function adjust(d) {
        const it = items[index]
        if (!it || !it.stepper || !onStep) return
        const v = Math.max(it.min, Math.min(it.max, it.value + d * (it.step || 1)))
        if (v === it.value) return
        const copy = items.slice(); copy[index] = Object.assign({}, it, { value: v })
        items = copy
        onStep(it, v)
    }
    function accept() {
        const it = items[index]
        if (!it || it.stepper) return
        if (onPick) onPick(it)
        hide()
    }

    // dim the panel behind
    Rectangle { anchors.fill: parent; color: "#000"; opacity: 0.55
        MouseArea { anchors.fill: parent; onClicked: ch.hide() } }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: 560 * ch.s
        height: col.height + 48 * ch.s
        color: T.popup
        border.color: T.tileBorder
        Column {
            id: col
            x: 24 * ch.s; y: 24 * ch.s
            width: parent.width - 48 * ch.s
            spacing: 10 * ch.s
            Text { text: ch.title; color: T.text; font.pixelSize: 32 * ch.s; font.bold: true; font.italic: true
                   bottomPadding: 8 * ch.s }
            Repeater {
                model: ch.items
                Rectangle {
                    width: col.width
                    height: 76 * ch.s
                    color: index === ch.index ? "#1b2a36" : "#0d0d10"
                    border.color: index === ch.index ? T.accent : T.tileBorder
                    border.width: index === ch.index ? 3 * ch.s : 1
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 22 * ch.s
                        text: modelData.label
                        color: T.text
                        font.pixelSize: 28 * ch.s
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right; anchors.rightMargin: 22 * ch.s
                        text: modelData.stepper ? "◀  " + modelData.value + (modelData.unit || "") + "  ▶"
                                                : (modelData.sub || "")
                        color: modelData.selected || modelData.stepper ? T.accent : T.dim
                        font.pixelSize: 26 * ch.s
                    }
                    Rectangle {
                        visible: !!modelData.selected
                        width: 6 * ch.s; height: parent.height - 20 * ch.s
                        anchors.verticalCenter: parent.verticalCenter
                        color: T.accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: (m) => {
                            ch.index = index
                            if (modelData.stepper) ch.adjust(m.x > width / 2 ? 1 : -1)
                            else ch.accept()
                        }
                    }
                }
            }
            Text {
                text: "Ⓐ Select    Ⓑ Back" + (ch.items.some(i => i.stepper) ? "    ◀ ▶ Adjust" : "")
                color: T.dim; font.pixelSize: 22 * ch.s; topPadding: 8 * ch.s
            }
        }
    }
}
