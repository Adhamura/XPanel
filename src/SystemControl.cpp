#include "SystemControl.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

static QString readFile(const QString &fn)
{
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

static QString findHwmon(const QString &name)
{
    const QDir dir("/sys/class/hwmon");
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = dir.absoluteFilePath(e);
        if (readFile(p + "/name") == name)
            return p;
    }
    return {};
}

SystemControl::SystemControl(QObject *parent) : QObject(parent)
{
    m_hwCpu = findHwmon("k10temp");
    m_hwGpu = findHwmon("amdgpu");
    m_hwFan = findHwmon("asus");
    if (m_hwFan.isEmpty())
        m_hwFan = findHwmon("asus_custom_fan_curve");
    const QDir drm("/sys/class/drm");
    for (const QString &e : drm.entryList({"card*"}, QDir::Dirs)) {
        const QString p = drm.absoluteFilePath(e) + "/device/gpu_busy_percent";
        if (QFile::exists(p)) {
            m_gpuBusy = p;
            break;
        }
    }

    // Find the internal display in KDE's brightness service.
    for (int i = 0; i < 8; ++i) {
        const QString path = QString("/org/kde/ScreenBrightness/display%1").arg(i);
        QDBusInterface d("org.kde.ScreenBrightness", path, "org.kde.ScreenBrightness.Display");
        if (!d.isValid())
            break;
        if (d.property("IsInternal").toBool()) {
            m_blPath = path;
            m_blMax = qMax(1, d.property("MaxBrightness").toInt());
            break;
        }
    }

    connect(&m_slow, &QTimer::timeout, this, [this] {
        readBattery();
        tickClock();
    });
    m_slow.start(5000);
    connect(&m_fast, &QTimer::timeout, this, &SystemControl::readStats);
    m_fast.setInterval(1500);

    m_volumeDebounce.setSingleShot(true);
    m_volumeDebounce.setInterval(60);
    connect(&m_volumeDebounce, &QTimer::timeout, this, [this] {
        if (m_pendingVolume < 0)
            return;
        run("wpctl", {"set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@",
                      QString::number(m_pendingVolume / 100.0, 'f', 2)});
        m_pendingVolume = -1;
    });

    refreshAll();
}

void SystemControl::run(const QString &prog, const QStringList &args,
                        std::function<void(const QString &)> done)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    connect(p, &QProcess::finished, this, [p, done](int, QProcess::ExitStatus) {
        if (done)
            done(QString::fromUtf8(p->readAllStandardOutput()));
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            p->deleteLater();
    });
    p->start(prog, args);
}

void SystemControl::refreshAll()
{
    readBrightness();
    readVolume();
    readBattery();
    readRadios();
    readRefresh();
    tickClock();
}

void SystemControl::setSampling(bool on)
{
    if (on == m_sampling)
        return;
    m_sampling = on;
    if (on) {
        readStats();
        m_fast.start();
    } else {
        m_fast.stop();
    }
    emit samplingChanged();
}

// ---------- brightness ----------
void SystemControl::readBrightness()
{
    if (m_blPath.isEmpty())
        return;
    QDBusInterface d("org.kde.ScreenBrightness", m_blPath, "org.kde.ScreenBrightness.Display");
    const int raw = d.property("Brightness").toInt();
    const int pct = qRound(raw * 100.0 / m_blMax);
    if (pct != m_brightness) {
        m_brightness = pct;
        emit brightnessChanged();
    }
}

void SystemControl::setBrightness(int percent)
{
    percent = qBound(1, percent, 100);
    m_brightness = percent;
    emit brightnessChanged();
    if (m_blPath.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.ScreenBrightness", m_blPath, "org.kde.ScreenBrightness.Display", "SetBrightness");
    // flags = 1: do not show the KDE brightness OSD, our slider is the indicator.
    msg << int(qRound(percent / 100.0 * m_blMax)) << uint(1);
    QDBusConnection::sessionBus().asyncCall(msg);
}

// ---------- volume ----------
void SystemControl::readVolume()
{
    run("wpctl", {"get-volume", "@DEFAULT_AUDIO_SINK@"}, [this](const QString &out) {
        // "Volume: 0.45" or "Volume: 0.45 [MUTED]"
        static const QRegularExpression re("Volume:\\s*([0-9.]+)");
        const auto m = re.match(out);
        if (!m.hasMatch())
            return;
        m_volume = qRound(m.captured(1).toDouble() * 100);
        m_muted = out.contains("MUTED");
        emit volumeChanged();
    });
}

void SystemControl::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    emit volumeChanged();
    m_pendingVolume = m_volume;
    m_volumeDebounce.start();
}

void SystemControl::toggleMute()
{
    m_muted = !m_muted;
    emit volumeChanged();
    run("wpctl", {"set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
}

// ---------- battery / clock ----------
void SystemControl::readBattery()
{
    const int cap = readFile("/sys/class/power_supply/BAT0/capacity").toInt();
    const QString st = readFile("/sys/class/power_supply/BAT0/status");
    const bool chg = st == "Charging" || st == "Full" || readFile("/sys/class/power_supply/AC0/online") == "1";
    if (cap != m_battery || chg != m_charging) {
        m_battery = cap;
        m_charging = chg;
        emit batteryChanged();
    }
}

void SystemControl::tickClock()
{
    const QString c = QLocale::system().toString(QTime::currentTime(), QLocale::ShortFormat);
    if (c != m_clock) {
        m_clock = c;
        emit clockChanged();
    }
}

// ---------- radios ----------
void SystemControl::readRadios()
{
    run("nmcli", {"radio", "wifi"}, [this](const QString &out) {
        const bool on = out.trimmed() == "enabled";
        if (on != m_wifi) {
            m_wifi = on;
            emit radiosChanged();
        }
    });
    run("bluetoothctl", {"show"}, [this](const QString &out) {
        const bool on = out.contains("Powered: yes");
        if (on != m_bluetooth) {
            m_bluetooth = on;
            emit radiosChanged();
        }
    });
}

void SystemControl::setWifi(bool on)
{
    m_wifi = on;
    emit radiosChanged();
    run("nmcli", {"radio", "wifi", on ? "on" : "off"}, [this](const QString &) { readRadios(); });
}

void SystemControl::setBluetooth(bool on)
{
    m_bluetooth = on;
    emit radiosChanged();
    // rfkill first: when the radio is soft-blocked, bluetoothctl cannot power it on.
    run("rfkill", {on ? "unblock" : "block", "bluetooth"}, [this, on](const QString &) {
        run("bluetoothctl", {"power", on ? "on" : "off"}, [this](const QString &) {
            QTimer::singleShot(800, this, &SystemControl::readRadios);
        });
    });
}

// ---------- refresh rate (KDE, internal panel) ----------
void SystemControl::readRefresh()
{
    run("kscreen-doctor", {"-j"}, [this](const QString &out) {
        const QJsonArray outputs = QJsonDocument::fromJson(out.toUtf8()).object().value("outputs").toArray();
        // display layout: which screens are on, and where
        QJsonObject edp, ext;
        for (const QJsonValue &ov : outputs) {
            const QJsonObject o = ov.toObject();
            if (o.value("name").toString().startsWith("eDP")) edp = o;
            else if (o.value("connected").toBool() && ext.isEmpty()) ext = o;
        }
        m_extName = ext.value("name").toString();
        QString mode = "handheld";
        if (!ext.isEmpty() && ext.value("enabled").toBool()) {
            if (!edp.value("enabled").toBool()) mode = "external";
            else if (edp.value("pos").toObject() == ext.value("pos").toObject()) mode = "duplicate";
            else mode = "extend";
        }
        // remember the user's own scales (Duplicate changes them)
        if (mode != "duplicate") {
            if (edp.value("enabled").toBool()) m_edpScale = edp.value("scale").toDouble(m_edpScale);
            if (ext.value("enabled").toBool()) m_extScale = ext.value("scale").toDouble(m_extScale);
        }
        m_displayMode = mode;
        emit displayChanged();
        for (const QJsonValue &ov : outputs) {
            const QJsonObject o = ov.toObject();
            if (!o.value("name").toString().startsWith("eDP"))
                continue;
            m_edpName = o.value("name").toString();
            const QString cur = o.value("currentModeId").toString();
            for (const QJsonValue &mv : o.value("modes").toArray()) {
                const QJsonObject m = mv.toObject();
                if (m.value("id").toString() == cur) {
                    m_refresh = qRound(m.value("refreshRate").toDouble());
                    emit refreshChanged();
                }
            }
        }
    });
}

void SystemControl::toggleRefreshRate()
{
    run("kscreen-doctor", {"-j"}, [this](const QString &out) {
        const QJsonArray outputs = QJsonDocument::fromJson(out.toUtf8()).object().value("outputs").toArray();
        for (const QJsonValue &ov : outputs) {
            const QJsonObject o = ov.toObject();
            if (!o.value("name").toString().startsWith("eDP"))
                continue;
            const QString cur = o.value("currentModeId").toString();
            const QJsonArray modes = o.value("modes").toArray();
            QJsonObject curMode;
            for (const QJsonValue &mv : modes)
                if (mv.toObject().value("id").toString() == cur)
                    curMode = mv.toObject();
            const double target = curMode.value("refreshRate").toDouble() > 90 ? 60 : 120;
            const QJsonObject size = curMode.value("size").toObject();
            QString best;
            double bestDiff = 1e9;
            for (const QJsonValue &mv : modes) {
                const QJsonObject m = mv.toObject();
                if (m.value("size").toObject() != size)
                    continue;
                const double diff = qAbs(m.value("refreshRate").toDouble() - target);
                if (diff < bestDiff) {
                    bestDiff = diff;
                    best = m.value("id").toString();
                }
            }
            if (best.isEmpty() || best == cur)
                return;
            m_refresh = qRound(target);
            emit refreshChanged();
            run("kscreen-doctor", {QString("output.%1.mode.%2").arg(o.value("name").toString(), best)},
                [this](const QString &) { readRefresh(); });
            return;
        }
    });
}

// ---------- display mode (KDE) ----------
// handheld: Ally screen only · external: external screen only · duplicate: same picture on both
// (both at 0,0 with the same logical size) · extend: Ally screen primary, external to its right
void SystemControl::setDisplayMode(const QString &mode)
{
    run("kscreen-doctor", {"-j"}, [this, mode](const QString &out) {
        QJsonObject edp, ext;
        for (const QJsonValue &ov : QJsonDocument::fromJson(out.toUtf8()).object().value("outputs").toArray()) {
            const QJsonObject o = ov.toObject();
            if (o.value("name").toString().startsWith("eDP")) edp = o;
            else if (o.value("connected").toBool() && ext.isEmpty()) ext = o;
        }
        const QString e = edp.value("name").toString(), x = ext.value("name").toString();
        if (e.isEmpty() || (x.isEmpty() && mode != "handheld"))
            return; // no external screen: nothing to switch
        auto modeSize = [](const QJsonObject &o) {
            for (const QJsonValue &m : o.value("modes").toArray())
                if (m.toObject().value("id").toString() == o.value("currentModeId").toString())
                    return m.toObject().value("size").toObject();
            return o.value("size").toObject();
        };
        QStringList a;
        const QString sc = [](double v) { return QString::number(v, 'f', 2); }(m_edpScale);
        if (mode == "handheld") {
            a << "output." + e + ".enable" << "output." + e + ".priority.1";
            if (!x.isEmpty()) a << "output." + x + ".disable";
        } else if (mode == "external") {
            a << "output." + x + ".enable" << "output." + x + ".priority.1" << "output." + e + ".disable";
        } else if (mode == "duplicate") {
            // same logical size on both: the external scale follows the Ally screen
            const double logicalW = edp.value("size").toObject().value("width").toDouble(1920) / m_edpScale;
            const double extW = modeSize(ext).value("width").toDouble(1920);
            const double extScale = qBound(0.5, extW / logicalW, 3.0);
            a << "output." + e + ".enable" << "output." + x + ".enable"
              << "output." + e + ".scale." + sc << "output." + x + ".scale." + QString::number(extScale, 'f', 2)
              << "output." + e + ".position.0,0" << "output." + x + ".position.0,0"
              << "output." + e + ".priority.1";
        } else if (mode == "extend") {
            const int edpLogicalW = qRound(modeSize(edp).value("width").toDouble(1920) / m_edpScale);
            a << "output." + e + ".enable" << "output." + x + ".enable"
              << "output." + e + ".scale." + sc << "output." + x + ".scale." + QString::number(m_extScale, 'f', 2)
              << "output." + e + ".position.0,0" << "output." + x + ".position." + QString::number(edpLogicalW) + ",0"
              << "output." + e + ".priority.1";
        } else {
            return;
        }
        m_displayMode = mode;
        emit displayChanged();
        run("kscreen-doctor", a, [this](const QString &) { QTimer::singleShot(800, this, &SystemControl::readRefresh); });
    });
}

// ---------- sensors ----------
void SystemControl::readStats()
{
    // CPU load from /proc/stat
    const QStringList f = readFile("/proc/stat").section('\n', 0, 0).split(' ', Qt::SkipEmptyParts);
    if (f.size() > 5) {
        quint64 total = 0;
        for (int i = 1; i < f.size(); ++i)
            total += f[i].toULongLong();
        const quint64 idle = f[4].toULongLong() + f[5].toULongLong();
        if (m_lastTotal && total > m_lastTotal)
            m_cpuLoad = int(100 - (idle - m_lastIdle) * 100 / (total - m_lastTotal));
        m_lastTotal = total;
        m_lastIdle = idle;
    }
    if (!m_hwCpu.isEmpty())
        m_cpuTemp = readFile(m_hwCpu + "/temp1_input").toInt() / 1000;
    if (!m_hwGpu.isEmpty()) {
        m_gpuTemp = readFile(m_hwGpu + "/temp1_input").toInt() / 1000;
        QString pw = readFile(m_hwGpu + "/power1_average");
        if (pw.isEmpty())
            pw = readFile(m_hwGpu + "/power1_input");
        m_socWatts = pw.toDouble() / 1e6;
    }
    if (!m_gpuBusy.isEmpty())
        m_gpuLoad = readFile(m_gpuBusy).toInt();
    if (!m_hwFan.isEmpty())
        m_fanRpm = readFile(m_hwFan + "/fan1_input").toInt();
    m_batWatts = readFile("/sys/class/power_supply/BAT0/power_now").toDouble() / 1e6;

    const QStringList mem = readFile("/proc/meminfo").split('\n');
    qint64 totalKb = 0, availKb = 0;
    for (const QString &l : mem) {
        if (l.startsWith("MemTotal:"))
            totalKb = l.section(' ', 1, 1, QString::SectionSkipEmpty).toLongLong();
        else if (l.startsWith("MemAvailable:"))
            availKb = l.section(' ', 1, 1, QString::SectionSkipEmpty).toLongLong();
    }
    m_ramMb = int((totalKb - availKb) / 1024);
    emit statsChanged();
}

// ---------- actions ----------
void SystemControl::showDesktop()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.kglobalaccel", "/component/kwin", "org.kde.kglobalaccel.Component", "invokeShortcut");
    msg << QString("Show Desktop");
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SystemControl::openKeyboard()
{
    QDBusInterface vk("org.kde.KWin", "/VirtualKeyboard", "org.kde.kwin.VirtualKeyboard");
    if (vk.isValid() && vk.property("available").toBool()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.kde.KWin", "/VirtualKeyboard", "org.freedesktop.DBus.Properties", "Set");
        msg << QString("org.kde.kwin.VirtualKeyboard") << QString("active")
            << QVariant::fromValue(QDBusVariant(true));
        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }
    // Fallback: the Steam on-screen keyboard (works when Steam runs).
    QProcess::startDetached("xdg-open", {"steam://open/keyboard"});
}

void SystemControl::screenshot()
{
    // Wait for the panel to slide away first.
    QTimer::singleShot(450, this, [] {
        QProcess::startDetached("spectacle", {"--background", "--fullscreen", "--nonotify"});
    });
}

void SystemControl::power(const QString &what)
{
    if (what == "logout") {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.kde.Shutdown", "/Shutdown", "org.kde.Shutdown", "logout");
        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }
    if (what == "suspend" || what == "reboot" || what == "poweroff")
        QProcess::startDetached("systemctl", {what});
}
