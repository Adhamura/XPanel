#include "DeviceInfo.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QGuiApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>

#ifdef Q_OS_WIN
#include <QSettings>
#include <windows.h>
#include <winioctl.h> // CTL_CODE, FILE_DEVICE_BATTERY (not in WIN32_LEAN_AND_MEAN)
#include <setupapi.h>
#include <batclass.h>
#define SECURITY_WIN32
#include <security.h>   // GetUserNameExW
#include <sddl.h>       // ConvertSidToStringSidW
#include <xinput.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <QUrl>
#else
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

#ifdef Q_OS_WIN
static QString regString(const QString &key, const QString &name)
{
    QSettings r(key, QSettings::NativeFormat);
    return r.value(name).toString().trimmed();
}

// Serial number: type 1 (System Information) of the SMBIOS table, readable without admin rights
static QString smbiosSerial()
{
    const UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size < 8)
        return {};
    QByteArray buf(int(size), 0);
    if (GetSystemFirmwareTable('RSMB', 0, buf.data(), size) != size)
        return {};
    // RawSMBIOSData: 4 bytes of version info, DWORD Length, then the structures
    const uchar *d = reinterpret_cast<const uchar *>(buf.constData());
    quint32 len = quint32(d[4]) | (quint32(d[5]) << 8) | (quint32(d[6]) << 16) | (quint32(d[7]) << 24);
    if (len > size - 8)
        len = size - 8;
    const uchar *t = d + 8;
    quint32 pos = 0;
    while (pos + 4 <= len) {
        const uchar type = t[pos], flen = t[pos + 1];
        if (flen < 4 || pos + flen > len)
            break;
        // string set after the formatted area, ends with a double NUL
        quint32 sp = pos + flen;
        QList<QByteArray> strs;
        while (sp < len && t[sp] != 0) {
            const quint32 start = sp;
            while (sp < len && t[sp] != 0)
                ++sp;
            strs << QByteArray(reinterpret_cast<const char *>(t + start), int(sp - start));
            ++sp;
        }
        if (type == 1 && flen > 7) {
            const int idx = t[pos + 7];
            if (idx >= 1 && idx <= strs.size())
                return QString::fromLatin1(strs.at(idx - 1)).trimmed();
            return {};
        }
        if (type == 127)
            break;
        pos = sp + 1; // skip the terminating NUL of the string set
    }
    return {};
}
#endif

QVariantMap DeviceInfo::info() const
{
    QVariantMap m;
#ifdef Q_OS_WIN
    const QString bios = "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS";
    const QString cv = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const QString board = regString(bios, "BaseBoardProduct");
    QString product = regString(bios, "SystemProductName");
    // "ROG Ally X RC72LA_RC72LA": drop the repeated board code
    if (!board.isEmpty()) {
        product.remove("_" + board);
        product = product.trimmed();
        if (product.endsWith(" " + board))
            product.chop(board.size() + 1);
    }
    m["model"] = product;
    m["board"] = board;
    m["vendor"] = QString();
    m["bios"] = regString(bios, "BIOSVersion");
    const QString date = regString(bios, "BIOSReleaseDate"); // MM/DD/YYYY
    const QStringList dp = date.split('/');
    m["biosDate"] = dp.size() == 3 ? dp[2] + "/" + dp[0] + "/" + dp[1] : date;
    m["cpu"] = regString("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");
    m["cores"] = QString::number(QThread::idealThreadCount());
    ULONGLONG kb = 0;
    double bytes = 0;
    if (GetPhysicallyInstalledSystemMemory(&kb) && kb > 0) {
        bytes = double(kb) * 1024;
    } else {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
            bytes = double(ms.ullTotalPhys);
    }
    if (bytes > 0)
        m["ram"] = QString::number(bytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
    m["gpu"] = regString("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
                         "DriverDesc");
    QString os = regString(cv, "ProductName");
    if (regString(cv, "CurrentBuildNumber").toInt() >= 22000)
        os.replace("Windows 10", "Windows 11");
    const QString dv = regString(cv, "DisplayVersion");
    if (!dv.isEmpty())
        os += " " + dv;
    m["os"] = os;
    const QString ubr = regString(cv, "UBR");
    m["kernel"] = ubr.isEmpty() ? QSysInfo::kernelVersion() : QSysInfo::kernelVersion() + "." + ubr;
    m["serial"] = smbiosSerial();
#else
    m["model"] = readFile("/sys/class/dmi/id/product_name");
    m["board"] = readFile("/sys/class/dmi/id/board_name");
    m["vendor"] = readFile("/sys/class/dmi/id/sys_vendor");
    m["bios"] = readFile("/sys/class/dmi/id/bios_version");
    m["biosDate"] = readFile("/sys/class/dmi/id/bios_date");
    for (const QString &l : readFile("/proc/cpuinfo").split('\n'))
        if (l.startsWith("model name")) { m["cpu"] = l.section(':', 1).trimmed(); break; }
    m["cores"] = QString::number(QThread::idealThreadCount());
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
#endif
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
#ifdef Q_OS_WIN
        // drive letters only (skips volumes without one: recovery, EFI, reserved)
        const QString root = s.rootPath();
        if (root.size() < 2 || !root.at(0).isLetter() || root.at(1) != ':')
            continue;
        const QString letter = root.left(2).toUpper();
        const QString label = s.displayName() == s.rootPath() ? QString() : s.displayName();
        out << QVariantMap{{"name", label.isEmpty() ? letter : letter + " " + label},
                           {"path", root}, {"device", letter}, {"fs", QString::fromUtf8(s.fileSystemType())},
                           {"total", double(s.bytesTotal())}, {"free", double(s.bytesAvailable())}};
        continue;
#else
        const QString dev = QString::fromUtf8(s.device());
        if (!dev.startsWith("/dev/") || seen.contains(dev))
            continue; // btrfs subvolumes share one device: show it once
        seen.insert(dev);
        out << QVariantMap{{"name", s.displayName() == "/" ? tr("System (/)") : s.displayName()},
                           {"path", s.rootPath()}, {"device", dev}, {"fs", QString::fromUtf8(s.fileSystemType())},
                           {"total", double(s.bytesTotal())}, {"free", double(s.bytesAvailable())}};
#endif
    }
    return out;
}

#ifdef Q_OS_WIN
// Battery interface class GUID_DEVICE_BATTERY (batclass.h), written out so no INITGUID is needed
static const GUID kBatteryInterface = {0x72631e54, 0x78A4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};

QVariantMap DeviceInfo::battery() const
{
    QVariantMap m;
    m["capacity"] = 0;
    m["status"] = tr("Unknown");
    m["cycles"] = 0;
    m["health"] = 0;
    m["full"] = QString::fromUtf8("\xE2\x80\x94");
    m["design"] = QString::fromUtf8("\xE2\x80\x94");
    m["technology"] = QString();
    m["manufacturer"] = QString();

    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps) && sps.BatteryLifePercent <= 100)
        m["capacity"] = int(sps.BatteryLifePercent);

    HDEVINFO set = SetupDiGetClassDevsW(&kBatteryInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return m;
    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kBatteryInterface, i, &did); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
        if (need == 0)
            continue;
        QByteArray buf(int(need), 0);
        auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &did, det, need, nullptr, nullptr))
            continue;
        HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        DWORD wait = 0, tag = 0, got = 0;
        const bool ok = DeviceIoControl(h, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &got, nullptr)
                        && tag != 0;
        if (ok) {
            BATTERY_QUERY_INFORMATION bqi = {};
            bqi.BatteryTag = tag;
            bqi.InformationLevel = BatteryInformation;
            BATTERY_INFORMATION bi = {};
            if (DeviceIoControl(h, IOCTL_BATTERY_QUERY_INFORMATION, &bqi, sizeof(bqi), &bi, sizeof(bi), &got, nullptr)) {
                const bool relative = (bi.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0;
                // mWh normally; relative units have no real unit, so show them bare
                auto fmt = [relative](ULONG v) {
                    return relative ? QString::number(v) : QString::number(v / 1000.0, 'f', 1) + " Wh";
                };
                if (bi.DesignedCapacity > 0 && bi.DesignedCapacity != 0xFFFFFFFF) {
                    m["design"] = fmt(bi.DesignedCapacity);
                    if (bi.FullChargedCapacity > 0 && bi.FullChargedCapacity != 0xFFFFFFFF) {
                        m["full"] = fmt(bi.FullChargedCapacity);
                        m["health"] = qMin(100, qRound(bi.FullChargedCapacity * 100.0 / bi.DesignedCapacity));
                    }
                }
                m["cycles"] = int(bi.CycleCount);
            }
            BATTERY_WAIT_STATUS bws = {};
            bws.BatteryTag = tag;
            BATTERY_STATUS bs = {};
            if (DeviceIoControl(h, IOCTL_BATTERY_QUERY_STATUS, &bws, sizeof(bws), &bs, sizeof(bs), &got, nullptr)) {
                QString st = tr("Unknown");
                if (bs.PowerState & BATTERY_CHARGING)
                    st = tr("Charging");
                else if (bs.PowerState & BATTERY_DISCHARGING)
                    st = tr("Discharging");
                else if (bs.PowerState & BATTERY_POWER_ON_LINE)
                    st = tr("Not charging");
                m["status"] = st;
            }
        }
        CloseHandle(h);
        if (ok)
            break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return m;
}
#else
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
    // sysfs status words ("Not charging" ...): shown translated, the rest stays as the kernel says
    const QString status = readFile(b + "status");
    const QHash<QString, QString> kStatus = {
        {"Charging", tr("Charging")}, {"Discharging", tr("Discharging")}, {"Full", tr("Full")},
        {"Not charging", tr("Not charging")}, {"Unknown", tr("Unknown")}};
    m["status"] = kStatus.value(status, status);
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
#endif

QVariantMap DeviceInfo::user() const
{
    QVariantMap m;
#ifdef Q_OS_WIN
    m["login"] = qEnvironmentVariable("USERNAME");
    wchar_t dn[256];
    ULONG dnLen = 256;
    if (GetUserNameExW(NameDisplay, dn, &dnLen) && dnLen > 0)
        m["name"] = QString::fromWCharArray(dn, int(dnLen)).trimmed();
    // account picture: the registry lists the files Windows made for this SID
    QString sid;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        DWORD need = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
        if (need) {
            QByteArray buf(int(need), 0);
            if (GetTokenInformation(tok, TokenUser, buf.data(), need, &need)) {
                LPWSTR str = nullptr;
                if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid, &str)) {
                    sid = QString::fromWCharArray(str);
                    LocalFree(str);
                }
            }
        }
        CloseHandle(tok);
    }
    QStringList pics;
    if (!sid.isEmpty()) {
        const QString key = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\" + sid;
        pics << regString(key, "Image448") << regString(key, "Image192");
    }
    const QString pd = qEnvironmentVariable("ProgramData", "C:/ProgramData");
    pics << pd + "/Microsoft/User Account Pictures/user.png" << pd + "/Microsoft/User Account Pictures/user-192.png";
    for (const QString &p : std::as_const(pics))
        if (!p.isEmpty() && QFile::exists(p)) { m["avatar"] = QUrl::fromLocalFile(p).toString(); break; }
#else
    if (const passwd *pw = getpwuid(getuid())) {
        m["login"] = QString::fromLocal8Bit(pw->pw_name);
        m["name"] = QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0);
    }
#endif
    if (m["name"].toString().isEmpty())
        m["name"] = m["login"];
#ifndef Q_OS_WIN
    for (const QString &p : {QDir::homePath() + "/.face.icon", QDir::homePath() + "/.face",
                             "/var/lib/AccountsService/icons/" + m["login"].toString()})
        if (QFile::exists(p)) { m["avatar"] = "file://" + p; break; }
#endif
    return m;
}

QVariantList DeviceInfo::controllers() const
{
#ifdef Q_OS_WIN
    QVariantList out;
    // HID game controllers (usage page 1, usage 4 joystick / 5 gamepad). Pads that XInput
    // covers have "ig_" in the path: those are listed through XInput below.
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    int internalXi = 0; // the Ally's own pad (ASUS, VID 0B05) is an XInput device too
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA did;
        did.cbSize = sizeof(did);
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &did); ++i) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
            if (need == 0)
                continue;
            QByteArray buf(int(need), 0);
            auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &did, det, need, nullptr, nullptr))
                continue;
            const QString path = QString::fromWCharArray(det->DevicePath);
            HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            HIDD_ATTRIBUTES at = {};
            at.Size = sizeof(at);
            PHIDP_PREPARSED_DATA pp = nullptr;
            HIDP_CAPS caps = {};
            const bool ok = HidD_GetAttributes(h, &at) && HidD_GetPreparsedData(h, &pp);
            if (ok) {
                HidP_GetCaps(pp, &caps);
                HidD_FreePreparsedData(pp);
            }
            if (ok && caps.UsagePage == 1 && (caps.Usage == 4 || caps.Usage == 5)) {
                const bool xi = path.contains("&ig_", Qt::CaseInsensitive);
                if (xi && at.VendorID == 0x0b05) {
                    ++internalXi;
                } else if (!xi && at.VendorID != 0x0b05) {
                    wchar_t prod[128] = {};
                    HidD_GetProductString(h, prod, sizeof(prod));
                    const bool bt = path.contains("00001124-0000-1000-8000-00805f9b34fb", Qt::CaseInsensitive);
                    QString name = QString::fromWCharArray(prod).trimmed();
                    if (name.isEmpty())
                        name = tr("Game controller");
                    out << QVariantMap{{"name", name},
                                       {"id", QString("%1:%2").arg(at.VendorID, 4, 16, QChar('0')).arg(at.ProductID, 4, 16, QChar('0'))},
                                       {"bus", bt ? "Bluetooth" : "USB"}};
                }
            }
            CloseHandle(h);
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        XINPUT_STATE st;
        if (XInputGetState(slot, &st) != ERROR_SUCCESS)
            continue;
        if (internalXi > 0) { // skip the Ally's own pad
            --internalXi;
            continue;
        }
        out << QVariantMap{{"name", tr("Xbox-compatible controller %1").arg(slot + 1)},
                           {"id", "XInput:" + QString::number(slot + 1)}, {"bus", "XInput"}};
    }
    return out;
#else
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
#endif
}
