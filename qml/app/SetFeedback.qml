import QtQuick
import "../Theme.js" as T

// Settings > Feedback: a pre-filled GitHub issue (browser or QR code for a phone). XPanel sends
// nothing itself and has no GitHub token: the user submits the issue with their own account.
Item {
    id: root
    CcPage {
        id: page
        anchors.fill: parent
        property string topic: "Idea"
        property bool includeInfo: true
        property string msg
        readonly property string title: fbTitle.text
        readonly property string body: fbText.text
        // what is filled in automatically (the device state may arrive after the page is shown)
        readonly property var infoLines: (device.rev, feedback.infoLines(includeInfo))
        function ready() {
            if (feedback.hasContent(title, body)) return true
            msg = qsTr("Write something first.")
            return false
        }
        function send() {
            if (!ready()) return
            const r = feedback.openInBrowser(topic, title, body, includeInfo)
            msg = !r.opened ? qsTr("Could not open a browser. Use the QR code, or Copy and paste it on github.com.")
                : r.truncated ? qsTr("Opened in your browser. The text was too long for a link and is shortened there; the full text is copied to the clipboard.")
                : qsTr("Opened in your browser. Submit the issue there.")
        }
        function copyText() {
            if (!ready()) return
            feedback.copy(topic, title, body, includeInfo)
            msg = qsTr("Copied to the clipboard.")
        }
        function save() {
            if (!ready()) return
            if (tweaks.saveFeedback(topic, (title ? title + "\n\n" : "") + body)) {
                msg = qsTr("Saved to %1").arg(tweaks.feedbackPath())
                fbTitle.text = ""; fbText.text = ""
            }
        }

        CcSection { title: qsTr("Feedback")
            note: qsTr("Opens a GitHub issue — you need a GitHub account. Nothing is sent until you submit it there.")
                  + (desktop.gamescope ? "\n" + qsTr("In Steam Game Mode a browser is often not available: scan the QR code with your phone.") : "") }
        CcRow { title: qsTr("Type")
            CcChoice { options: [{ value: "Idea", label: qsTr("Idea") }, { value: "Problem", label: qsTr("Problem") }, { value: "Other", label: qsTr("Other") }]
                       current: page.topic; onPicked: (v) => page.topic = v } }
        CcField { id: fbTitle; width: parent.width; placeholder: qsTr("Short title") }
        CcField { id: fbText; width: parent.width; multiline: true; placeholder: qsTr("Your feedback…") }
        CcRow { title: qsTr("Include device info")
            description: qsTr("Fills in model, BIOS, distro, desktop and versions. Never your serial number, user name or files. XPanel's version is always included.")
            CcSwitch { checked: page.includeInfo; onToggled: (v) => page.includeInfo = v } }
        Rectangle {
            width: parent.width; radius: 3; color: "#101014"; border.color: "#2a2a33"
            implicitHeight: infoCol.implicitHeight + 24; height: implicitHeight
            Column {
                id: infoCol
                x: 20; y: 12; width: parent.width - 40; spacing: 2
                Text { text: qsTr("Added to the report"); color: T.dim; font.pixelSize: 14; font.bold: true }
                Text { text: page.infoLines.map(l => l.label + ": " + l.value).join("   ·   "); color: T.text; font.pixelSize: 15
                       width: infoCol.width; wrapMode: Text.Wrap; lineHeight: 1.2 }
            }
        }
        Flow {
            width: parent.width; spacing: 12
            CcButton { text: qsTr("Send on GitHub"); primary: !desktop.gamescope; onClicked: page.send() }
            CcButton { id: qrBtn; text: qsTr("Show QR code"); primary: desktop.gamescope
                       onClicked: { if (page.ready()) qrView.show() } }
            CcButton { text: qsTr("Copy"); onClicked: page.copyText() }
            CcButton { text: qsTr("Save locally"); onClicked: page.save() }
        }
        Text { visible: !!page.msg; text: page.msg; color: T.dim; font.pixelSize: 17
               width: parent.width; wrapMode: Text.WordWrap }
    }

    // ---------- QR code ----------
    Rectangle {
        id: qrView
        anchors.fill: parent; color: "#f2050507"; visible: false
        readonly property bool shortLink: feedback.qrIsShort(page.topic, page.title, page.body, page.includeInfo)
        readonly property string link: feedback.qrUrl(page.topic, page.title, page.body, page.includeInfo)
        readonly property int side: Math.max(240, Math.min(640, root.height - 300, root.width - 120))
        function show() {
            if (shortLink) feedback.copy(page.topic, page.title, page.body, page.includeInfo) // the text, for a browser
            visible = true
            app.pushBack(() => qrView.hide())
            Qt.callLater(() => qrClose.forceActiveFocus())
        }
        function hide() { visible = false; Qt.callLater(() => qrBtn.forceActiveFocus()) }
        MouseArea { anchors.fill: parent }
        Column {
            anchors.horizontalCenter: parent.horizontalCenter; y: 16
            width: parent.width - 80; spacing: 10
            Text { text: qsTr("Scan with your phone"); color: T.text; font.pixelSize: 30; font.bold: true
                   anchors.horizontalCenter: parent.horizontalCenter }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: qrView.side; height: qrView.side; color: "white"
                Image {
                    anchors.fill: parent
                    source: qrView.visible ? feedback.qrSource(qrView.link) : ""
                    sourceSize: Qt.size(qrView.side, qrView.side)
                    smooth: false; cache: false; fillMode: Image.PreserveAspectFit
                }
            }
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                   color: qrView.shortLink ? T.warn : T.dim; font.pixelSize: 15
                   text: qrView.shortLink
                         ? qsTr("Your text is too long for a QR code. The code opens a report with title and device info only; the full text is copied to the clipboard.")
                         : qsTr("Opens the GitHub issue with your text. Submit it there with your GitHub account.") }
            Text { width: Math.min(parent.width, 760); anchors.horizontalCenter: parent.horizontalCenter
                   horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAnywhere
                   color: "#5d5d66"; font.pixelSize: 12; elide: Text.ElideRight; maximumLineCount: 2
                   text: qrView.link }
            CcButton { id: qrClose; text: qsTr("Close"); primary: true; anchors.horizontalCenter: parent.horizontalCenter
                       onClicked: { qrView.hide(); app.popBack() } }
        }
    }
}
