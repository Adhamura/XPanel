import QtQuick
import "../Theme.js" as T

// Settings > Update Center:
//  0. XPanel itself (GitHub releases, src/AppUpdate.h)
//  1. ROG Ally X firmware from ASUS (BIOS, controller MCU): installed vs latest
//  2. Drivers & firmware packages (kernel, linux-firmware, microcode, Mesa ...)
//  3. Device firmware through fwupd / LVFS
//  4. Other system packages
CcPage {
    id: page
    Component.onCompleted: {
        if (!updates.lastCheck && !updates.checking) updates.check()
        if (appUpdate.autoCheck && !appUpdate.checked && !appUpdate.checking) appUpdate.check()
    }
    function installText() {
        const k = appUpdate.installKind
        if (k === "appimage-installed") return qsTr("Installed in /opt/xpanel: the update asks for your password, then replaces XPanel and restarts xpanel-daemon.")
        if (k === "appimage-portable") return qsTr("AppImage: the new version is saved next to this file (the old one is kept).")
        if (k === "windows") return qsTr("The installer runs (Windows asks for permission), then XPanel starts again.")
        if (appUpdate.packageManaged) return qsTr("Installed by your package manager: update XPanel with it.")
        return qsTr("This copy was built from source or unpacked from an archive: download the new version from the release page.")
    }
    function statusText(s) {
        return s === "update" ? qsTr("Update available") : s === "current" ? qsTr("Up to date")
             : s === "newer" ? qsTr("Newer than ASUS's public file") : qsTr("Unknown")
    }
    // Windows: firmware = BIOS, MCU or a file that needs AC power
    function isFirmware(r) {
        return r.bios === true || r.acPower === true || /MCU|BIOS/i.test(r.title || "")
    }
    // Windows: no driver of this kind found on the PC (ASUS lists a version, nothing installed matches)
    function notInstalled(r) { return r.isStore !== true && (r.status === "unknown" || !r.installed) }
    function canInstall(r) {
        return (r.status === "update" || notInstalled(r)) && !!r.sha256 && !!r.url && r.isStore !== true
               && !updates.installBusy && !updates.downloadBusy
    }
    // Windows driver list: updates first, then not installed, Store items, up to date last
    function driverRank(r) {
        return r.status === "update" ? 0 : notInstalled(r) ? 1 : r.isStore === true ? 2 : r.status === "newer" ? 3 : 4
    }
    function sortedDrivers() {
        return updates.drivers.slice().sort((a, b) => driverRank(a) - driverRank(b)
                                                       || String(a.title).localeCompare(String(b.title)))
    }
    function startInstall(r) {
        if (isFirmware(r)) { confirmInstall.row = r; confirmInstall.visible = true }
        else updates.install(r)
    }
    function statusColor(s) { return s === "update" ? T.accent : s === "unknown" ? T.dim : "#5fd38d" }

    // ---------- 0. XPanel ----------
    CcSection { title: "XPanel"
                note: appUpdate.checking ? qsTr("Checking GitHub …")
                    : appUpdate.error && !appUpdate.busy && appUpdate.state !== "failed" ? appUpdate.error
                    : appUpdate.lastCheck ? qsTr("Last check: %1").arg(appUpdate.lastCheck) : qsTr("Not checked yet.") }
    CcRow {
        title: qsTr("Installed version %1").arg(appUpdate.currentVersion)
        description: !appUpdate.checked ? "" : appUpdate.available
                     ? (appUpdate.skipped ? qsTr("Version %1 is available (skipped).") : qsTr("Version %1 is available.")).arg(appUpdate.latestVersion)
                     : qsTr("XPanel is up to date (latest release: %1).").arg(appUpdate.latestVersion)
        CcButton { text: appUpdate.checking ? qsTr("Checking…") : qsTr("Check now"); onClicked: appUpdate.check() }
    }
    // release notes (Markdown from the GitHub release). Gamepad: up / down scroll, then move on.
    Rectangle {
        id: notesBox
        visible: appUpdate.available && !!appUpdate.notes
        width: parent.width; height: Math.min(notesText.implicitHeight + 28, 260)
        color: "#131318"; radius: 3
        border.color: activeFocus ? T.accent : "#24242a"; border.width: activeFocus ? 3 : 1
        activeFocusOnTab: visible
        function navKey(a) {
            const max = notesFlick.contentHeight - notesFlick.height
            if (a === "down" && notesFlick.contentY < max - 1) { notesFlick.contentY = Math.min(max, notesFlick.contentY + 90); return true }
            if (a === "up" && notesFlick.contentY > 1) { notesFlick.contentY = Math.max(0, notesFlick.contentY - 90); return true }
            return false
        }
        Keys.onUpPressed: (e) => { e.accepted = navKey("up") }
        Keys.onDownPressed: (e) => { e.accepted = navKey("down") }
        CcScrollBar { flickable: notesFlick }
        Flickable {
            id: notesFlick
            anchors.fill: parent; anchors.margins: 14
            contentWidth: width; contentHeight: notesText.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            Behavior on contentY { NumberAnimation { duration: 120 } }
            Text {
                id: notesText
                width: notesFlick.width
                text: appUpdate.notes; textFormat: Text.MarkdownText
                color: T.text; linkColor: T.accent; font.pixelSize: 15; wrapMode: Text.WordWrap
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }
    CcRow {
        visible: appUpdate.available
        title: qsTr("XPanel %1").arg(appUpdate.latestVersion)
        description: page.installText()
        Row { spacing: 10
            // one button from start to end, so the gamepad focus stays on it: Update now -> Restart XPanel
            CcButton {
                readonly property bool done: appUpdate.state === "installed" || appUpdate.state === "ready"
                visible: appUpdate.installKind !== "manual"; primary: true
                text: done ? qsTr("Restart XPanel") : appUpdate.busy ? qsTr("Updating…") : qsTr("Update now")
                onClicked: done ? appUpdate.restart() : appUpdate.update()
            }
            CcButton { visible: appUpdate.state === "downloading"; text: qsTr("Cancel"); onClicked: appUpdate.cancel() }
            CcButton { text: appUpdate.skipped ? qsTr("Skipped") : qsTr("Skip this version"); onClicked: appUpdate.skip() }
            CcButton { text: qsTr("Release page"); primary: appUpdate.installKind === "manual"; onClicked: appUpdate.openReleasePage() }
        }
    }
    // download / install progress and result
    Column {
        visible: appUpdate.state !== "idle"
        width: parent.width; spacing: 8
        Rectangle {
            visible: appUpdate.state === "downloading" || appUpdate.state === "verifying"
            width: parent.width; height: 8; radius: 4; color: "#24242a"
            Rectangle { width: parent.width * Math.max(0, Math.min(1, appUpdate.progress)); height: parent.height; radius: 4; color: T.accent }
        }
        Text { visible: !!appUpdate.status; width: parent.width; wrapMode: Text.WordWrap
               text: appUpdate.status + (appUpdate.state === "downloading" ? "  " + Math.round(appUpdate.progress * 100) + " %" : "")
               color: T.text; font.pixelSize: 16 }
        Text { visible: appUpdate.state === "failed"; width: parent.width; wrapMode: Text.WordWrap
               text: appUpdate.error; color: T.warn; font.pixelSize: 16 }
    }
    CcRow {
        visible: appUpdate.state === "command"
        title: qsTr("Install in a terminal")
        description: appUpdate.manualCommand
        CcButton { text: qsTr("Copy command"); onClicked: { appUpdate.copyCommand(); text = qsTr("Copied") } }
    }
    CcRow {
        title: qsTr("Check for updates automatically")
        description: qsTr("Once a day XPanel asks api.github.com for the latest release. Nothing else is sent.")
        CcSwitch { checked: appUpdate.autoCheck; onToggled: (on) => appUpdate.autoCheck = on }
    }

    CcSection { title: qsTr("Update Center"); note: updates.checking ? qsTr("Checking…") : (updates.lastCheck ? qsTr("Last check: %1").arg(updates.lastCheck) : qsTr("Not checked yet.")) }
    CcRow { title: qsTr("Check for Updates")
        description: desktop.windows ? qsTr("ASUS support site: BIOS, controller firmware and drivers for your ROG Ally X.")
                                     : qsTr("ASUS support site, fwupd (LVFS), and the CachyOS / Arch repositories and AUR.")
        CcButton { text: updates.checking ? qsTr("Checking…") : qsTr("Check for Updates"); enabledButton: !updates.checking; primary: true
                   onClicked: updates.check() } }

    // Windows: download progress and result (shared by firmware and driver rows)
    Column {
        visible: desktop.windows && (updates.downloadBusy || !!updates.downloadStatus)
        width: parent.width; spacing: 8
        Rectangle {
            visible: updates.downloadBusy
            width: parent.width; height: 8; radius: 4; color: "#24242a"
            Rectangle { width: parent.width * Math.max(0, Math.min(1, updates.downloadProgress)); height: parent.height; radius: 4; color: T.accent }
        }
        Text { width: parent.width; wrapMode: Text.WordWrap; color: T.text; font.pixelSize: 16
               text: updates.downloadStatus + (updates.downloadBusy ? "  " + Math.round(updates.downloadProgress * 100) + " %" : "") }
        CcButton { visible: updates.downloadBusy; text: qsTr("Cancel"); onClicked: updates.cancelDownload() }
    }
    Text { visible: desktop.windows; width: parent.width; wrapMode: Text.WordWrap; color: T.dim; font.pixelSize: 15
           text: qsTr("Downloads come from ASUS (dlcdnets.asus.com) and are checked with the ASUS SHA-256. Install runs the ASUS installer after a Windows permission prompt. BIOS and MCU updates need the charger connected.") }
    // Windows: install progress and result
    Column {
        visible: desktop.windows && (updates.installBusy || !!updates.installStatus || updates.restartNeeded)
        width: parent.width; spacing: 8
        Text { visible: !!updates.installStatus; width: parent.width; wrapMode: Text.WordWrap; color: T.text; font.pixelSize: 16
               text: updates.installStatus }
        CcButton { visible: updates.restartNeeded && !confirmRestart.visible; text: qsTr("Restart now"); primary: true
                   onClicked: confirmRestart.visible = true }
        Rectangle {
            id: confirmRestart
            visible: false; width: parent.width; height: 70; color: "#2a1016"; border.color: T.warn; radius: 3
            Row {
                anchors.verticalCenter: parent.verticalCenter; x: 20; spacing: 14
                Text { text: qsTr("Restart the device now? Save your work first."); color: T.text; font.pixelSize: 18
                       anchors.verticalCenter: parent.verticalCenter }
                CcButton { text: qsTr("Restart"); danger: true; onClicked: { confirmRestart.visible = false; updates.restartNow() } }
                CcButton { text: qsTr("Cancel"); onClicked: confirmRestart.visible = false }
            }
        }
    }
    // Windows: firmware (BIOS, MCU, AC-only) needs a confirmation before the install starts
    Rectangle {
        id: confirmInstall
        property var row: null
        visible: false; width: parent.width; height: warnText.implicitHeight + 90; color: "#2a1016"; border.color: T.warn; radius: 3
        Column {
            anchors.fill: parent; anchors.margins: 20; spacing: 12
            Text { id: warnText; width: parent.width; wrapMode: Text.WordWrap; color: T.text; font.pixelSize: 18
                   text: qsTr("Do not turn off or unplug the Ally while the update runs. A failed BIOS or MCU update can make the device unusable.") }
            Row { spacing: 14
                CcButton { text: qsTr("Install"); danger: true
                           onClicked: { confirmInstall.visible = false; updates.install(confirmInstall.row) } }
                CcButton { text: qsTr("Cancel"); onClicked: confirmInstall.visible = false }
            }
        }
    }

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
                visible: !desktop.windows && modelData.bios === true
                spacing: 10; leftPadding: 20
                CcButton { text: qsTr("Download BIOS %1 (EZ Flash)").arg(modelData.latest); primary: modelData.status === "update"
                           onClicked: updates.downloadBios(modelData.url, modelData.latest) }
                CcButton { text: qsTr("Restart to BIOS"); onClicked: confirmReboot.visible = true }
            }
            Row {
                visible: desktop.windows && !!modelData.url
                spacing: 10; leftPadding: 20
                CcButton { visible: modelData.isStore !== true && !!modelData.sha256 && modelData.status === "update"
                           text: qsTr("Install %1").arg(modelData.latest); primary: true
                           enabledButton: page.canInstall(modelData)
                           onClicked: page.startInstall(modelData) }
                CcButton { visible: modelData.isStore !== true && modelData.status !== "current" && modelData.status !== "newer"
                           text: qsTr("Download %1").arg(modelData.latest)
                           enabledButton: !updates.downloadBusy && !updates.installBusy
                           onClicked: updates.download(modelData.url, modelData.latest, modelData.sha256, modelData.folder) }
                CcButton { visible: modelData.isStore === true; text: qsTr("Open in Store"); primary: true
                           onClicked: updates.openStore(modelData.url) }
            }
        }
    }
    Text { visible: !desktop.windows && !!updates.downloadStatus; text: updates.downloadStatus; color: T.text; font.pixelSize: 16
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

    // ---------- 2a. Windows drivers (ASUS) ----------
    CcSection { visible: desktop.windows; title: qsTr("Drivers"); note: qsTr("ASUS drivers for your model. The installed version is read from the devices in this PC.") }
    Repeater {
        model: desktop.windows ? page.sortedDrivers() : []
        CcRow {
            readonly property bool missing: page.notInstalled(modelData)
            readonly property bool current: !missing && modelData.isStore !== true && modelData.status !== "update"
            title: modelData.title
            description: (modelData.category ? modelData.category + "   ·   " : "")
                         + [modelData.date, modelData.size].filter(x => !!x).join("   ·   ")
            Row { spacing: 12
                Text { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 17
                       text: modelData.isStore === true ? qsTr("Microsoft Store")
                           : missing ? qsTr("Not installed  ·  ASUS %1").arg(modelData.latest)
                           : modelData.status === "update" ? modelData.installed + "  →  " + modelData.latest
                           : qsTr("%1  ·  up to date").arg(modelData.installed)
                       color: modelData.status === "update" ? T.accent : missing ? T.warn : current ? "#5fd38d" : T.dim }
                CcButton { visible: !current && modelData.isStore !== true && !!modelData.sha256
                           text: qsTr("Install"); primary: true
                           enabledButton: page.canInstall(modelData)
                           onClicked: page.startInstall(modelData) }
                CcButton { visible: !current && modelData.isStore !== true && !!modelData.url; text: qsTr("Download")
                           enabledButton: !updates.downloadBusy && !updates.installBusy
                           onClicked: updates.download(modelData.url, modelData.latest, modelData.sha256, modelData.folder) }
                CcButton { visible: modelData.isStore === true; text: qsTr("Open in Store"); primary: true
                           onClicked: updates.openStore(modelData.url) }
            }
        }
    }
    CcRow {
        visible: desktop.windows
        title: qsTr("Install all driver updates")
        description: qsTr("Installs the driver updates one after another and stops at the first failure. Each install asks Windows for permission.")
        CcButton { text: qsTr("Install all"); primary: true
                   enabledButton: !updates.installBusy && !updates.downloadBusy && !updates.checking
                   onClicked: updates.installAllDrivers() }
    }

    // ---------- 2. drivers & firmware packages ----------
    CcSection { visible: !desktop.windows; title: qsTr("Drivers & firmware packages")
                note: qsTr("On Linux the Ally drivers come with the kernel and these packages. They update with the system.") }
    Repeater {
        model: desktop.windows ? [] : updates.drivers
        CcRow { title: modelData.name; description: modelData.note
            Text { text: modelData.update ? modelData.installed + "  →  " + modelData.update : qsTr("%1  ·  up to date").arg(modelData.installed)
                   color: modelData.update ? T.accent : T.dim; font.pixelSize: 17 } }
    }

    // ---------- 3. fwupd ----------
    CcSection { visible: !desktop.windows; title: qsTr("Device firmware (fwupd)"); note: qsTr("Firmware that the Linux Vendor Firmware Service (LVFS) can update.") }
    CcRow { visible: !desktop.windows; title: qsTr("Firmware updates: %1").arg(updates.firmware.length)
            description: updates.firmware.length ? updates.firmware.map(f => f.name + " " + f.from + " → " + f.to).join("   ·   ")
                                                 : qsTr("Nothing new on LVFS for this device.")
        Row { spacing: 10
            CcButton { text: qsTr("Refresh list"); onClicked: updates.refreshFirmwareMetadata() }
            CcButton { text: qsTr("Update firmware"); enabledButton: updates.firmware.length > 0; onClicked: updates.updateFirmware() } } }
    Repeater {
        model: desktop.windows ? [] : updates.fwDevices
        CcRow { title: modelData.name; description: (modelData.vendor || "") + "  ·  " + modelData.plugin
            Text { text: modelData.version + (modelData.updatable ? "   ·   " + qsTr("updatable") : ""); color: T.dim; font.pixelSize: 17 } }
    }

    // ---------- 4. system packages ----------
    CcSection { visible: !desktop.windows; title: qsTr("System updates (%1)").arg(updates.packages.length) }
    CcRow { visible: !desktop.windows && updates.packages.length > 0; title: qsTr("Install"); description: qsTr("Opens a terminal: the update asks for your password.")
        CcButton { text: qsTr("Update system"); onClicked: updates.updatePackages() } }
    Repeater {
        model: desktop.windows ? [] : updates.packages
        CcRow { title: modelData.name; description: modelData.source
            Text { text: modelData.from + "  →  " + modelData.to; color: T.accent; font.pixelSize: 17 } }
    }
    CcRow { visible: !desktop.windows && updates.packages.length === 0 && !!updates.lastCheck; title: qsTr("The system is up to date"); Item { width: 1; height: 1 } }
}
