#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// Client for xpanel-daemon (/run/xpanel/xpanel.sock): TDP mode, fan, charge limit, lighting.
class DeviceClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // why the daemon refused us ("" when connected or not running)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // the command that installs the daemon for this kind of install (AppImage, /opt, source)
    Q_PROPERTY(QString installHint READ installHint CONSTANT)
    Q_PROPERTY(int rev READ rev NOTIFY stateChanged)

public:
    explicit DeviceClient(QObject *parent = nullptr);

    // true once the daemon has sent its state (a refused connection never counts)
    bool connected() const { return m_ready; }
    QString error() const { return m_error; }
    static QString installHint();
    int rev() const { return m_rev; }

    Q_INVOKABLE QVariant value(const QString &key) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    // measure the gyro offset (~1 s, the Ally lying still); result in value("gyroCalibration")
    Q_INVOKABLE void calibrateGyro();
    // true: the Ally pad drives XPanel (menus), not the game
    void setGrab(bool grab);
    // true: the daemon sends "press" events for every logical button (Control Mode button test)
    Q_INVOKABLE void setButtonTest(bool on);

signals:
    void connectedChanged();
    void errorChanged();
    void stateChanged();
    void special(const QString &name); // Ally buttons: left_button, left_button_long, right_button
    void nav(const QString &name);     // menu navigation while grabbed: up down left right a b x y lb rb
    void action(const QString &name);  // functions XPanel runs for the input layer: osk
    void released(const QString &name); // button test: a logical button went up
    void axes(const QVariantMap &values); // button test: lx ly rx ry (-1..1, y up), lt rt (0..1)
    void pressed(const QString &name);  // button test: a logical button went down (names as in ControlDefaults.js)

private:
    void connectNow();
    void onRead();
    void send(const QJsonObject &msg);

    QLocalSocket m_sock;
    QJsonObject m_state;
    QTimer m_retry;
    int m_rev = 0;
    bool m_grab = false;
    bool m_test = false;
    bool m_ready = false;
    QString m_error;
};
