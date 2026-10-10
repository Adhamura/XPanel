#include "Hardware.h"
#include "Aura.h"
#include "FanCurve.h"

#include <QDir>
#include <QFile>
#include <QThread>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {

const char *kArmoury = "/sys/class/firmware-attributes/asus-armoury/attributes/";

QString readFile(const QString &fn)
{
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool writeFile(const QString &fn, const QString &value)
{
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("xpanel-daemon: cannot open %s: %s", qPrintable(fn), qPrintable(f.errorString()));
        return false;
    }
    const bool ok = f.write(value.toUtf8()) >= 0 && f.flush();
    if (!ok)
        qWarning("xpanel-daemon: write '%s' to %s failed: %s", qPrintable(value), qPrintable(fn), qPrintable(f.errorString()));
    return ok;
}

int attr(const char *name, const char *field)
{
    return readFile(QString(kArmoury) + name + "/" + field).toInt();
}

QString findHwmon(const QString &name)
{
    const QDir dir("/sys/class/hwmon");
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = dir.absoluteFilePath(e);
        if (readFile(p + "/name") == name)
            return p;
    }
    return {};
}

// ---------- Aura HID ----------

// The Ally "N-KEY Device" interface with vendor usage page 0xFF31 takes the
// lighting commands. Find it by its report descriptor.
QString findAuraHidraw()
{
    const QDir dir("/sys/class/hidraw");
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = dir.absoluteFilePath(e) + "/device/";
        const QString uevent = readFile(base + "uevent");
        if (!uevent.contains("0B05", Qt::CaseInsensitive))
            continue; // not ASUS
        QFile rd(base + "report_descriptor");
        if (!rd.open(QIODevice::ReadOnly))
            continue;
        const QByteArray d = rd.readAll();
        // "Usage Page (0xFF31)" = 06 31 FF, and report id 0x5A = 85 5A
        if (d.contains(QByteArray("\x06\x31\xff", 3)) && d.contains(QByteArray("\x85\x5a", 2)))
            return "/dev/" + e;
    }
    return {};
}

} // namespace

namespace Hardware {

bool onAc()
{
    const QDir dir("/sys/class/power_supply");
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = dir.absoluteFilePath(e);
        if (readFile(p + "/type") == "Mains" && readFile(p + "/online") == "1")
            return true;
    }
    return false;
}

namespace {
// The built-in controller's USB product id (0 = none found).
quint16 controllerPid()
{
    const QDir dir("/sys/bus/usb/devices");
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = dir.absoluteFilePath(e);
        const quint16 vendor = readFile(p + "/idVendor").toUShort(nullptr, 16);
        const quint16 product = readFile(p + "/idProduct").toUShort(nullptr, 16);
        if (Model::isController(vendor, product))
            return product;
    }
    return 0;
}

// The firmware's range for one asus-armoury power attribute, else the model's.
Model::Range range(const char *name)
{
    const int lo = attr(name, "min_value"), hi = attr(name, "max_value");
    if (hi > 0 && hi >= lo)
        return {lo, hi};
    const Model::Ranges &r = onAc() ? model().ac : model().battery;
    if (!strcmp(name, "ppt_pl2_sppt")) return r.sppt;
    if (!strcmp(name, "ppt_pl3_fppt")) return r.fppt;
    return r.spl;
}
} // namespace

const Model::Info &model()
{
    static const Model::Info m = Model::detect(readFile("/sys/class/dmi/id/board_name"), controllerPid());
    return m;
}

QJsonObject limits()
{
    const Model::Range spl = range("ppt_pl1_spl"), sppt = range("ppt_pl2_sppt"), fppt = range("ppt_pl3_fppt");
    return {{"tdpMin", spl.min}, {"tdpMax", spl.max}, {"spptMin", sppt.min}, {"spptMax", sppt.max},
            {"fpptMin", fppt.min}, {"fpptMax", fppt.max}};
}

Tdp readTdp()
{
    Tdp t;
    t.spl = attr("ppt_pl1_spl", "current_value");
    t.sppt = attr("ppt_pl2_sppt", "current_value");
    t.fppt = attr("ppt_pl3_fppt", "current_value");
    t.profile = readFile("/sys/firmware/acpi/platform_profile");
    return t;
}

bool applyTdp(const Tdp &t)
{
    auto clampAttr = [](const char *name, int v) {
        const Model::Range r = range(name);
        return qBound(r.min, v, r.max);
    };
    bool ok = true;
    // The profile first: the firmware may reset the limits when it changes.
    if (!t.profile.isEmpty() && readFile("/sys/firmware/acpi/platform_profile") != t.profile)
        ok &= writeFile("/sys/firmware/acpi/platform_profile", t.profile);
    const int spl = clampAttr("ppt_pl1_spl", t.spl);
    const int sppt = clampAttr("ppt_pl2_sppt", qMax(t.sppt, spl));
    const int fppt = clampAttr("ppt_pl3_fppt", qMax(t.fppt, sppt));
    ok &= writeFile(QString(kArmoury) + "ppt_pl1_spl/current_value", QString::number(spl));
    ok &= writeFile(QString(kArmoury) + "ppt_pl2_sppt/current_value", QString::number(sppt));
    ok &= writeFile(QString(kArmoury) + "ppt_pl3_fppt/current_value", QString::number(fppt));
    return ok;
}

bool applyFan(const QString &preset, const QJsonObject &curve)
{
    const QString hw = findHwmon("asus_custom_fan_curve");
    if (hw.isEmpty())
        return false;
    // temperatures (°C) and duty (0..255) for the 8 curve points
    static const int temps[8] = {40, 50, 60, 70, 78, 85, 90, 95};
    static const int quiet[8] = {0, 20, 40, 65, 95, 130, 170, 210};
    static const int maxed[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    // custom: fan 1 (CPU) and fan 3 follow the "cpu" curve, fan 2 (GPU) the "gpu" curve
    const FanCurve::Curve cpuCurve = FanCurve::cpu(curve), gpuCurve = FanCurve::gpu(curve);
    bool ok = true;
    for (int fan = 1; fan <= 3; ++fan) {
        const QString en = QString("%1/pwm%2_enable").arg(hw).arg(fan);
        if (!QFile::exists(en))
            continue;
        if (preset == "auto") {
            ok &= writeFile(en, "2"); // firmware curve
            continue;
        }
        const FanCurve::Curve &mine = fan == 2 ? gpuCurve : cpuCurve;
        for (int p = 0; p < 8; ++p) {
            int temp = temps[p], duty = preset == "max" ? maxed[p] : quiet[p];
            if (preset == "custom") {
                temp = mine[size_t(p)].temp;
                duty = FanCurve::toPwm(mine[size_t(p)].percent);
            }
            ok &= writeFile(QString("%1/pwm%2_auto_point%3_temp").arg(hw).arg(fan).arg(p + 1), QString::number(temp));
            ok &= writeFile(QString("%1/pwm%2_auto_point%3_pwm").arg(hw).arg(fan).arg(p + 1), QString::number(duty));
        }
        ok &= writeFile(en, "1"); // use the custom curve
    }
    return ok;
}

int readChargeLimit()
{
    const int v = readFile("/sys/class/power_supply/BAT0/charge_control_end_threshold").toInt();
    return v > 0 ? v : 100;
}

bool applyChargeLimit(int percent)
{
    return writeFile("/sys/class/power_supply/BAT0/charge_control_end_threshold",
                     QString::number(qBound(50, percent, 100)));
}

int batteryPercent()
{
    const QString v = readFile("/sys/class/power_supply/BAT0/capacity");
    return v.isEmpty() ? -1 : qBound(0, v.toInt(), 100);
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

    const QString dev = findAuraHidraw();
    if (dev.isEmpty()) {
        qWarning("xpanel-daemon: Aura HID device not found");
        return false;
    }
    const int fd = ::open(dev.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning("xpanel-daemon: cannot open %s: %s", qPrintable(dev), strerror(errno));
        return false;
    }

    bool ok = true;
    for (const QByteArray &b : cmds) {
        if (::write(fd, b.constData(), b.size()) != b.size())
            ok = false;
        QThread::msleep(4);
    }
    ::close(fd);
    if (!ok)
        qWarning("xpanel-daemon: Aura HID write failed on %s", qPrintable(dev));
    return ok;
}

namespace {
QString gpuDevice()
{
    const QDir drm("/sys/class/drm");
    for (const QString &e : drm.entryList({"card?"}, QDir::Dirs)) {
        const QString p = drm.absoluteFilePath(e) + "/device";
        if (QFile::exists(p + "/pp_od_clk_voltage"))
            return p;
    }
    return {};
}
} // namespace

bool applyCpu(bool eco, const QString &opmode)
{
    QString epp = "balance_performance";
    if (eco || opmode == "silent") epp = "power";
    else if (opmode == "performance") epp = "balance_power";
    else if (opmode == "turbo") epp = "performance";
    bool ok = true;
    const QDir dir("/sys/devices/system/cpu/cpufreq");
    for (const QString &pol : dir.entryList({"policy*"}, QDir::Dirs)) {
        const QString base = dir.absoluteFilePath(pol);
        // EPP can only change while the governor is powersave (amd-pstate-epp)
        if (readFile(base + "/energy_performance_preference") != epp)
            ok &= writeFile(base + "/energy_performance_preference", epp);
    }
    if (QFile::exists("/sys/devices/system/cpu/cpufreq/boost"))
        ok &= writeFile("/sys/devices/system/cpu/cpufreq/boost", eco ? "0" : "1");
    return ok;
}

QJsonObject gpuRange()
{
    QJsonObject o{{"gpuMin", 0}, {"gpuMax", 0}};
    const QString dev = gpuDevice();
    if (dev.isEmpty())
        return o;
    // OD_RANGE: "SCLK:     800Mhz       2700Mhz"
    for (const QString &l : readFile(dev + "/pp_od_clk_voltage").split('\n')) {
        if (!l.startsWith("SCLK:"))
            continue;
        const QStringList f = l.mid(5).split(' ', Qt::SkipEmptyParts);
        if (f.size() >= 2) {
            o["gpuMin"] = f[0].chopped(3).toInt();
            o["gpuMax"] = f[1].chopped(3).toInt();
        }
    }
    return o;
}

bool applyGpuClock(int mhz)
{
    const QString dev = gpuDevice();
    if (dev.isEmpty())
        return false;
    if (mhz <= 0) {
        writeFile(dev + "/pp_od_clk_voltage", "r");
        writeFile(dev + "/pp_od_clk_voltage", "c");
        return writeFile(dev + "/power_dpm_force_performance_level", "auto");
    }
    const QJsonObject r = gpuRange();
    mhz = qBound(r["gpuMin"].toInt(), mhz, r["gpuMax"].toInt());
    bool ok = writeFile(dev + "/power_dpm_force_performance_level", "manual");
    ok &= writeFile(dev + "/pp_od_clk_voltage", QString("s 1 %1").arg(mhz));
    ok &= writeFile(dev + "/pp_od_clk_voltage", "c");
    return ok;
}

int readAttr(const QString &name)
{
    return readFile(QString(kArmoury) + name + "/current_value").toInt();
}

bool writeAttr(const QString &name, int value)
{
    if (!QFile::exists(QString(kArmoury) + name))
        return false;
    return writeFile(QString(kArmoury) + name + "/current_value", QString::number(value));
}

QJsonObject deviceInfo()
{
    return {
        {"product", readFile("/sys/class/dmi/id/product_name")},
        {"board", readFile("/sys/class/dmi/id/board_name")},
        {"bios", readFile("/sys/class/dmi/id/bios_version")},
        {"biosDate", readFile("/sys/class/dmi/id/bios_date")},
        {"serial", readFile("/sys/class/dmi/id/product_serial")},
        {"model", QString::fromLatin1(model().id)},
        {"modelName", QString::fromLatin1(model().name)},
    };
}

} // namespace Hardware
