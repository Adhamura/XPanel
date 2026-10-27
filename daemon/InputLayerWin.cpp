// Windows input layer (see InputLayerStub.h). Ported from the Linux InputLayer.cpp (evdev / uinput);
// the comments name the Linux function each part follows.
//
// - Pad in: the built-in Ally pad is an XInput device (HID ...&MI_05&IG_00). It is read with
//   XInputGetState at ~250 Hz on a worker thread. XInput has no back paddles; they arrive on the
//   ASUS HID collection below (codes 0x4B and 0xA5).
// - Button remaps in game mode: pure button remaps (pad -> pad / key / mouse button, paddles) are programmed
//   into the controller MCU (AllyMcu.h, feature reports on the collection below). No virtual pad, no double
//   input. The MCU is reset to the factory mapping at start, at exit, while the XPanel UI uses the pad (grab)
//   and during the button test.
// - Pad out: a virtual Xbox 360 pad (ViGEmBus, third_party/vigem) only for what the MCU cannot do (stick and
//   trigger curves, deadzones, gyro to stick, repeat, a second function while a paddle is held, combos).
//   The real pad stays visible, so games may see two controllers. Rumble from games goes to the real pad with
//   XInputSetState, scaled by Vibration.
// - Desktop mode: pad -> mouse / keyboard with SendInput.
// - Gyro: Windows.Devices.Sensors.Gyrometer (GyroWin.cpp) -> mouse / right stick, GyroMath.h.
// - ASUS N-KEY HID collection (VID 0B05, usage page 0xFF31, usage 0x0076): input report 0x5A with a
//   code, then 0x5A 00 on release (the release code is shared, so the held code is tracked).
//   Left menu button 0xA6, right menu button 0x38, back paddles 0x4B and 0xA5, long press of the right button 0xA7 (measured on the ROG Ally X;
//   on Linux hid-asus maps the menu codes to KEY_F16 and KEY_PROG1). A long press sends the same
//   code, so only short presses exist. HID input reports go to every open handle: Armoury Crate
//   still sees them too.
#include "InputLayerStub.h"
#include "AllyMcuWin.h"
#include "InputLayerWinHelpers.h"
#include "GyroWin.h"

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <cstring>

#include <setupapi.h>
#include <xinput.h>
extern "C" {
#include <hidsdi.h>
}
#include <ViGEm/Client.h>

namespace {

constexpr USHORT kVendor = 0x0B05;
constexpr USHORT kUsagePage = 0xFF31;
constexpr USHORT kUsage = 0x0076;

// The menu-button collection, opened for overlapped reads; INVALID_HANDLE_VALUE if not present.
HANDLE openButtons(USHORT *inLen)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    HANDLE found = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0)
            continue;
        QByteArray buf(int(size), '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        const HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof(attr);
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        const bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == kVendor
                           && HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS
                           && caps.UsagePage == kUsagePage && caps.Usage == kUsage && caps.InputReportByteLength >= 2;
        if (pp)
            HidD_FreePreparsedData(pp);
        if (match) {
            found = h;
            *inLen = caps.InputReportByteLength;
        } else {
            CloseHandle(h);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

qint64 nowMs() { return qint64(GetTickCount64()); }

// The logical buttons held now (names as qml/ControlDefaults.js). paddleBits: 1 = Left, 2 = Right; Control
// "swapPaddles" trades the two names right here, where the raw HID codes become logical buttons.
QSet<QString> logicalHeld(const Cfg &c, const XINPUT_GAMEPAD &g, int paddleBits)
{
    QSet<QString> held;
    const struct { WORD mask; const char *name; } bt[] = {
        {XINPUT_GAMEPAD_A, "A"}, {XINPUT_GAMEPAD_B, "B"}, {XINPUT_GAMEPAD_X, "X"}, {XINPUT_GAMEPAD_Y, "Y"},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
        {XINPUT_GAMEPAD_BACK, "View"}, {XINPUT_GAMEPAD_START, "Menu"},
        {XINPUT_GAMEPAD_LEFT_THUMB, "L3"}, {XINPUT_GAMEPAD_RIGHT_THUMB, "R3"},
        {XINPUT_GAMEPAD_DPAD_UP, "D-pad up"}, {XINPUT_GAMEPAD_DPAD_DOWN, "D-pad down"},
        {XINPUT_GAMEPAD_DPAD_LEFT, "D-pad left"}, {XINPUT_GAMEPAD_DPAD_RIGHT, "D-pad right"}};
    for (const auto &b : bt)
        if (g.wButtons & b.mask)
            held.insert(b.name);
    if (shapeTrigger(g.bLeftTrigger / 255.0, c.ltStart, c.ltEnd) > 127) held.insert("LT");
    const int rt = c.mirror ? shapeTrigger(g.bRightTrigger / 255.0, c.ltStart, c.ltEnd)
                            : shapeTrigger(g.bRightTrigger / 255.0, c.rtStart, c.rtEnd);
    if (rt > 127) held.insert("RT");
    if (c.swapPaddles) paddleBits = ((paddleBits & 1) << 1) | ((paddleBits & 2) >> 1);
    if (paddleBits & 1) held.insert("Left paddle");
    if (paddleBits & 2) held.insert("Right paddle");
    return held;
}

} // namespace

// ---------------------------------------------------------------- the Core

struct InputLayer::Core {
    explicit Core(InputLayer *owner) : q(owner) {}

    InputLayer *q;
    QThread *thread = nullptr;
    WinGyro gyro;

    // shared with the main thread
    QMutex mu;                          // cfg, bias, calibration sums
    std::shared_ptr<const Cfg> cfg = std::make_shared<Cfg>();
    Gyro::Vec3 bias;
    std::atomic<bool> grab{false};
    std::atomic<bool> calibrating{false};
    double calSum[3] = {}, calSq[3] = {}, calTime = 0;
    int calN = 0;
    bool calSignalled = false;
    std::atomic<int> calGen{0};
    QString calResult; // "" | running | done | moved | busy | no data | no device
    Gyro::MouseAccumulator gyroMouse; // gyro thread
    std::atomic<double> gyroSx{0}, gyroSy{0}; // right stick part from the gyro (-1..1, y down)
    std::atomic<bool> ltHeld{false};
    std::atomic<int> paddles{0}; // back paddles held: 1 = Left, 2 = Right (kAsusCodes)
    std::atomic<bool> buttonTest{false};

    enum Mode { Pass = 0, Virt = 1, Desk = 2 };
    std::atomic<int> modeNow{Pass};
    std::atomic<bool> vigemMissing{false}, padConnected{false};
    std::atomic<int> mcuState{0};   // 0 no device, 1 factory mapping, 2 remaps programmed, 3 write failed
    std::atomic<int> mcuCount{0};   // remapped buttons
    std::atomic<int> realSlot{-1};
    std::atomic<int> virtSlot{-1};
    std::atomic<double> vibL{100}, vibR{100};
    std::atomic<qint64> retryAt{0};

    // worker thread only
    PVIGEM_CLIENT vigem = nullptr;
    PVIGEM_TARGET target = nullptr;
    bool plugged = false;
    AllyMcuWin mcu;
    std::map<std::string, AllyMcu::Target> mcuApplied;
    bool mcuAppliedValid = false;
    qint64 mcuRetryAt = 0;
    std::shared_ptr<const Cfg> appliedCfg;
    XUSB_REPORT lastReport{};
    bool reportSent = false;
    QSet<QString> logicalDown;
    QHash<QString, QString> activeFn;
    QHash<QString, qint64> repeatAt;
    QSet<QString> padFns;           // "pad:" functions held now (names)
    QSet<int> heldKeys;             // VK codes pressed by us (ext flag = 0x10000)
    QSet<QString> heldMouse;
    double mouseRemX = 0, mouseRemY = 0, wheelRem = 0, hwheelRem = 0;
    struct Rep { bool down = false; qint64 next = 0; };
    QHash<QString, Rep> nav;
    QSet<QString> testDown;
    double lastAxes[6] = {2, 2, 2, 2, 2, 2};
    qint64 lastAxesAt = 0;

    void changed() { QMetaObject::invokeMethod(q, &InputLayer::statusChanged, Qt::QueuedConnection); }
    std::shared_ptr<const Cfg> cfgNow() { QMutexLocker l(&mu); return cfg; }
    void emitNav(const QString &n) { QMetaObject::invokeMethod(q, [this, n] { emit q->nav(n); }, Qt::QueuedConnection); }

    void run();
    void shutdownCleanup();
    bool ensureVigem();
    bool plug();
    void unplug();
    bool ensureMcu();
    void syncMcu(const Cfg &c, bool factory, qint64 now);
    void resetMcu();
    void applyMode(const Cfg &c);
    void frame(const Cfg &c, const XINPUT_STATE &s, double dt, qint64 now);
    void tickDesktop(const Cfg &c, double lx, double ly, double rx, double ry, double dt);
    void navigate(WORD buttons, double lx, double ly, qint64 now);
    void releaseAll();
    void runFunction(const QString &fn, bool down);
    void setLogical(const Cfg &c, const QString &b, bool down, qint64 now);
    void sendReport(const XUSB_REPORT &r, bool force = false);
    void rumble(UCHAR large, UCHAR small);
    static VOID CALLBACK onRumble(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small, UCHAR, LPVOID user);
    void onGyro(double x, double y, double z, double dt);
    void updateGyro();                      // main thread
    void finishCalibration(QString result); // main thread
};

// ---------------------------------------------------------------- ViGEm

bool InputLayer::Core::ensureVigem()
{
    if (vigem)
        return true;
    PVIGEM_CLIENT c = vigem_alloc();
    if (!c)
        return false;
    const VIGEM_ERROR e = vigem_connect(c);
    if (!VIGEM_SUCCESS(e)) {
        vigem_free(c);
        if (!vigemMissing.exchange(true))
            qInfo("xpanel-daemon input: ViGEmBus not available (error 0x%x), passthrough only", unsigned(e));
        return false;
    }
    vigemMissing = false;
    vigem = c;
    return true;
}

VOID CALLBACK InputLayer::Core::onRumble(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small, UCHAR, LPVOID user)
{
    static_cast<Core *>(user)->rumble(large, small);
}

// Linux readVirtualPad(): effects from games are scaled by Vibration (left = strong, right = weak)
// and sent to the real motors.
void InputLayer::Core::rumble(UCHAR large, UCHAR small)
{
    const int slot = realSlot.load();
    if (slot < 0)
        return;
    XINPUT_VIBRATION v{};
    v.wLeftMotorSpeed = WORD(std::clamp(large * 257.0 * vibL.load() / 100.0, 0.0, 65535.0));
    v.wRightMotorSpeed = WORD(std::clamp(small * 257.0 * vibR.load() / 100.0, 0.0, 65535.0));
    XInputSetState(DWORD(slot), &v);
}

bool InputLayer::Core::plug()
{
    if (plugged)
        return true;
    if (!ensureVigem())
        return false;
    PVIGEM_TARGET t = vigem_target_x360_alloc();
    if (!t)
        return false;
    const VIGEM_ERROR e = vigem_target_add(vigem, t);
    if (!VIGEM_SUCCESS(e)) {
        qWarning("xpanel-daemon input: cannot plug the virtual pad (0x%x)", unsigned(e));
        vigem_target_free(t);
        return false;
    }
    target = t;
    plugged = true;
    vigem_target_x360_register_notification(vigem, target, &onRumble, this);
    ULONG idx = ULONG(-1);
    if (VIGEM_SUCCESS(vigem_target_x360_get_user_index(vigem, target, &idx)))
        virtSlot = int(idx);
    qInfo("xpanel-daemon input: virtual Xbox 360 pad plugged (XInput slot %d)", virtSlot.load());
    reportSent = false;
    return true;
}

void InputLayer::Core::unplug()
{
    if (!plugged)
        return;
    vigem_target_x360_unregister_notification(target);
    vigem_target_remove(vigem, target);
    vigem_target_free(target);
    target = nullptr;
    plugged = false;
    virtSlot = -1;
    qInfo("xpanel-daemon input: virtual pad unplugged");
}

void InputLayer::Core::sendReport(const XUSB_REPORT &r, bool force)
{
    if (!plugged)
        return;
    if (!force && reportSent && std::memcmp(&r, &lastReport, sizeof(r)) == 0)
        return;
    vigem_target_x360_update(vigem, target, r);
    lastReport = r;
    reportSent = true;
}

// ---------------------------------------------------------------- controller MCU

// Open the collection and send "built-in controller ON" and "game mode": the pad is always usable.
bool InputLayer::Core::ensureMcu()
{
    if (mcu.isOpen())
        return true;
    if (!mcu.open())
        return false;
    if (!mcu.selfHeal())
        qWarning("xpanel-daemon input: MCU: controller-on / game-mode reports were not accepted");
    else
        qInfo("xpanel-daemon input: MCU: built-in controller on, game mode");
    mcuAppliedValid = false;
    return true;
}

// Program the wanted mapping into the MCU (factory = the factory mapping). Does nothing when it is there already.
void InputLayer::Core::syncMcu(const Cfg &c, bool factory, qint64 now)
{
    static const std::map<std::string, AllyMcu::Target> none;
    const auto &want = factory ? none : c.mcuMap;
    if (mcuAppliedValid && want == mcuApplied)
        return;
    if (now < mcuRetryAt)
        return;
    if (!ensureMcu()) {
        mcuRetryAt = now + 5000;
        if (mcuState.exchange(0) != 0)
            changed();
        return;
    }
    if (mcu.apply(want)) {
        mcuApplied = want;
        mcuAppliedValid = true;
        mcuCount = int(want.size());
        mcuState = want.empty() ? 1 : 2;
        qInfo("xpanel-daemon input: MCU: %s", want.empty() ? "factory mapping programmed" : "button remaps programmed");
    } else {
        qWarning("xpanel-daemon input: MCU: a mapping report was not accepted");
        mcu.close();
        mcuAppliedValid = false;
        mcuRetryAt = now + 5000;
        mcuState = 3;
    }
    changed();
}

// At exit: leave the controller with the factory mapping.
void InputLayer::Core::resetMcu()
{
    if (mcuAppliedValid && mcuApplied.empty()) {
        mcu.close();
        return;
    }
    if (!mcu.isOpen() && !mcu.open())
        return;
    if (!mcu.resetDefaults())
        qWarning("xpanel-daemon input: MCU: factory reset was not accepted");
    mcuApplied.clear();
    mcuState = 1;
    mcu.close();
}

// Switch between passthrough / virtual pad / desktop mode. The real pad stays visible: the virtual pad is only
// plugged for what the MCU cannot do.
void InputLayer::Core::applyMode(const Cfg &c)
{
    const bool wantVirt = c.needVirtual;
    const qint64 t = nowMs();
    if (t < retryAt.load() && wantVirt && !plugged)
        return; // failed a moment ago
    if (!wantVirt && plugged)
        unplug();
    bool ok = true;
    if (wantVirt && !plugged)
        ok = plug();
    retryAt = ok ? 0 : t + 5000;
    const int m = plugged ? Virt : c.desktop ? Desk : Pass;
    if (m != modeNow.exchange(m) || appliedCfg != cfgNow())
        changed();
    appliedCfg = cfgNow();
    const bool mute = c.vibL == 0 && c.vibR == 0 && (vibL.load() != 0 || vibR.load() != 0);
    vibL = c.vibL;
    vibR = c.vibR;
    if (mute)
        rumble(0, 0);
}

// ---------------------------------------------------------------- functions (Linux runFunction)

void InputLayer::Core::runFunction(const QString &fn, bool down)
{
    const QString kind = fn.section(':', 0, 0), name = fn.section(':', 1);
    if (kind == "pad") {
        if (down) padFns.insert(name); else padFns.remove(name);
    } else if (kind == "key" || kind == "numpad") {
        if (keyTable().contains(name)) {
            const Vk k = keyTable()[name];
            const int id = k.vk | (k.ext ? 0x10000 : 0);
            if (down == heldKeys.contains(id))
                return;
            if (down) heldKeys.insert(id); else heldKeys.remove(id);
            sendKey(k, down);
        }
    } else if (kind == "mouse") {
        if (isMouseButton(name)) {
            if (down) heldMouse.insert(name); else heldMouse.remove(name);
            mouseButton(name, down);
        } else if (down && (name == "Wheel up" || name == "Wheel down")) {
            sendMouse(MOUSEEVENTF_WHEEL, 0, 0, DWORD(name == "Wheel up" ? 120 : -120));
        }
    } else if (kind == "combo") {
        if (name == "On-screen keyboard") {
            if (down)
                QMetaObject::invokeMethod(q, [this] { emit q->action("osk"); }, Qt::QueuedConnection);
            return;
        }
        const QList<Vk> keys = comboTable().value(name);
        if (down) for (const Vk &k : keys) sendKey(k, true);
        else for (auto it = keys.rbegin(); it != keys.rend(); ++it) sendKey(*it, false);
    }
}

void InputLayer::Core::releaseAll()
{
    for (const QString &fn : std::as_const(activeFn))
        runFunction(fn, false);
    activeFn.clear();
    logicalDown.clear();
    repeatAt.clear();
    padFns.clear();
    for (const QString &m : QSet<QString>(heldMouse))
        mouseButton(m, false);
    heldMouse.clear();
    for (int id : QSet<int>(heldKeys))
        sendKey({WORD(id & 0xFFFF), (id & 0x10000) != 0}, false);
    heldKeys.clear();
}

void InputLayer::Core::setLogical(const Cfg &c, const QString &b, bool down, qint64 now)
{
    const QString fn = activeFn.value(b);
    runFunction(fn, down);
    if (down && c.repeatOn && !fn.startsWith("none"))
        repeatAt[b] = now + 400;
    if (!down) { repeatAt.remove(b); activeFn.remove(b); }
}

// ---------------------------------------------------------------- one frame (Linux frame() and tick())

void InputLayer::Core::frame(const Cfg &c, const XINPUT_STATE &s, double dt, qint64 now)
{
    const XINPUT_GAMEPAD &g = s.Gamepad;
    auto stickN = [](SHORT v) { return std::clamp(v / 32767.0, -1.0, 1.0); };
    // Xbox / evdev convention inside: y positive = down
    double lx = stickN(g.sThumbLX), ly = -stickN(g.sThumbLY), rx = stickN(g.sThumbRX), ry = -stickN(g.sThumbRY);
    const double rawLx = lx, rawLy = ly, rawRx = rx, rawRy = ry;
    auto shape = [](double &x, double &y, const StickCfg &sc) {
        const double r = std::hypot(x, y);
        const double k = r > 0 ? shapeStick(r, sc) / r : 0;
        x *= k; y *= k;
    };
    shape(lx, ly, c.l);
    shape(rx, ry, c.r);
    const double ltRaw = g.bLeftTrigger / 255.0, rtRaw = g.bRightTrigger / 255.0;
    const int lt = shapeTrigger(ltRaw, c.ltStart, c.ltEnd);
    const int rt = c.mirror ? shapeTrigger(rtRaw, c.ltStart, c.ltEnd) : shapeTrigger(rtRaw, c.rtStart, c.rtEnd);
    ltHeld = ltRaw > 0.3; // gyro "only while LT is held", from the physical trigger

    const int pd = paddles.load();
    const bool paddleHeld = pd != 0;
    QSet<QString> held = logicalHeld(c, g, pd);

    // Linux fnFor(): the secondary function applies while a paddle is held 
    auto fnFor = [&](const QString &b) {
        const QString sec = c.secondary.value(b, "none:Disabled");
        if (paddleHeld && !b.contains("paddle") && !sec.startsWith("none"))
            return sec;
        return c.primary.value(b, b.contains("paddle") ? "none:Disabled" : "pad:" + b);
    };
    // analog triggers pass through when they are mapped to themselves
    const bool ltAnalog = fnFor("LT") == "pad:LT", rtAnalog = fnFor("RT") == "pad:RT";
    if (ltAnalog) held.remove("LT");
    if (rtAnalog) held.remove("RT");

    for (const QString &b : QSet<QString>(logicalDown))
        if (!held.contains(b)) setLogical(c, b, false, now);
    for (const QString &b : held)
        if (!logicalDown.contains(b)) {
            activeFn[b] = fnFor(b);
            setLogical(c, b, true, now);
        }
    logicalDown = held;

    // hold to repeat (Linux tick())
    for (auto it = repeatAt.begin(); it != repeatAt.end(); ++it) {
        if (now < it.value())
            continue;
        const QString fn = activeFn.value(it.key());
        runFunction(fn, false);
        if (fn.startsWith("pad:") && plugged) { // let the game see the release
            XUSB_REPORT r = lastReport;
            r.wButtons = 0;
            sendReport(r, true);
        }
        runFunction(fn, true);
        it.value() = now + std::max(30, c.repeatMs);
    }

    if (c.desktop) {
        tickDesktop(c, rawLx, rawLy, rawRx, rawRy, dt);
        return;
    }
    if (!plugged)
        return;

    XUSB_REPORT r{};
    auto has = [&](const char *n) { return padFns.contains(n); };
    if (has("A")) r.wButtons |= XUSB_GAMEPAD_A;
    if (has("B")) r.wButtons |= XUSB_GAMEPAD_B;
    if (has("X")) r.wButtons |= XUSB_GAMEPAD_X;
    if (has("Y")) r.wButtons |= XUSB_GAMEPAD_Y;
    if (has("LB")) r.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (has("RB")) r.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (has("View")) r.wButtons |= XUSB_GAMEPAD_BACK;
    if (has("Menu")) r.wButtons |= XUSB_GAMEPAD_START;
    if (has("L3")) r.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (has("R3")) r.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (has("Guide")) r.wButtons |= XUSB_GAMEPAD_GUIDE;
    if (has("D-pad up")) r.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (has("D-pad down")) r.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (has("D-pad left")) r.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (has("D-pad right")) r.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
    const bool lStick = c.l.mode == "stick", rStick = c.r.mode == "stick";
    auto s16 = [](double v) { return SHORT(std::clamp(v, -1.0, 1.0) * 32767.0); };
    r.sThumbLX = lStick ? s16(lx) : 0;
    r.sThumbLY = lStick ? s16(-ly) : 0;
    const Gyro::Stick rs = Gyro::combine(rStick ? Gyro::Stick{rx, ry} : Gyro::Stick{}, {gyroSx.load(), gyroSy.load()});
    r.sThumbRX = s16(rs.x);
    r.sThumbRY = s16(-rs.y);
    r.bLeftTrigger = has("LT") ? 255 : 0;
    r.bRightTrigger = has("RT") ? 255 : 0;
    if (ltAnalog) r.bLeftTrigger = BYTE(std::max<int>(r.bLeftTrigger, lt));
    if (rtAnalog) r.bRightTrigger = BYTE(std::max<int>(r.bRightTrigger, rt));
    sendReport(r);
}

// Desktop mode: sticks as mouse / scroll (Linux tick()). x, y are the unshaped stick (-1..1, y down).
void InputLayer::Core::tickDesktop(const Cfg &c, double lx, double ly, double rx, double ry, double dt)
{
    const struct { const StickCfg *sc; double x, y; } sticks[] = {{&c.l, lx, ly}, {&c.r, rx, ry}};
    for (const auto &st : sticks) {
        const StickCfg &sc = *st.sc;
        if (sc.mode == "stick")
            continue;
        double x = st.x, y = st.y;
        const double r = std::hypot(x, y);
        const double k = r > 0 ? shapeStick(r, sc) / r : 0;
        x *= k; y *= k;
        const double sens = sc.sens / 100.0;
        if (sc.mode == "mouse") {
            mouseRemX += x * sens * 1600.0 * dt; // up to 1600 px/s at 100 %
            mouseRemY += y * sens * 1600.0 * dt;
            const int dx = int(mouseRemX), dy = int(mouseRemY);
            mouseRemX -= dx; mouseRemY -= dy;
            if (dx || dy)
                sendMouse(MOUSEEVENTF_MOVE, dx, dy);
        } else if (sc.mode == "scroll") {
            wheelRem += -y * sens * 2400.0 * dt; // 120 units = one notch
            hwheelRem += x * sens * 2400.0 * dt;
            if (std::abs(wheelRem) >= 20) {
                const int w = int(wheelRem);
                wheelRem -= w;
                sendMouse(MOUSEEVENTF_WHEEL, 0, 0, DWORD(w));
            }
            if (std::abs(hwheelRem) >= 20) {
                const int w = int(hwheelRem);
                hwheelRem -= w;
                sendMouse(MOUSEEVENTF_HWHEEL, 0, 0, DWORD(w));
            }
        }
    }
}

// Panel navigation while grabbed: D-pad or left stick, A B X Y, bumpers; same names as Linux navPress().
void InputLayer::Core::navigate(WORD buttons, double lx, double ly, qint64 now)
{
    auto press = [&](const QString &name, bool down, bool repeat) {
        Rep &r = nav[name];
        if (down && !r.down) {
            r.down = true;
            emitNav(name);
            r.next = now + 400; // first repeat
        } else if (!down && r.down) {
            r.down = false;
        } else if (down && repeat && now >= r.next) {
            emitNav(name);
            r.next = now + 110;
        }
    };
    auto axis = [&](const QString &name, double v) { // press at 0.6, release at 0.4
        Rep &r = nav[name + "#s"];
        r.down = v > (r.down ? 0.4 : 0.6);
        return r.down;
    };
    press("up", (buttons & XINPUT_GAMEPAD_DPAD_UP) || axis("up", -ly), true);
    press("down", (buttons & XINPUT_GAMEPAD_DPAD_DOWN) || axis("down", ly), true);
    press("left", (buttons & XINPUT_GAMEPAD_DPAD_LEFT) || axis("left", -lx), true);
    press("right", (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || axis("right", lx), true);
    press("a", buttons & XINPUT_GAMEPAD_A, false);
    press("b", buttons & XINPUT_GAMEPAD_B, false);
    press("x", buttons & XINPUT_GAMEPAD_X, false);
    press("y", buttons & XINPUT_GAMEPAD_Y, false);
    press("lb", buttons & XINPUT_GAMEPAD_LEFT_SHOULDER, false);
    press("rb", buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER, false);
}

// ---------------------------------------------------------------- worker thread

void InputLayer::Core::run()
{
    ensureMcu(); // self-heal (controller on, game mode); the first syncMcu() also clears remaps left by a crash
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    bool connected[XUSER_MAX_COUNT] = {};
    XINPUT_STATE slotState[XUSER_MAX_COUNT] = {};
    qint64 probeAt = 0;
    bool wasGrab = false, hadPad = false;
    int slot = -1;

    for (;;) {
        LARGE_INTEGER due;
        due.QuadPart = -40000; // 4 ms
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        const HANDLE waits[2] = {timer, q->m_stop};
        if (WaitForMultipleObjects(2, waits, FALSE, 100) == WAIT_OBJECT_0 + 1)
            break;
        LARGE_INTEGER nowQ;
        QueryPerformanceCounter(&nowQ);
        const double dt = std::min(0.05, double(nowQ.QuadPart - last.QuadPart) / double(freq.QuadPart));
        last = nowQ;
        const qint64 now = nowMs();
        const std::shared_ptr<const Cfg> c = cfgNow();

        // the real pad: the first connected XInput slot that is not our virtual pad
        const bool probe = now >= probeAt; // a missing slot is slow to query: look again once a second
        if (probe)
            probeAt = now + 1000;
        if (plugged && probe) { // ViGEm's slot number can lag behind the plug: ask again
            ULONG idx = ULONG(-1);
            if (VIGEM_SUCCESS(vigem_target_x360_get_user_index(vigem, target, &idx)))
                virtSlot = int(idx);
        }
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            // never read our own virtual pad as the real one; the real pad was found before it was plugged
            if (int(i) == virtSlot.load() && int(i) != slot) {
                connected[i] = false;
                continue;
            }
            if (!connected[i] && !probe)
                continue;
            connected[i] = XInputGetState(i, &slotState[i]) == ERROR_SUCCESS;
        }
        if (slot >= 0 && !connected[slot])
            slot = -1;
        for (int i = 0; slot < 0 && i < int(XUSER_MAX_COUNT); ++i)
            if (connected[i])
                slot = i;
        const bool have = slot >= 0;
        realSlot = slot;
        if (have != hadPad) {
            hadPad = have;
            padConnected = have;
            mcu.close(); // the MCU may have restarted (sleep, replug): open and program it again
            mcuAppliedValid = false;
            mcuRetryAt = 0;
            qInfo("xpanel-daemon input: pad %s (XInput slot %d)", have ? "found" : "gone", slot);
            QMetaObject::invokeMethod(q, [this] { updateGyro(); emit q->statusChanged(); }, Qt::QueuedConnection);
        }
        applyMode(*c);

        const bool g = grab.load();
        // the XPanel UI (grab) and the button test read the physical pad: factory mapping then
        syncMcu(*c, g || buttonTest.load(), now);
        if (g != wasGrab) {
            wasGrab = g;
            releaseAll();
            nav.clear();
            gyroSx = gyroSy = 0;
            sendReport(XUSB_REPORT{}, true);
        }
        if (!have)
            continue;
        if (buttonTest.load()) { // Control Mode button test: every logical button that goes down
            const QSet<QString> now = logicalHeld(*c, slotState[slot].Gamepad, paddles.load());
            for (const QString &b : now)
                if (!testDown.contains(b))
                    QMetaObject::invokeMethod(q, [this, b] { emit q->pressed(b); }, Qt::QueuedConnection);
            for (const QString &b : testDown)
                if (!now.contains(b))
                    QMetaObject::invokeMethod(q, [this, b] { emit q->released(b); }, Qt::QueuedConnection);
            testDown = now;
            const XINPUT_GAMEPAD &gp = slotState[slot].Gamepad;
            const double cur[6] = {std::max(-1.0, gp.sThumbLX / 32767.0), std::max(-1.0, gp.sThumbLY / 32767.0),
                                   std::max(-1.0, gp.sThumbRX / 32767.0), std::max(-1.0, gp.sThumbRY / 32767.0),
                                   gp.bLeftTrigger / 255.0, gp.bRightTrigger / 255.0};
            bool diff = false;
            for (int i = 0; i < 6; ++i) if (std::abs(cur[i] - lastAxes[i]) > 0.005) diff = true;
            if (diff && nowMs() - lastAxesAt >= 33) {
                lastAxesAt = nowMs();
                for (int i = 0; i < 6; ++i) lastAxes[i] = cur[i];
                const double a0 = cur[0], a1 = cur[1], a2 = cur[2], a3 = cur[3], a4 = cur[4], a5 = cur[5];
                QMetaObject::invokeMethod(q, [this, a0, a1, a2, a3, a4, a5] { emit q->axes(a0, a1, a2, a3, a4, a5); }, Qt::QueuedConnection);
            }
        } else if (!testDown.isEmpty()) {
            testDown.clear();
        }

        if (g) { // the pad drives XPanel: merge all connected non-virtual pads for navigation
            WORD buttons = 0;
            double lx = 0, ly = 0;
            for (int i = 0; i < int(XUSER_MAX_COUNT); ++i) {
                if (!connected[i])
                    continue;
                buttons |= slotState[i].Gamepad.wButtons;
                const double x = slotState[i].Gamepad.sThumbLX / 32767.0, y = -slotState[i].Gamepad.sThumbLY / 32767.0;
                if (std::abs(x) > std::abs(lx)) lx = x;
                if (std::abs(y) > std::abs(ly)) ly = y;
            }
            navigate(buttons, lx, ly, now);
            continue;
        }
        // passthrough with nothing to change: the game reads the pad itself
        if (!c->desktop && !plugged)
            continue;
        frame(*c, slotState[slot], dt, now);
    }
    CloseHandle(timer);
    shutdownCleanup();
}

void InputLayer::Core::shutdownCleanup()
{
    releaseAll();
    resetMcu();
    if (plugged)
        unplug();
    if (vigem) {
        vigem_disconnect(vigem);
        vigem_free(vigem);
        vigem = nullptr;
    }
    if (realSlot.load() >= 0) {
        XINPUT_VIBRATION off{};
        XInputSetState(DWORD(realSlot.load()), &off);
    }
}

// ---------------------------------------------------------------- gyro (Linux onGyro / updateGyro / calibration)

void InputLayer::Core::onGyro(double x, double y, double z, double dt)
{
    std::shared_ptr<const Cfg> c;
    Gyro::Vec3 b;
    {
        QMutexLocker l(&mu);
        c = cfg;
        b = bias;
        if (calibrating) {
            const double v[3] = {x, y, z};
            for (int i = 0; i < 3; ++i) {
                calSum[i] += v[i];
                calSq[i] += v[i] * v[i];
            }
            ++calN;
            calTime += dt;
            if (calTime >= 1.0 && calN >= 50 && !calSignalled) {
                calSignalled = true;
                QMetaObject::invokeMethod(q, [this] { finishCalibration({}); }, Qt::QueuedConnection);
            }
        }
    }
    if (grab.load() || !padConnected.load())
        return;
    const bool live = c->gyroMode != "off" && (!c->ratchet || ltHeld.load());
    const Gyro::Aim aim = live ? Gyro::aimRate({x, y, z}, b, c->gyro) : Gyro::Aim{};
    if (c->gyroMode == "mouse" && live) {
        int dx = 0, dy = 0;
        gyroMouse.add(aim, dt, c->gyro, &dx, &dy);
        if (dx || dy)
            sendMouse(MOUSEEVENTF_MOVE, dx, dy);
    } else {
        gyroMouse.reset();
    }
    const Gyro::Stick s = c->gyroMode == "rstick" && live ? Gyro::toStick(aim, c->gyro) : Gyro::Stick{};
    gyroSx = s.x;
    gyroSy = s.y;
}

void InputLayer::Core::updateGyro()
{
    const std::shared_ptr<const Cfg> c = cfgNow();
    const bool on = calibrating || (c->gyroMode != "off" && padConnected && !grab.load());
    if (!on || c->gyroMode != "rstick")
        gyroSx = gyroSy = 0;
    if (on == gyro.running())
        return;
    gyroMouse.reset();
    if (on)
        gyro.start([this](double x, double y, double z, double dt) { onGyro(x, y, z, dt); });
    else
        gyro.stop();
    emit q->statusChanged();
}

void InputLayer::Core::finishCalibration(QString result)
{
    double mean[3] = {};
    bool done = false;
    {
        QMutexLocker l(&mu);
        if (!calibrating)
            return;
        calibrating = false;
        if (result.isEmpty()) {
            bool still = calN > 0;
            for (int i = 0; i < 3 && calN > 0; ++i) {
                mean[i] = calSum[i] / calN;
                const double sd = std::sqrt(std::max(0.0, calSq[i] / calN - mean[i] * mean[i]));
                still = still && sd < 0.035 && std::abs(mean[i]) < 0.1; // ~2 deg/s noise, ~6 deg/s offset
            }
            if (still) {
                bias = {mean[0], mean[1], mean[2]};
                result = "done";
                done = true;
                qInfo("xpanel-daemon gyro: calibrated, offset %.4f %.4f %.4f rad/s (%d samples)", mean[0], mean[1], mean[2], calN);
            } else {
                result = "moved";
            }
        }
        calResult = result;
    }
    updateGyro();
    if (done)
        emit q->gyroBiasChanged(QJsonArray{mean[0], mean[1], mean[2]}); // saved and broadcast by the daemon
    emit q->statusChanged();
}

// ---------------------------------------------------------------- InputLayer

InputLayer::InputLayer(QObject *parent) : QObject(parent)
{
    m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_core = new Core(this);
    m_reader = QThread::create([this] { readButtons(); });
    m_reader->start();
    m_core->thread = QThread::create([this] { m_core->run(); });
    m_core->thread->start();
    // The daemon quits through the event loop: reset the MCU and unplug before the process ends.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { shutdown(); });
}

InputLayer::~InputLayer()
{
    shutdown();
    delete m_core->thread;
    delete m_core;
    delete m_reader;
    CloseHandle(m_stop);
}

void InputLayer::shutdown()
{
    if (m_down.exchange(true))
        return;
    SetEvent(m_stop);
    m_core->gyro.stop();
    m_reader->wait(3000);
    if (!m_core->thread->wait(15000))
        qWarning("xpanel-daemon input: pad worker did not stop in time");
}

void InputLayer::setControl(const QJsonObject &control)
{
    const auto c = parseCfg(control);
    {
        QMutexLocker l(&m_core->mu);
        m_core->cfg = c;
    }
    m_core->retryAt = 0;
    m_core->updateGyro();
    emit statusChanged();
}

void InputLayer::setButtonTest(bool on)
{
    m_core->buttonTest = on;
}

void InputLayer::setGrab(bool g)
{
    if (g == m_core->grab.exchange(g))
        return;
    m_core->updateGyro();
    emit statusChanged();
}

void InputLayer::setGyroBias(const QJsonArray &bias)
{
    if (bias.size() != 3)
        return;
    QMutexLocker l(&m_core->mu);
    m_core->bias = {bias[0].toDouble(), bias[1].toDouble(), bias[2].toDouble()};
}

void InputLayer::calibrateGyro()
{
    Core &c = *m_core;
    if (c.calibrating)
        return;
    if (!c.gyro.available()) {
        c.calResult = "no device";
        emit statusChanged();
        return;
    }
    {
        QMutexLocker l(&c.mu);
        std::fill(std::begin(c.calSum), std::end(c.calSum), 0.0);
        std::fill(std::begin(c.calSq), std::end(c.calSq), 0.0);
        c.calTime = 0;
        c.calN = 0;
        c.calSignalled = false;
        c.calResult = "running";
        c.calibrating = true;
    }
    const int gen = ++c.calGen;
    QTimer::singleShot(4000, this, [this, gen] {
        Core &cc = *m_core;
        if (cc.calibrating && cc.calGen == gen)
            cc.finishCalibration(cc.calN == 0 ? "no data" : "moved");
    });
    c.updateGyro();
    emit statusChanged();
}

QJsonObject InputLayer::status() const
{
    Core &c = *m_core;
    QString mode;
    switch (c.modeNow.load()) {
    case Core::Virt: mode = "Windows: virtual pad active (ViGEm): games may see two controllers"; break;
    case Core::Desk: mode = "Windows: desktop mode (pad drives mouse and keyboard with SendInput)"; break;
    default:
        if (c.vigemMissing && c.cfgNow()->needVirtual)
            mode = "Windows: passthrough (no ViGEmBus, settings that need a virtual pad are not applied)";
        else if (c.mcuState.load() == 2)
            mode = QString("Windows: %1 button remap(s) programmed into the controller (no virtual pad)").arg(c.mcuCount.load());
        else if (c.mcuState.load() == 3)
            mode = "Windows: passthrough (the controller did not accept the button remaps)";
        else
            mode = "Windows: passthrough (no remapping needed)";
    }
    const bool pad = c.padConnected.load();
    QString gyro = "no device";
    if (c.gyro.available())
        gyro = c.gyro.running() ? "active" : "ready";
    QString cal;
    {
        QMutexLocker l(&c.mu);
        cal = c.calResult;
    }
    return {{"inputLayer", pad || m_buttons ? "active" : "no device"},
            {"inputMode", mode},
            {"inputPad", pad ? QString("XInput slot %1").arg(c.realSlot.load()) : QString()},
            {"inputVirtualSlot", c.virtSlot.load()},
            {"inputGrab", c.grab.load()},
            {"inputPaddles", "both back paddles (HID 0xFF31 codes 0x4B and 0xA5); which is left or right is unconfirmed"},
            {"inputMenuButtons", m_buttons.load()},
            {"inputMcu", c.mcuState.load() == 2 ? "remaps programmed" : c.mcuState.load() == 3 ? "write failed"
                         : c.mcuState.load() == 1 ? "factory mapping" : "no device"},
            {"gyro", gyro},
            {"gyroMethod", "Windows.Devices.Sensors.Gyrometer"},
            {"gyroCalibration", cal}};
}

void InputLayer::readButtons()
{
    const HANDLE readDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(m_stop, 0) != WAIT_OBJECT_0) {
        USHORT inLen = 0;
        const HANDLE dev = openButtons(&inLen);
        if (dev == INVALID_HANDLE_VALUE) { // not present (yet): look again after a while
            if (m_buttons.exchange(false))
                QMetaObject::invokeMethod(this, &InputLayer::statusChanged, Qt::QueuedConnection);
            WaitForSingleObject(m_stop, 5000);
            continue;
        }
        if (!m_buttons.exchange(true))
            QMetaObject::invokeMethod(this, &InputLayer::statusChanged, Qt::QueuedConnection);
        QByteArray report(inLen, '\0');
        for (;;) {
            OVERLAPPED ov{};
            ov.hEvent = readDone;
            ResetEvent(readDone);
            DWORD got = 0;
            if (!ReadFile(dev, report.data(), DWORD(report.size()), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
                break; // device gone (unplugged, sleep): open it again
            const HANDLE waits[2] = {readDone, m_stop};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIo(dev);
                GetOverlappedResult(dev, &ov, &got, TRUE);
                break;
            }
            if (!GetOverlappedResult(dev, &ov, &got, FALSE))
                break;
            if (got < 2 || quint8(report[0]) != 0x5A)
                continue;
            const quint8 code = quint8(report[1]);
            // One table for the ASUS codes (VID 0B05, report 0x5A). Back paddles: 0x4B = KEY_F14 and 0xA5 = KEY_F15,
            // which InputPlumber and the Linux InputLayer call Left and Right paddle (not yet confirmed on this
            // unit). 0x00 = release of whatever was held (shared); 0xA8 = release of the long press; 0xEC is spurious.
            if (code == 0x00 || code == 0xA8) {
                m_core->paddles = 0;
                continue;
            }
            if (code == 0x4B || code == 0xA5) { // a repeated press report changes nothing
                m_core->paddles |= code == 0x4B ? 1 : 2;
                continue;
            }
            const char *name = code == 0xA6 ? "left_button" : code == 0x38 ? "right_button" : code == 0xA7 ? "right_button_long" : nullptr;
            if (name) { // unknown codes are ignored
                const QString n = QString::fromLatin1(name);
                const bool test = m_core->buttonTest.load() && code != 0xA7;
                QMetaObject::invokeMethod(this, [this, n, test, code] {
                    if (test) emit pressed(code == 0xA6 ? "QAM" : "Armoury"); // the UI keeps it lit for a moment
                    emit special(n);
                }, Qt::QueuedConnection);
            }
        }
        m_core->paddles = 0;
        CloseHandle(dev);
        WaitForSingleObject(m_stop, 1000);
    }
    CloseHandle(readDone);
}
