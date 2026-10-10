import QtQuick
import "../Theme.js" as T

// Settings > Feedback: notes about XPanel, kept in a file for the next version.
CcPage {
    id: page
    property string topic: "Idea"
    property string msg
    CcSection { title: qsTr("Feedback"); note: qsTr("Write what to fix or add. Notes are saved to %1").arg(tweaks.feedbackPath()) }
    CcRow { title: qsTr("Type")
        CcChoice { options: [{ value: "Idea", label: qsTr("Idea") }, { value: "Problem", label: qsTr("Problem") }, { value: "Other", label: qsTr("Other") }]
                   current: page.topic; onPicked: (v) => page.topic = v } }
    CcField { id: fbText; width: parent.width; multiline: true; placeholder: qsTr("Your feedback…") }
    Row { spacing: 12
        CcButton { text: qsTr("Save"); primary: true
                   onClicked: { if (tweaks.saveFeedback(page.topic, fbText.text)) { page.msg = qsTr("Saved. Thank you."); fbText.text = "" } else page.msg = qsTr("Write something first.") } }
        Text { text: page.msg; color: T.dim; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter } }
}
