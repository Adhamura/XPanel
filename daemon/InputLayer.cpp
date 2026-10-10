#include "InputLayer.h"
#include "Model.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSocketNotifier>

#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace {

QString readFile(const QString &fn)
{
    QFile f(fn);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

bool testBit(int fd, int ev, int code)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(ev, sizeof(bits)), bits) < 0)
        return false;
    return bits[code / (8 * sizeof(long))] & (1UL << (code % (8 * sizeof(long))));
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Raw Ally key code -> logical button (names match the Control Mode page)
const QHash<int, QString> kRawButtons = {
    {BTN_SOUTH, "A"}, {BTN_EAST, "B"}, {BTN_NORTH, "X"}, {BTN_WEST, "Y"},
    {BTN_TL, "LB"}, {BTN_TR, "RB"}, {BTN_SELECT, "View"}, {BTN_START, "Menu"},
    {BTN_THUMBL, "L3"}, {BTN_THUMBR, "R3"}, {BTN_MODE, "Guide"},
    {KEY_F17, "Left paddle"}, {KEY_F18, "Right paddle"},
};
// Special buttons the app handles
const QHash<int, QString> kRawSpecial = {
    {KEY_F16, "left_button"}, {KEY_PROG1, "right_button"},
};

// "pad:<name>" -> Xbox pad button code
const QHash<QString, int> kPadButtons = {
    {"A", BTN_SOUTH}, {"B", BTN_EAST}, {"X", BTN_NORTH}, {"Y", BTN_WEST},
    {"LB", BTN_TL}, {"RB", BTN_TR}, {"View", BTN_SELECT}, {"Menu", BTN_START},
    {"L3", BTN_THUMBL}, {"R3", BTN_THUMBR}, {"Guide", BTN_MODE},
};

const QHash<QString, int> &keyCodes()
{
    static QHash<QString, int> k;
    if (!k.isEmpty())
        return k;
    k = {{"Esc", KEY_ESC}, {"Enter", KEY_ENTER}, {"Space", KEY_SPACE}, {"Tab", KEY_TAB},
         {"Backspace", KEY_BACKSPACE}, {"Shift", KEY_LEFTSHIFT}, {"Ctrl", KEY_LEFTCTRL},
         {"Alt", KEY_LEFTALT}, {"Meta", KEY_LEFTMETA}, {"Up", KEY_UP}, {"Down", KEY_DOWN},
         {"Left", KEY_LEFT}, {"Right", KEY_RIGHT}, {"Page Up", KEY_PAGEUP}, {"Page Down", KEY_PAGEDOWN},
         {"Home", KEY_HOME}, {"End", KEY_END},
         {"Num 0", KEY_KP0}, {"Num 1", KEY_KP1}, {"Num 2", KEY_KP2}, {"Num 3", KEY_KP3}, {"Num 4", KEY_KP4},
         {"Num 5", KEY_KP5}, {"Num 6", KEY_KP6}, {"Num 7", KEY_KP7}, {"Num 8", KEY_KP8}, {"Num 9", KEY_KP9},
         {"Num +", KEY_KPPLUS}, {"Num -", KEY_KPMINUS}, {"Num *", KEY_KPASTERISK}, {"Num /", KEY_KPSLASH},
         {"Num Enter", KEY_KPENTER}, {"Num .", KEY_KPDOT}};
    const int fkeys[] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};
    for (int i = 0; i < 12; ++i)
        k.insert(QString("F%1").arg(i + 1), fkeys[i]);
    const char *letters = "QWERTYUIOPASDFGHJKLZXCVBNM";
    const int lcodes[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D,
                          KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M};
    for (int i = 0; i < 26; ++i)
        k.insert(QString(QChar(letters[i])), lcodes[i]);
    const int dcodes[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    for (int i = 0; i < 10; ++i)
        k.insert(QString::number(i), dcodes[i]);
    return k;
}

const QHash<QString, QList<int>> kCombos = {
    {"Alt+Tab", {KEY_LEFTALT, KEY_TAB}}, {"Alt+F4", {KEY_LEFTALT, KEY_F4}},
    {"Ctrl+C", {KEY_LEFTCTRL, KEY_C}}, {"Ctrl+V", {KEY_LEFTCTRL, KEY_V}},
    {"Ctrl+Z", {KEY_LEFTCTRL, KEY_Z}}, {"Ctrl+S", {KEY_LEFTCTRL, KEY_S}},
    {"Meta+D (desktop)", {KEY_LEFTMETA, KEY_D}}, {"Screenshot", {KEY_SYSRQ}},
    {"Volume up", {KEY_VOLUMEUP}}, {"Volume down", {KEY_VOLUMEDOWN}}, {"Mute", {KEY_MUTE}},
};

const QHash<QString, int> kMouseButtons = {
    {"Left click", BTN_LEFT}, {"Right click", BTN_RIGHT}, {"Middle click", BTN_MIDDLE},
    {"Back", BTN_SIDE}, {"Forward", BTN_EXTRA},
};

} // namespace

// ---------------------------------------------------------------- setup

InputLayer::InputLayer(QObject *parent) : QObject(parent)
{
    connect(&m_scanTimer, &QTimer::timeout, this, &InputLayer::scan);
    m_scanTimer.start(3000);
    m_tick.setInterval(8); // 125 Hz (menu repeat, mouse / scroll, button repeat)
    connect(&m_tick, &QTimer::timeout, this, &InputLayer::tick);
    m_tick.start();
    scan();
}

InputLayer::~InputLayer()
{
    release();
}

QJsonObject InputLayer::status() const
{
    QString st = "no device";
    if (m_pad >= 0 && m_vpad >= 0) st = "active";
    else if (!m_padNode.isEmpty()) st = "busy";
    return {{"inputLayer", st}, {"inputPad", m_padNode}, {"inputGrab", m_grab}};
}

void InputLayer::scan()
{
    const QDir dir("/sys/class/input");
    for (const QString &e : dir.entryList({"event*"}, QDir::Dirs | QDir::System)) {
        if ((m_pad >= 0 && e == m_padNode)
            || std::any_of(m_kbds.cbegin(), m_kbds.cend(), [&e](const Keyboard &k) { return k.node == e; }))
            continue; // ours already
        const QString base = dir.absoluteFilePath(e) + "/device/";
        // The built-in controller (any supported model), never a virtual device: XPanel's own
        // keyboard / mouse carries the ASUS id on BUS_VIRTUAL, and its pad is a uinput device too.
        const quint16 vendor = readFile(base + "id/vendor").toUShort(nullptr, 16);
        const quint16 product = readFile(base + "id/product").toUShort(nullptr, 16);
        if (!Model::isController(vendor, product))
            continue;
        const QString name = readFile(base + "name");
        if (readFile(base + "id/bustype").toUShort(nullptr, 16) == BUS_VIRTUAL || name.startsWith("XPanel")
            || QFileInfo(base).canonicalFilePath().contains("/devices/virtual/"))
            continue;
        // What the node is, from its capabilities (names differ between the models and drivers).
        const int probe = ::open(("/dev/input/" + e).toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (probe < 0)
            continue;
        const bool pad = testBit(probe, EV_ABS, ABS_X) && testBit(probe, EV_KEY, BTN_SOUTH);
        const bool keys = !pad && (testBit(probe, EV_KEY, KEY_F16) || testBit(probe, EV_KEY, KEY_PROG1)
                                   || testBit(probe, EV_KEY, KEY_DELETE));
        ::close(probe);
        if (pad && m_pad < 0)
            openPad(e);
        else if (keys)
            openKeyboard(e); // the N-KEY keyboard nodes (the N-KEY mouse node has none of these keys)
    }
}

static int grabNode(const QString &node, QString *why)
{
    const int fd = ::open(("/dev/input/" + node).toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { *why = QString("open: %1").arg(strerror(errno)); return -1; }
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        *why = errno == EBUSY ? "in use by another program (InputPlumber?)" : QString("grab: %1").arg(strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

bool InputLayer::openPad(const QString &node)
{
    m_padNode = node;
    QString why;
    const int fd = grabNode(node, &why);
    if (fd < 0) {
        static QString lastWhy;
        if (why != lastWhy) qWarning("xpanel-daemon input: cannot take %s: %s", qPrintable(node), qPrintable(why));
        lastWhy = why;
        emit statusChanged();
        return false;
    }
    m_pad = fd;
    // Hide the raw pad (event node, joystick node, hidraw) from games: they use our virtual pad.
    QStringList files{"/dev/input/" + node};
    const QString dev = "/sys/class/input/" + node + "/device/";
    for (const QString &js : QDir(dev).entryList({"js*"}, QDir::Dirs))
        files << "/dev/input/" + js;
    for (const QString &h : QDir(dev + "device/hidraw").entryList({"hidraw*"}, QDir::Dirs))
        files << "/dev/" + h;
    for (const QString &f : files) {
        struct stat st {};
        if (::stat(f.toLocal8Bit().constData(), &st) == 0) {
            m_savedModes.insert(f, st.st_mode & 07777);
            ::chmod(f.toLocal8Bit().constData(), 0);
        }
    }
    m_padN = new QSocketNotifier(m_pad, QSocketNotifier::Read, this);
    connect(m_padN, &QSocketNotifier::activated, this, &InputLayer::readPad);
    // start from the real stick / trigger positions
    for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
        input_absinfo ai {};
        if (ioctl(m_pad, EVIOCGABS(code), &ai) == 0) {
            m_abs[code] = ai.value;
            // Ally X (hid-asus): sticks 0..65535, triggers 0..1023; Ally (xpad): sticks
            // -32768..32767, triggers 0..255
            if (ai.maximum > ai.minimum)
                m_absRange[code] = {ai.minimum, ai.maximum};
        }
    }
    if (m_vpad < 0)
        createVirtual();
    qInfo("xpanel-daemon input: using %s (%s), virtual Xbox 360 pad ready", qPrintable(node),
          qPrintable(readFile(dev + "name")));
    emit statusChanged();
    return true;
}

bool InputLayer::openKeyboard(const QString &node)
{
    // an N-KEY keyboard node: Ctrl+Alt+Del here = Quick Panel held; on the original Ally the
    // menu buttons (KEY_F16, KEY_PROG1) arrive here too
    QString why;
    const int fd = grabNode(node, &why);
    if (fd < 0) return false;
    Keyboard k;
    k.fd = fd;
    k.node = node;
    k.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(k.notifier, &QSocketNotifier::activated, this, [this, fd] { readKeyboard(fd); });
    m_kbds.append(k);
    if (m_vkbm < 0)
        createVirtual();
    return true;
}

void InputLayer::closeKeyboard(int fd)
{
    for (int i = 0; i < m_kbds.size(); ++i) {
        if (m_kbds[i].fd != fd) continue;
        m_kbds[i].notifier->setEnabled(false);
        m_kbds[i].notifier->deleteLater(); // may be called from its own activated()
        ioctl(fd, EVIOCGRAB, 0);
        ::close(fd);
        m_kbds.removeAt(i);
        return;
    }
}

bool InputLayer::createVirtual()
{
    if (m_vpad < 0) {
        const int fd = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { qWarning("xpanel-daemon input: /dev/uinput: %s", strerror(errno)); return false; }
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (int b : kPadButtons) ioctl(fd, UI_SET_KEYBIT, b);
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        auto absSetup = [fd](int code, int min, int max, int fuzz, int flat) {
            uinput_abs_setup s {};
            s.code = code;
            s.absinfo.minimum = min; s.absinfo.maximum = max; s.absinfo.fuzz = fuzz; s.absinfo.flat = flat;
            ioctl(fd, UI_ABS_SETUP, &s);
        };
        for (int c : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) absSetup(c, -32768, 32767, 16, 128);
        absSetup(ABS_Z, 0, 255, 0, 0);
        absSetup(ABS_RZ, 0, 255, 0, 0);
        absSetup(ABS_HAT0X, -1, 1, 0, 0);
        absSetup(ABS_HAT0Y, -1, 1, 0, 0);
        ioctl(fd, UI_SET_EVBIT, EV_FF);
        ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
        ioctl(fd, UI_SET_FFBIT, FF_GAIN);
        uinput_setup us {};
        us.id.bustype = BUS_USB; us.id.vendor = 0x045e; us.id.product = 0x028e; us.id.version = 0x0114;
        strncpy(us.name, "Microsoft X-Box 360 pad", UINPUT_MAX_NAME_SIZE - 1);
        us.ff_effects_max = 16;
        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            qWarning("xpanel-daemon input: cannot create the virtual pad: %s", strerror(errno));
            ::close(fd);
            return false;
        }
        m_vpad = fd;
        m_vpadN = new QSocketNotifier(m_vpad, QSocketNotifier::Read, this);
        connect(m_vpadN, &QSocketNotifier::activated, this, &InputLayer::readVirtualPad);
    }
    if (m_vkbm < 0) {
        const int fd = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (int k = 1; k < 249; ++k) ioctl(fd, UI_SET_KEYBIT, k);
        for (int b : kMouseButtons) ioctl(fd, UI_SET_KEYBIT, b);
        ioctl(fd, UI_SET_EVBIT, EV_REL);
        for (int r : {REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES})
            ioctl(fd, UI_SET_RELBIT, r);
        uinput_setup us {};
        us.id.bustype = BUS_VIRTUAL; us.id.vendor = 0x0b05; us.id.product = 0x1b4c; us.id.version = 1;
        strncpy(us.name, "XPanel Keyboard Mouse", UINPUT_MAX_NAME_SIZE - 1);
        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            ::close(fd);
            return false;
        }
        m_vkbm = fd;
    }
    return true;
}

void InputLayer::release()
{
    for (auto it = m_savedModes.begin(); it != m_savedModes.end(); ++it)
        ::chmod(it.key().toLocal8Bit().constData(), it.value());
    m_savedModes.clear();
    delete m_padN; m_padN = nullptr;
    delete m_vpadN; m_vpadN = nullptr;
    if (m_pad >= 0) { ioctl(m_pad, EVIOCGRAB, 0); ::close(m_pad); m_pad = -1; }
    while (!m_kbds.isEmpty()) closeKeyboard(m_kbds.first().fd);
    if (m_vpad >= 0) { ioctl(m_vpad, UI_DEV_DESTROY); ::close(m_vpad); m_vpad = -1; }
    if (m_vkbm >= 0) { ioctl(m_vkbm, UI_DEV_DESTROY); ::close(m_vkbm); m_vkbm = -1; }
}

// ---------------------------------------------------------------- settings

void InputLayer::setControl(const QJsonObject &control)
{
    m_control = control;
}

QJsonObject InputLayer::preset() const
{
    const QString p = m_control["preset"].toString("gamepad");
    return m_control[p].toObject();
}

void InputLayer::setGrab(bool grab)
{
    if (grab == m_grab)
        return;
    m_grab = grab;
    // release everything the game held, so nothing stays pressed while XPanel has the pad
    for (const QString &b : std::as_const(m_activeFn))
        runFunction(b, false);
    m_activeFn.clear();
    m_logicalDown.clear();
    m_navDown.clear();
    m_navAt.clear();
    if (m_vpad >= 0) {
        for (int c : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y})
            emitKey(m_vpad, -c - 1, 0); // negative = ABS code
        syn(m_vpad);
    }
    emit statusChanged();
}

// ---------------------------------------------------------------- output helpers

void InputLayer::emitKey(int fd, int code, int value)
{
    if (fd < 0) return;
    input_event ev {};
    if (code < 0) { ev.type = EV_ABS; ev.code = -code - 1; }
    else { ev.type = EV_KEY; ev.code = code; }
    ev.value = value;
    (void)!::write(fd, &ev, sizeof(ev));
}

void InputLayer::syn(int fd)
{
    if (fd < 0) return;
    input_event ev {};
    ev.type = EV_SYN; ev.code = SYN_REPORT;
    (void)!::write(fd, &ev, sizeof(ev));
}

static void emitRel(int fd, int code, int value)
{
    if (fd < 0 || value == 0) return;
    input_event ev {};
    ev.type = EV_REL; ev.code = code; ev.value = value;
    (void)!::write(fd, &ev, sizeof(ev));
}

// Press / release one mapped function ("kind:name").
void InputLayer::runFunction(const QString &fn, bool down)
{
    const QString kind = fn.section(':', 0, 0), name = fn.section(':', 1);
    const int v = down ? 1 : 0;
    if (kind == "pad") {
        if (kPadButtons.contains(name)) { emitKey(m_vpad, kPadButtons[name], v); syn(m_vpad); }
        else if (name == "LT") { emitKey(m_vpad, -ABS_Z - 1, down ? 255 : 0); syn(m_vpad); }
        else if (name == "RT") { emitKey(m_vpad, -ABS_RZ - 1, down ? 255 : 0); syn(m_vpad); }
        else if (name == "D-pad up") { emitKey(m_vpad, -ABS_HAT0Y - 1, down ? -1 : 0); syn(m_vpad); }
        else if (name == "D-pad down") { emitKey(m_vpad, -ABS_HAT0Y - 1, down ? 1 : 0); syn(m_vpad); }
        else if (name == "D-pad left") { emitKey(m_vpad, -ABS_HAT0X - 1, down ? -1 : 0); syn(m_vpad); }
        else if (name == "D-pad right") { emitKey(m_vpad, -ABS_HAT0X - 1, down ? 1 : 0); syn(m_vpad); }
    } else if (kind == "key" || kind == "numpad") {
        if (keyCodes().contains(name)) { emitKey(m_vkbm, keyCodes()[name], v); syn(m_vkbm); }
    } else if (kind == "mouse") {
        if (kMouseButtons.contains(name)) { emitKey(m_vkbm, kMouseButtons[name], v); syn(m_vkbm); }
        else if (down && (name == "Wheel up" || name == "Wheel down")) {
            const int d = name == "Wheel up" ? 1 : -1;
            emitRel(m_vkbm, REL_WHEEL, d);
            emitRel(m_vkbm, REL_WHEEL_HI_RES, d * 120);
            syn(m_vkbm);
        }
    } else if (kind == "combo") {
        if (name == "On-screen keyboard") { if (down) emit action("osk"); return; }
        const QList<int> keys = kCombos.value(name);
        if (down) for (int k : keys) emitKey(m_vkbm, k, 1);
        else for (auto it = keys.rbegin(); it != keys.rend(); ++it) emitKey(m_vkbm, *it, 0);
        syn(m_vkbm);
    }
}

// ---------------------------------------------------------------- input

void InputLayer::readPad()
{
    input_event ev[64];
    for (;;) {
        const ssize_t n = ::read(m_pad, ev, sizeof(ev));
        if (n <= 0) {
            if (n < 0 && errno == ENODEV) { // unplugged / suspended: look again later
                qInfo("xpanel-daemon input: gamepad gone, waiting for it");
                delete m_padN; m_padN = nullptr;
                ::close(m_pad); m_pad = -1;
                m_savedModes.clear();
                emit statusChanged();
            }
            return;
        }
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event &e = ev[i];
            if (e.type == EV_KEY) {
                if (e.value == 2) continue;
                if (kRawSpecial.contains(e.code)) {
                    if (e.value) emit special(kRawSpecial[e.code]);
                    continue;
                }
                if (!kRawButtons.contains(e.code)) {
                    if (e.value) { qInfo("xpanel-daemon input: unknown key %d", e.code); emit unknownKey(e.code); }
                    continue;
                }
                if (e.value) m_keysDown.insert(e.code); else m_keysDown.remove(e.code);
            } else if (e.type == EV_ABS) {
                m_abs[e.code] = e.value;
            } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                frame();
            }
        }
    }
}

void InputLayer::readKeyboard(int fd)
{
    input_event ev[64];
    for (;;) {
        const ssize_t n = ::read(fd, ev, sizeof(ev));
        if (n <= 0) {
            if (n < 0 && errno == ENODEV) closeKeyboard(fd);
            return;
        }
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event &e = ev[i];
            if (e.type != EV_KEY || e.value == 2) continue;
            // Original Ally (hid-asus): the menu buttons come from the N-KEY device, not the pad.
            // Left = KEY_F16, right (ROG) = KEY_PROG1, right held = KEY_F17 then KEY_F18.
            if (kRawSpecial.contains(e.code)) {
                if (e.value) emit special(kRawSpecial[e.code]);
                continue;
            }
            if (e.code == KEY_F17 || e.code == KEY_F18) {
                if (e.code == KEY_F17 && e.value) emit special("right_button_long");
                continue;
            }
            if (e.value) m_kbdDown.insert(e.code); else m_kbdDown.remove(e.code);
            // Quick Panel held: the firmware sends Ctrl+Alt+Del. Never pass that on.
            const bool combo = m_kbdDown.contains(KEY_LEFTCTRL) && m_kbdDown.contains(KEY_LEFTALT);
            if (e.code == KEY_DELETE && e.value && combo) { emit special("left_button_long"); continue; }
            if (e.code == KEY_LEFTCTRL || e.code == KEY_LEFTALT || e.code == KEY_DELETE) continue;
            // anything else (Ally macro keys) goes on unchanged
            emitKey(m_vkbm, e.code, e.value);
            syn(m_vkbm);
        }
    }
}

// Force feedback from games on the virtual pad -> the real motors, scaled by Vibration.
void InputLayer::readVirtualPad()
{
    input_event ev[32];
    const ssize_t n = ::read(m_vpad, ev, sizeof(ev));
    if (n <= 0) return;
    const QJsonObject vib = preset()["vibration"].toObject();
    const double left = vib["left"].toDouble(100) / 100.0, right = vib["right"].toDouble(100) / 100.0;
    for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
        const input_event &e = ev[i];
        if (e.type == EV_UINPUT && e.code == UI_FF_UPLOAD) {
            uinput_ff_upload up {};
            up.request_id = e.value;
            if (ioctl(m_vpad, UI_BEGIN_FF_UPLOAD, &up) < 0) continue;
            ff_effect eff = up.effect;
            if (eff.type == FF_RUMBLE) {
                eff.u.rumble.strong_magnitude = quint16(eff.u.rumble.strong_magnitude * left);
                eff.u.rumble.weak_magnitude = quint16(eff.u.rumble.weak_magnitude * right);
            }
            eff.id = m_ffMap.value(up.effect.id, -1);
            up.retval = (m_pad >= 0 && ioctl(m_pad, EVIOCSFF, &eff) == 0) ? 0 : -EIO;
            if (up.retval == 0) m_ffMap[up.effect.id] = eff.id;
            ioctl(m_vpad, UI_END_FF_UPLOAD, &up);
        } else if (e.type == EV_UINPUT && e.code == UI_FF_ERASE) {
            uinput_ff_erase er {};
            er.request_id = e.value;
            if (ioctl(m_vpad, UI_BEGIN_FF_ERASE, &er) < 0) continue;
            if (m_ffMap.contains(er.effect_id) && m_pad >= 0)
                ioctl(m_pad, EVIOCRMFF, m_ffMap.take(er.effect_id));
            er.retval = 0;
            ioctl(m_vpad, UI_END_FF_ERASE, &er);
        } else if (e.type == EV_FF && m_pad >= 0) {
            input_event out = e;
            if (e.code != FF_GAIN) {
                if (!m_ffMap.contains(e.code)) continue;
                out.code = m_ffMap[e.code];
            }
            (void)!::write(m_pad, &out, sizeof(out));
        }
    }
}

// ---------------------------------------------------------------- shaping

double InputLayer::shapeStick(double r, const QJsonObject &c) const
{
    const double dz = c["deadzone"].toDouble(5) / 100.0, outer = qMax(dz + 0.01, c["outer"].toDouble(98) / 100.0);
    const double anti = c["anti"].toDouble(0) / 100.0;
    if (r <= dz) return 0;
    double t = qMin(1.0, (r - dz) / (outer - dz));
    const QString curve = c["curve"].toString("linear");
    if (curve == "precise") t = t * t;
    else if (curve == "aggressive") t = std::sqrt(t);
    else if (curve == "instant") t = 1.0;
    return anti + (1.0 - anti) * t;
}

double InputLayer::axis(int code) const
{
    const QPair<int, int> r = m_absRange.value(code, {0, 65535});
    const double mid = (r.first + double(r.second)) / 2.0, half = (r.second - double(r.first)) / 2.0;
    return qBound(-1.0, (m_abs.value(code, int(mid)) - mid) / half, 1.0);
}

double InputLayer::trigger(int code) const
{
    const QPair<int, int> r = m_absRange.value(code, {0, 1023});
    return qBound(0.0, (m_abs.value(code, r.first) - double(r.first)) / (r.second - double(r.first)), 1.0);
}

int InputLayer::shapeTrigger(double p, const QJsonObject &c) const
{
    const double start = c["start"].toDouble(5) / 100.0, end = qMax(start + 0.01, c["end"].toDouble(96) / 100.0);
    return int(std::lround(255.0 * qBound(0.0, (p - start) / (end - start), 1.0)));
}

// ---------------------------------------------------------------- one frame

void InputLayer::frame()
{
    const QJsonObject p = preset();
    // sticks -1..1, triggers 0..1, from the pad's own ranges
    auto norm = [this](int code) { return axis(code); };
    double lx = norm(ABS_X), ly = norm(ABS_Y), rx = norm(ABS_RX), ry = norm(ABS_RY);
    auto shape = [this](double &x, double &y, const QJsonObject &c) {
        const double r = std::hypot(x, y);
        const double s = r > 0 ? shapeStick(r, c) / r : 0;
        x *= s; y *= s;
    };
    shape(lx, ly, p["lstick"].toObject());
    shape(rx, ry, p["rstick"].toObject());
    const QJsonObject tl = p["lt"].toObject();
    const QJsonObject tr = p["trigger"].toObject()["mirror"].toBool(true) ? tl : p["rt"].toObject();
    const int lt = shapeTrigger(trigger(ABS_Z), tl), rt = shapeTrigger(trigger(ABS_RZ), tr);

    // logical buttons held now
    QSet<QString> now;
    for (int k : std::as_const(m_keysDown)) now.insert(kRawButtons.value(k));
    const int hx = m_abs.value(ABS_HAT0X), hy = m_abs.value(ABS_HAT0Y);
    if (hy < 0) now.insert("D-pad up");
    if (hy > 0) now.insert("D-pad down");
    if (hx < 0) now.insert("D-pad left");
    if (hx > 0) now.insert("D-pad right");
    if (lt > 127) now.insert("LT");
    if (rt > 127) now.insert("RT");

    if (m_grab) {
        // menu navigation for XPanel: D-pad or left stick, face buttons, bumpers
        const double sx = norm(ABS_X), sy = norm(ABS_Y);
        navPress("up", now.contains("D-pad up") || sy < -0.6);
        navPress("down", now.contains("D-pad down") || sy > 0.6);
        navPress("left", now.contains("D-pad left") || sx < -0.6);
        navPress("right", now.contains("D-pad right") || sx > 0.6);
        static const QList<QPair<QString, QString>> btn = {{"A", "a"}, {"B", "b"}, {"X", "x"}, {"Y", "y"}, {"LB", "lb"}, {"RB", "rb"}};
        for (const auto &b : btn) navPress(b.second, now.contains(b.first));
        return;
    }

    const QJsonObject map = p["map"].toObject();
    auto fnFor = [&](const QString &b, bool paddle) {
        const QJsonObject m = map[b].toObject();
        const QString sec = m["secondary"].toString("none:Disabled");
        if (paddle && !b.contains("paddle") && !sec.startsWith("none"))
            return sec;
        return m["primary"].toString(b.contains("paddle") ? "none:Disabled" : "pad:" + b);
    };
    const bool paddleHeld = now.contains("Left paddle") || now.contains("Right paddle");
    // analog triggers pass through when they are mapped to themselves
    const bool ltAnalog = fnFor("LT", paddleHeld) == "pad:LT", rtAnalog = fnFor("RT", paddleHeld) == "pad:RT";
    if (ltAnalog) now.remove("LT");
    if (rtAnalog) now.remove("RT");

    for (const QString &b : std::as_const(m_logicalDown))
        if (!now.contains(b)) setLogical(b, false);
    for (const QString &b : std::as_const(now))
        if (!m_logicalDown.contains(b)) {
            m_activeFn[b] = fnFor(b, paddleHeld);
            setLogical(b, true);
        }
    m_logicalDown = now;

    // analog outputs
    if (m_vpad >= 0) {
        const bool lStick = p["lstick"].toObject()["mode"].toString("stick") == "stick";
        const bool rStick = p["rstick"].toObject()["mode"].toString("stick") == "stick";
        emitKey(m_vpad, -ABS_X - 1, lStick ? int(lx * 32767) : 0);
        emitKey(m_vpad, -ABS_Y - 1, lStick ? int(ly * 32767) : 0);
        emitKey(m_vpad, -ABS_RX - 1, rStick ? int(rx * 32767) : 0);
        emitKey(m_vpad, -ABS_RY - 1, rStick ? int(ry * 32767) : 0);
        if (ltAnalog) emitKey(m_vpad, -ABS_Z - 1, lt);
        if (rtAnalog) emitKey(m_vpad, -ABS_RZ - 1, rt);
        syn(m_vpad);
    }
}

void InputLayer::setLogical(const QString &button, bool down)
{
    const QString fn = m_activeFn.value(button);
    runFunction(fn, down);
    if (down && preset()["repeat"].toObject()["on"].toBool(false) && !fn.startsWith("none"))
        m_repeatAt[button] = nowMs() + 400;
    if (!down) { m_repeatAt.remove(button); m_activeFn.remove(button); }
}

void InputLayer::navPress(const QString &name, bool down)
{
    if (down && !m_navDown.contains(name)) {
        m_navDown.insert(name);
        emit nav(name);
        m_navAt[name] = nowMs() + 400; // first repeat
    } else if (!down && m_navDown.contains(name)) {
        m_navDown.remove(name);
        m_navAt.remove(name);
    }
}

// 125 Hz: Desktop mode mouse / scroll, button repeat, menu repeat.
void InputLayer::tick()
{
    const qint64 t = nowMs();
    if (m_grab) {
        for (auto it = m_navAt.begin(); it != m_navAt.end(); ++it) {
            const bool dir = it.key() == "up" || it.key() == "down" || it.key() == "left" || it.key() == "right";
            if (dir && t >= it.value()) { emit nav(it.key()); it.value() = t + 110; }
        }
        return;
    }
    // hold to repeat
    const int every = preset()["repeat"].toObject()["ms"].toInt(100);
    for (auto it = m_repeatAt.begin(); it != m_repeatAt.end(); ++it) {
        if (t < it.value()) continue;
        const QString fn = m_activeFn.value(it.key());
        runFunction(fn, false);
        runFunction(fn, true);
        it.value() = t + qMax(30, every);
    }
    // sticks as mouse / scroll
    if (m_pad < 0 || m_vkbm < 0) return;
    const QJsonObject p = preset();
    auto norm = [this](int code) { return axis(code); };
    bool wrote = false;
    for (const auto &st : {qMakePair(QString("lstick"), qMakePair(ABS_X, ABS_Y)), qMakePair(QString("rstick"), qMakePair(ABS_RX, ABS_RY))}) {
        const QJsonObject c = p[st.first].toObject();
        const QString mode = c["mode"].toString("stick");
        if (mode == "stick") continue;
        double x = norm(st.second.first), y = norm(st.second.second);
        const double r = std::hypot(x, y);
        const double s = r > 0 ? shapeStick(r, c) / r : 0;
        x *= s; y *= s;
        const double sens = c["sens"].toDouble(50) / 100.0;
        if (mode == "mouse") {
            m_mouseRemX += x * sens * 1600.0 * 0.008; // up to 1600 px/s at 100 %
            m_mouseRemY += y * sens * 1600.0 * 0.008;
            const int dx = int(m_mouseRemX), dy = int(m_mouseRemY);
            m_mouseRemX -= dx; m_mouseRemY -= dy;
            emitRel(m_vkbm, REL_X, dx); emitRel(m_vkbm, REL_Y, dy);
            wrote |= dx || dy;
        } else if (mode == "scroll") {
            m_wheelRem += -y * sens * 2400.0 * 0.008; // hi-res units (120 = one notch)
            m_hwheelRem += x * sens * 2400.0 * 0.008;
            const int w = int(m_wheelRem), hw = int(m_hwheelRem);
            m_wheelRem -= w; m_hwheelRem -= hw;
            if (w) { emitRel(m_vkbm, REL_WHEEL_HI_RES, w); wrote = true; }
            if (hw) { emitRel(m_vkbm, REL_HWHEEL_HI_RES, hw); wrote = true; }
            static int notch = 0, hnotch = 0;
            notch += w; hnotch += hw;
            if (std::abs(notch) >= 120) { emitRel(m_vkbm, REL_WHEEL, notch / 120); notch %= 120; }
            if (std::abs(hnotch) >= 120) { emitRel(m_vkbm, REL_HWHEEL, hnotch / 120); hnotch %= 120; }
        }
    }
    if (wrote) syn(m_vkbm);
}
