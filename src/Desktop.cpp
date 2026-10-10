#include "Desktop.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace Desktop {

static Kind fromName(const QString &raw)
{
    const QString n = raw.trimmed().toLower();
    if (n.isEmpty())
        return Kind::Other;
    if (n.contains("gamescope") || n == "steam")
        return Kind::Gamescope;
    if (n == "kde" || n.contains("plasma"))
        return Kind::KDE;
    if (n == "gnome" || n.startsWith("gnome-") || n == "ubuntu" || n == "ubuntu-wayland" || n == "ubuntu-xorg")
        return Kind::GNOME;
    return Kind::Other;
}

Kind detect(const QProcessEnvironment &env)
{
    const QString forced = env.value("XPANEL_DESKTOP").trimmed().toLower();
    if (forced == "kde") return Kind::KDE;
    if (forced == "gnome") return Kind::GNOME;
    if (forced == "gamescope") return Kind::Gamescope;
    if (forced == "other") return Kind::Other;

    // XDG_CURRENT_DESKTOP is a colon separated list, most specific first ("ubuntu:GNOME")
    const QStringList current = env.value("XDG_CURRENT_DESKTOP").split(':', Qt::SkipEmptyParts);
    for (const QString &c : current)
        if (fromName(c) == Kind::Gamescope)
            return Kind::Gamescope;
    for (const QString &c : current) {
        const Kind k = fromName(c);
        if (k != Kind::Other)
            return k;
    }
    // Started by gamescope (its children get this), e.g. Game Mode sessions without XDG_CURRENT_DESKTOP.
    // A known XDG_CURRENT_DESKTOP above wins: a stale value imported into the systemd user
    // manager must not turn the desktop session into Game Mode.
    if (current.isEmpty() && !env.value("GAMESCOPE_WAYLAND_DISPLAY").isEmpty())
        return Kind::Gamescope;
    if (!current.isEmpty())
        return Kind::Other;
    for (const char *var : {"XDG_SESSION_DESKTOP", "DESKTOP_SESSION"}) {
        const Kind k = fromName(env.value(var).section('/', -1)); // DESKTOP_SESSION can be a path
        if (k != Kind::Other)
            return k;
    }
    return Kind::Other;
}

Kind current()
{
    static const Kind k = detect(QProcessEnvironment::systemEnvironment());
    return k;
}

QString id(Kind k)
{
    switch (k) {
    case Kind::KDE: return "kde";
    case Kind::GNOME: return "gnome";
    case Kind::Gamescope: return "gamescope";
    case Kind::Other: break;
    }
    return "other";
}

QString label(Kind k)
{
    switch (k) {
    case Kind::KDE: return "KDE Plasma";
    case Kind::GNOME: return "GNOME";
    case Kind::Gamescope: return "Steam Game Mode";
    case Kind::Other: break;
    }
    return "Other desktop";
}

Caps capabilities(Kind k, const std::function<bool(const QString &)> &haveTool, bool haveSessionId)
{
    Caps c;
    switch (k) {
    case Kind::KDE:
        c.refreshRate = c.displayMode = c.colorPresets = c.wideColor = c.shortcuts = true;
        c.showDesktop = c.screenshot = c.logout = c.userSettings = true;
        c.screenshotTool = "spectacle";
        break;
    case Kind::GNOME:
        c.colorPresets = haveTool("gsettings");
        c.screenshot = haveTool("gnome-screenshot");
        if (c.screenshot)
            c.screenshotTool = "gnome-screenshot";
        c.logout = true; // org.gnome.SessionManager
        c.userSettings = haveTool("gnome-control-center");
        break;
    case Kind::Gamescope:
        // Steam owns the screen: screenshots (Steam + R1), power menu and keyboard are Steam's
        c.tray = false;
        break;
    case Kind::Other:
        for (const char *t : {"spectacle", "gnome-screenshot", "grim"})
            if (haveTool(t)) {
                c.screenshot = true;
                c.screenshotTool = t;
                break;
            }
        c.logout = haveSessionId && haveTool("loginctl");
        break;
    }
    return c;
}

const Caps &caps()
{
    static const Caps c = capabilities(
        current(), [](const QString &t) { return !QStandardPaths::findExecutable(t).isEmpty(); },
        !qEnvironmentVariableIsEmpty("XDG_SESSION_ID"));
    return c;
}

QStringList terminalCommand(const QString &terminal, const QStringList &cmd)
{
    if (terminal == "konsole")
        return QStringList{"--hold", "-e"} + cmd;
    if (terminal == "xterm")
        return QStringList{"-hold", "-e"} + cmd;
    // the others close with the command: wait for Enter so the output can be read
    const QStringList hold = QStringList{"sh", "-c", "\"$@\"; echo; printf 'Press Enter to close. '; read -r _", "sh"} + cmd;
    if (terminal == "x-terminal-emulator")
        return QStringList{"-e"} + hold;
    return QStringList{"--"} + hold; // kgx, ptyxis, gnome-terminal
}

bool runInTerminal(const QStringList &cmd)
{
    QStringList order;
    switch (current()) {
    case Kind::KDE: order = {"konsole", "x-terminal-emulator", "kgx", "ptyxis", "gnome-terminal", "xterm"}; break;
    case Kind::GNOME: order = {"kgx", "ptyxis", "gnome-terminal", "x-terminal-emulator", "konsole", "xterm"}; break;
    default: order = {"x-terminal-emulator", "konsole", "kgx", "ptyxis", "gnome-terminal", "xterm"}; break;
    }
    for (const QString &t : std::as_const(order))
        if (!QStandardPaths::findExecutable(t).isEmpty())
            return QProcess::startDetached(t, terminalCommand(t, cmd));
    qWarning("XPanel: no terminal program found to run %s", qPrintable(cmd.join(' ')));
    return false;
}

void showInFileManager(const QString &path, bool selectFile)
{
    if (current() == Kind::KDE && !QStandardPaths::findExecutable("dolphin").isEmpty()) {
        QProcess::startDetached("dolphin", selectFile ? QStringList{"--select", path} : QStringList{path});
        return;
    }
    // The freedesktop file manager interface: Nautilus, Dolphin, Nemo, Thunar ...
    auto bus = QDBusConnection::sessionBus();
    const QString fm = "org.freedesktop.FileManager1";
    if (bus.isConnected() && bus.interface()
        && (bus.interface()->isServiceRegistered(fm).value()
            || bus.interface()->activatableServiceNames().value().contains(fm))) {
        QDBusMessage msg = QDBusMessage::createMethodCall(fm, "/org/freedesktop/FileManager1", fm,
                                                          selectFile ? "ShowItems" : "ShowFolders");
        msg << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
        bus.asyncCall(msg);
        return;
    }
    QProcess::startDetached("xdg-open", {selectFile ? QFileInfo(path).absolutePath() : path});
}

} // namespace Desktop

QString DesktopInfo::where() const
{
    switch (Desktop::current()) {
    case Desktop::Kind::Gamescope: return "in Steam Game Mode";
    case Desktop::Kind::Other: return "on this desktop";
    default: break;
    }
    return "on " + label();
}

QStringList DesktopInfo::unavailableTiles() const
{
    const Desktop::Caps &c = Desktop::caps();
    QStringList out;
    if (!c.refreshRate) out << "refresh";
    if (!c.displayMode) out << "display";
    if (!c.showDesktop) out << "desktop";
    if (!c.screenshot) out << "screenshot";
    return out;
}
