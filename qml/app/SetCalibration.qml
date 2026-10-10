import QtQuick
import "../Theme.js" as T

// Settings > Calibration: sticks, triggers, gyro. Measured by XPanel's input layer.
CcPage {
    CcSection { title: "Calibration"
                note: "Calibration reads the raw stick, trigger and gyro values. That needs XPanel's own input layer, " +
                      "the next part of the project. The steps below become active then." }
    Repeater {
        model: [["Left stick", "Move the stick in full circles, then let it go."],
                ["Right stick", "Move the stick in full circles, then let it go."],
                ["Left trigger", "Pull the trigger fully a few times."],
                ["Right trigger", "Pull the trigger fully a few times."],
                ["Gyro", "Put the Ally on a flat table and do not touch it."]]
        CcRow { title: modelData[0]; description: modelData[1]
            CcButton { text: "Start"; enabledButton: false } }
    }
    CcRow { title: "Stick and trigger limits for now"; description: "Dead zones and trigger ranges in Control Mode work as a calibration once the input layer runs."
        CcButton { text: "Open Control Mode"; onClicked: app.tab = "control" } }
}
