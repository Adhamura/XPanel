#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <atomic>

class QThread;

// Windows version of InputLayer (InputLayer.h is the Linux evdev / uinput one). It reads the
// built-in XInput pad, applies the Control Mode settings and, when they need it, plugs a virtual
// Xbox 360 pad (ViGEmBus) only for what the controller MCU cannot do; button remaps go into the MCU (AllyMcu.h).
// Desktop mode drives the mouse and keyboard with SendInput; the gyro comes from
// Windows.Devices.Sensors. The two menu buttons are read from the ASUS N-KEY HID collection.
// Code is in InputLayerWin.cpp (core, MCU, ViGEm) and GyroWin.cpp (gyrometer).
class InputLayer : public QObject {
    Q_OBJECT
public:
    explicit InputLayer(QObject *parent = nullptr);
    ~InputLayer() override;
    void setControl(const QJsonObject &control); // resolved Control Mode settings from the app
    void setGrab(bool grab);                     // true: the pad drives XPanel, not the game
    void calibrateGyro();                        // ~1 s at rest -> gyroBiasChanged()
    void setGyroBias(const QJsonArray &bias);    // [x, y, z] rad/s, device frame
    void setButtonTest(bool on);                 // true: pressed() for every logical button
    QJsonObject status() const;

signals:
    void special(const QString &name);
    void nav(const QString &name);
    void action(const QString &name);
    void pressed(const QString &name); // button test: a logical button went down
    void released(const QString &name); // button test: a logical button went up
    void axes(double lx, double ly, double rx, double ry, double lt, double rt); // button test: sticks -1..1 (y up), triggers 0..1
    void unknownKey(int code);
    void statusChanged();
    void gyroBiasChanged(const QJsonArray &bias);

private:
    struct Core; // the pad worker, ViGEm, MCU, gyro (InputLayerWin.cpp)
    void readButtons(); // runs on m_reader
    void shutdown();    // stop the threads, unplug the virtual pad, reset the MCU (safe to call twice)
    QThread *m_reader = nullptr;
    void *m_stop = nullptr; // event handle: set to end the reader and the pad worker
    std::atomic<bool> m_buttons{false}; // the menu-button collection is open
    std::atomic<bool> m_down{false};
    Core *m_core = nullptr;
};
