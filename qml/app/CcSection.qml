import QtQuick
import "../Theme.js" as T

// Section heading inside a page.
Item {
    property string title
    property string note
    width: parent ? parent.width : 600
    implicitHeight: col.implicitHeight + 18
    Column {
        id: col
        y: 12
        width: parent.width
        spacing: 4
        Row {
            spacing: 10
            Rectangle { width: 4; height: 24; color: T.accent; anchors.verticalCenter: parent.verticalCenter }
            Text { text: title; color: T.text; font.pixelSize: 22; font.bold: true; font.italic: true }
        }
        Text { visible: !!note; text: note; color: T.dim; font.pixelSize: 15; width: parent.width; wrapMode: Text.WordWrap }
    }
}
