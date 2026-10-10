#include "DeviceInfo.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QSysInfo>

#ifndef Q_OS_WIN
#include <pwd.h>
#include <unistd.h>
#endif

static QString readFile(const QString &fn)
{
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QVariantMap DeviceInfo::info() const
{
    QVariantMap m;
    m["model"] = readFile("/sys/class/dmi/id/product_name");
    m["vendor"] = readFile("/sys/class/dmi/id/sys_vendor");
    m["bios"] = readFile("/sys/class/dmi/id/bios_version");
    m["biosDate"] = readFile("/sys/class/dmi/id/bios_date");
    for (const QString &l : readFile("/proc/cpuinfo").split('\n'))
        if (l.startsWith("model name")) { m["cpu"] = l.section(':', 1).trimmed(); break; }
    m["cores"] = QString::number(sysconf(_SC_NPROCESSORS_ONLN));
    for (const QString &l : readFile("/proc/meminfo").split('\n'))
        if (l.startsWith("MemTotal:")) {
            m["ram"] = QString::number(l.section(' ', 1, 1, QString::SectionSkipEmpty).toDouble() / 1024 / 1024, 'f', 1) + " GB";
            break;
        }
    // GPU: the DRM card with a PCI vendor; the marketing name is in the amdgpu "product_name"
    const QDir drm("/sys/class/drm");
    for (const QString &e : drm.entryList({"card?"}, QDir::Dirs)) {
        const QString dev = drm.absoluteFilePath(e) + "/device/";
        const QString pci = readFile(dev + "uevent").section("PCI_ID=", 1).section('\n', 0, 0);
        if (pci.isEmpty()) continue;
        const QString name = readFile(dev + "product_name");
        m["gpu"] = name.isEmpty() ? "AMD Radeon Graphics (" + pci + ")" : name;
        break;
    }
    for (const QString &l : readFile("/etc/os-release").split('\n'))
        if (l.startsWith("PRETTY_NAME=")) { m["os"] = l.mid(12).remove('"'); break; }
    m["kernel"] = QSysInfo::kernelVersion();
    if (QScreen *s = QGuiApplication::primaryScreen())
        m["display"] = QString("%1 x %2 @ %3 Hz").arg(s->size().width() * s->devicePixelRatio())
                           .arg(s->size().height() * s->devicePixelRatio()).arg(qRound(s->refreshRate()));
    m["hostname"] = QSysInfo::machineHostName();
    return m;
}

QVariantList DeviceInfo::storage() const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid() || !s.isReady() || s.bytesTotal() < 1024LL * 1024 * 1024)
            continue;
        const QString dev = QString::fromUtf8(s.device());
        if (!dev.startsWith("/dev/") || seen.contains(dev))
            continue; // btrfs subvolumes share one device: show it once
        seen.insert(dev);
        out << QVariantMap{{"name", s.displayName() == "/" ? "System (/)" : s.displayName()},
                           {"path", s.rootPath()}, {"device", dev}, {"fs", QString::fromUtf8(s.fileSystemType())},
                           {"total", double(s.bytesTotal())}, {"free", double(s.bytesAvailable())}};
    }
    return out;
}

QVariantMap DeviceInfo::battery() const
{
    const QString b = "/sys/class/power_supply/BAT0/";
    auto num = [&](const QString &a, const QString &alt) {
        const QString v = readFile(b + a);
        return (v.isEmpty() ? readFile(b + alt) : v).toDouble();
    };
    const double full = num("energy_full", "charge_full");
    const double design = num("energy_full_design", "charge_full_design");
    QVariantMap m;
    m["capacity"] = readFile(b + "capacity").toInt();
    m["status"] = readFile(b + "status");
    m["cycles"] = readFile(b + "cycle_count").toInt();
    m["health"] = design > 0 ? qRound(full * 100 / design) : 0;
    // µWh or µAh; show Wh when the energy files exist
    const bool energy = QFile::exists(b + "energy_full");
    m["full"] = energy ? QString::number(full / 1e6, 'f', 1) + " Wh" : QString::number(full / 1e3, 'f', 0) + " mAh";
    m["design"] = energy ? QString::number(design / 1e6, 'f', 1) + " Wh" : QString::number(design / 1e3, 'f', 0) + " mAh";
    m["technology"] = readFile(b + "technology");
    m["manufacturer"] = readFile(b + "manufacturer");
    return m;
}

QVariantMap DeviceInfo::user() const
{
    QVariantMap m;
#ifdef Q_OS_WIN
    m["login"] = qEnvironmentVariable("USERNAME");
#else
    if (const passwd *pw = getpwuid(getuid())) {
        m["login"] = QString::fromLocal8Bit(pw->pw_name);
        m["name"] = QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0);
    }
#endif
    if (m["name"].toString().isEmpty())
        m["name"] = m["login"];
    for (const QString &p : {QDir::homePath() + "/.face.icon", QDir::homePath() + "/.face",
                             "/var/lib/AccountsService/icons/" + m["login"].toString()})
        if (QFile::exists(p)) { m["avatar"] = "file://" + p; break; }
    return m;
}

QVariantList DeviceInfo::controllers() const
{
    // /proc/bus/input/devices blocks; joysticks have a "js" handler
    QVariantList out;
    for (const QString &block : readFile("/proc/bus/input/devices").split("\n\n")) {
        if (!block.contains(QRegularExpression("Handlers=.*\\bjs\\d")))
            continue;
        const QString name = block.section("N: Name=\"", 1).section('"', 0, 0);
        const QString vendor = block.section("Vendor=", 1).section(' ', 0, 0);
        const QString product = block.section("Product=", 1).section(' ', 0, 0);
        // the Ally itself, or a virtual pad (XPanel's own Xbox pad lives under /devices/virtual)
        if (vendor == "0b05" || block.contains("Sysfs=/devices/virtual"))
            continue; // the Ally itself or its virtual pad
        const bool bt = block.contains("Bus=0005");
        out << QVariantMap{{"name", name}, {"id", vendor + ":" + product}, {"bus", bt ? "Bluetooth" : "USB"}};
    }
    return out;
}
