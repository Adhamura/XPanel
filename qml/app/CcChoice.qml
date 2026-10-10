import QtQuick
import "../Theme.js" as T

// Segmented choice: options [{value, label}]. Gamepad: left / right move, A next.
Item {
    id: ch
    property var options: []
    property var current
    property bool enabledChoice: true
    signal picked(var value)
    readonly property int index: {
        for (let i = 0; i < options.length; ++i) if (options[i].value === current) return i
        return -1
    }
    function step(d) {
        if (!enabledChoice || options.length === 0) return
        const i = Math.max(0, Math.min(options.length - 1, (index < 0 ? 0 : index) + d))
        if (i !== index) picked(options[i].value)
    }
    function activate() { if (enabledChoice && options.length) picked(options[(index + 1) % options.length].value) }

    activeFocusOnTab: enabledChoice
    implicitWidth: row.implicitWidth; implicitHeight: 44
    opacity: enabledChoice ? 1 : 0.4
    Rectangle {
        anchors.fill: parent; anchors.margins: -5; radius: 4
        color: "transparent"; border.color: T.accent; border.width: 3; visible: ch.activeFocus
    }
    Row {
        id: row
        spacing: 4
        Repeater {
            model: ch.options
            Rectangle {
                width: Math.max(96, lbl.implicitWidth + 28); height: 44; radius: 3
                color: index === ch.index ? "#1b3446" : "#1a1a20"
                border.color: index === ch.index ? T.accent : "#34343d"
                Text { id: lbl; anchors.centerIn: parent; text: modelData.label
                       color: index === ch.index ? "#ffffff" : T.dim; font.pixelSize: 17 }
                MouseArea { anchors.fill: parent; onClicked: { ch.forceActiveFocus(); if (ch.enabledChoice) ch.picked(modelData.value) } }
            }
        }
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
}
