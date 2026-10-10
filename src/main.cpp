// XPanel - an Handheld control "Quick Panel" side panel for the
// ROG Ally / Ally X on Linux.
//
// Parts:
//   xpanel-daemon (root)   TDP, fan curve, charge limit, lighting   -> DeviceClient
//   xpanel-daemon input    Ally buttons, menu navigation, Control Mode -> DeviceClient
//   KDE / PipeWire / NM   brightness, volume, refresh rate, radios -> SystemControl
//
// Run modes:
//   XPanel              start and open the panel (or toggle a running instance)
//   XPanel --toggle     toggle the panel of the running instance
//   XPanel --open       open the panel
//   XPanel --close      close the panel
//   XPanel --quit       stop the running instance
//   XPanel --background start with only the tray icon (for autostart)
//   XPanel --app [page] open the Control Center window (library, control, gallery, user, settings)
//   XPanel --version

#include "AudioControl.h"
#include "Config.h"
#include "DesktopTweaks.h"
#include "DeviceClient.h"
#include "DeviceInfo.h"
#include "GameLibrary.h"
#include "MediaGallery.h"
#include "NetworkControl.h"
#include "UpdateCenter.h"
#include "PanelController.h"
#include "SystemControl.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QSocketNotifier>
#include <QSystemTrayIcon>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QProcess>
#include <functional>
#include <windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif


// "image://theme/<name>": icons of installed apps from the desktop icon theme.
class ThemeIconProvider : public QQuickImageProvider {
public:
    ThemeIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        const QSize s = requested.isValid() ? requested : QSize(128, 128);
        const QIcon icon = id.startsWith('/') ? QIcon(id) : QIcon::fromTheme(id, QIcon::fromTheme("applications-games"));
        const QPixmap pm = icon.pixmap(s);
        if (size) *size = pm.size();
        return pm;
    }
};

// Autostart and shortcuts can start us with a thin environment; fill in the basics.
static void repairEnvironment()
{
#ifndef Q_OS_WIN
    const QByteArray runtime = "/run/user/" + QByteArray::number(getuid());
    if (qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))
        setenv("XDG_RUNTIME_DIR", runtime.constData(), 1);
    if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"))
        setenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + runtime + "/bus").constData(), 1);
    if (qEnvironmentVariableIsEmpty("PATH"))
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
#endif
}

static PanelController::Backend chooseBackend()
{
#ifdef Q_OS_WIN
    return PanelController::Backend::X11; // a plain frameless Qt window
#else
    if (qEnvironmentVariable("XPANEL_BACKEND") == "x11") {
        setenv("QT_QPA_PLATFORM", "xcb", 1);
        return PanelController::Backend::X11;
    }
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        const QByteArray sock = qgetenv("XDG_RUNTIME_DIR") + "/wayland-0";
        if (QFile::exists(QString::fromLocal8Bit(sock)))
            setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    }
#ifdef XPANEL_HAVE_LAYERSHELL
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        setenv("QT_QPA_PLATFORM", "wayland", 1);
        return PanelController::Backend::LayerShell;
    }
#endif
    setenv("QT_QPA_PLATFORM", "xcb", 1);
    return PanelController::Backend::X11;
#endif
}

// Full path in the runtime dir, so every copy finds it, whatever TMPDIR is.
static QString serverName()
{
#ifdef Q_OS_WIN
    return "xpanel-app"; // named pipe, per user session
#else
    QByteArray dir = qgetenv("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = "/run/user/" + QByteArray::number(getuid());
    return QString::fromLocal8Bit(dir) + "/xpanel.sock";
#endif
}

// Send a command to a running instance. Returns true when one answered.
static bool sendToRunning(const QByteArray &cmd)
{
    QLocalSocket s;
    s.connectToServer(serverName());
    if (!s.waitForConnected(300))
        return false;
    s.write(cmd + "\n");
    s.flush();
    s.waitForBytesWritten(300);
    s.waitForReadyRead(300);
    return true;
}

#ifdef Q_OS_WIN
// Global hotkeys (the Ally's own buttons need the input layer, which is Linux-only so far).
class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    std::function<void(int)> onHotkey;
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG")
            return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_HOTKEY)
            return false;
        onHotkey(int(msg->wParam));
        return true;
    }
};

// xpanel-daemon runs in the user's session on Windows: start it when it is not running.
static void startDaemon()
{
    QLocalSocket probe;
    probe.connectToServer(qEnvironmentVariable("XPANEL_SOCKET", "xpanel"));
    if (probe.waitForConnected(300))
        return;
    const QString exe = QCoreApplication::applicationDirPath() + "/xpanel-daemon.exe";
    if (!QProcess::startDetached(exe, {}))
        qWarning("XPanel: cannot start %s", qPrintable(exe));
}
#else
// SIGTERM / SIGINT -> normal Qt quit (the daemon also ends our pad grab when we go).
static int g_sigFd[2] = {-1, -1};
static void onSignal(int)
{
    const char c = 1;
    (void)!::write(g_sigFd[0], &c, 1);
}
#endif

int main(int argc, char **argv)
{
    QByteArray command;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--version")) {
            printf("XPanel %s\n", XPANEL_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "--toggle")) command = "toggle";
        else if (!strcmp(argv[i], "--open")) command = "open";
        else if (!strcmp(argv[i], "--close")) command = "close";
        else if (!strcmp(argv[i], "--quit")) command = "quit";
        else if (!strcmp(argv[i], "--background")) command = "background";
        else if (!strcmp(argv[i], "--app")) {
            command = "app";
            if (i + 1 < argc && argv[i + 1][0] != '-')
                command += " " + QByteArray(argv[++i]);
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("Usage: XPanel [--toggle|--open|--close|--quit|--background|--app [page]|--version]\n");
            return 0;
        }
    }

    repairEnvironment();

    if (sendToRunning(command.isEmpty() ? "toggle" : command)) {
        fprintf(stderr, "XPanel: sent '%s' to the running instance\n",
                command.isEmpty() ? "toggle" : command.constData());
        return 0;
    }
    if (command == "quit" || command == "close")
        return 0;

    const PanelController::Backend backend = chooseBackend();

    QApplication app(argc, argv);
    app.setApplicationName("XPanel");
    app.setApplicationVersion(XPANEL_VERSION);
    app.setOrganizationName("XPanel");
    app.setDesktopFileName("xpanel");
    app.setQuitOnLastWindowClosed(false);
    const QIcon appIcon(":/qml/icons/app.svg");
    app.setWindowIcon(appIcon);

    Config config;
    DeviceClient device;
    SystemControl sys;
    PanelController panel(backend, &config);
    panel.setDevice(&device);
    // The Ally's own buttons. As on Windows: one opens the Quick Panel, the other Control Center.
    QObject::connect(&device, &DeviceClient::special, [&](const QString &name) {
        const QString open = config.button(); // which button opens the Quick Panel
        auto toggleApp = [&] { panel.appOpen() ? panel.closeApp() : panel.openApp(); };
        if (name == "left_button") {
            if (open == "left_button") panel.toggle(); else if (open == "right_button") toggleApp();
        } else if (name == "right_button") {
            if (open == "right_button") panel.toggle(); else if (open == "left_button") toggleApp();
        } else if (name == "left_button_long") {
            sys.openKeyboard();
        }
    });
    QObject::connect(&device, &DeviceClient::action, [&](const QString &name) {
        if (name == "osk") sys.openKeyboard();
    });

#ifdef Q_OS_WIN
    startDaemon();
    HotkeyFilter hotkeys;
    hotkeys.onHotkey = [&](int id) {
        if (id == 1) panel.toggle();
        else if (id == 2) panel.appOpen() ? panel.closeApp() : panel.openApp();
    };
    app.installNativeEventFilter(&hotkeys);
    RegisterHotKey(nullptr, 1, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'Q'); // Quick Panel
    RegisterHotKey(nullptr, 2, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'C'); // Control Center
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) == 0) {
        auto *sn = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, &QApplication::quit);
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
        std::signal(SIGHUP, onSignal);
    }
#endif

    // Single instance + remote commands (XPanel --toggle from a KDE shortcut).
    QLocalServer server;
#ifndef Q_OS_WIN
    QLocalServer::removeServer(serverName());
#endif
    server.listen(serverName());
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::readyRead, c, [&, c] {
                const QByteArray cmd = c->readAll().trimmed();
                c->write("ok\n");
                c->flush();
                c->disconnectFromServer();
                if (cmd == "toggle") panel.toggle();
                else if (cmd == "open") panel.requestOpen();
                else if (cmd == "close") panel.requestClose();
                else if (cmd == "quit") app.quit();
                else if (cmd.startsWith("app")) panel.openApp(QString::fromUtf8(cmd.mid(4)).trimmed());
                // "background": already running, nothing to do
            });
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });

    DeviceInfo deviceInfo;
    AudioControl audio;
    NetworkControl network;
    UpdateCenter updates;
    GameLibrary library(&config);
    MediaGallery gallery;
    DesktopTweaks tweaks(&config);
    QObject::connect(&tweaks, &DesktopTweaks::shortcutPressed, [&](const QString &action) {
        if (action == "controlcenter") panel.appOpen() ? panel.closeApp() : panel.openApp();
        else panel.toggle();
    });

    QQmlEngine engine;
    engine.addImageProvider("theme", new ThemeIconProvider);
    engine.rootContext()->setContextProperty("device", &device);
    engine.rootContext()->setContextProperty("sys", &sys);
    engine.rootContext()->setContextProperty("panel", &panel);
    engine.rootContext()->setContextProperty("config", &config);
    engine.rootContext()->setContextProperty("deviceInfo", &deviceInfo);
    engine.rootContext()->setContextProperty("audio", &audio);
    engine.rootContext()->setContextProperty("network", &network);
    engine.rootContext()->setContextProperty("updates", &updates);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("gallery", &gallery);
    engine.rootContext()->setContextProperty("tweaks", &tweaks);
    engine.rootContext()->setContextProperty("appVersion", QString(XPANEL_VERSION));
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QApplication::quit);

    QQuickView view(&engine, nullptr);
    QQuickView osd(&engine, nullptr);
    panel.setViews(&view, &osd);
    view.setSource(QUrl("qrc:/qml/Main.qml"));
    osd.setSource(QUrl("qrc:/qml/MonitorOsd.qml"));
    if (view.status() == QQuickView::Error)
        return 1;
    QQuickView appView(&engine, nullptr);
    appView.setColor(QColor("#0b0b0e"));
    panel.setAppView(&appView);
    appView.setSource(QUrl("qrc:/qml/app/AppWindow.qml"));
    if (appView.status() == QQuickView::Error)
        return 1;

    // Tray icon in the taskbar: click toggles the panel, right click has a menu.
    QSystemTrayIcon tray(QIcon(":/qml/icons/tray.svg")); // no background tile: the mark fills the tray slot
    QMenu trayMenu;
    trayMenu.addAction("Open Quick Panel", [&] { panel.requestOpen(); });
    trayMenu.addAction(QIcon::fromTheme("configure"), "Control Center", [&] { panel.openApp(); });
    QMenu *pages = trayMenu.addMenu("Control Center pages");
    for (const auto &pg : QList<QPair<QString, QString>>{
             {"library", "Game Library"}, {"control", "Control Mode"}, {"gallery", "Media Gallery"},
             {"user", "User Center"}, {"settings", "Settings"}, {"settings/performance", "Settings › Performance"},
             {"settings/lighting", "Settings › Lighting"}, {"settings/battery", "Settings › Battery Care"}})
        pages->addAction(pg.second, [&panel, id = pg.first] { panel.openApp(id); });
    trayMenu.addSeparator();
    QMenu *btnMenu = trayMenu.addMenu("Open with Ally button");
    const QList<QPair<QString, QString>> buttons = {
        {"left_button", "Left menu button"},
        {"right_button", "Right menu button"},
        {"none", "No button (tray / shortcut only)"},
    };
    for (const auto &b : buttons) {
        QAction *a = btnMenu->addAction(b.second, [&config, id = b.first] { config.setButton(id); });
        a->setCheckable(true);
        a->setChecked(config.button() == b.first);
        QObject::connect(&config, &Config::buttonChanged, a, [a, &config, id = b.first] {
            a->setChecked(config.button() == id);
        });
    }
    trayMenu.addSeparator();
    trayMenu.addAction("Quit XPanel", [&] { app.quit(); });
    tray.setContextMenu(&trayMenu);
    tray.setToolTip("XPanel Quick Panel " XPANEL_VERSION);
    QObject::connect(&tray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            panel.toggle();
    });
    tray.show();

    if (command.startsWith("app"))
        panel.openApp(QString::fromUtf8(command.mid(4)).trimmed());
    else if (command != "background")
        panel.requestOpen();

    return app.exec();
}
