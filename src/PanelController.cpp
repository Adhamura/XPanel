#include "PanelController.h"

#include "Config.h"
#include "Desktop.h"
#include "DeviceClient.h"

#include <QGuiApplication>
#include <QScreen>

#ifdef Q_OS_WIN
#include <vector>
#include <windows.h>
#endif

#ifdef XPANEL_HAVE_XCB
#include <QtGui/qguiapplication_platform.h>
#if QT_CONFIG(xcb)
#include <xcb/xcb.h>
#include <cstdlib>
#include <cstring>
#define XPANEL_GAMESCOPE_OVERLAY 1
#endif
#endif

#ifdef XPANEL_HAVE_LAYERSHELL
#include <LayerShellQt/Window>
#endif

#ifdef Q_OS_WIN
// GDI name ("\.\DISPLAY1") of the active internal panel: QueryDisplayConfig target with an
// internal or embedded DisplayPort output. Empty when there is none.
static QString internalGdiName()
{
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS)
        return {};
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS)
        return {};
    for (UINT32 i = 0; i < np; ++i) {
        const DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY t = paths[i].targetInfo.outputTechnology;
        if (t != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL && t != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED)
            continue;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) == ERROR_SUCCESS)
            return QString::fromWCharArray(src.viewGdiDeviceName);
    }
    return {};
}
#endif

// Steam Game Mode: gamescope composites an X11 window over the game when the window has the
// CARDINAL property GAMESCOPE_EXTERNAL_OVERLAY = 1 (as MangoHud's mangoapp does). gamescope
// picks one such window (the most opaque) and does not look at whether it is mapped, so only the
// window that is shown carries the value 1. It never gets input focus: the gamepad reaches
// XPanel through xpanel-daemon, which is how the panel is driven anyway.
static void setGamescopeOverlay(QWindow *w, bool on)
{
#ifdef XPANEL_GAMESCOPE_OVERLAY
    auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (!w || !x11 || !x11->connection())
        return;
    xcb_connection_t *c = x11->connection();
    static xcb_atom_t atom = XCB_ATOM_NONE;
    if (atom == XCB_ATOM_NONE) {
        const char *name = "GAMESCOPE_EXTERNAL_OVERLAY";
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, uint16_t(strlen(name)), name), nullptr);
        if (!r)
            return;
        atom = r->atom;
        free(r);
    }
    const uint32_t value = on ? 1 : 0;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, xcb_window_t(w->winId()), atom, XCB_ATOM_CARDINAL, 32, 1, &value);
    xcb_flush(c);
#else
    Q_UNUSED(w);
    Q_UNUSED(on);
#endif
}

PanelController::PanelController(Backend backend, Config *config, QObject *parent)
    : QObject(parent), m_backend(backend), m_config(config),
      m_gamescope(backend == Backend::X11 && Desktop::current() == Desktop::Kind::Gamescope)
{
    connect(m_config, &Config::monitorOsdChanged, this, &PanelController::updateOsd);
}

void PanelController::setDevice(DeviceClient *device)
{
    m_device = device;
    connect(m_device, &DeviceClient::nav, this, &PanelController::routeAction);
}

void PanelController::setAppView(QQuickView *app)
{
    m_app = app;
    m_app->setTitle(tr("Control Center"));
    m_app->setResizeMode(QQuickView::SizeRootObjectToView);
    m_app->setMinimumSize(QSize(960, 600));
    m_app->resize(1600, 900);
    connect(m_app, &QWindow::visibleChanged, this, [this](bool v) {
        if (!v && m_gamescope)
            setGamescopeOverlay(m_app, false);
        if (!v && m_appOpen) { m_appOpen = false; emit appOpenChanged(); }
        updateGrab();
        updateOsd();
    });
    connect(m_app, &QWindow::activeChanged, this, &PanelController::updateGrab);
}

void PanelController::openApp(const QString &page)
{
    if (!page.isEmpty() && page != m_appPage) {
        m_appPage = page;
        emit appPageChanged();
    }
    if (m_open)
        requestClose();
    if (m_gamescope) {
        // Game Mode: one screen, no window manager to speak of. Cover it, as an overlay.
        if (m_panel && m_panel->isVisible()) { // no slide-out: only one overlay is drawn
            m_panel->hide();
            setGamescopeOverlay(m_panel, false);
        }
        m_app->setFlags(m_app->flags() | Qt::FramelessWindowHint);
        if (QScreen *s = QGuiApplication::primaryScreen())
            m_app->setGeometry(s->geometry());
        setGamescopeOverlay(m_app, true);
        m_app->show();
        m_appOpen = true;
        emit appOpenChanged();
        updateGrab();
        updateOsd();
        return;
    }
    // full screen on the handheld's own screen, a normal window elsewhere
    QScreen *s = QGuiApplication::primaryScreen();
    bool internal = false;
#ifdef Q_OS_WIN
    const QString gdi = internalGdiName(); // the Ally's panel; the primary screen when it is not active
    for (QScreen *sc : QGuiApplication::screens())
        if (!gdi.isEmpty() && sc->name() == gdi) { s = sc; internal = true; }
#else
    for (QScreen *sc : QGuiApplication::screens())
        if (sc->name().startsWith("eDP")) { s = sc; internal = true; }
#endif
    if (s) m_app->setScreen(s);
    if (s && internal && QGuiApplication::screens().size() == 1)
        m_app->showFullScreen();
    else
        m_app->show();
    m_app->raise();
    m_app->requestActivate();
    m_appOpen = true;
    emit appOpenChanged();
    updateGrab();
}

void PanelController::closeApp()
{
    if (m_app)
        m_app->hide();
}

// The pad drives the panel while it is open, the Control Center window while it has focus,
// and the game the rest of the time. (gamescope never focuses an overlay: there, while it is open.)
void PanelController::updateGrab()
{
    if (m_device)
        m_device->setGrab(m_open || (m_appOpen && m_app && (m_app->isActive() || m_gamescope)));
}

void PanelController::routeAction(const QString &name)
{
    if (m_open)
        emit gamepadAction(name);
    else if (m_appOpen)
        emit appAction(name);
}

QString PanelController::backend() const
{
    return m_backend == Backend::LayerShell ? "layershell" : "x11";
}

void PanelController::setViews(QQuickView *panel, QQuickView *osd)
{
    m_panel = panel;
    m_osd = osd;
    preparePanelWindow();
    prepareOsdWindow();
    updateOsd();
}

void PanelController::preparePanelWindow()
{
    m_panel->setColor(Qt::transparent);
    m_panel->setTitle("XPanel");
    m_panel->setResizeMode(QQuickView::SizeRootObjectToView);

#ifdef XPANEL_HAVE_LAYERSHELL
    if (m_backend == Backend::LayerShell) {
        if (auto *ls = LayerShellQt::Window::get(m_panel)) {
            ls->setScope("xpanel-panel");
            ls->setLayer(LayerShellQt::Window::LayerOverlay);
            ls->setAnchors(LayerShellQt::Window::Anchors(
                LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
                LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
            ls->setExclusiveZone(-1); // cover panels / taskbar too
            ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
#ifdef XPANEL_LAYERSHELL_ACTIVE_SCREEN
            ls->setWantsToBeOnActiveScreen(true);
#endif
        }
        return;
    }
#endif
    m_panel->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    if (QScreen *s = QGuiApplication::primaryScreen())
        m_panel->setGeometry(s->geometry());
    if (m_gamescope)
        setGamescopeOverlay(m_panel, false); // creates the X11 window; 1 while it is shown
}

void PanelController::prepareOsdWindow()
{
    if (!m_osd)
        return;
    m_osd->setColor(Qt::transparent);
    m_osd->setTitle(tr("XPanel Monitor"));
    m_osd->setResizeMode(QQuickView::SizeRootObjectToView);
    m_osd->resize(800, 44);
#ifdef XPANEL_HAVE_LAYERSHELL
    if (m_backend == Backend::LayerShell) {
        if (auto *ls = LayerShellQt::Window::get(m_osd)) {
            ls->setScope("xpanel-monitor");
            ls->setLayer(LayerShellQt::Window::LayerOverlay);
            ls->setAnchors(LayerShellQt::Window::Anchors(
                LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight));
            ls->setMargins(QMargins(0, 12, 12, 0));
            ls->setExclusiveZone(-1);
            ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
#ifdef XPANEL_LAYERSHELL_ACTIVE_SCREEN
            ls->setWantsToBeOnActiveScreen(true);
#endif
        }
        return;
    }
#endif
    m_osd->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                    Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    if (QScreen *s = QGuiApplication::primaryScreen())
        m_osd->setPosition(s->geometry().right() - 800 - 12, s->geometry().top() + 12);
}

void PanelController::updateOsd()
{
    if (!m_osd)
        return;
    // gamescope draws a single external overlay: the Monitor steps aside while the panel is open
    const bool want = m_config->monitorOsd() && !(m_gamescope && (m_open || m_appOpen));
    if (want && !m_osd->isVisible()) {
        if (m_gamescope)
            setGamescopeOverlay(m_osd, true);
        m_osd->show();
    } else if (!want && m_osd->isVisible()) {
        m_osd->hide();
        if (m_gamescope)
            setGamescopeOverlay(m_osd, false);
    }
}

void PanelController::requestOpen()
{
    if (m_open)
        return;
    m_open = true;
    if (m_backend == Backend::X11) {
        if (QScreen *s = QGuiApplication::primaryScreen())
            m_panel->setGeometry(s->geometry());
    }
    if (m_gamescope) {
        if (m_appOpen && m_app) // one overlay at a time: the panel replaces the Control Center
            m_app->hide();
        updateOsd(); // and the Monitor
        setGamescopeOverlay(m_panel, true);
    }
    // GNOME (XWayland): full screen, so the top bar does not cover the panel's header
    if (m_backend == Backend::X11 && Desktop::current() == Desktop::Kind::GNOME)
        m_panel->showFullScreen();
    else
        m_panel->show();
    m_panel->raise();
    m_panel->requestActivate();
    emit openChanged();
    updateGrab(); // the controller drives the panel, not the game
}

void PanelController::requestClose()
{
    if (!m_open)
        return;
    m_open = false;
    emit openChanged();
    updateGrab(); // give the controller back at once; the slide-out is cosmetic
}

void PanelController::closeFinished()
{
    if (!m_open && m_panel && m_panel->isVisible()) {
        m_panel->hide();
        if (m_gamescope) {
            setGamescopeOverlay(m_panel, false);
            updateOsd();
        }
    }
}

void PanelController::retranslate()
{
    if (m_app)
        m_app->setTitle(tr("Control Center"));
    if (m_osd)
        m_osd->setTitle(tr("XPanel Monitor"));
}

void PanelController::toggle()
{
    if (m_open)
        requestClose();
    else
        requestOpen();
}
