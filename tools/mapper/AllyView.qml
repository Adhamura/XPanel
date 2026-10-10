import QtQuick

// Schematic ROG Ally X with clickable controls. Design space: 1000 x 440.
// side: "front" (with the top edge controls) or "back" (seen from behind, so it is mirrored).
Item {
    id: view
    property string side: "front"
    property var assigned: ({})     // control id -> true when some input is mapped to it
    property var active: ({})       // control id -> true while its input is pressed / moved
    property string highlight: ""   // control of the selected list row
    property string armed: ""       // control waiting for its input (blinks)
    property int rev: 0             // bump to refresh assigned / active
    signal picked(string control)

    readonly property real k: Math.min(width / 1000, height / 440)
    implicitWidth: 1000; implicitHeight: 440

    readonly property var frontControls: [
        // top edge (seen from the front: triggers behind, bumpers in front)
        { id: "LT", label: "LT", x: 95, y: 6, w: 130, h: 26 },
        { id: "LB", label: "LB", x: 95, y: 36, w: 130, h: 24 },
        { id: "RT", label: "RT", x: 775, y: 6, w: 130, h: 26 },
        { id: "RB", label: "RB", x: 775, y: 36, w: 130, h: 24 },
        { id: "VolumeDown", label: "Vol−", x: 590, y: 30, w: 50, h: 22 },
        { id: "VolumeUp", label: "Vol+", x: 644, y: 30, w: 50, h: 22 },
        { id: "Power", label: "Power", x: 700, y: 30, w: 64, h: 22 },
        // left side
        { id: "LeftStick", label: "L stick", x: 105, y: 105, w: 90, h: 90, round: true },
        { id: "DPadUp", label: "↑", x: 136, y: 222, w: 30, h: 36 },
        { id: "DPadDown", label: "↓", x: 136, y: 290, w: 30, h: 36 },
        { id: "DPadLeft", label: "←", x: 98, y: 258, w: 38, h: 32 },
        { id: "DPadRight", label: "→", x: 166, y: 258, w: 38, h: 32 },
        { id: "View", label: "View", x: 214, y: 112, w: 34, h: 34, round: true },
        { id: "LeftMenu", label: "LM", x: 214, y: 300, w: 34, h: 34, round: true },
        // right side
        { id: "Y", label: "Y", x: 835, y: 92, w: 42, h: 42, round: true },
        { id: "X", label: "X", x: 790, y: 134, w: 42, h: 42, round: true },
        { id: "B", label: "B", x: 880, y: 134, w: 42, h: 42, round: true },
        { id: "A", label: "A", x: 835, y: 176, w: 42, h: 42, round: true },
        { id: "RightStick", label: "R stick", x: 805, y: 240, w: 90, h: 90, round: true },
        { id: "Menu", label: "Menu", x: 752, y: 112, w: 34, h: 34, round: true },
        { id: "RightMenu", label: "RM", x: 752, y: 300, w: 34, h: 34, round: true }
    ]
    readonly property var backControls: [
        // seen from behind: the player's left is on the right of this picture
        { id: "RT", label: "RT", x: 95, y: 22, w: 130, h: 30 },
        { id: "LT", label: "LT", x: 775, y: 22, w: 130, h: 30 },
        { id: "RightPaddle", label: "R paddle", x: 120, y: 285, w: 110, h: 34 },
        { id: "LeftPaddle", label: "L paddle", x: 770, y: 285, w: 110, h: 34 }
    ]
    readonly property var controls: side === "front" ? frontControls : backControls

    Item {
        id: art
        width: 1000 * view.k; height: 440 * view.k
        anchors.centerIn: parent

        Canvas {
            anchors.fill: parent
            onPaint: {
                const c = getContext("2d"), k = view.k
                c.reset(); c.scale(k, k)
                c.lineWidth = 2
                // grips
                c.fillStyle = "#1c1c22"; c.strokeStyle = "#5a5a64"
                for (const gx of [160, 840]) {
                    c.beginPath(); c.ellipse(gx - 120, 230, 240, 200); c.fill(); c.stroke()
                }
                // body
                c.beginPath(); c.roundedRect(50, 50, 900, 300, 90, 90); c.fill(); c.stroke()
                if (view.side === "front") {
                    c.fillStyle = "#050507"; c.strokeStyle = "#3a3a44"
                    c.beginPath(); c.roundedRect(265, 82, 470, 266, 10, 10); c.fill(); c.stroke()
                    c.fillStyle = "#5a5a64"; c.font = "22px sans-serif"; c.textAlign = "center"
                    c.fillText("screen", 500, 222)
                    // d-pad cross
                    c.fillStyle = "#2a2a32"
                    c.fillRect(136, 222, 30, 104); c.fillRect(98, 258, 106, 32)
                } else {
                    // vents and stand area on the back
                    c.strokeStyle = "#33333b"
                    for (let i = 0; i < 9; ++i) { c.beginPath(); c.moveTo(380 + i * 30, 70); c.lineTo(380 + i * 30, 120); c.stroke() }
                    c.fillStyle = "#5a5a64"; c.font = "22px sans-serif"; c.textAlign = "center"
                    c.fillText("back (seen from behind)", 500, 222)
                }
            }
            Connections { target: view; function onKChanged() { parent.requestPaint() } }
            Component.onCompleted: requestPaint()
        }

        Repeater {
            model: view.controls
            Rectangle {
                readonly property bool isActive: view.rev >= 0 && !!view.active[modelData.id]
                readonly property bool isAssigned: view.rev >= 0 && !!view.assigned[modelData.id]
                readonly property bool isHighlight: view.highlight === modelData.id || view.armed === modelData.id
                x: modelData.x * view.k; y: modelData.y * view.k
                width: modelData.w * view.k; height: modelData.h * view.k
                radius: modelData.round ? width / 2 : 5 * view.k
                color: isActive ? "#ffb020" : (isHighlight ? "#1f6f99" : (hover.containsMouse ? "#3a3a46" : "#26262e"))
                border.color: isHighlight ? "#1fb6ff" : (isAssigned ? "#1fb6ff" : "#7a7a86")
                border.width: (isHighlight ? 3 : (isAssigned ? 2 : 1)) * Math.max(1, view.k)
                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: "#f2f2f2"
                    font.pixelSize: Math.max(9, Math.min(parent.height * 0.5, 20 * view.k))
                    font.bold: parent.isAssigned
                }
                SequentialAnimation on opacity {
                    running: view.armed === modelData.id
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) parent.opacity = 1
                    NumberAnimation { to: 0.35; duration: 350 }
                    NumberAnimation { to: 1; duration: 350 }
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: view.picked(modelData.id)
                }
            }
        }
    }
}
