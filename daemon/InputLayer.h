#pragma once

#include "GyroMath.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <linux/input.h>

class Imu;
class QSocketNotifier;

// XPanel's own input layer for the ROG Ally / Ally X (runs in xpanel-daemon, as root).
//
// - Takes the Ally gamepad (evdev: hid-asus on the Ally X, xpad on the Ally) and the Ally
//   N-KEY keyboard nodes for itself (EVIOCGRAB) and hides the raw gamepad from games
//   (permissions 0, like other handheld tools). Nodes are found by the controller's USB id
//   (Model.h) and their capabilities, not by name.
// - Creates two uinput devices: an Xbox 360 pad (045e:028e) and a keyboard + mouse.
// - Applies the Control Mode settings sent by the XPanel app (key mapping with a secondary
//   function while a rear paddle is held, button repeat, stick dead zones / curves,
//   trigger ranges, vibration strength, Desktop mode mouse / scroll, gyro as mouse or right
//   stick from the built-in IMU, Imu.h).
// - Reports the two menu buttons (left and right of the screen) and, while XPanel has the pad
//   ("grab"), menu navigation to the app.
// If the daemon stops, the kernel ends the grab and systemd restores the device permissions,
// so games see the plain Ally pad again.
class InputLayer : public QObject {
    Q_OBJECT
public:
    explicit InputLayer(QObject *parent = nullptr);
    ~InputLayer() override;

    void setControl(const QJsonObject &control); // resolved Control Mode settings from the app
    void setGrab(bool grab);                     // true: the pad drives XPanel, not the game
    QJsonObject status() const;                  // for the daemon state
    void calibrateGyro();                        // ~1 s at rest -> gyroBiasChanged()
    void setGyroBias(const QJsonArray &bias);    // [x, y, z] rad/s, device frame
    void setButtonTest(bool on);                 // true: pressed() for every logical button (Control Mode button test)

signals:
    void special(const QString &name);      // quickpanel, left_button_long, right_button, right_button_long
    void nav(const QString &name);          // up, down, left, right, a, b, x, y, lb, rb
    void action(const QString &name);       // functions the app runs: osk
    void pressed(const QString &name);      // button test: logical button went down (names as ControlDefaults.js)
    void released(const QString &name); // button test: a logical button went up
    void axes(double lx, double ly, double rx, double ry, double lt, double rt); // button test: sticks -1..1 (y up), triggers 0..1
    void unknownKey(int code);
    void statusChanged();
    void gyroBiasChanged(const QJsonArray &bias); // new calibration, to be saved

private:
    struct Out; // one output function

    void scan();
    void release();
    bool openPad(const QString &node);
    bool openKeyboard(const QString &node);
    bool createVirtual();
    void readPad();
    void readKeyboard(int fd);
    void closeKeyboard(int fd);
    double axis(int code) const;    // stick: -1..1 from the pad's own range
    double trigger(int code) const; // trigger: 0..1 from the pad's own range
    void readVirtualPad(); // force feedback requests from games
    bool vibrationOn() const;
    void applyGain();      // game gain, or 0 while Vibration is off
    void setPadGain(int gain);
    void frame();          // one input frame (EV_SYN): compute and write the outputs
    void tick();           // 125 Hz: mouse / scroll from sticks, repeats, nav repeats

    void setLogical(const QString &button, bool down);
    void runFunction(const QString &fn, bool down);
    void emitKey(int fd, int code, int value);
    void syn(int fd);
    void navPress(const QString &name, bool down);

    // gyro
    Gyro::Settings gyroSettings(QString *mode, bool *ratchet) const; // of the active preset
    void updateGyro(); // run the IMU only while gyro is on and the game has the pad (or calibrating)
    void onGyro(double x, double y, double z, double dt);
    void writeRightStick(); // physical right stick + gyro
    void finishCalibration(QString result);

    QJsonObject preset() const;
    double shapeStick(double v, const QJsonObject &cfg) const; // -1..1 in, -1..1 out
    int shapeTrigger(double p, const QJsonObject &cfg) const;  // 0..1 in, 0..255 out

    struct Keyboard {
        int fd = -1;
        QString node;
        QSocketNotifier *notifier = nullptr;
    };

    int m_pad = -1, m_vpad = -1, m_vkbm = -1;
    QString m_padNode;
    QList<Keyboard> m_kbds; // the N-KEY nodes with keys (several on the original Ally)
    QSocketNotifier *m_padN = nullptr, *m_vpadN = nullptr;
    QHash<QString, uint> m_savedModes; // device file -> original mode
    QTimer m_scanTimer, m_tick;

    QJsonObject m_control;
    bool m_grab = false;
    bool m_buttonTest = false;
    QSet<QString> m_testDown;   // logical buttons held, for pressed()

    // raw pad state
    QHash<int, int> m_abs;      // ABS code -> value
    QHash<int, QPair<int, int>> m_absRange; // ABS code -> pad's min, max
    QSet<int> m_keysDown;       // raw key codes held
    QSet<QString> m_logicalDown;// logical buttons held (A, LB, D-pad up, Left paddle ...)
    QHash<QString, QString> m_activeFn; // logical button -> function it pressed
    QSet<int> m_kbdDown;        // N-KEY keyboard keys held (Ctrl+Alt+Del = Quick Panel hold)

    // repeat (hold to repeat) and nav repeat
    QHash<QString, qint64> m_repeatAt;
    QHash<QString, qint64> m_navAt;
    QSet<QString> m_navDown;
    double m_mouseRemX = 0, m_mouseRemY = 0, m_wheelRem = 0, m_hwheelRem = 0;

    // force feedback: virtual effect id -> physical effect id
    QHash<int, int> m_ffMap;
    int m_ffGain = 0xFFFF;  // last gain a game set on the virtual pad

    // gyro
    Imu *m_imu = nullptr;
    Gyro::Vec3 m_gyroBias;
    Gyro::MouseAccumulator m_gyroMouse;
    Gyro::Stick m_rPhys, m_rGyro; // right stick: shaped physical stick, gyro part
    bool m_ltHeld = false;        // physical LT pulled (gyro "only while held")
    bool m_calibrating = false;
    double m_calSum[3] = {}, m_calSq[3] = {}, m_calTime = 0;
    int m_calN = 0;
    QString m_calResult;          // "" | running | done | moved | busy | no data | no device
    QTimer m_calTimeout;
};
