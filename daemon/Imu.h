#pragma once

#include "GyroMath.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QTimer>

class QSocketNotifier;

// The ROG Ally / Ally X motion sensor (Bosch BMI323, kernel IIO driver "bmi323-imu"; any other
// IIO device with angular velocity channels is accepted as a fallback).
//
// While running it emits the angular velocity in rad/s in the device frame (the IIO mount
// matrix applied). It prefers the IIO buffer (/dev/iio:deviceN with the device's own data-ready
// trigger, ~200 Hz) and falls back to polling the in_anglvel_*_raw files at 125 Hz when the
// buffer cannot be used (no trigger, no IRQ). If another program (hhd, InputPlumber) holds the
// buffer, the state is Busy and it tries again every few seconds. Everything it changed in
// sysfs (buffer/enable, scan elements, trigger, sampling frequency) is put back on stop().
//
// XPANEL_IIO_ROOT (default /sys/bus/iio/devices) and XPANEL_IIO_DEV (default /dev) are only
// for tests with a fake sysfs tree.
class Imu : public QObject {
    Q_OBJECT
public:
    enum class State { NoDevice, Ready, Active, Busy };

    explicit Imu(QObject *parent = nullptr);
    ~Imu() override;

    void setRunning(bool on); // start or stop reading (and the busy retries)
    bool running() const { return m_wanted; }
    State state() const { return m_state; }
    QString stateName() const; // "no device" | "ready" | "active" | "busy"
    QString method() const { return m_fd >= 0 ? "buffer" : (m_rawFd[0] >= 0 ? "poll" : ""); }
    double frequency() const { return m_freq; }

signals:
    void sample(double x, double y, double z, double dt); // rad/s, device frame; dt in s
    void stateChanged();

private:
    bool find();          // the IIO device; false: none
    bool start();         // false: busy or failed (m_state says which)
    void stop();
    bool startBuffer();
    bool startPolling();
    void readBuffer();
    void poll();
    void setState(State s);

    QString attr(const QString &name) const; // read a file of the device directory
    // write one sysfs file, remembering its old value for stop(); returns 0 or errno
    int writeAttr(const QString &path, const QByteArray &value, bool remember = true);

    QString m_root, m_devRoot;
    QString m_dir;   // .../iio:deviceN
    QString m_node;  // iio:deviceN
    QString m_name;
    Gyro::Mat3 m_mount;
    double m_scale = 1.0;
    double m_freq = 0;

    State m_state = State::NoDevice;
    bool m_wanted = false;
    bool m_busyNow = false; // the last start() found the sensor in use by another program
    QTimer m_retry, m_poll;

    // buffer
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QList<Gyro::ScanChannel> m_channels; // x, y, z, (timestamp)
    int m_axisAt[3] = {-1, -1, -1};      // index into m_channels
    int m_tsAt = -1;
    int m_record = 0;
    QByteArray m_pending;
    qint64 m_lastTs = 0;

    // polling
    int m_rawFd[3] = {-1, -1, -1};
    QElapsedTimer m_clock;

    QList<QPair<QString, QByteArray>> m_restore; // sysfs file -> old value
};
