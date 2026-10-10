#include "DesktopTweaks.h"
#include "Config.h"
#include "Desktop.h"
#include "DeviceClient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

static const char *kComponent = "xpanel";

DesktopTweaks::DesktopTweaks(Config *config, QObject *parent) : QObject(parent), m_config(config)
{
    registerShortcuts();
}

// ---------- Display colour presets ----------
// Colour temperature presets: KWin Night Color in "always on" mode, or GNOME Night Light with a
// 0:00 - 0:00 schedule (gnome-settings-daemon treats equal times as the whole day).
static int presetKelvin(const QString &p)
{
    if (p == "cinema") return 5800;
    if (p == "eyecare") return 4500;
    if (p == "night") return 3400;
    if (p == "rts") return 7200; // cooler, crisper look
    return 0;                    // default: Night Color off
}

QString DesktopTweaks::colorPreset() const
{
    return m_config->get("display.preset", "default").toString();
}

void DesktopTweaks::setColorPreset(const QString &preset)
{
    const int k = presetKelvin(preset);
    if (!Desktop::caps().colorPresets)
        return;
    if (Desktop::current() == Desktop::Kind::GNOME) {
        const QString schema = "org.gnome.settings-daemon.plugins.color";
        auto g = [&schema](const QString &key, const QString &val) {
            QProcess::execute("gsettings", {"set", schema, key, val});
        };
        if (k == 0) {
            g("night-light-enabled", "false");
        } else {
            g("night-light-schedule-automatic", "false");
            g("night-light-schedule-from", "0.0");
            g("night-light-schedule-to", "0.0");
            g("night-light-temperature", QString::number(k));
            g("night-light-enabled", "true");
        }
        m_config->set("display.preset", preset);
        emit colorPresetChanged();
        return;
    }
    auto w = [](const QString &key, const QString &val) {
        QProcess::execute("kwriteconfig6", {"--file", "kwinrc", "--group", "NightColor", "--key", key, val});
    };
    if (k == 0) {
        w("Active", "false");
    } else {
        w("Active", "true");
        w("Mode", "Constant");
        w("NightTemperature", QString::number(k));
    }
    // KWin reads its config again
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall("org.kde.KWin", "/KWin", "org.kde.KWin", "reconfigure"));
    m_config->set("display.preset", preset);
    emit colorPresetChanged();
}

void DesktopTweaks::setWideColor(bool on)
{
    if (!Desktop::caps().wideColor)
        return;
    QProcess::startDetached("sh", {"-c", QString("kscreen-doctor $(kscreen-doctor -o | sed 's/\\x1b\\[[0-9;]*m//g' "
                                                 "| awk '/^Output:/ && $3 ~ /^eDP/ {print \"output.\" $3 \".wcg.%1\"}')")
                                             .arg(on ? "enable" : "disable")});
    m_config->set("display.wcg", on);
}

// ---------- Global shortcuts (KDE kglobalaccel) ----------
static QStringList actionId(const QString &action)
{
    const QString friendly = action == "controlcenter" ? "Open Control Center" : "Open / close Quick Panel";
    return {kComponent, action, "XPanel", friendly};
}

void DesktopTweaks::registerShortcuts()
{
    // kglobalaccel is KDE's; elsewhere the Ally buttons (through xpanel-daemon) open the panel
    if (!Desktop::caps().shortcuts)
        return;
    QDBusInterface kga("org.kde.kglobalaccel", "/kglobalaccel", "org.kde.KGlobalAccel");
    if (!kga.isValid())
        return;
    for (const QString &a : {QString("quickpanel"), QString("controlcenter")}) {
        kga.call("doRegister", actionId(a));
        const QString keys = shortcut(a);
        QList<int> codes;
        if (!keys.isEmpty())
            codes << QKeySequence(keys)[0].toCombined();
        // flags: 2 = SetPresent (we are running), 4 = NoAutoloading (use our keys, not the saved ones)
        kga.call("setShortcut", actionId(a), QVariant::fromValue(codes), uint(2 | 4));
    }
    QDBusConnection::sessionBus().connect("org.kde.kglobalaccel", QString("/component/") + kComponent,
                                          "org.kde.kglobalaccel.Component", "globalShortcutPressed",
                                          this, SLOT(onGlobalShortcut(QString, QString, qlonglong)));
}

QString DesktopTweaks::shortcut(const QString &action) const
{
    const QString def = action == "controlcenter" ? "Meta+Shift+C" : "Meta+Shift+Q";
    return m_config->get("shortcut." + action, def).toString();
}

bool DesktopTweaks::setShortcut(const QString &action, const QString &keys)
{
    if (!keys.isEmpty() && QKeySequence(keys).isEmpty())
        return false;
    m_config->set("shortcut." + action, keys);
    registerShortcuts();
    return true;
}

void DesktopTweaks::onGlobalShortcut(const QString &component, const QString &action, qlonglong)
{
    if (component == kComponent)
        emit shortcutPressed(action);
}

// ---------- Feedback ----------
QString DesktopTweaks::feedbackPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/feedback.md";
}

bool DesktopTweaks::saveFeedback(const QString &topic, const QString &text)
{
    if (text.trimmed().isEmpty())
        return false;
    QDir().mkpath(QFileInfo(feedbackPath()).absolutePath());
    QFile f(feedbackPath());
    if (!f.open(QIODevice::Append))
        return false;
    f.write(QString("## %1 — %2\n\n%3\n\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"),
                                                 topic, text.trimmed()).toUtf8());
    return true;
}

void DesktopTweaks::openSystemSettings(const QString &module)
{
    static const QRegularExpression ok("^kcm_[a-z_]+$");
    if (!ok.match(module).hasMatch() || !Desktop::caps().userSettings)
        return;
    if (Desktop::current() == Desktop::Kind::GNOME) {
        // the same pages in GNOME Settings
        const QString panel = module == "kcm_users" ? "users" : module == "kcm_keys" ? "keyboard" : QString();
        QProcess::startDetached("gnome-control-center", panel.isEmpty() ? QStringList{} : QStringList{panel});
        return;
    }
    QProcess::startDetached("systemsettings", {module});
}

// ---------- App Diagnostics ----------
QVariantList DesktopTweaks::diagnostics() const
{
    QVariantList out;
    auto add = [&](const QString &name, bool ok, const QString &detail) {
        out << QVariantMap{{"name", name}, {"ok", ok}, {"detail", detail}};
    };
    QLocalSocket s;
    s.connectToServer("/run/xpanel/xpanel.sock");
    const bool daemon = s.waitForConnected(500);
    add("Desktop", true, Desktop::label(Desktop::current()) + " (XDG_CURRENT_DESKTOP="
        + qEnvironmentVariable("XDG_CURRENT_DESKTOP", "unset") + ")");
    add("xpanel-daemon", daemon, daemon ? "running (/run/xpanel/xpanel.sock)" : "not reachable: " + DeviceClient::installHint());
    if (Desktop::current() == Desktop::Kind::KDE)
        add("Wayland overlay", !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"), qEnvironmentVariable("WAYLAND_DISPLAY", "no Wayland"));
    else
        add("X11 overlay (XWayland)", !qEnvironmentVariableIsEmpty("DISPLAY"), qEnvironmentVariable("DISPLAY", "no DISPLAY"));
    add("TDP interface", QFile::exists("/sys/class/firmware-attributes/asus-armoury/attributes/ppt_pl1_spl"), "asus-armoury ppt_pl1_spl");
    bool fan = false;
    for (const QString &e : QDir("/sys/class/hwmon").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile n("/sys/class/hwmon/" + e + "/name");
        if (n.open(QIODevice::ReadOnly) && n.readAll().trimmed() == "asus_custom_fan_curve") fan = true;
    }
    add("Fan curve interface", fan, "hwmon asus_custom_fan_curve");
    add("Charge limit", QFile::exists("/sys/class/power_supply/BAT0/charge_control_end_threshold"), "BAT0 charge_control_end_threshold");
    QStringList tools{"wpctl", "pactl", "nmcli", "bluetoothctl"};
    switch (Desktop::current()) {
    case Desktop::Kind::KDE: tools << "kscreen-doctor" << "spectacle"; break;
    case Desktop::Kind::GNOME: tools << "gsettings" << "gnome-screenshot" << "ddcutil"; break;
    case Desktop::Kind::Gamescope: break;
    case Desktop::Kind::Other: tools << "ddcutil"; break;
    }
    tools << "checkupdates" << "fwupdmgr";
    for (const QString &tool : std::as_const(tools))
        add("Tool: " + tool, !QStandardPaths::findExecutable(tool).isEmpty(), QStandardPaths::findExecutable(tool));
    return out;
}
