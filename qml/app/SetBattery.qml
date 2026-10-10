import QtQuick
import "../Theme.js" as T

// Settings > Battery Care: charge limit, battery health, power options.
CcPage {
    id: page
    readonly property var bat: (sys.battery, deviceInfo.battery())

    CcSection { title: "Battery Care"; note: "Keeping the battery below 100% while plugged in makes it last longer." }
    CcRow { title: "Charge limit"
        CcChoice { enabledChoice: device.connected
                   options: [{ value: 100, label: "Full (100%)" }, { value: 90, label: "Balanced (90%)" },
                             { value: 80, label: "Maximum lifespan (80%)" }, { value: 60, label: "60%" }]
                   current: (device.rev, device.value("charge")); onPicked: (v) => device.setValue("charge", v) } }

    CcSection { title: "Battery health" }
    CcRow { title: "Health"; description: "Full charge now compared with the design capacity."
        Text { text: page.bat.health + " %"; color: page.bat.health < 80 ? T.warn : T.text; font.pixelSize: 20 } }
    CcRow { title: "Capacity"; Text { text: page.bat.full + " of " + page.bat.design; color: T.text; font.pixelSize: 18 } }
    CcRow { title: "Charge cycles"; Text { text: page.bat.cycles > 0 ? String(page.bat.cycles) : "not reported"; color: T.text; font.pixelSize: 18 } }
    CcRow { title: "Now"; Text { text: page.bat.capacity + " %  ·  " + page.bat.status; color: T.text; font.pixelSize: 18 } }

    CcSection { title: "Power options" }
    CcRow { title: "MCU power saving"; description: "The controller chip sleeps with the device. Off can help if the controller does not wake up."
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("mcuPowersave")); onToggled: (v) => device.setValue("mcuPowersave", v) } }
    CcRow { title: "Boot sound"; description: "The sound when the Ally starts."
        CcSwitch { enabledSwitch: device.connected; checked: (device.rev, !!device.value("bootSound")); onToggled: (v) => device.setValue("bootSound", v) } }
}
