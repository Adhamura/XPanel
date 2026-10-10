import QtQuick
import "../Theme.js" as T

// Settings > Keyboard Shortcuts: global keys (any keyboard) to open the Quick Panel / Control Center.
CcPage {
    id: page
    property string msg
    CcSection { title: "Keyboard Shortcuts"; note: "Global shortcuts, for an external keyboard. Write keys like  Meta+Shift+C  or  Ctrl+Alt+G." }
    Repeater {
        model: [{ id: "quickpanel", title: "Open / close Quick Panel" }, { id: "controlcenter", title: "Open Control Center" }]
        CcRow { title: modelData.title
            Row { spacing: 10
                CcField { id: keys; width: 260; text: tweaks.shortcut(modelData.id) }
                CcButton { text: "Apply"; onClicked: page.msg = tweaks.setShortcut(modelData.id, keys.text)
                                                           ? modelData.title + ": " + (keys.text || "none") : "Not a valid key: " + keys.text }
                CcButton { text: "Clear"; onClicked: { keys.text = ""; tweaks.setShortcut(modelData.id, "") } } } }
    }
    Text { text: page.msg; color: T.dim; font.pixelSize: 16 }
    CcRow { title: "All KDE shortcuts"; description: "XPanel's shortcuts also appear in KDE System Settings › Shortcuts › XPanel."
        CcButton { text: "Open"; onClicked: tweaks.openSystemSettings("kcm_keys") } }
}
