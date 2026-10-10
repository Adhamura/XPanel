import QtQuick
import "../Theme.js" as T

// Settings > Feedback: notes about XPanel, kept in a file for the next version.
CcPage {
    id: page
    property string topic: "Idea"
    property string msg
    CcSection { title: "Feedback"; note: "Write what to fix or add. Notes are saved to " + tweaks.feedbackPath() }
    CcRow { title: "Type"
        CcChoice { options: [{ value: "Idea", label: "Idea" }, { value: "Problem", label: "Problem" }, { value: "Other", label: "Other" }]
                   current: page.topic; onPicked: (v) => page.topic = v } }
    CcField { id: fbText; width: parent.width; multiline: true; placeholder: "Your feedback…" }
    Row { spacing: 12
        CcButton { text: "Save"; primary: true
                   onClicked: { if (tweaks.saveFeedback(page.topic, fbText.text)) { page.msg = "Saved. Thank you."; fbText.text = "" } else page.msg = "Write something first." } }
        Text { text: page.msg; color: T.dim; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter } }
}
