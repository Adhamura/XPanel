import QtQuick
import "../Theme.js" as T

// Settings > Display: screen colour presets (colour temperature through KWin Night Color or
// GNOME Night Light), display mode, refresh rate, brightness. KDE-only rows hide elsewhere.
CcPage {
    id: page
    readonly property var presets: [
        { id: "default", name: qsTr("Default"), note: qsTr("Native panel colours.") },
        { id: "rts", name: qsTr("RTS / RPG"), note: qsTr("A little cooler and crisper (7200 K).") },
        { id: "cinema", name: qsTr("Cinema"), note: qsTr("Warmer, film-like (5800 K).") },
        { id: "eyecare", name: qsTr("Eye Care"), note: qsTr("Less blue light (4500 K).") },
        { id: "night", name: qsTr("Night"), note: qsTr("Very warm, for the evening (3400 K).") }
    ]
    CcSection { title: qsTr("Color presets")
                note: !desktop.colorPresets ? qsTr("Not available %1.").arg(desktop.where)
                      : desktop.kde ? qsTr("Screen colour presets. They use KDE Night Color, so they apply to the whole screen.")
                      : qsTr("Screen colour presets. They use GNOME Night Light (always on), so they apply to the whole screen.") }
    Repeater {
        model: desktop.colorPresets ? page.presets : []
        CcRow { title: modelData.name; description: modelData.note
            CcButton { text: tweaks.colorPreset === modelData.id ? qsTr("Selected") : qsTr("Use"); primary: tweaks.colorPreset === modelData.id
                       onClicked: tweaks.setColorPreset(modelData.id) } }
    }
    CcSection { title: qsTr("Display")
                note: desktop.displayMode ? ""
                      : desktop.gamescope ? qsTr("Display mode, refresh rate and wide colour use KDE's display settings: in Steam Game Mode, use Steam's Display settings.")
                      : qsTr("Display mode, refresh rate and wide colour use KDE's display settings: not available %1.").arg(desktop.where) }
    CcRow { visible: desktop.displayMode; title: qsTr("Display mode")
            description: sys.hasExternal ? qsTr("Ally screen only, external screen only, the same picture on both, or one desktop over both.")
                                         : qsTr("Connect an external screen to use the other modes.")
        CcChoice { enabledChoice: sys.hasExternal
                   options: [{ value: "handheld", label: qsTr("Handheld") }, { value: "external", label: qsTr("External") },
                             { value: "duplicate", label: qsTr("Duplicate") }, { value: "extend", label: qsTr("Extend") }]
                   current: sys.displayMode; onPicked: (v) => sys.setDisplayMode(v) } }
    CcRow { visible: desktop.refreshRate; title: qsTr("Refresh rate")
        CcChoice { options: [{ value: 120, label: "120 Hz" }, { value: 60, label: "60 Hz" }]
                   current: sys.refreshRate > 90 ? 120 : 60
                   onPicked: (v) => { if ((v > 90) !== (sys.refreshRate > 90)) sys.toggleRefreshRate() } } }
    CcRow { visible: desktop.wideColor; title: qsTr("Wide color gamut"); description: qsTr("More saturated colours (KWin colour management).")
        CcSwitch { checked: (config.rev, config.get("display.wcg", false)); onToggled: (v) => tweaks.setWideColor(v) } }
    // one row per active screen (Ally, external monitor, or both when extended)
    Repeater {
        model: Math.max(1, sys.displays.length)   // a count, so the sliders survive value updates
        CcRow {
            id: row
            readonly property var e: index < sys.displays.length
                ? sys.displays[index] : { id: "", label: "", internal: true, brightness: sys.brightness }
            title: sys.displays.length > 1 || (sys.displays.length === 1 && !e.internal)
                   ? qsTr("Brightness \u2014 %1").arg(e.label) : qsTr("Brightness")
            CcSlider { width: 460; from: 1; to: 100; unit: " %"; value: row.e.brightness
                       onMoved: (v) => { if (row.e.id === "") sys.setBrightness(v); else sys.setDisplayBrightness(row.e.id, v) } }
        }
    }
}
