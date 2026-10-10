import QtQuick
import "../Theme.js" as T

// Settings > Battery Care: charge limit, battery health, power options.
CcPage {
    id: page
    readonly property var bat: (sys.battery, deviceInfo.battery())

    CcSection { title: qsTr("Battery Care"); note: qsTr("Keeping the battery below 100% while plugged in makes it last longer.") }
    CcRow { title: qsTr("Charge limit")
        CcChoice { enabledChoice: device.connected
                   options: [{ value: 100, label: qsTr("Full (100%)") }, { value: 90, label: qsTr("Balanced (90%)") },
                             { value: 80, label: qsTr("Maximum lifespan (80%)") }, { value: 60, label: "60%" }]
                   current: (device.rev, device.value("charge")); onPicked: (v) => device.setValue("charge", v) } }

    CcSection { title: qsTr("Battery health") }
    CcRow { title: qsTr("Health"); description: qsTr("Full charge now compared with the design capacity.")
        Text { text: page.bat.health + " %"; color: page.bat.health < 80 ? T.warn : T.text; font.pixelSize: 20 } }
    CcRow { title: qsTr("Capacity"); Text { text: qsTr("%1 of %2").arg(page.bat.full).arg(page.bat.design); color: T.text; font.pixelSize: 18 } }
    CcRow { title: qsTr("Charge cycles"); Text { text: page.bat.cycles > 0 ? String(page.bat.cycles) : qsTr("not reported"); color: T.text; font.pixelSize: 18 } }
    CcRow { title: qsTr("Now"); Text { text: page.bat.capacity + " %  ·  " + page.bat.status; color: T.text; font.pixelSize: 18 } }

    CcSection { title: qsTr("Power options") }
    CcRow { title: qsTr("MCU power saving"); description: qsTr("The controller chip sleeps with the device. Off can help if the controller does not wake up.")
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("mcuPowersave")); onToggled: (v) => device.setValue("mcuPowersave", v) } }
    CcRow { title: qsTr("Boot sound"); description: qsTr("The sound when the Ally starts.")
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("bootSound")); onToggled: (v) => device.setValue("bootSound", v) } }
}
