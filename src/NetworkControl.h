#pragma once

#include <QObject>
#include <QVariantList>

// Settings > Connection: Wi-Fi networks (NetworkManager, nmcli) and Bluetooth devices (bluetoothctl).
class NetworkControl : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit NetworkControl(QObject *parent = nullptr);
    QVariantList networks() const { return m_networks; }
    QVariantList devices() const { return m_devices; }
    bool busy() const { return m_busy > 0; }
    QString message() const { return m_message; }

    Q_INVOKABLE void scanWifi();
    Q_INVOKABLE void connectWifi(const QString &ssid, const QString &password);
    Q_INVOKABLE void disconnectWifi();
    Q_INVOKABLE void scanBluetooth();
    Q_INVOKABLE void connectBluetooth(const QString &mac);
    Q_INVOKABLE void disconnectBluetooth(const QString &mac);
    Q_INVOKABLE void removeBluetooth(const QString &mac);

signals:
    void networksChanged();
    void devicesChanged();
    void busyChanged();
    void messageChanged();

private:
    void listBluetooth();
    void setMessage(const QString &m);
    void begin();
    void end();
    QVariantList m_networks, m_devices;
    int m_busy = 0;
    QString m_message;
};
