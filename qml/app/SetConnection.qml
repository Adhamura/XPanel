import QtQuick
import "../Theme.js" as T

// Settings > Connection: Wi-Fi networks and Bluetooth devices.
CcPage {
    id: page
    Component.onCompleted: { network.scanWifi(); network.scanBluetooth() }

    CcSection { title: "Wi-Fi"; note: network.message }
    CcRow { title: "Wi-Fi"
        Row { spacing: 12
            CcButton { text: network.busy ? "Scanning…" : "Scan"; enabledButton: sys.wifi && !network.busy; onClicked: network.scanWifi() }
            CcSwitch { checked: sys.wifi; onToggled: (v) => { sys.setWifi(v); if (v) network.scanWifi() } } } }
    Repeater {
        model: sys.wifi ? network.networks : []
        CcRow {
            title: (modelData.active ? "● " : "") + modelData.ssid
            description: modelData.signal + "%" + (modelData.secure ? "  ·  " + modelData.security : "  ·  open")
            CcButton {
                text: modelData.active ? "Disconnect" : "Connect"; primary: modelData.active
                onClicked: modelData.active ? network.disconnectWifi()
                         : (modelData.secure ? pass.show(modelData.ssid) : network.connectWifi(modelData.ssid, ""))
            }
        }
    }

    CcSection { title: "Bluetooth" }
    CcRow { title: "Bluetooth"
        Row { spacing: 12
            CcButton { text: network.busy ? "Searching…" : "Search devices"; enabledButton: sys.bluetooth && !network.busy
                       onClicked: network.scanBluetooth() }
            CcSwitch { checked: sys.bluetooth; onToggled: (v) => sys.setBluetooth(v) } } }
    Repeater {
        model: sys.bluetooth ? network.devices : []
        CcRow {
            title: modelData.name
            description: modelData.mac + (modelData.connected ? "  ·  connected" : (modelData.paired ? "  ·  paired" : ""))
            Row { spacing: 10
                CcButton { text: modelData.connected ? "Disconnect" : "Connect"; primary: modelData.connected
                           onClicked: modelData.connected ? network.disconnectBluetooth(modelData.mac) : network.connectBluetooth(modelData.mac) }
                CcButton { visible: modelData.paired; text: "Forget"; danger: true; onClicked: network.removeBluetooth(modelData.mac) } }
        }
    }

    // Wi-Fi password
    Rectangle {
        id: pass
        property string ssid
        parent: page.parent; anchors.fill: parent; color: "#d0000000"; visible: false; z: 10
        function show(s) { ssid = s; field.text = ""; visible = true; app.pushBack(() => pass.visible = false); Qt.callLater(() => field.activate()) }
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent; width: 620; height: 250; color: T.popup; border.color: T.tileBorder
            Column {
                x: 28; y: 24; width: parent.width - 56; spacing: 16
                Text { text: "Password for " + pass.ssid; color: T.text; font.pixelSize: 22 }
                CcField { id: field; width: parent.width; password: true; placeholder: "Wi-Fi password"
                          onAccepted: { network.connectWifi(pass.ssid, text); pass.visible = false; app.popBack() } }
                Row { spacing: 12; anchors.right: parent.right
                    CcButton { text: "Cancel"; onClicked: { pass.visible = false; app.popBack() } }
                    CcButton { text: "Connect"; primary: true; onClicked: { network.connectWifi(pass.ssid, field.text); pass.visible = false; app.popBack() } } }
            }
        }
    }
}
