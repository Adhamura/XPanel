#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <functional>

// Everything that is not in xpanel-daemon: screen brightness, volume, battery, radios,
// refresh rate, sensors, and the KDE / system actions.
class SystemControl : public QObject {
    Q_OBJECT
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY brightnessChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    Q_PROPERTY(int battery READ battery NOTIFY batteryChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY batteryChanged)
    Q_PROPERTY(bool wifi READ wifi NOTIFY radiosChanged)
    Q_PROPERTY(bool bluetooth READ bluetooth NOTIFY radiosChanged)
    Q_PROPERTY(int refreshRate READ refreshRate NOTIFY refreshChanged)
    // Display Mode: handheld | external | duplicate | extend ; hasExternal = a second screen is connected
    Q_PROPERTY(QString displayMode READ displayMode NOTIFY displayChanged)
    Q_PROPERTY(bool hasExternal READ hasExternal NOTIFY displayChanged)
    Q_PROPERTY(QString clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(bool sampling READ sampling WRITE setSampling NOTIFY samplingChanged)
    // Sensors (Real-time Monitor)
    Q_PROPERTY(int cpuLoad READ cpuLoad NOTIFY statsChanged)
    Q_PROPERTY(int cpuTemp READ cpuTemp NOTIFY statsChanged)
    Q_PROPERTY(int gpuLoad READ gpuLoad NOTIFY statsChanged)
    Q_PROPERTY(int gpuTemp READ gpuTemp NOTIFY statsChanged)
    Q_PROPERTY(int fanRpm READ fanRpm NOTIFY statsChanged)
    Q_PROPERTY(double socWatts READ socWatts NOTIFY statsChanged)
    Q_PROPERTY(double batteryWatts READ batteryWatts NOTIFY statsChanged)
    Q_PROPERTY(int ramUsedMb READ ramUsedMb NOTIFY statsChanged)

public:
    explicit SystemControl(QObject *parent = nullptr);

    int brightness() const { return m_brightness; }
    bool brightnessAvailable() const { return !m_blPath.isEmpty(); }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    int battery() const { return m_battery; }
    bool charging() const { return m_charging; }
    bool wifi() const { return m_wifi; }
    bool bluetooth() const { return m_bluetooth; }
    int refreshRate() const { return m_refresh; }
    QString displayMode() const { return m_displayMode; }
    bool hasExternal() const { return !m_extName.isEmpty(); }
    QString clock() const { return m_clock; }
    bool sampling() const { return m_sampling; }
    void setSampling(bool on);

    int cpuLoad() const { return m_cpuLoad; }
    int cpuTemp() const { return m_cpuTemp; }
    int gpuLoad() const { return m_gpuLoad; }
    int gpuTemp() const { return m_gpuTemp; }
    int fanRpm() const { return m_fanRpm; }
    double socWatts() const { return m_socWatts; }
    double batteryWatts() const { return m_batWatts; }
    int ramUsedMb() const { return m_ramMb; }

    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setWifi(bool on);
    Q_INVOKABLE void setBluetooth(bool on);
    Q_INVOKABLE void toggleRefreshRate();
    Q_INVOKABLE void setDisplayMode(const QString &mode);
    Q_INVOKABLE void showDesktop();
    Q_INVOKABLE void openKeyboard();
    Q_INVOKABLE void screenshot();
    Q_INVOKABLE void power(const QString &what); // suspend, reboot, poweroff, logout
    Q_INVOKABLE void refreshAll();

signals:
    void brightnessChanged();
    void volumeChanged();
    void batteryChanged();
    void radiosChanged();
    void refreshChanged();
    void displayChanged();
    void clockChanged();
    void samplingChanged();
    void statsChanged();

private:
    void run(const QString &prog, const QStringList &args,
             std::function<void(const QString &)> done = nullptr);
    void readBrightness();
    void readVolume();
    void readBattery();
    void readRadios();
    void readRefresh();
    void readStats();
    void tickClock();

    QString m_blPath;      // D-Bus path of the internal display (org.kde.ScreenBrightness)
    int m_blMax = 100;
    int m_brightness = 50;
    int m_volume = 50;
    bool m_muted = false;
    int m_battery = 0;
    bool m_charging = false;
    bool m_wifi = false;
    bool m_bluetooth = false;
    int m_refresh = 0;
    QString m_edpName;
    QString m_extName;           // connected external screen (DP / HDMI), empty when none
    QString m_displayMode = "handheld";
    double m_edpScale = 1.5, m_extScale = 1.0; // scales to restore after Duplicate
    QString m_clock;
    bool m_sampling = false;

    QString m_hwCpu, m_hwGpu, m_hwFan, m_gpuBusy;
    quint64 m_lastIdle = 0, m_lastTotal = 0;
    int m_cpuLoad = 0, m_cpuTemp = 0, m_gpuLoad = 0, m_gpuTemp = 0, m_fanRpm = 0, m_ramMb = 0;
    double m_socWatts = 0, m_batWatts = 0;

    QTimer m_slow;   // battery, clock, radios
    QTimer m_fast;   // sensors
    QTimer m_volumeDebounce;
    int m_pendingVolume = -1;
};
