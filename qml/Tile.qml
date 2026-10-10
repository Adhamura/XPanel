import QtQuick
import "Theme.js" as T

// One Quick Panel tile: icon, optional red value line, title.
Item {
    id: tile
    property real s: 1
    property string icon
    property string title
    property string value
    property string badge         // small red text over the icon, e.g. "13W"
    property bool focused: false
    property bool enabledTile: true
    signal clicked()

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed ? "#1a1a20" : T.tile
        border.color: T.tileBorder
        border.width: 1
        opacity: tile.enabledTile ? 1 : 0.45
    }

    // bracket corners at the bottom, like the original tiles
    Repeater {
        model: 2
        Item {
            x: index === 0 ? 0 : tile.width - width
            y: tile.height - height
            width: 14 * tile.s; height: 14 * tile.s
            Rectangle { width: 1.5 * tile.s; height: parent.height; color: T.corner; x: index === 0 ? 0 : parent.width - width }
            Rectangle { height: 1.5 * tile.s; width: parent.width; color: T.corner; y: parent.height - height }
        }
    }

    // focus ring with glow
    Rectangle {
        anchors.fill: parent
        anchors.margins: -7 * tile.s
        color: "transparent"
        border.color: T.accent
        border.width: 3.5 * tile.s
        radius: 2 * tile.s
        visible: tile.focused
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3 * tile.s
            color: "transparent"
            border.color: T.accent
            border.width: 2 * tile.s
            opacity: 0.25
        }
    }

    Image {
        id: img
        source: tile.icon ? "qrc:/qml/icons/" + tile.icon + ".svg" : ""
        width: 70 * tile.s; height: 70 * tile.s
        sourceSize: Qt.size(width * 2, height * 2)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 42 * tile.s
        opacity: tile.enabledTile ? 1 : 0.6
    }
    Text {
        text: tile.badge
        visible: text.length > 0
        color: T.accent
        font.pixelSize: 22 * tile.s
        font.bold: true
        font.italic: true
        anchors.left: img.right
        anchors.leftMargin: -14 * tile.s
        anchors.bottom: img.bottom
        anchors.bottomMargin: -4 * tile.s
        style: Text.Outline; styleColor: "#000"
    }

    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 34 * tile.s
        spacing: 6 * tile.s
        Text {
            id: valueText
            width: parent.width
            text: tile.value
            visible: text.length > 0
            color: T.accent
            font.pixelSize: 27 * tile.s
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            leftPadding: 10 * tile.s; rightPadding: 10 * tile.s
            text: tile.title
            color: T.text
            font.pixelSize: 27 * tile.s
            lineHeight: 0.95
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
        }
    }

    // short pulse when the value changes, so the new state is easy to see
    onValueChanged: if (valueText.text.length > 0) pulse.restart()
    SequentialAnimation {
        id: pulse
        NumberAnimation { target: valueText; property: "scale"; to: 1.18; duration: 90; easing.type: Easing.OutQuad }
        NumberAnimation { target: valueText; property: "scale"; to: 1.0; duration: 160; easing.type: Easing.InOutQuad }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: tile.clicked()
    }
}
