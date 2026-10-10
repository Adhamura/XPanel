import QtQuick
import "../Theme.js" as T

// On / off switch. Gamepad: A toggles, left / right set off / on.
Item {
    id: sw
    property bool checked: false
    property bool enabledSwitch: true
    signal toggled(bool on)
    function activate() { if (enabledSwitch) toggled(!checked) }
    function step(d) { if (enabledSwitch && (d > 0) !== checked) toggled(d > 0) }

    activeFocusOnTab: enabledSwitch
    implicitWidth: 76; implicitHeight: 40
    opacity: enabledSwitch ? 1 : 0.4
    Rectangle {
        anchors.fill: parent; anchors.margins: -4; radius: height / 2
        color: "transparent"; border.color: T.accent; border.width: 3; visible: sw.activeFocus
    }
    Rectangle {
        anchors.fill: parent; radius: height / 2
        color: sw.checked ? "#1f6f99" : "#2a2a33"
        border.color: sw.checked ? T.accent : "#4a4a55"
        Rectangle {
            width: parent.height - 8; height: width; radius: width / 2
            y: 4; x: sw.checked ? parent.width - width - 4 : 4
            color: sw.checked ? "#ffffff" : "#9a9aa4"
            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
    MouseArea { anchors.fill: parent; onClicked: { sw.forceActiveFocus(); sw.activate() } }
}
