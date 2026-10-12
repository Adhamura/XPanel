#include "DesktopTweaks.h"
#include "Config.h"
#include "Desktop.h"
#include "DeviceClient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "BrightnessWin.h"
#endif

static const char *kComponent = "xpanel";

#ifdef Q_OS_WIN
// ---------- Windows global hotkeys (RegisterHotKey, ids 1 = quickpanel, 2 = controlcenter) ----------
// Qt key sequence ("Meta+Shift+Q") -> MOD_* flags and a virtual key. False when it has no Windows form.
static bool toHotkey(const QString &keys, UINT &mods, UINT &vk)
{
    const QKeySequence seq(keys);
    if (seq.isEmpty())
        return false;
    const QKeyCombination c = seq[0];
    const Qt::KeyboardModifiers m = c.keyboardModifiers();
    mods = MOD_NOREPEAT;
    if (m & Qt::MetaModifier) mods |= MOD_WIN;
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::AltModifier) mods |= MOD_ALT;
    if (m & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (!(mods & ~UINT(MOD_NOREPEAT)))
        return false; // a bare key would block normal typing
    const int k = c.key();
    vk = 0;
    if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9))
        vk = UINT(k);
    else if (k >= Qt::Key_F1 && k <= Qt::Key_F24)
        vk = UINT(VK_F1 + (k - Qt::Key_F1));
    else switch (k) {
    case Qt::Key_Space: vk = VK_SPACE; break;
    case Qt::Key_Return: case Qt::Key_Enter: vk = VK_RETURN; break;
    case Qt::Key_Escape: vk = VK_ESCAPE; break;
    case Qt::Key_Tab: vk = VK_TAB; break;
    case Qt::Key_Backspace: vk = VK_BACK; break;
    case Qt::Key_Insert: vk = VK_INSERT; break;
    case Qt::Key_Delete: vk = VK_DELETE; break;
    case Qt::Key_Home: vk = VK_HOME; break;
    case Qt::Key_End: vk = VK_END; break;
    case Qt::Key_PageUp: vk = VK_PRIOR; break;
    case Qt::Key_PageDown: vk = VK_NEXT; break;
    case Qt::Key_Left: vk = VK_LEFT; break;
    case Qt::Key_Right: vk = VK_RIGHT; break;
    case Qt::Key_Up: vk = VK_UP; break;
    case Qt::Key_Down: vk = VK_DOWN; break;
    default:
        if (k > 0 && k < 0x80) { // punctuation: the key that types this character
            const SHORT r = VkKeyScanW(wchar_t(k));
            if (r != -1)
                vk = UINT(r & 0xFF);
        }
    }
    return vk != 0;
}

class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    DesktopTweaks *owner = nullptr;
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG")
            return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_HOTKEY || (msg->wParam != 1 && msg->wParam != 2))
            return false;
        emit owner->shortcutPressed(msg->wParam == 2 ? "controlcenter" : "quickpanel");
        return true;
    }
};

static bool registerHotkey(int id, const QString &keys)
{
    UnregisterHotKey(nullptr, id);
    if (keys.isEmpty())
        return true;
    UINT mods = 0, vk = 0;
    return toHotkey(keys, mods, vk) && RegisterHotKey(nullptr, id, mods, vk);
}

static bool serviceRunning(const wchar_t *name)
{
    bool run = false;
    if (SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)) {
        if (SC_HANDLE sv = OpenServiceW(scm, name, SERVICE_QUERY_STATUS)) {
            SERVICE_STATUS st;
            run = QueryServiceStatus(sv, &st) && st.dwCurrentState == SERVICE_RUNNING;
            CloseServiceHandle(sv);
        }
        CloseServiceHandle(scm);
    }
    return run;
}

// Vendor usage page 0xFF31 of an ASUS HID device: the Aura lighting collection
static bool auraHidPresent()
{
    GUID g;
    HidD_GetHidGuid(&g);
    const HDEVINFO set = SetupDiGetClassDevsW(&g, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, nullptr, &g, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0)
            continue;
        QByteArray buf(int(size), '\0');
        auto *d = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        d->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, d, size, nullptr, nullptr))
            continue;
        const HANDLE h = CreateFileW(d->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES at{};
        at.Size = sizeof(at);
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetAttributes(h, &at) && at.VendorID == 0x0B05 && HidD_GetPreparsedData(h, &pp)) {
            found = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0xFF31;
            HidD_FreePreparsedData(pp);
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}
#endif

DesktopTweaks::DesktopTweaks(Config *config, QObject *parent) : QObject(parent), m_config(config)
{
    registerShortcuts();
}

// ---------- Display colour presets ----------
// Colour temperature presets: KWin Night Color in "always on" mode, or GNOME Night Light with a
// 0:00 - 0:00 schedule (gnome-settings-daemon treats equal times as the whole day).
static int presetKelvin(const QString &p)
{
    if (p == "cinema") return 5800;
    if (p == "eyecare") return 4500;
    if (p == "night") return 3400;
    if (p == "rts") return 7200; // cooler, crisper look
    return 0;                    // default: Night Color off
}

QString DesktopTweaks::colorPreset() const
{
    return m_config->get("display.preset", "default").toString();
}

void DesktopTweaks::setColorPreset(const QString &preset)
{
    const int k = presetKelvin(preset);
    if (!Desktop::caps().colorPresets)
        return;
    if (Desktop::current() == Desktop::Kind::GNOME) {
        const QString schema = "org.gnome.settings-daemon.plugins.color";
        auto g = [&schema](const QString &key, const QString &val) {
            QProcess::execute("gsettings", {"set", schema, key, val});
        };
        if (k == 0) {
            g("night-light-enabled", "false");
        } else {
            g("night-light-schedule-automatic", "false");
            g("night-light-schedule-from", "0.0");
            g("night-light-schedule-to", "0.0");
            g("night-light-temperature", QString::number(k));
            g("night-light-enabled", "true");
        }
        m_config->set("display.preset", preset);
        emit colorPresetChanged();
        return;
    }
    auto w = [](const QString &key, const QString &val) {
        QProcess::execute("kwriteconfig6", {"--file", "kwinrc", "--group", "NightColor", "--key", key, val});
    };
    if (k == 0) {
        w("Active", "false");
    } else {
        w("Active", "true");
        w("Mode", "Constant");
        w("NightTemperature", QString::number(k));
    }
    // KWin reads its config again
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall("org.kde.KWin", "/KWin", "org.kde.KWin", "reconfigure"));
    m_config->set("display.preset", preset);
    emit colorPresetChanged();
}

void DesktopTweaks::setWideColor(bool on)
{
    if (!Desktop::caps().wideColor)
        return;
    QProcess::startDetached("sh", {"-c", QString("kscreen-doctor $(kscreen-doctor -o | sed 's/\\x1b\\[[0-9;]*m//g' "
                                                 "| awk '/^Output:/ && $3 ~ /^eDP/ {print \"output.\" $3 \".wcg.%1\"}')")
                                             .arg(on ? "enable" : "disable")});
    m_config->set("display.wcg", on);
}

// ---------- Global shortcuts (KDE kglobalaccel) ----------
static QStringList actionId(const QString &action)
{
    const QString friendly = action == "controlcenter" ? "Open Control Center" : "Open / close Quick Panel";
    return {kComponent, action, "XPanel", friendly};
}

void DesktopTweaks::registerShortcuts()
{
#ifdef Q_OS_WIN
    static HotkeyFilter filter;
    static bool installed = false;
    if (!installed) {
        installed = true;
        filter.owner = this;
        QCoreApplication::instance()->installNativeEventFilter(&filter);
    }
    registerHotkey(1, shortcut("quickpanel"));
    registerHotkey(2, shortcut("controlcenter"));
    return;
#endif
    // kglobalaccel is KDE's; elsewhere the Ally buttons (through xpanel-daemon) open the panel
    if (!Desktop::caps().shortcuts)
        return;
    QDBusInterface kga("org.kde.kglobalaccel", "/kglobalaccel", "org.kde.KGlobalAccel");
    if (!kga.isValid())
        return;
    for (const QString &a : {QString("quickpanel"), QString("controlcenter")}) {
        kga.call("doRegister", actionId(a));
        const QString keys = shortcut(a);
        QList<int> codes;
        if (!keys.isEmpty())
            codes << QKeySequence(keys)[0].toCombined();
        // flags: 2 = SetPresent (we are running), 4 = NoAutoloading (use our keys, not the saved ones)
        kga.call("setShortcut", actionId(a), QVariant::fromValue(codes), uint(2 | 4));
    }
    QDBusConnection::sessionBus().connect("org.kde.kglobalaccel", QString("/component/") + kComponent,
                                          "org.kde.kglobalaccel.Component", "globalShortcutPressed",
                                          this, SLOT(onGlobalShortcut(QString, QString, qlonglong)));
}

QString DesktopTweaks::shortcut(const QString &action) const
{
    const QString def = action == "controlcenter" ? "Meta+Shift+C" : "Meta+Shift+Q";
    return m_config->get("shortcut." + action, def).toString();
}

bool DesktopTweaks::setShortcut(const QString &action, const QString &keys)
{
    if (!keys.isEmpty() && QKeySequence(keys).isEmpty())
        return false;
#ifdef Q_OS_WIN
    UINT mods = 0, vk = 0;
    if (!keys.isEmpty() && !toHotkey(keys, mods, vk))
        return false; // needs a modifier and a key Windows knows
    const int id = action == "controlcenter" ? 2 : 1;
    if (!registerHotkey(id, keys)) { // in use by another program: keep the old key
        registerHotkey(id, shortcut(action));
        return false;
    }
    m_config->set("shortcut." + action, keys);
    return true;
#endif
    m_config->set("shortcut." + action, keys);
    registerShortcuts();
    return true;
}

void DesktopTweaks::onGlobalShortcut(const QString &component, const QString &action, qlonglong)
{
    if (component == kComponent)
        emit shortcutPressed(action);
}

// ---------- Feedback ----------
QString DesktopTweaks::feedbackPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/feedback.md";
}

bool DesktopTweaks::saveFeedback(const QString &topic, const QString &text)
{
    if (text.trimmed().isEmpty())
        return false;
    QDir().mkpath(QFileInfo(feedbackPath()).absolutePath());
    QFile f(feedbackPath());
    if (!f.open(QIODevice::Append))
        return false;
    f.write(QString("## %1 — %2\n\n%3\n\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"),
                                                 topic, text.trimmed()).toUtf8());
    return true;
}

void DesktopTweaks::openSystemSettings(const QString &module)
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl(module == "kcm_keys" ? "ms-settings:easeofaccess-keyboard" : "ms-settings:yourinfo"));
    return;
#endif
    static const QRegularExpression ok("^kcm_[a-z_]+$");
    if (!ok.match(module).hasMatch() || !Desktop::caps().userSettings)
        return;
    if (Desktop::current() == Desktop::Kind::GNOME) {
        // the same pages in GNOME Settings
        const QString panel = module == "kcm_users" ? "users" : module == "kcm_keys" ? "keyboard" : QString();
        QProcess::startDetached("gnome-control-center", panel.isEmpty() ? QStringList{} : QStringList{panel});
        return;
    }
    QProcess::startDetached("systemsettings", {module});
}

// ---------- App Diagnostics ----------
QVariantList DesktopTweaks::diagnostics() const
{
    QVariantList out;
    auto add = [&](const QString &name, bool ok, const QString &detail) {
        out << QVariantMap{{"name", name}, {"ok", ok}, {"detail", detail}};
    };
#ifdef Q_OS_WIN
    {
        QLocalSocket w;
        w.connectToServer(qEnvironmentVariable("XPANEL_SOCKET", "xpanel"));
        const bool daemon = w.waitForConnected(500);
        add(tr("System"), true, QSysInfo::prettyProductName());
        add("xpanel-daemon", daemon, daemon ? tr("running (pipe \"xpanel\")") : tr("not reachable: start XPanel again, it starts xpanel-daemon.exe"));
        HANDLE h = CreateFileW(L"\\\\.\\ATKACPI", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool atk = h != INVALID_HANDLE_VALUE;
        if (atk)
            CloseHandle(h);
        add(tr("ASUS ACPI driver"), atk, atk ? "\\\\.\\ATKACPI" : tr("cannot open \\\\.\\ATKACPI: install the ASUS System Control Interface driver"));
        const bool aura = auraHidPresent();
        add(tr("Lighting (Aura HID)"), aura, aura ? tr("ASUS HID, usage page 0xFF31") : tr("no ASUS HID device with usage page 0xFF31"));
        bool wmi = false;
        for (const BrightnessWin::Monitor &m : BrightnessWin::enumerate(false))
            if (m.internal) wmi = true;
        add(tr("Screen brightness (WMI)"), wmi, wmi ? "WmiMonitorBrightness" : tr("not available for the internal screen"));
        for (const wchar_t *svc : {L"ArmouryCrateSEService", L"ASUSOptimization"}) {
            const bool run = serviceRunning(svc);
            add(tr("Armoury Crate: %1").arg(QString::fromWCharArray(svc)), !run,
                run ? tr("running: it changes the same settings as XPanel (TDP, fan, lighting). Stop it to avoid conflicts.")
                    : tr("not running"));
        }
        return out;
    }
#endif
    QLocalSocket s;
    s.connectToServer("/run/xpanel/xpanel.sock");
    const bool daemon = s.waitForConnected(500);
    add(tr("Desktop"), true, tr("%1 (XDG_CURRENT_DESKTOP=%2)").arg(Desktop::label(Desktop::current()),
                                                                  qEnvironmentVariable("XDG_CURRENT_DESKTOP", tr("unset"))));
    add("xpanel-daemon", daemon, daemon ? tr("running (/run/xpanel/xpanel.sock)") : tr("not reachable: %1").arg(DeviceClient::installHint()));
    if (Desktop::current() == Desktop::Kind::KDE)
        add(tr("Wayland overlay"), !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"), qEnvironmentVariable("WAYLAND_DISPLAY", tr("no Wayland")));
    else
        add(tr("X11 overlay (XWayland)"), !qEnvironmentVariableIsEmpty("DISPLAY"), qEnvironmentVariable("DISPLAY", tr("no DISPLAY")));
    add(tr("TDP interface"), QFile::exists("/sys/class/firmware-attributes/asus-armoury/attributes/ppt_pl1_spl"), "asus-armoury ppt_pl1_spl");
    bool fan = false;
    for (const QString &e : QDir("/sys/class/hwmon").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile n("/sys/class/hwmon/" + e + "/name");
        if (n.open(QIODevice::ReadOnly) && n.readAll().trimmed() == "asus_custom_fan_curve") fan = true;
    }
    add(tr("Fan curve interface"), fan, "hwmon asus_custom_fan_curve");
    add(tr("Charge limit"), QFile::exists("/sys/class/power_supply/BAT0/charge_control_end_threshold"), "BAT0 charge_control_end_threshold");
    QStringList tools{"wpctl", "pactl", "nmcli", "bluetoothctl"};
    switch (Desktop::current()) {
    case Desktop::Kind::KDE: tools << "kscreen-doctor" << "spectacle"; break;
    case Desktop::Kind::GNOME: tools << "gsettings" << "gnome-screenshot" << "ddcutil"; break;
    case Desktop::Kind::Gamescope: break;
    case Desktop::Kind::Other: tools << "ddcutil"; break;
    }
    tools << "checkupdates" << "fwupdmgr";
    for (const QString &tool : std::as_const(tools))
        add(tr("Tool: %1").arg(tool), !QStandardPaths::findExecutable(tool).isEmpty(), QStandardPaths::findExecutable(tool));
    return out;
}
