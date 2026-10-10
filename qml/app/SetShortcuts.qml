import QtQuick
import "../Theme.js" as T

// Settings > Keyboard Shortcuts: global keys (any keyboard) to open the Quick Panel / Control Center.
CcPage {
    id: page
    property string msg
    CcSection { title: "Keyboard Shortcuts"
                note: desktop.shortcuts ? "Global shortcuts, for an external keyboard. Write keys like  Meta+Shift+C  or  Ctrl+Alt+G."
                      : "Global shortcuts use KDE's shortcut service: not available " + desktop.where + ". The Ally buttons work "
                        + "everywhere" + (desktop.gamescope ? "." : "; for a keyboard key, bind the command  XPanel --toggle  "
                        + "(or  XPanel --app ) in your desktop's keyboard settings.") }
    Repeater {
        model: !desktop.shortcuts ? [] : [{ id: "quickpanel", title: "Open / close Quick Panel" }, { id: "controlcenter", title: "Open Control Center" }]
        CcRow { title: modelData.title
            Row { spacing: 10
                CcField { id: keys; width: 260; text: tweaks.shortcut(modelData.id) }
                CcButton { text: "Apply"; onClicked: page.msg = tweaks.setShortcut(modelData.id, keys.text)
                                                           ? modelData.title + ": " + (keys.text || "none") : "Not a valid key: " + keys.text }
                CcButton { text: "Clear"; onClicked: { keys.text = ""; tweaks.setShortcut(modelData.id, "") } } } }
    }
    Text { text: page.msg; color: T.dim; font.pixelSize: 16 }
    CcRow { visible: desktop.shortcuts; title: "All KDE shortcuts"; description: "XPanel's shortcuts also appear in KDE System Settings › Shortcuts › XPanel."
        CcButton { text: "Open"; onClicked: tweaks.openSystemSettings("kcm_keys") } }
}
