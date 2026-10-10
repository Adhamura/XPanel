import QtQuick
import "../Theme.js" as T

// User Center: local profile, My Products (this device), Notifications.
// ASUS member login, registered events and redemption need an ASUS account: not on Linux.
CcPage {
    id: page
    readonly property var user: deviceInfo.user()
    readonly property var info: deviceInfo.info()

    Row {
        spacing: 24
        Rectangle {
            width: 120; height: 120; radius: 60; color: "#1d1d25"; clip: true; border.color: T.accent; border.width: 3
            Image { anchors.fill: parent; anchors.margins: 3; visible: !!page.user.avatar; source: page.user.avatar || ""
                    fillMode: Image.PreserveAspectCrop }
            Text { anchors.centerIn: parent; visible: !page.user.avatar; text: (page.user.name || "?").charAt(0).toUpperCase()
                   color: T.text; font.pixelSize: 54; font.bold: true }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { text: page.user.name || ""; color: T.text; font.pixelSize: 34; font.bold: true }
            Text { text: "Local account: " + (page.user.login || "") + "  ·  " + (page.info.hostname || ""); color: T.dim; font.pixelSize: 17 }
            CcButton { visible: desktop.userSettings; text: desktop.kde ? "Edit profile (KDE)" : "Edit profile"
                       onClicked: tweaks.openSystemSettings("kcm_users") }
        }
    }

    CcSection { title: "My Products" }
    CcRow { title: (device.rev, device.value("model") && device.value("model") !== "unknown"
                    ? "ASUS " + device.value("modelName") : page.info.vendor + " " + page.info.model)
            description: "BIOS " + page.info.bios + " (" + page.info.biosDate + ")" +
                         (device.value("serial") ? "  ·  Serial " + device.value("serial") : "")
        CcButton { text: "Device info"; onClicked: { app.settingsPage = "general"; app.tab = "settings" } } }
    Repeater {
        model: deviceInfo.controllers()
        CcRow { title: modelData.name; description: "Controller  ·  " + modelData.bus + "  ·  " + modelData.id; Item { width: 1; height: 1 } }
    }

    CcSection { title: "Notifications"; note: "What XPanel changed during this session." }
    Repeater {
        model: app.notifications
        CcRow { title: model.text; description: model.time; Item { width: 1; height: 1 } }
    }
    CcRow { visible: app.notifications.count === 0; title: "No notifications yet"; Item { width: 1; height: 1 } }

    CcSection { title: "ASUS account"
                note: "Member login, Registered Events and Redemption use ASUS online services that have no Linux version. " +
                      "XPanel keeps everything on this device." }
}
