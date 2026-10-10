import QtQuick
import "../Theme.js" as T

// Settings > Calibration: sticks, triggers, gyro. Measured by XPanel's input layer.
CcPage {
    id: cal
    readonly property string gyroState: (device.rev, device.value("gyro") || "")
    readonly property string gyroResult: {
        const r = (device.rev, device.value("gyroCalibration") || "")
        return ({ "running": qsTr("Measuring…"), "done": qsTr("Done. The offset is saved."), "moved": qsTr("The Ally moved. Try again."),
                  "busy": qsTr("The motion sensor is used by another program."), "no data": qsTr("No data from the motion sensor."),
                  "no device": qsTr("No motion sensor found.") })[r] || ""
    }
    CcSection { title: qsTr("Calibration")
                note: qsTr("Gyro calibration measures the motion sensor's offset at rest, so the aim does not drift. " +
                           "Stick and trigger calibration come later; until then the dead zones and trigger ranges in Control Mode do that job.") }
    Repeater {
        model: [[qsTr("Left stick"), qsTr("Move the stick in full circles, then let it go.")],
                [qsTr("Right stick"), qsTr("Move the stick in full circles, then let it go.")],
                [qsTr("Left trigger"), qsTr("Pull the trigger fully a few times.")],
                [qsTr("Right trigger"), qsTr("Pull the trigger fully a few times.")]]
        CcRow { title: modelData[0]; description: modelData[1]
            CcButton { text: qsTr("Start"); enabledButton: false } }
    }
    CcRow { title: qsTr("Gyro")
            description: qsTr("Put the Ally on a flat table and do not touch it, then press Start (about 1 second).")
                         + (cal.gyroResult ? "  " + cal.gyroResult : "")
                         + (cal.gyroState === "no device" && !cal.gyroResult ? "  " + qsTr("No motion sensor found.") : "")
                         + (cal.gyroState === "" ? "  " + qsTr("Needs xpanel-daemon (Linux).") : "")
        CcButton { text: qsTr("Start"); enabledButton: cal.gyroState === "ready" || cal.gyroState === "active"
                   onClicked: device.calibrateGyro() } }
    CcRow { title: qsTr("Stick and trigger limits for now"); description: qsTr("Dead zones and trigger ranges in Control Mode work as a calibration.")
        CcButton { text: qsTr("Open Control Mode"); onClicked: app.tab = "control" } }
}
