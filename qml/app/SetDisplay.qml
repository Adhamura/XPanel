import QtQuick
import "../Theme.js" as T

// Settings > Display: screen colour presets (colour temperature through KWin Night Color or
// GNOME Night Light), display mode, refresh rate, brightness. KDE-only rows hide elsewhere.
CcPage {
    id: page
    readonly property var presets: [
        { id: "default", name: "Default", note: "Native panel colours." },
        { id: "rts", name: "RTS / RPG", note: "A little cooler and crisper (7200 K)." },
        { id: "cinema", name: "Cinema", note: "Warmer, film-like (5800 K)." },
        { id: "eyecare", name: "Eye Care", note: "Less blue light (4500 K)." },
        { id: "night", name: "Night", note: "Very warm, for the evening (3400 K)." }
    ]
    CcSection { title: "Color presets"
                note: !desktop.colorPresets ? "Not available " + desktop.where + "."
                      : desktop.kde ? "Screen colour presets. They use KDE Night Color, so they apply to the whole screen."
                      : "Screen colour presets. They use GNOME Night Light (always on), so they apply to the whole screen." }
    Repeater {
        model: desktop.colorPresets ? page.presets : []
        CcRow { title: modelData.name; description: modelData.note
            CcButton { text: tweaks.colorPreset === modelData.id ? "Selected" : "Use"; primary: tweaks.colorPreset === modelData.id
                       onClicked: tweaks.setColorPreset(modelData.id) } }
    }
    CcSection { title: "Display"
                note: desktop.displayMode ? "" : "Display mode, refresh rate and wide colour use KDE's display settings: "
                      + (desktop.gamescope ? "in Steam Game Mode, use Steam's Display settings." : "not available " + desktop.where + ".") }
    CcRow { visible: desktop.displayMode; title: "Display mode"
            description: sys.hasExternal ? "Ally screen only, external screen only, the same picture on both, or one desktop over both."
                                         : "Connect an external screen to use the other modes."
        CcChoice { enabledChoice: sys.hasExternal
                   options: [{ value: "handheld", label: "Handheld" }, { value: "external", label: "External" },
                             { value: "duplicate", label: "Duplicate" }, { value: "extend", label: "Extend" }]
                   current: sys.displayMode; onPicked: (v) => sys.setDisplayMode(v) } }
    CcRow { visible: desktop.refreshRate; title: "Refresh rate"
        CcChoice { options: [{ value: 120, label: "120 Hz" }, { value: 60, label: "60 Hz" }]
                   current: sys.refreshRate > 90 ? 120 : 60
                   onPicked: (v) => { if ((v > 90) !== (sys.refreshRate > 90)) sys.toggleRefreshRate() } } }
    CcRow { visible: desktop.wideColor; title: "Wide color gamut"; description: "More saturated colours (KWin colour management)."
        CcSwitch { checked: (config.rev, config.get("display.wcg", false)); onToggled: (v) => tweaks.setWideColor(v) } }
    // one row per active screen (Ally, external monitor, or both when extended)
    Repeater {
        model: Math.max(1, sys.displays.length)   // a count, so the sliders survive value updates
        CcRow {
            id: row
            readonly property var e: index < sys.displays.length
                ? sys.displays[index] : { id: "", label: "", internal: true, brightness: sys.brightness }
            title: sys.displays.length > 1 || (sys.displays.length === 1 && !e.internal)
                   ? "Brightness \u2014 " + e.label : "Brightness"
            CcSlider { width: 460; from: 1; to: 100; unit: " %"; value: row.e.brightness
                       onMoved: (v) => { if (row.e.id === "") sys.setBrightness(v); else sys.setDisplayBrightness(row.e.id, v) } }
        }
    }
}
