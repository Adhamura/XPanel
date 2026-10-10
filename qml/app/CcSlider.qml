import QtQuick
import "../Theme.js" as T

// Horizontal slider. Gamepad: left / right step.
Item {
    id: sl
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value: 0
    property string unit: ""
    property bool enabledSlider: true
    property bool showValue: true
    signal moved(real value)
    function step(d) {
        if (!enabledSlider) return
        const v = Math.max(from, Math.min(to, value + d * stepSize))
        if (v !== value) moved(v)
    }
    function activate() {}

    activeFocusOnTab: enabledSlider
    implicitWidth: 420; implicitHeight: 40
    opacity: enabledSlider ? 1 : 0.4
    readonly property real frac: to > from ? (value - from) / (to - from) : 0

    Rectangle {
        id: track
        anchors.left: parent.left; anchors.right: valueText.left; anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        height: 6; radius: 3; color: "#2e2e36"
        Rectangle { width: parent.width * sl.frac; height: parent.height; radius: 3; color: T.accent }
        Rectangle {
            x: parent.width * sl.frac - width / 2; anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 24; radius: 12; color: "#e8e8ec"
            border.color: sl.activeFocus ? T.accent : "#8a8a94"; border.width: sl.activeFocus ? 4 : 1
        }
        MouseArea {
            anchors.fill: parent; anchors.margins: -14
            preventStealing: true
            function apply(mx) {
                const f = Math.max(0, Math.min(1, (mx - 14) / track.width))
                const raw = sl.from + f * (sl.to - sl.from)
                sl.moved(Math.round(raw / sl.stepSize) * sl.stepSize)
            }
            onPressed: (m) => { sl.forceActiveFocus(); apply(m.x) }
            onPositionChanged: (m) => { if (pressed) apply(m.x) }
        }
    }
    Text {
        id: valueText
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: sl.showValue ? 90 : 0; visible: sl.showValue
        horizontalAlignment: Text.AlignRight
        text: (sl.stepSize < 1 ? sl.value.toFixed(2) : Math.round(sl.value)) + sl.unit
        color: T.accent; font.pixelSize: 18
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
}
