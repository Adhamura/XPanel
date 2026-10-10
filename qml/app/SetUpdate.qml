import QtQuick
import "../Theme.js" as T

// Settings > Update Center:
//  1. ROG Ally X firmware from ASUS (BIOS, controller MCU): installed vs latest
//  2. Drivers & firmware packages (kernel, linux-firmware, microcode, Mesa ...)
//  3. Device firmware through fwupd / LVFS
//  4. Other system packages
CcPage {
    id: page
    Component.onCompleted: if (!updates.lastCheck && !updates.checking) updates.check()
    function statusText(s) {
        return s === "update" ? qsTr("Update available") : s === "current" ? qsTr("Up to date")
             : s === "newer" ? qsTr("Newer than ASUS's public file") : qsTr("Unknown")
    }
    function statusColor(s) { return s === "update" ? T.accent : s === "unknown" ? T.dim : "#5fd38d" }

    CcSection { title: qsTr("Update Center"); note: updates.checking ? qsTr("Checking…") : (updates.lastCheck ? qsTr("Last check: %1").arg(updates.lastCheck) : qsTr("Not checked yet.")) }
    CcRow { title: qsTr("Check for Updates"); description: qsTr("ASUS support site, fwupd (LVFS), and the CachyOS / Arch repositories and AUR.")
        CcButton { text: updates.checking ? qsTr("Checking…") : qsTr("Check for Updates"); enabledButton: !updates.checking; primary: true
                   onClicked: updates.check() } }

    // ---------- 1. Ally firmware ----------
    CcSection { title: qsTr("ROG Ally X firmware"); note: qsTr("From the ASUS support site for your model.") }
    Repeater {
        model: updates.allyFirmware
        Column {
            width: parent.width; spacing: 6
            CcRow {
                title: modelData.installed ? qsTr("%1   ·   installed %2").arg(modelData.name).arg(modelData.installed) : modelData.name
                description: (modelData.latest ? qsTr("ASUS: %1  (%2, %3)").arg(modelData.latest).arg(modelData.date).arg(modelData.size) + "   ·   " : "") + modelData.how
                Text { text: page.statusText(modelData.status); color: page.statusColor(modelData.status); font.pixelSize: 18 }
            }
            Row {
                visible: modelData.bios === true
                spacing: 10; leftPadding: 20
                CcButton { text: qsTr("Download BIOS %1 (EZ Flash)").arg(modelData.latest); primary: modelData.status === "update"
                           onClicked: updates.downloadBios(modelData.url, modelData.latest) }
                CcButton { text: qsTr("Restart to BIOS"); onClicked: confirmReboot.visible = true }
            }
        }
    }
    Text { visible: !!updates.downloadStatus; text: updates.downloadStatus; color: T.text; font.pixelSize: 16
           width: parent.width; wrapMode: Text.WordWrap }
    Rectangle {
        id: confirmReboot
        visible: false; width: parent.width; height: 70; color: "#2a1016"; border.color: T.warn; radius: 3
        Row {
            anchors.verticalCenter: parent.verticalCenter; x: 20; spacing: 14
            Text { text: qsTr("Restart now into the BIOS setup? Save your work first."); color: T.text; font.pixelSize: 18
                   anchors.verticalCenter: parent.verticalCenter }
            CcButton { text: qsTr("Restart"); danger: true; onClicked: updates.restartToFirmware() }
            CcButton { text: qsTr("Cancel"); onClicked: confirmReboot.visible = false }
        }
    }

    // ---------- 2. drivers & firmware packages ----------
    CcSection { title: qsTr("Drivers & firmware packages")
                note: qsTr("On Linux the Ally drivers come with the kernel and these packages. They update with the system.") }
    Repeater {
        model: updates.drivers
        CcRow { title: modelData.name; description: modelData.note
            Text { text: modelData.update ? modelData.installed + "  →  " + modelData.update : qsTr("%1  ·  up to date").arg(modelData.installed)
                   color: modelData.update ? T.accent : T.dim; font.pixelSize: 17 } }
    }

    // ---------- 3. fwupd ----------
    CcSection { title: qsTr("Device firmware (fwupd)"); note: qsTr("Firmware that the Linux Vendor Firmware Service (LVFS) can update.") }
    CcRow { title: qsTr("Firmware updates: %1").arg(updates.firmware.length)
            description: updates.firmware.length ? updates.firmware.map(f => f.name + " " + f.from + " → " + f.to).join("   ·   ")
                                                 : qsTr("Nothing new on LVFS for this device.")
        Row { spacing: 10
            CcButton { text: qsTr("Refresh list"); onClicked: updates.refreshFirmwareMetadata() }
            CcButton { text: qsTr("Update firmware"); enabledButton: updates.firmware.length > 0; onClicked: updates.updateFirmware() } } }
    Repeater {
        model: updates.fwDevices
        CcRow { title: modelData.name; description: (modelData.vendor || "") + "  ·  " + modelData.plugin
            Text { text: modelData.version + (modelData.updatable ? "   ·   " + qsTr("updatable") : ""); color: T.dim; font.pixelSize: 17 } }
    }

    // ---------- 4. system packages ----------
    CcSection { title: qsTr("System updates (%1)").arg(updates.packages.length) }
    CcRow { visible: updates.packages.length > 0; title: qsTr("Install"); description: qsTr("Opens a terminal: the update asks for your password.")
        CcButton { text: qsTr("Update system"); onClicked: updates.updatePackages() } }
    Repeater {
        model: updates.packages
        CcRow { title: modelData.name; description: modelData.source
            Text { text: modelData.from + "  →  " + modelData.to; color: T.accent; font.pixelSize: 17 } }
    }
    CcRow { visible: updates.packages.length === 0 && !!updates.lastCheck; title: qsTr("The system is up to date"); Item { width: 1; height: 1 } }
}
