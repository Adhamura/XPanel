import QtQuick
import "../Theme.js" as T

// Media Gallery: screenshots and recordings. Filter, select, open, share, delete (to the trash).
Item {
    id: page
    property string filter: "all"
    property var selected: []
    property bool selecting: false
    readonly property var shown: gallery.items.filter(m =>
        filter === "all" || (filter === "screenshots" && m.screenshot) || (filter === "videos" && m.video)
        || (filter === "pictures" && !m.video))
    Component.onCompleted: gallery.rescan()

    function toggle(path) {
        const s = selected.slice(); const i = s.indexOf(path)
        if (i >= 0) s.splice(i, 1); else s.push(path)
        selected = s
    }

    Row {
        id: bar
        x: 32; y: 20; spacing: 12
        CcChoice {
            options: [{ value: "all", label: qsTr("All") }, { value: "screenshots", label: qsTr("Screenshots") },
                      { value: "pictures", label: qsTr("Pictures") }, { value: "videos", label: qsTr("Videos") }]
            current: page.filter; onPicked: (v) => { page.filter = v; page.selected = [] }
        }
        CcButton { text: page.selecting ? qsTr("Done selecting") : qsTr("Select"); primary: page.selecting
                   onClicked: { page.selecting = !page.selecting; page.selected = [] } }
        CcButton { visible: page.selecting; text: qsTr("Select all"); onClicked: page.selected = page.shown.map(m => m.path) }
        CcButton { visible: page.selecting; text: qsTr("Share (%1)").arg(page.selected.length); enabledButton: page.selected.length > 0
                   onClicked: { gallery.copyToClipboard(page.selected); app.note(qsTr("%n file(s) copied to the clipboard", "", page.selected.length)) } }
        CcButton { visible: page.selecting; text: qsTr("Delete (%1)").arg(page.selected.length); danger: true; enabledButton: page.selected.length > 0
                   onClicked: confirm.show() }
        CcButton { text: qsTr("Refresh"); onClicked: gallery.rescan() }
    }
    Text { anchors.right: parent.right; anchors.rightMargin: 32; y: 32
           text: qsTr("%n item(s)  ·  ~/Pictures, ~/Videos", "", page.shown.length); color: T.dim; font.pixelSize: 16 }

    GridView {
        id: grid
        anchors.top: bar.bottom; anchors.topMargin: 20; anchors.left: parent.left; anchors.leftMargin: 32
        anchors.right: parent.right; anchors.bottom: parent.bottom
        clip: true; model: page.shown
        cellWidth: 330; cellHeight: 230
        delegate: Item {
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                id: card
                readonly property bool sel: page.selected.indexOf(modelData.path) >= 0
                width: grid.cellWidth - 20; height: grid.cellHeight - 20
                color: "#15151b"; radius: 3
                border.color: activeFocus ? T.accent : (sel ? T.accent : "#2c2c33"); border.width: activeFocus || sel ? 3 : 1
                activeFocusOnTab: true
                onActiveFocusChanged: if (activeFocus) grid.positionViewAtIndex(index, GridView.Contain)
                function activate() { page.selecting ? page.toggle(modelData.path) : viewer.show(modelData) }
                function secondary() { page.selecting = true; page.toggle(modelData.path) }
                Image {
                    x: 4; y: 4; width: parent.width - 8; height: parent.height - 40
                    visible: !modelData.video
                    source: modelData.video ? "" : modelData.url
                    sourceSize: Qt.size(400, 260); fillMode: Image.PreserveAspectCrop; asynchronous: true
                }
                Text { visible: modelData.video; anchors.centerIn: parent; text: "▶"; color: T.text; font.pixelSize: 56 }
                Text { x: 10; anchors.bottom: parent.bottom; anchors.bottomMargin: 10; width: parent.width - 20
                       text: modelData.date + "   " + modelData.name; color: T.dim; font.pixelSize: 13; elide: Text.ElideRight }
                Rectangle { visible: page.selecting; x: parent.width - 34; y: 8; width: 24; height: 24; radius: 12
                            color: card.sel ? T.accent : "#000000aa"; border.color: "#ffffff"
                            Text { anchors.centerIn: parent; text: card.sel ? "✓" : ""; color: "#fff"; font.pixelSize: 15 } }
                MouseArea { anchors.fill: parent; onClicked: { card.forceActiveFocus(); card.activate() } }
            }
        }
        Text { anchors.centerIn: parent; visible: page.shown.length === 0
               text: qsTr("No pictures or videos yet.\nScreenshots from the Quick Panel appear here.")
               horizontalAlignment: Text.AlignHCenter; color: T.dim; font.pixelSize: 20 }
    }

    // full view
    Rectangle {
        id: viewer
        property var item: null
        anchors.fill: parent; color: "#f5050507"; visible: !!item
        function show(m) { item = m; app.pushBack(() => viewer.item = null); Qt.callLater(() => openBtn.forceActiveFocus()) }
        MouseArea { anchors.fill: parent }
        Image { anchors.fill: parent; anchors.margins: 30; anchors.bottomMargin: 100
                source: viewer.item && !viewer.item.video ? viewer.item.url : ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 26
            spacing: 12
            CcButton { id: openBtn; text: viewer.item && viewer.item.video ? qsTr("Play") : qsTr("Open"); primary: true
                       onClicked: gallery.openFile(viewer.item.path) }
            CcButton { text: qsTr("Share"); onClicked: { gallery.copyToClipboard([viewer.item.path]); app.note(qsTr("Copied to the clipboard: %1").arg(viewer.item.name)) } }
            CcButton { text: qsTr("Show in folder"); onClicked: gallery.showInFolder(viewer.item.path) }
            CcButton { text: qsTr("Delete"); danger: true; onClicked: { page.selected = [viewer.item.path]; confirm.show() } }
            CcButton { text: qsTr("Back"); onClicked: { viewer.item = null; app.popBack() } }
        }
    }

    // delete confirmation
    Rectangle {
        id: confirm
        anchors.fill: parent; color: "#d0000000"; visible: false
        function show() { visible = true; app.pushBack(() => confirm.visible = false); Qt.callLater(() => noBtn.forceActiveFocus()) }
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent; width: 560; height: 210; color: T.popup; border.color: T.tileBorder
            Text { x: 28; y: 26; width: parent.width - 56; wrapMode: Text.WordWrap; color: T.text; font.pixelSize: 20
                   text: qsTr("Move %n item(s) to the trash?", "", page.selected.length) + "\n" + qsTr("You can restore them from the trash later.") }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 24
                spacing: 12
                CcButton { id: noBtn; text: qsTr("Cancel"); onClicked: { confirm.visible = false; app.popBack() } }
                CcButton { text: qsTr("Move to trash"); danger: true; onClicked: {
                        gallery.trash(page.selected); app.note(qsTr("%n item(s) moved to the trash", "", page.selected.length))
                        page.selected = []; confirm.visible = false; app.popBack()
                        if (viewer.item) { viewer.item = null; app.popBack() } } }
            }
        }
    }
}
