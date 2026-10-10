// Windows implementation of Hardware.h for the ROG Ally / Ally X (experimental).
//
//   TDP, Operating Mode, fan curve, charge limit, boot sound, MCU power save:
//     the ASUS System Control Interface driver (\\.\ATKACPI, installed on every Ally), with
//     the same ACPI methods (DEVS set / DSTS get) and device ids as Linux's asus-wmi driver.
//   Lighting: the same Aura HID reports as on Linux, on the N-KEY collection with vendor
//     usage page 0xFF31, through the Windows HID API.
//   CPU: processor boost and energy preference of the active power plan (powercfg).
// Armoury Crate changes the same settings: it must not run at the same time.
#include "Hardware.h"
#include "Aura.h"
#include "FanCurve.h"

#include <QHash>
#include <QProcess>
#include <QSettings>
#include <QThread>
#include <QtEndian>

#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}

namespace {

constexpr DWORD kControlCode = 0x0022240C;
constexpr quint32 kDSTS = 0x53545344; // "DSTS": get
constexpr quint32 kDEVS = 0x53564544; // "DEVS": set

constexpr quint32 kThermalPolicy = 0x00120075; // 0 balanced, 1 turbo, 2 silent
constexpr quint32 kPptSpl = 0x001200A3;        // sustained (PL1)
constexpr quint32 kPptSppt = 0x001200A0;       // slow boost (PL2)
constexpr quint32 kPptFppt = 0x001200C1;       // fast boost
constexpr quint32 kCpuFanCurve = 0x00110024;
constexpr quint32 kGpuFanCurve = 0x00110025;
constexpr quint32 kChargeLimit = 0x00120057;
constexpr quint32 kBootSound = 0x00130022;
constexpr quint32 kMcuPowersave = 0x001200E2;

// Power limit ranges: the model's (Model.h). The Linux driver reports them itself; here they are fixed.
Hardware::Tdp g_tdp; // what was applied last (the firmware cannot be read back reliably)

HANDLE acpi()
{
    static HANDLE h = INVALID_HANDLE_VALUE;
    static bool warned = false;
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileW(L"\\\\.\\ATKACPI", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE && !warned) {
            warned = true;
            qWarning("xpanel-daemon: cannot open \\\\.\\ATKACPI (error %lu): is the ASUS System Control "
                     "Interface driver installed?", GetLastError());
        }
    }
    return h;
}

QByteArray callMethod(quint32 method, const QByteArray &args)
{
    const HANDLE h = acpi();
    if (h == INVALID_HANDLE_VALUE)
        return {};
    QByteArray in(8 + args.size(), '\0');
    qToLittleEndian<quint32>(method, in.data());
    qToLittleEndian<quint32>(quint32(args.size()), in.data() + 4);
    memcpy(in.data() + 8, args.constData(), size_t(args.size()));
    QByteArray out(16, '\0');
    DWORD got = 0;
    if (!DeviceIoControl(h, kControlCode, in.data(), DWORD(in.size()), out.data(), DWORD(out.size()), &got, nullptr)) {
        qWarning("xpanel-daemon: ATKACPI call %08x failed (error %lu)", method, GetLastError());
        return {};
    }
    return out;
}

QByteArray arg32(quint32 a, quint32 b)
{
    QByteArray r(8, '\0');
    qToLittleEndian<quint32>(a, r.data());
    qToLittleEndian<quint32>(b, r.data() + 4);
    return r;
}

// DEVS: 1 = done.
bool devs(quint32 id, quint32 value)
{
    const QByteArray out = callMethod(kDEVS, arg32(id, value));
    const bool ok = !out.isEmpty() && qFromLittleEndian<qint32>(out.constData()) == 1;
    if (!ok)
        qWarning("xpanel-daemon: ASUS device %08x refused value %u", id, value);
    return ok;
}

bool devsBytes(quint32 id, const QByteArray &params)
{
    QByteArray a(4, '\0');
    qToLittleEndian<quint32>(id, a.data());
    const QByteArray out = callMethod(kDEVS, a + params);
    const bool ok = !out.isEmpty() && qFromLittleEndian<qint32>(out.constData()) == 1;
    if (!ok)
        qWarning("xpanel-daemon: ASUS device %08x refused the new values", id);
    return ok;
}

// DSTS: the value has bit 16 set when the device exists; -1 = not available.
int dsts(quint32 id)
{
    const QByteArray out = callMethod(kDSTS, arg32(id, 0));
    if (out.isEmpty())
        return -1;
    const qint32 v = qFromLittleEndian<qint32>(out.constData());
    return (v & 0x10000) ? (v - 0x10000) : -1;
}

int policyFor(const QString &profile)
{
    if (profile == "low-power") return 2;
    if (profile == "performance") return 1;
    return 0;
}

// ---------- Aura HID ----------

struct Hid {
    HANDLE h = INVALID_HANDLE_VALUE;
    USHORT outLen = 0, featLen = 0;
};

// The N-KEY collection with vendor usage page 0xFF31 takes the lighting commands
// (any ASUS product id: the Ally and the Ally X have it).
Hid openAura()
{
    Hid r;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return r;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0)
            continue;
        QByteArray buf(int(size), '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        const HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof(attr);
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        const bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == 0x0B05
                           && HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS
                           && caps.UsagePage == 0xFF31;
        if (pp)
            HidD_FreePreparsedData(pp);
        if (match) {
            r = {h, caps.OutputReportByteLength, caps.FeatureReportByteLength};
            break;
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return r;
}

// The built-in controller's USB product id (0 = none found), from the HID device paths
// ("\\?\hid#vid_0b05&pid_1abe&mi_02#...").
quint16 controllerPid()
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return 0;
    quint16 found = 0;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; !found && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0)
            continue;
        QByteArray buf(int(size), '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        const QString path = QString::fromWCharArray(detail->DevicePath).toLower();
        const int at = path.indexOf(QLatin1String("vid_0b05&pid_"));
        if (at < 0)
            continue;
        const quint16 pid = path.mid(at + 13, 4).toUShort(nullptr, 16);
        if (Model::isController(Model::kAsusVendor, pid))
            found = pid;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

// Output report (as hidraw write() on Linux); feature report if the collection has none.
bool sendReport(const Hid &d, QByteArray report)
{
    if (d.outLen >= report.size()) {
        report.resize(d.outLen, '\0');
        DWORD written = 0;
        if (WriteFile(d.h, report.constData(), DWORD(report.size()), &written, nullptr))
            return true;
    }
    if (d.featLen >= 64) {
        report.resize(d.featLen, '\0');
        return HidD_SetFeature(d.h, report.data(), ULONG(report.size()));
    }
    return false;
}

void runHidden(const QString &program, const QStringList &args)
{
    QProcess p;
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    p.start(program, args);
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        qWarning("xpanel-daemon: %s %s failed", qPrintable(program), qPrintable(args.join(' ')));
}

} // namespace

namespace Hardware {

bool onAc()
{
    SYSTEM_POWER_STATUS s{};
    return GetSystemPowerStatus(&s) && s.ACLineStatus == 1;
}

const Model::Info &model()
{
    static const Model::Info m = [] {
        const QSettings bios("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS", QSettings::NativeFormat);
        return Model::detect(bios.value("BaseBoardProduct").toString(), controllerPid());
    }();
    return m;
}

QJsonObject limits()
{
    const Model::Ranges &r = onAc() ? model().ac : model().battery;
    return {{"tdpMin", r.spl.min}, {"tdpMax", r.spl.max}, {"spptMin", r.sppt.min}, {"spptMax", r.sppt.max},
            {"fpptMin", r.fppt.min}, {"fpptMax", r.fppt.max}};
}

Tdp readTdp()
{
    return g_tdp;
}

bool applyTdp(const Tdp &want)
{
    const Model::Ranges &r = onAc() ? model().ac : model().battery;
    Tdp t = want;
    t.spl = qBound(r.spl.min, t.spl, r.spl.max);
    t.sppt = qBound(qMax(t.spl, r.sppt.min), t.sppt, r.sppt.max);
    t.fppt = qBound(qMax(t.sppt, r.fppt.min), t.fppt, r.fppt.max);
    // The thermal policy resets the power limits: set it first.
    bool ok = devs(kThermalPolicy, quint32(policyFor(t.profile)));
    ok &= devs(kPptSpl, quint32(t.spl));
    ok &= devs(kPptSppt, quint32(t.sppt));
    ok &= devs(kPptFppt, quint32(t.fppt));
    g_tdp = t;
    return ok;
}

bool applyFan(const QString &preset, const QJsonObject &custom)
{
    if (preset == "auto") // setting the thermal policy again restores the firmware curve
        return devs(kThermalPolicy, quint32(policyFor(g_tdp.profile)));
    // 8 temperatures (°C), then 8 fan speeds (%)
    static const char temps[8] = {40, 50, 60, 70, 78, 85, 90, 95};
    static const char quiet[8] = {0, 8, 16, 25, 37, 51, 67, 82};
    static const char maxed[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    auto bytes = [](const FanCurve::Curve &c) {
        QByteArray t, d;
        for (const FanCurve::Point &p : c) {
            t.append(char(p.temp));
            d.append(char(p.percent));
        }
        return t + d;
    };
    QByteArray cpuCurve = QByteArray(temps, 8) + QByteArray(preset == "max" ? maxed : quiet, 8);
    QByteArray gpuCurve = cpuCurve;
    if (preset == "custom") { // same layout: 8 temperatures, 8 duty values in %
        cpuCurve = bytes(FanCurve::cpu(custom));
        gpuCurve = bytes(FanCurve::gpu(custom));
    }
    const bool cpu = devsBytes(kCpuFanCurve, cpuCurve);
    const bool gpu = devsBytes(kGpuFanCurve, gpuCurve);
    return cpu || gpu;
}

int readChargeLimit()
{
    const int v = dsts(kChargeLimit);
    return v > 0 && v <= 100 ? v : 100;
}

bool applyChargeLimit(int percent)
{
    return devs(kChargeLimit, quint32(qBound(50, percent, 100)));
}

int batteryPercent()
{
    SYSTEM_POWER_STATUS s{};
    return GetSystemPowerStatus(&s) && s.BatteryLifePercent <= 100 ? int(s.BatteryLifePercent) : -1;
}

bool applyLighting(const QString &effect, const QString &color, int brightness, const QString &speed,
                   const QString &color2, const QString &direction)
{
    Aura::Params p;
    p.effect = effect;
    p.color = color;
    p.brightness = brightness;
    p.speed = speed;
    if (!color2.isEmpty()) p.color2 = color2;
    if (!direction.isEmpty()) p.direction = direction;
    const QList<QByteArray> cmds = Aura::packets(p);
    if (cmds.isEmpty())
        return false;
    const Hid d = openAura();
    if (d.h == INVALID_HANDLE_VALUE) {
        qWarning("xpanel-daemon: Aura HID device not found");
        return false;
    }
    bool ok = true;
    for (const QByteArray &c : cmds) {
        ok &= sendReport(d, c);
        QThread::msleep(4);
    }
    CloseHandle(d.h);
    if (!ok)
        qWarning("xpanel-daemon: Aura HID write failed (error %lu)", GetLastError());
    return ok;
}

bool applyCpu(bool eco, const QString &opmode)
{
    // Energy preference 0 (performance) .. 100 (power); boost 0 off, 2 aggressive.
    int epp = 33;
    if (eco || opmode == "silent") epp = 80;
    else if (opmode == "performance") epp = 50;
    else if (opmode == "turbo") epp = 0;
    const QString boost = eco ? "0" : "2";
    for (const QString &mode : {QStringLiteral("/setacvalueindex"), QStringLiteral("/setdcvalueindex")}) {
        runHidden("powercfg", {mode, "SCHEME_CURRENT", "SUB_PROCESSOR", "PERFEPP", QString::number(epp)});
        runHidden("powercfg", {mode, "SCHEME_CURRENT", "SUB_PROCESSOR", "PERFBOOSTMODE", boost});
    }
    runHidden("powercfg", {"/setactive", "SCHEME_CURRENT"});
    return true;
}

bool applyGpuClock(int mhz)
{
    return mhz <= 0; // not available on Windows yet (the AMD driver owns the GPU clock)
}

QJsonObject gpuRange()
{
    return {{"gpuMin", 0}, {"gpuMax", 0}};
}

int readAttr(const QString &name)
{
    const int v = dsts(name == "boot_sound" ? kBootSound : kMcuPowersave);
    return v < 0 ? 0 : v;
}

bool writeAttr(const QString &name, int value)
{
    if (name == "boot_sound") return devs(kBootSound, quint32(value));
    if (name == "mcu_powersave") return devs(kMcuPowersave, quint32(value));
    return false;
}

QJsonObject deviceInfo()
{
    const QSettings bios("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS", QSettings::NativeFormat);
    return {
        {"product", bios.value("SystemProductName").toString()},
        {"board", bios.value("BaseBoardProduct").toString()},
        {"bios", bios.value("BIOSVersion").toString()},
        {"biosDate", bios.value("BIOSReleaseDate").toString()},
        {"serial", QString()},
        {"model", QString::fromLatin1(model().id)},
        {"modelName", QString::fromLatin1(model().name)},
    };
}

} // namespace Hardware
