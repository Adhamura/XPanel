import QtQuick
import QtQuick.Window

// Scrolling page body. Keeps the focused control in view (gamepad navigation).
Flickable {
    id: page
    default property alias content: col.data
    property real margin: 32
    contentWidth: width
    contentHeight: col.implicitHeight + 2 * margin
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Column {
        id: col
        x: page.margin; y: page.margin
        width: page.width - 2 * page.margin
        spacing: 10
    }
    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            const it = page.Window.window ? page.Window.window.activeFocusItem : null
            if (!it) return
            // is it inside this page?
            let p = it
            while (p && p !== col) p = p.parent
            if (!p) return
            const pos = it.mapToItem(col, 0, 0)
            const top = pos.y + page.margin - 20, bottom = pos.y + it.height + page.margin + 20
            if (top < page.contentY) page.contentY = Math.max(0, top)
            else if (bottom > page.contentY + page.height) page.contentY = Math.min(page.contentHeight - page.height, bottom - page.height)
        }
    }
    Behavior on contentY { NumberAnimation { duration: 120 } }
}
