#pragma once

#include <QObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QVariantList>

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

    Q_INVOKABLE void check();
    Q_INVOKABLE void updatePackages(); // opens a terminal: the update needs the password
    Q_INVOKABLE void updateFirmware();
    Q_INVOKABLE void refreshFirmwareMetadata(); // fwupdmgr refresh (LVFS lists)
    Q_INVOKABLE void downloadBios(const QString &url, const QString &version); // EZ Flash file to ~/Downloads
    Q_INVOKABLE void restartToFirmware();       // reboot into the BIOS setup (EZ Flash)
    Q_INVOKABLE void openUrl(const QString &url);

signals:
    void changed();

private:
    void done();
    void checkAsus();
    void checkFwDevices();
    void checkDrivers();
    QVariantList m_packages, m_firmware, m_collect, m_allyFw, m_fwDevices, m_drivers;
    QString m_downloadStatus;
    QNetworkAccessManager m_net;
    QMap<QString, QString> m_installed;  // pacman -Q
    QMap<QString, QString> m_fwVersions; // from fwupd: "bios" / "mcu" -> installed version
    int m_pending = 0;
    QString m_lastCheck;
};
