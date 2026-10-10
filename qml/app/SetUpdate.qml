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
        return s === "update" ? "Update available" : s === "current" ? "Up to date"
             : s === "newer" ? "Newer than ASUS's public file" : "Unknown"
    }
    function statusColor(s) { return s === "update" ? T.accent : s === "unknown" ? T.dim : "#5fd38d" }

    CcSection { title: "Update Center"; note: updates.checking ? "Checking…" : (updates.lastCheck ? "Last check: " + updates.lastCheck : "Not checked yet.") }
    CcRow { title: "Check for Updates"; description: "ASUS support site, fwupd (LVFS), and the CachyOS / Arch repositories and AUR."
        CcButton { text: updates.checking ? "Checking…" : "Check for Updates"; enabledButton: !updates.checking; primary: true
                   onClicked: updates.check() } }

    // ---------- 1. Ally firmware ----------
    CcSection { title: "ROG Ally X firmware"; note: "From the ASUS support site for your model." }
    Repeater {
        model: updates.allyFirmware
        Column {
            width: parent.width; spacing: 6
            CcRow {
                title: modelData.name + (modelData.installed ? "   ·   installed " + modelData.installed : "")
                description: (modelData.latest ? "ASUS: " + modelData.latest + "  (" + modelData.date + ", " + modelData.size + ")   ·   " : "") + modelData.how
                Text { text: page.statusText(modelData.status); color: page.statusColor(modelData.status); font.pixelSize: 18 }
            }
            Row {
                visible: modelData.bios === true
                spacing: 10; leftPadding: 20
                CcButton { text: "Download BIOS " + modelData.latest + " (EZ Flash)"; primary: modelData.status === "update"
                           onClicked: updates.downloadBios(modelData.url, modelData.latest) }
                CcButton { text: "Restart to BIOS"; onClicked: confirmReboot.visible = true }
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
            Text { text: "Restart now into the BIOS setup? Save your work first."; color: T.text; font.pixelSize: 18
                   anchors.verticalCenter: parent.verticalCenter }
            CcButton { text: "Restart"; danger: true; onClicked: updates.restartToFirmware() }
            CcButton { text: "Cancel"; onClicked: confirmReboot.visible = false }
        }
    }

    // ---------- 2. drivers & firmware packages ----------
    CcSection { title: "Drivers & firmware packages"
                note: "On Linux the Ally drivers come with the kernel and these packages. They update with the system." }
    Repeater {
        model: updates.drivers
        CcRow { title: modelData.name; description: modelData.note
            Text { text: modelData.update ? modelData.installed + "  →  " + modelData.update : modelData.installed + "  ·  up to date"
                   color: modelData.update ? T.accent : T.dim; font.pixelSize: 17 } }
    }

    // ---------- 3. fwupd ----------
    CcSection { title: "Device firmware (fwupd)"; note: "Firmware that the Linux Vendor Firmware Service (LVFS) can update." }
    CcRow { title: "Firmware updates: " + updates.firmware.length
            description: updates.firmware.length ? updates.firmware.map(f => f.name + " " + f.from + " → " + f.to).join("   ·   ")
                                                 : "Nothing new on LVFS for this device."
        Row { spacing: 10
            CcButton { text: "Refresh list"; onClicked: updates.refreshFirmwareMetadata() }
            CcButton { text: "Update firmware"; enabledButton: updates.firmware.length > 0; onClicked: updates.updateFirmware() } } }
    Repeater {
        model: updates.fwDevices
        CcRow { title: modelData.name; description: (modelData.vendor || "") + "  ·  " + modelData.plugin
            Text { text: modelData.version + (modelData.updatable ? "   ·   updatable" : ""); color: T.dim; font.pixelSize: 17 } }
    }

    // ---------- 4. system packages ----------
    CcSection { title: "System updates (" + updates.packages.length + ")" }
    CcRow { visible: updates.packages.length > 0; title: "Install"; description: "Opens a terminal: the update asks for your password."
        CcButton { text: "Update system"; onClicked: updates.updatePackages() } }
    Repeater {
        model: updates.packages
        CcRow { title: modelData.name; description: modelData.source
            Text { text: modelData.from + "  →  " + modelData.to; color: T.accent; font.pixelSize: 17 } }
    }
    CcRow { visible: updates.packages.length === 0 && !!updates.lastCheck; title: "The system is up to date"; Item { width: 1; height: 1 } }
}
