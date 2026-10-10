import QtQuick
import "../Theme.js" as T

// One setting line: title (+ optional description) on the left, the control on the right.
Item {
    id: r
    property string title
    property string description
    default property alias control: holder.data
    width: parent ? parent.width : 600
    implicitHeight: Math.max(64, texts.implicitHeight + 20, holder.childrenRect.height + 16)
    Rectangle { anchors.fill: parent; color: "#131318"; radius: 3 }
    Column {
        id: texts
        x: 20; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - holder.width - 60
        spacing: 3
        Text { text: r.title; color: T.text; font.pixelSize: 19; width: parent.width; wrapMode: Text.WordWrap }
        Text { visible: !!r.description; text: r.description; color: T.dim; font.pixelSize: 14
               width: parent.width; wrapMode: Text.WordWrap }
    }
    Item {
        id: holder
        anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width; height: childrenRect.height
    }
}
