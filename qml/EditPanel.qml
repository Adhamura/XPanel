import QtQuick
import "Theme.js" as T

// "Edit Quick Panel": every tile in the catalog; tap to add or remove.
Rectangle {
    id: ed
    property real s: 1
    property var catalog: []      // [{id, title, icon}]
    property int index: 0
    property bool active: false
    color: "#121216"
    opacity: 0.97

    readonly property int cols: 3
    function move(dx, dy) {
        let i = index + dx + dy * cols
        index = Math.max(0, Math.min(catalog.length - 1, i))
        flick.ensureVisible(index)
    }
    function accept() { if (catalog[index]) config.toggleTile(catalog[index].id) }

    Text {
        x: 44 * ed.s; y: 36 * ed.s
        text: qsTr("Edit Quick Panel")
        color: T.text; font.pixelSize: 34 * ed.s; font.bold: true; font.italic: true
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 44 * ed.s; y: 46 * ed.s
        text: qsTr("Reset")
        color: T.accent; font.pixelSize: 24 * ed.s
        MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: config.resetTiles() }
    }

    Flickable {
        id: flick
        x: 44 * ed.s; y: 110 * ed.s
        width: parent.width - 88 * ed.s
        height: parent.height - y - 80 * ed.s
        clip: true
        contentHeight: grid.height + 20 * ed.s
        function ensureVisible(i) {
            const row = Math.floor(i / ed.cols)
            const top = row * (cellH + gap), bottom = top + cellH
            if (top < contentY) contentY = top
            else if (bottom > contentY + height) contentY = bottom - height + 10 * ed.s
        }
        readonly property real gap: 30 * ed.s
        readonly property real cellW: (width - 2 * gap - 20 * ed.s) / 3
        readonly property real cellH: cellW * 0.98

        Grid {
            id: grid
            x: 10 * ed.s; y: 10 * ed.s
            columns: ed.cols
            spacing: flick.gap
            Repeater {
                model: ed.catalog
                Item {
                    width: flick.cellW; height: flick.cellH
                    readonly property bool on: config.tiles.indexOf(modelData.id) >= 0
                    Tile {
                        anchors.fill: parent
                        s: ed.s * 0.85
                        icon: modelData.icon
                        title: modelData.title
                        enabledTile: parent.on
                        focused: ed.active && index === ed.index
                        onClicked: { ed.index = index; config.toggleTile(modelData.id) }
                    }
                    Rectangle {
                        width: 40 * ed.s; height: width; radius: width / 2
                        x: parent.width - width / 2 - 10 * ed.s; y: -width / 2 + 10 * ed.s
                        color: parent.on ? "#3a3a40" : T.accent
                        // "+" (add) or "✕" (remove), drawn as two bars so it sits exactly in the centre
                        Repeater {
                            model: 2
                            Rectangle {
                                anchors.centerIn: parent
                                width: 18 * ed.s; height: 3 * ed.s; radius: height / 2
                                color: T.text
                                rotation: (parent.parent.on ? 45 : 0) + index * 90
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.right: parent.right; anchors.rightMargin: 44 * ed.s
        anchors.bottom: parent.bottom; anchors.bottomMargin: 28 * ed.s
        text: qsTr("Ⓐ Add / Remove    Ⓨ Done")
        color: T.text; font.pixelSize: 26 * ed.s
    }
}
