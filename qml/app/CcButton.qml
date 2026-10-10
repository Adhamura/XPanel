import QtQuick
import "../Theme.js" as T

// Button. Gamepad: A = activate().
Rectangle {
    id: b
    property string text
    property string icon
    property bool primary: false
    property bool danger: false
    property bool enabledButton: true
    signal clicked()
    function activate() { if (enabledButton) clicked() }

    activeFocusOnTab: enabledButton
    implicitWidth: Math.max(140, row.implicitWidth + 36); implicitHeight: 48
    radius: 3
    opacity: enabledButton ? 1 : 0.4
    color: ma.pressed ? "#2a2a33" : (primary ? "#1b3446" : (danger ? "#3a1016" : "#1c1c22"))
    border.color: activeFocus ? T.accent : (primary ? T.accent : (danger ? T.warn : "#3a3a44"))
    border.width: activeFocus ? 3 : 1
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Image { visible: !!b.icon; source: b.icon ? "qrc:/qml/icons/" + b.icon + ".svg" : ""
                width: 24; height: 24; sourceSize: Qt.size(48, 48); anchors.verticalCenter: parent.verticalCenter }
        Text { text: b.text; color: T.text; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter }
    }
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
    MouseArea { id: ma; anchors.fill: parent; onClicked: { b.forceActiveFocus(); b.activate() } }
}
