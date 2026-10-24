import QtQuick

// Small always-on-top Real-time Monitor in the top-right corner.
Rectangle {
    width: 800; height: 44
    color: "#c0000000"
    radius: 6
    StatsStrip {
        anchors.centerIn: parent
        s: 0.72
    }
}
