#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickView>

class Config;
class DeviceClient;

// Owns the overlay windows (the panel and the Real-time Monitor OSD)
// and their show / hide state.
class PanelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString backend READ backend CONSTANT)
    Q_PROPERTY(bool appOpen READ appOpen NOTIFY appOpenChanged)
    Q_PROPERTY(QString appPage READ appPage NOTIFY appPageChanged)

public:
    enum class Backend { LayerShell, X11 };

    PanelController(Backend backend, Config *config, QObject *parent = nullptr);

    void setViews(QQuickView *panel, QQuickView *osd);
    void setDevice(DeviceClient *device); // input layer: menu navigation + grab
    void setAppView(QQuickView *app);
    bool appOpen() const { return m_appOpen; }
    QString appPage() const { return m_appPage; }

    // The full Control Center window. page: library, control, gallery, user, settings[/sub]
    Q_INVOKABLE void openApp(const QString &page = QString());
    Q_INVOKABLE void closeApp();
    bool isOpen() const { return m_open; }
    QString backend() const;

    Q_INVOKABLE void requestOpen();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void toggle();
    // Called by QML when the slide-out animation has finished.
    Q_INVOKABLE void closeFinished();
    // The window titles in the current UI language (after a language change).
    void retranslate();

signals:
    void openChanged();
    void gamepadAction(const QString &name);
    void appOpenChanged();
    void appPageChanged();
    void appAction(const QString &name); // gamepad input for the Control Center window

private:
    void preparePanelWindow();
    void prepareOsdWindow();
    void updateOsd();
    void updateGrab();
    void routeAction(const QString &name);

    Backend m_backend;
    Config *m_config;
    DeviceClient *m_device = nullptr;
    QPointer<QQuickView> m_panel;
    QPointer<QQuickView> m_osd;
    QPointer<QQuickView> m_app;
    bool m_appOpen = false;
    QString m_appPage = "library";
    bool m_open = false;
    bool m_gamescope = false; // Steam Game Mode: windows are gamescope external overlays
};
