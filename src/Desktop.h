#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <functional>

// Which desktop XPanel runs in, and what it can do there. One place for the decision:
// everything KDE-specific (KWin, kscreen-doctor, kglobalaccel, Spectacle ...) asks here first.
//
//   KDE        KDE Plasma 6: everything (the reference desktop)
//   GNOME      Mutter: no layer-shell (the panel runs on XWayland), brightness through logind,
//              Night Light presets, GNOME tools; no refresh rate / display mode / shortcuts
//   Gamescope  Steam Game Mode (SteamOS, Bazzite ...): the panel is a gamescope external overlay
//   Windows    Windows 11 (experimental): SystemControlWin.cpp
//   Other      any other desktop: generic tools (xdg-open, x-terminal-emulator), no KDE features
namespace Desktop {

enum class Kind { KDE, GNOME, Gamescope, Other, Windows };

// Pure function of the environment (unit tested):
//   XPANEL_DESKTOP=kde|gnome|gamescope|other   override (testing, unusual sessions)
//   XDG_CURRENT_DESKTOP                         "KDE", "GNOME", "ubuntu:GNOME", "gamescope" ...
//   GAMESCOPE_WAYLAND_DISPLAY                   set inside a gamescope session
//   XDG_SESSION_DESKTOP, DESKTOP_SESSION        "plasma", "gnome", "gamescope-wayland" ...
Kind detect(const QProcessEnvironment &env);
Kind current(); // detect() on this process's environment, decided once
QString id(Kind k);    // kde | gnome | gamescope | other
QString label(Kind k); // "KDE Plasma", "GNOME", "Steam Game Mode", "Other desktop"

// What works where. haveTool(name): is the program installed (QStandardPaths::findExecutable).
struct Caps {
    bool refreshRate = false;  // 60 / 120 Hz switch (kscreen-doctor)
    bool displayMode = false;  // Handheld / External / Duplicate / Extend (kscreen-doctor)
    bool colorPresets = false; // KWin Night Color or GNOME Night Light
    bool wideColor = false;    // KWin wide colour gamut
    bool shortcuts = false;    // global keyboard shortcuts (kglobalaccel)
    bool showDesktop = false;
    bool screenshot = false;
    bool logout = false;
    bool userSettings = false; // "Edit profile" in the desktop's settings app
    bool tray = true;          // system tray icon
    QString screenshotTool;    // program for screenshot()
};
Caps capabilities(Kind k, const std::function<bool(const QString &)> &haveTool, bool haveSessionId);
const Caps &caps(); // for current()

// Launchers that pick the right program for the desktop (fallbacks for unknown desktops).
// A terminal that runs cmd and stays open until Enter is pressed. false: no terminal found.
bool runInTerminal(const QStringList &cmd);
// The file manager on a folder, or with a file selected (org.freedesktop.FileManager1, xdg-open).
void showInFileManager(const QString &path, bool selectFile);
// Command line for runInTerminal() with the given terminal program (unit tested).
QStringList terminalCommand(const QString &terminal, const QStringList &cmd);

} // namespace Desktop

// The desktop and its capabilities for QML ("desktop" context property).
class DesktopInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    // for notes: "on GNOME", "in Steam Game Mode", "on this desktop"
    Q_PROPERTY(QString where READ where CONSTANT)
    Q_PROPERTY(bool kde READ kde CONSTANT)
    Q_PROPERTY(bool gamescope READ gamescope CONSTANT)
    Q_PROPERTY(bool refreshRate READ refreshRate CONSTANT)
    Q_PROPERTY(bool displayMode READ displayMode CONSTANT)
    Q_PROPERTY(bool colorPresets READ colorPresets CONSTANT)
    Q_PROPERTY(bool wideColor READ wideColor CONSTANT)
    Q_PROPERTY(bool shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(bool showDesktop READ showDesktop CONSTANT)
    Q_PROPERTY(bool screenshot READ screenshot CONSTANT)
    Q_PROPERTY(bool logout READ logout CONSTANT)
    Q_PROPERTY(bool userSettings READ userSettings CONSTANT)
    // XPanel runs on Windows (Windows-only sections, Linux-only sections hidden)
    Q_PROPERTY(bool windows READ windows CONSTANT)
    // Quick Panel tiles that do nothing on this desktop (hidden from the panel and the editor)
    Q_PROPERTY(QStringList unavailableTiles READ unavailableTiles CONSTANT)

public:
    using QObject::QObject;
    QString id() const { return Desktop::id(Desktop::current()); }
    QString label() const { return Desktop::label(Desktop::current()); }
    QString where() const;
    bool kde() const { return Desktop::current() == Desktop::Kind::KDE; }
    bool gamescope() const { return Desktop::current() == Desktop::Kind::Gamescope; }
    bool refreshRate() const { return Desktop::caps().refreshRate; }
    bool displayMode() const { return Desktop::caps().displayMode; }
    bool colorPresets() const { return Desktop::caps().colorPresets; }
    bool wideColor() const { return Desktop::caps().wideColor; }
    bool shortcuts() const { return Desktop::caps().shortcuts; }
    bool showDesktop() const { return Desktop::caps().showDesktop; }
    bool screenshot() const { return Desktop::caps().screenshot; }
    bool logout() const { return Desktop::caps().logout; }
    bool userSettings() const { return Desktop::caps().userSettings; }
#ifdef Q_OS_WIN
    bool windows() const { return true; }
#else
    bool windows() const { return false; }
#endif
    QStringList unavailableTiles() const;
};
