import QtQuick
import "../Theme.js" as T

// Settings > Display: screen colour presets (colour temperature through KWin).
CcPage {
    id: page
    readonly property var presets: [
        { id: "default", name: "Default", note: "Native panel colours." },
        { id: "rts", name: "RTS / RPG", note: "A little cooler and crisper (7200 K)." },
        { id: "cinema", name: "Cinema", note: "Warmer, film-like (5800 K)." },
        { id: "eyecare", name: "Eye Care", note: "Less blue light (4500 K)." },
        { id: "night", name: "Night", note: "Very warm, for the evening (3400 K)." }
    ]
    CcSection { title: "Color presets"; note: "Screen colour presets. They use KDE Night Color, so they apply to the whole screen." }
    Repeater {
        model: page.presets
        CcRow { title: modelData.name; description: modelData.note
            CcButton { text: tweaks.colorPreset === modelData.id ? "Selected" : "Use"; primary: tweaks.colorPreset === modelData.id
                       onClicked: tweaks.setColorPreset(modelData.id) } }
    }
    CcSection { title: "Display" }
    CcRow { title: "Display mode"
            description: sys.hasExternal ? "Ally screen only, external screen only, the same picture on both, or one desktop over both."
                                         : "Connect an external screen to use the other modes."
        CcChoice { enabledChoice: sys.hasExternal
                   options: [{ value: "handheld", label: "Handheld" }, { value: "external", label: "External" },
                             { value: "duplicate", label: "Duplicate" }, { value: "extend", label: "Extend" }]
                   current: sys.displayMode; onPicked: (v) => sys.setDisplayMode(v) } }
    CcRow { title: "Refresh rate"
        CcChoice { options: [{ value: 120, label: "120 Hz" }, { value: 60, label: "60 Hz" }]
                   current: sys.refreshRate > 90 ? 120 : 60
                   onPicked: (v) => { if ((v > 90) !== (sys.refreshRate > 90)) sys.toggleRefreshRate() } } }
    CcRow { title: "Wide color gamut"; description: "More saturated colours (KWin colour management)."
        CcSwitch { checked: (config.rev, config.get("display.wcg", false)); onToggled: (v) => tweaks.setWideColor(v) } }
    CcRow { title: "Brightness"
        CcSlider { width: 460; from: 1; to: 100; unit: " %"; value: sys.brightness; onMoved: (v) => sys.setBrightness(v) } }
}
