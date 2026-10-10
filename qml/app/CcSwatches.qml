import QtQuick
import "../Theme.js" as T

// A row of colour swatches. Gamepad: left / right move between them, A picks.
Row {
    id: sw
    property string current: ""
    property var colors: ["#ff0014", "#ff6a00", "#ffd000", "#3cff00", "#00c8ff", "#0040ff", "#9600ff", "#ff00b4", "#ffffff"]
    signal picked(string color)
    spacing: 8
    Repeater {
        model: sw.colors
        Rectangle {
            width: 40; height: 40; radius: 20; color: modelData
            border.color: activeFocus ? T.accent : (sw.current.toLowerCase() === modelData ? "#ffffff" : "#000000")
            border.width: activeFocus || sw.current.toLowerCase() === modelData ? 4 : 1
            activeFocusOnTab: true
            function activate() { sw.picked(modelData) }
            Keys.onReturnPressed: activate()
            Keys.onSpacePressed: activate()
            MouseArea { anchors.fill: parent; onClicked: { parent.forceActiveFocus(); parent.activate() } }
        }
    }
}
