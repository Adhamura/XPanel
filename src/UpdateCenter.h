#pragma once

#include <QObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QJsonArray>
#include <functional>

// Settings > Update Center: system packages (pacman, AUR via paru) and firmware (fwupd).
class UpdateCenter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList packages READ packages NOTIFY changed)
    Q_PROPERTY(QVariantList firmware READ firmware NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString lastCheck READ lastCheck NOTIFY changed)
    // ROG Ally X firmware from ASUS (BIOS, controller MCU): installed vs latest
    Q_PROPERTY(QVariantList allyFirmware READ allyFirmware NOTIFY changed)
    // every device fwupd knows, with its firmware version
    Q_PROPERTY(QVariantList fwDevices READ fwDevices NOTIFY changed)
    // drivers and firmware packages relevant to the Ally (kernel, linux-firmware, microcode, Mesa ...)
    Q_PROPERTY(QVariantList drivers READ drivers NOTIFY changed)
    Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY changed)
    // Windows: ASUS file download (0..1), true while a download runs
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
    Q_PROPERTY(bool downloadBusy READ downloadBusy NOTIFY changed)
    // Windows: install of a downloaded ASUS file (UAC prompt, then wait for the installer)
    Q_PROPERTY(bool installBusy READ installBusy NOTIFY changed)
    Q_PROPERTY(QString installStatus READ installStatus NOTIFY changed)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)

public:
    using QObject::QObject;
    QVariantList packages() const { return m_packages; }
    QVariantList firmware() const { return m_firmware; }
    bool checking() const { return m_pending > 0; }
    QString lastCheck() const { return m_lastCheck; }
    QVariantList allyFirmware() const { return m_allyFw; }
    QVariantList fwDevices() const { return m_fwDevices; }
    QVariantList drivers() const { return m_drivers; }
    QString downloadStatus() const { return m_downloadStatus; }
    double downloadProgress() const { return m_downloadProgress; }
    bool downloadBusy() const { return m_dlReply != nullptr; }
    bool installBusy() const { return m_installBusy; }
    QString installStatus() const { return m_installStatus; }
    bool restartNeeded() const { return m_restartNeeded; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void updatePackages(); // opens a terminal: the update needs the password
    Q_INVOKABLE void updateFirmware();
    Q_INVOKABLE void refreshFirmwareMetadata(); // fwupdmgr refresh (LVFS lists)
    Q_INVOKABLE void downloadBios(const QString &url, const QString &version); // EZ Flash file to ~/Downloads
    // Windows: download an ASUS file (https://dlcdnets.asus.com only) to Downloads/XPanel-ASUS/<folderName>-<version>/,
    // check the SHA-256 when ASUS gives one, then show it in Explorer. Never runs the file.
    Q_INVOKABLE void download(const QString &url, const QString &version, const QString &sha256, const QString &folderName);
    Q_INVOKABLE void cancelDownload();
    // Windows: download a row (allyFirmware / drivers entry), check its SHA-256, run the installer elevated.
    // Refused when ASUS gave no checksum. Firmware needs AC power and 20 % battery.
    Q_INVOKABLE void install(const QVariantMap &row);
    Q_INVOKABLE void installAllDrivers(); // every driver row with status "update", one after another
    Q_INVOKABLE void restartNow();        // shutdown /r /t 5 (the UI asks first)
    Q_INVOKABLE void openStore(const QString &productId);
    Q_INVOKABLE void restartToFirmware();       // reboot into the BIOS setup (EZ Flash)
    Q_INVOKABLE void openUrl(const QString &url);

signals:
    void changed();

private:
    void done();
    void checkAsus();
    void checkFwDevices();
    void checkDrivers();
    void checkLinux();
    void checkWin();      // UpdateCenterWin.cpp
    void checkAsusWin();
    void checkDriversWin();
    void startDownload(const QString &url, const QString &version, const QString &sha256, const QString &folderName,
                       std::function<void(bool, const QString &)> onDone);
    void launchDownloaded(const QString &path, const QVariantMap &row);
    void runElevated(const QString &file, const QString &params, const QVariantMap &row);
    void finishInstall(bool ok, const QString &msg, bool needRestart);
    void installNext();
    void setInstallStatus(const QString &text);
    void winFetch(const QString &baseUrl, bool canRetry, std::function<void(const QJsonArray &, const QString &)> cb);
    QVariantList m_packages, m_firmware, m_collect, m_allyFw, m_fwDevices, m_drivers;
    QString m_downloadStatus;
    QNetworkAccessManager m_net;
    QMap<QString, QString> m_installed;  // pacman -Q
    QMap<QString, QString> m_fwVersions; // from fwupd: "bios" / "mcu" -> installed version
    QNetworkReply *m_dlReply = nullptr;
    double m_downloadProgress = 0;
    bool m_installBusy = false, m_restartNeeded = false;
    QString m_installStatus;
    QVariantList m_queue;
    int m_pending = 0;
    QString m_lastCheck;
};
