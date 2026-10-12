import QtQuick
import QtQuick.Shapes
import "../Theme.js" as T

// User Center: local profile, My Products (this device), Notifications.
// ASUS member login, registered events and redemption need an ASUS account: not available in XPanel.
CcPage {
    id: page
    readonly property var user: deviceInfo.user()
    readonly property var info: deviceInfo.info()

    Row {
        spacing: 24
        Item {
            width: 120; height: 120
            Rectangle { anchors.fill: parent; radius: width / 2; color: "#1d1d25" }
            // the picture, cut to a circle (clip only cuts rectangles): a round Shape filled with the image
            Image { id: avatarImg; visible: false; source: page.user.avatar || ""; fillMode: Image.PreserveAspectCrop
                    width: 114; height: 114; sourceSize: Qt.size(228, 228) }
            Shape {
                anchors.centerIn: parent; width: 114; height: 114
                visible: !!page.user.avatar && avatarImg.status === Image.Ready
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    id: avatarPath
                    // the fill uses the picture at its own pixel size: scale it to cover the circle and centre it
                    readonly property real iw: Math.max(1, avatarImg.implicitWidth)
                    readonly property real ih: Math.max(1, avatarImg.implicitHeight)
                    readonly property real s: Math.max(114 / iw, 114 / ih)
                    strokeColor: "transparent"
                    fillItem: avatarImg
                    fillTransform: PlanarTransform.fromAffineMatrix(s, 0, 0, s, (114 - iw * s) / 2, (114 - ih * s) / 2)
                    PathAngleArc { centerX: 57; centerY: 57; radiusX: 57; radiusY: 57; startAngle: 0; sweepAngle: 360 }
                }
            }
            Text { anchors.centerIn: parent; visible: !page.user.avatar; text: (page.user.name || "?").charAt(0).toUpperCase()
                   color: T.text; font.pixelSize: 54; font.bold: true }
            Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.color: T.accent; border.width: 3 }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: page.user.name || ""; color: T.text; font.pixelSize: 34; font.bold: true }
            Text { text: qsTr("Local account: %1  ·  %2").arg(page.user.login || "").arg(page.info.hostname || ""); color: T.dim; font.pixelSize: 17 }
            CcButton { visible: desktop.userSettings || desktop.windows; text: desktop.kde ? qsTr("Edit profile (KDE)") : qsTr("Edit profile")
                       onClicked: tweaks.openSystemSettings("kcm_users") }
        }
    }

    CcSection { title: qsTr("My Products") }
    CcRow { title: (device.rev, device.value("model") && device.value("model") !== "unknown"
                    ? "ASUS " + device.value("modelName") : page.info.vendor + " " + page.info.model)
            description: qsTr("BIOS %1 (%2)").arg(page.info.bios).arg(page.info.biosDate) +
                         (device.value("serial") ? qsTr("  ·  Serial %1").arg(device.value("serial")) : "")
        CcButton { text: qsTr("Device info"); onClicked: { app.settingsPage = "general"; app.tab = "settings" } } }
    Repeater {
        model: deviceInfo.controllers()
        CcRow { title: modelData.name; description: qsTr("Controller  ·  %1  ·  %2").arg(modelData.bus).arg(modelData.id); Item { width: 1; height: 1 } }
    }

    CcSection { title: qsTr("Notifications"); note: qsTr("What XPanel changed during this session.") }
    Repeater {
        model: app.notifications
        CcRow { title: model.text; description: model.time; Item { width: 1; height: 1 } }
    }
    CcRow { visible: app.notifications.count === 0; title: qsTr("No notifications yet"); Item { width: 1; height: 1 } }

    CcSection { title: qsTr("ASUS account")
                note: desktop.windows ? qsTr("Member login, Registered Events and Redemption use ASUS online services that XPanel does not support. XPanel keeps everything on this device.")
                      : qsTr("Member login, Registered Events and Redemption use ASUS online services that have no Linux version. " +
                           "XPanel keeps everything on this device.") }
}
