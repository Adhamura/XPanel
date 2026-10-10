import QtQuick
import "../Theme.js" as T

// Text field (search, Wi-Fi password, feedback, shortcut keys).
Rectangle {
    id: f
    property string text: ""
    property string placeholder
    property bool password: false
    property bool multiline: false
    signal accepted()
    readonly property Item editor: multiline ? area : line
    function activate() { editor.forceActiveFocus() }

    activeFocusOnTab: true
    onActiveFocusChanged: if (activeFocus) editor.forceActiveFocus()
    implicitWidth: 420; implicitHeight: multiline ? 180 : 48
    radius: 3
    color: "#101014"
    border.color: editor.activeFocus ? T.accent : "#3a3a44"
    border.width: editor.activeFocus ? 3 : 1
    clip: true

    TextInput {
        id: line
        visible: !f.multiline; enabled: !f.multiline
        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
        verticalAlignment: TextInput.AlignVCenter
        color: T.text; font.pixelSize: 18
        echoMode: f.password ? TextInput.Password : TextInput.Normal
        text: f.text
        onTextEdited: f.text = text
        onAccepted: f.accepted()
    }
    TextEdit {
        id: area
        visible: f.multiline; enabled: f.multiline
        anchors.fill: parent; anchors.margins: 12
        color: T.text; font.pixelSize: 18; wrapMode: TextEdit.Wrap
        text: f.text
        onTextChanged: if (f.text !== text) f.text = text
    }
    Text {
        visible: !f.text && !f.editor.activeFocus
        text: f.placeholder; color: "#5d5d66"; font.pixelSize: 17
        x: 14; y: f.multiline ? 12 : (f.height - height) / 2
    }
    MouseArea { anchors.fill: parent; onClicked: f.editor.forceActiveFocus() }
}
