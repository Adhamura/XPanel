import QtQuick
import "../Theme.js" as T

// Settings > Keyboard Shortcuts: global keys (any keyboard) to open the Quick Panel / Control Center.
CcPage {
    id: page
    property string msg
    CcSection { title: qsTr("Keyboard Shortcuts")
                note: desktop.shortcuts ? qsTr("Global shortcuts, for an external keyboard. Write keys like  Meta+Shift+C  or  Ctrl+Alt+G.")
                      : qsTr("Global shortcuts use KDE's shortcut service: not available %1.").arg(desktop.where) + " "
                        + (desktop.gamescope ? qsTr("The Ally buttons work everywhere.")
                           : qsTr("The Ally buttons work everywhere; for a keyboard key, bind the command  XPanel --toggle  (or  XPanel --app ) in your desktop's keyboard settings.")) }
    Repeater {
        model: !desktop.shortcuts ? [] : [{ id: "quickpanel", title: qsTr("Open / close Quick Panel") }, { id: "controlcenter", title: qsTr("Open Control Center") }]
        CcRow { title: modelData.title
            Row { spacing: 10
                CcField { id: keys; width: 260; text: tweaks.shortcut(modelData.id) }
                CcButton { text: qsTr("Apply"); onClicked: page.msg = tweaks.setShortcut(modelData.id, keys.text)
                                                           ? qsTr("%1: %2").arg(modelData.title).arg(keys.text || qsTr("none")) : qsTr("Not a valid key: %1").arg(keys.text) }
                CcButton { text: qsTr("Clear"); onClicked: { keys.text = ""; tweaks.setShortcut(modelData.id, "") } } } }
    }
    Text { text: page.msg; color: T.dim; font.pixelSize: 16 }
    CcRow { visible: desktop.shortcuts; title: qsTr("All KDE shortcuts"); description: qsTr("XPanel's shortcuts also appear in KDE System Settings › Shortcuts › XPanel.")
        CcButton { text: qsTr("Open"); onClicked: tweaks.openSystemSettings("kcm_keys") } }
}
