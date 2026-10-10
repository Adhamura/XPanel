import QtQuick
import "../Theme.js" as T

// Settings > Calibration: sticks, triggers, gyro. Measured by XPanel's input layer.
CcPage {
    id: cal
    readonly property string gyroState: (device.rev, device.value("gyro") || "")
    readonly property string gyroResult: {
        const r = (device.rev, device.value("gyroCalibration") || "")
        return ({ "running": "Measuring…", "done": "Done. The offset is saved.", "moved": "The Ally moved. Try again.",
                  "busy": "The motion sensor is used by another program.", "no data": "No data from the motion sensor.",
                  "no device": "No motion sensor found." })[r] || ""
    }
    CcSection { title: "Calibration"
                note: "Gyro calibration measures the motion sensor's offset at rest, so the aim does not drift. " +
                      "Stick and trigger calibration come later; until then the dead zones and trigger ranges in Control Mode do that job." }
    Repeater {
        model: [["Left stick", "Move the stick in full circles, then let it go."],
                ["Right stick", "Move the stick in full circles, then let it go."],
                ["Left trigger", "Pull the trigger fully a few times."],
                ["Right trigger", "Pull the trigger fully a few times."]]
        CcRow { title: modelData[0]; description: modelData[1]
            CcButton { text: "Start"; enabledButton: false } }
    }
    CcRow { title: "Gyro"
            description: "Put the Ally on a flat table and do not touch it, then press Start (about 1 second)."
                         + (cal.gyroResult ? "  " + cal.gyroResult : "")
                         + (cal.gyroState === "no device" && !cal.gyroResult ? "  No motion sensor found." : "")
                         + (cal.gyroState === "" ? "  Needs xpanel-daemon (Linux)." : "")
        CcButton { text: "Start"; enabledButton: cal.gyroState === "ready" || cal.gyroState === "active"
                   onClicked: device.calibrateGyro() } }
    CcRow { title: "Stick and trigger limits for now"; description: "Dead zones and trigger ranges in Control Mode work as a calibration."
        CcButton { text: "Open Control Mode"; onClicked: app.tab = "control" } }
}
