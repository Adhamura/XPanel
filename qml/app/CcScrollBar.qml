import QtQuick
import "../Theme.js" as T

// Vertical scroll bar for a Flickable / ListView / GridView: drag the thumb or tap the track
// (touch and mouse). Shown only when the content is taller than the view.
// Usage (inside the view's parent): CcScrollBar { flickable: myView }
Item {
    id: bar
    property Flickable flickable
    readonly property real range: flickable ? Math.max(0, flickable.contentHeight - flickable.height) : 0
    readonly property real ratio: flickable && flickable.contentHeight > 0 ? Math.min(1, flickable.height / flickable.contentHeight) : 1
    readonly property bool active: hit.pressed || hit.containsMouse || (flickable && flickable.moving)

    visible: flickable && range > 1
    width: 28 // touch area; the visible bar is thinner
    anchors.top: flickable ? flickable.top : undefined
    anchors.bottom: flickable ? flickable.bottom : undefined
    anchors.right: flickable ? flickable.right : undefined
    z: 10

    function scrollTo(y) { // y = the thumb's top inside the track
        const free = track.height - thumb.height
        if (!flickable || free <= 0) return
        flickable.contentY = flickable.originY + Math.max(0, Math.min(1, y / free)) * range
    }

    Rectangle { // track
        id: track
        anchors.right: parent.right; anchors.rightMargin: 6
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.topMargin: 6; anchors.bottomMargin: 6
        width: bar.active ? 10 : 6
        radius: width / 2
        color: "#ffffff"; opacity: bar.active ? 0.10 : 0.05
        Behavior on width { NumberAnimation { duration: 120 } }
    }
    Rectangle { // thumb
        id: thumb
        anchors.horizontalCenter: track.horizontalCenter
        width: track.width; radius: width / 2
        height: Math.max(36, track.height * bar.ratio)
        y: track.y + (bar.range > 0 && bar.flickable
                      ? (bar.flickable.contentY - bar.flickable.originY) / bar.range * (track.height - height) : 0)
        color: hit.pressed ? T.accent : "#ffffff"
        opacity: hit.pressed ? 1 : (bar.active ? 0.55 : 0.30)
        Behavior on width { NumberAnimation { duration: 120 } }
    }
    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true // the Flickable must not take over the drag
        property real grab: -1 // finger offset inside the thumb, -1 = tapped the track
        onPressed: (m) => {
            const ty = thumb.y - track.y
            grab = (m.y - track.y >= ty && m.y - track.y <= ty + thumb.height) ? m.y - track.y - ty : thumb.height / 2
            bar.scrollTo(m.y - track.y - grab)
        }
        onPositionChanged: (m) => { if (pressed) bar.scrollTo(m.y - track.y - grab) }
        onWheel: (w) => { if (bar.flickable) bar.flickable.flick(0, w.angleDelta.y * 8) }
    }
}
