#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <linux/input.h>

class QSocketNotifier;

// XPanel's own input layer for the ROG Ally X (runs in xpanel-daemon, as root).
//
// - Takes the Ally gamepad (evdev, hid_asus_ally) and the Ally N-KEY keyboard for itself
//   (EVIOCGRAB) and hides the raw gamepad from games (permissions 0, like other handheld tools).
// - Creates two uinput devices: an Xbox 360 pad (045e:028e) and a keyboard + mouse.
// - Applies the Control Mode settings sent by the XPanel app (key mapping with a secondary
//   function while a rear paddle is held, button repeat, stick dead zones / curves,
//   trigger ranges, vibration strength, Desktop mode mouse / scroll).
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

signals:
    void special(const QString &name);      // quickpanel, left_button_long, right_button, right_button_long
    void nav(const QString &name);          // up, down, left, right, a, b, x, y, lb, rb
    void action(const QString &name);       // functions the app runs: osk
    void unknownKey(int code);
    void statusChanged();

private:
    struct Out; // one output function

    void scan();
    void release();
    bool openPad(const QString &node);
    bool openKeyboard(const QString &node);
    bool createVirtual();
    void readPad();
    void readKeyboard();
    void readVirtualPad(); // force feedback requests from games
    void frame();          // one input frame (EV_SYN): compute and write the outputs
    void tick();           // 125 Hz: mouse / scroll from sticks, repeats, nav repeats

    void setLogical(const QString &button, bool down);
    void runFunction(const QString &fn, bool down);
    void emitKey(int fd, int code, int value);
    void syn(int fd);
    void navPress(const QString &name, bool down);

    QJsonObject preset() const;
    double shapeStick(double v, const QJsonObject &cfg) const; // -1..1 in, -1..1 out
    int shapeTrigger(int raw, const QJsonObject &cfg) const;   // 0..1023 in, 0..255 out

    int m_pad = -1, m_kbd = -1, m_vpad = -1, m_vkbm = -1;
    QString m_padNode, m_kbdNode;
    QSocketNotifier *m_padN = nullptr, *m_kbdN = nullptr, *m_vpadN = nullptr;
    QHash<QString, uint> m_savedModes; // device file -> original mode
    QTimer m_scanTimer, m_tick;

    QJsonObject m_control;
    bool m_grab = false;

    // raw pad state
    QHash<int, int> m_abs;      // ABS code -> value
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
};
