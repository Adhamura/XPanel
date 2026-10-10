#include "PanelController.h"

#include "Config.h"
#include "DeviceClient.h"

#include <QGuiApplication>
#include <QScreen>

#ifdef XPANEL_HAVE_LAYERSHELL
#include <LayerShellQt/Window>
#endif

PanelController::PanelController(Backend backend, Config *config, QObject *parent)
    : QObject(parent), m_backend(backend), m_config(config)
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
    m_app->setTitle("Control Center");
    m_app->setResizeMode(QQuickView::SizeRootObjectToView);
    m_app->setMinimumSize(QSize(960, 600));
    m_app->resize(1600, 900);
    connect(m_app, &QWindow::visibleChanged, this, [this](bool v) {
        if (!v && m_appOpen) { m_appOpen = false; emit appOpenChanged(); }
        updateGrab();
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
    // full screen on the handheld's own screen, a normal window elsewhere
    QScreen *s = QGuiApplication::primaryScreen();
    for (QScreen *sc : QGuiApplication::screens())
        if (sc->name().startsWith("eDP")) s = sc;
    if (s) m_app->setScreen(s);
    if (s && s->name().startsWith("eDP") && QGuiApplication::screens().size() == 1)
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
// and the game the rest of the time.
void PanelController::updateGrab()
{
    if (m_device)
        m_device->setGrab(m_open || (m_appOpen && m_app && m_app->isActive()));
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
}

void PanelController::prepareOsdWindow()
{
    if (!m_osd)
        return;
    m_osd->setColor(Qt::transparent);
    m_osd->setTitle("XPanel Monitor");
    m_osd->setResizeMode(QQuickView::SizeRootObjectToView);
    m_osd->resize(560, 44);
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
        m_osd->setPosition(s->geometry().right() - 560 - 12, s->geometry().top() + 12);
}

void PanelController::updateOsd()
{
    if (!m_osd)
        return;
    const bool want = m_config->monitorOsd();
    if (want && !m_osd->isVisible())
        m_osd->show();
    else if (!want && m_osd->isVisible())
        m_osd->hide();
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
    if (!m_open && m_panel && m_panel->isVisible())
        m_panel->hide();
}

void PanelController::toggle()
{
    if (m_open)
        requestClose();
    else
        requestOpen();
}
