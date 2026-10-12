#include "SystemControl.h"
#include "Desktop.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QDBusArgument>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <memory>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <QPointer>
#include <QThreadPool>
#include <QDesktopServices>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QUrl>
#include <thread>
#include "BrightnessWin.h"
#include "SystemControlWin.h"
#endif

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

    // Displays in KDE's brightness service: the internal panel and (Plasma >= 6.2) DDC/CI monitors.
    // Re-enumerate when the service reports a change or the set of active screens changes.
    m_screenDebounce.setSingleShot(true);
    m_screenDebounce.setInterval(400);
    connect(&m_screenDebounce, &QTimer::timeout, this, &SystemControl::rescanDisplays);
    auto bus = QDBusConnection::sessionBus();
    const QString svc = "org.kde.ScreenBrightness";
    bus.connect(svc, "/org/kde/ScreenBrightness", svc, "DisplayAdded", this, SLOT(rescanDisplays()));
    bus.connect(svc, "/org/kde/ScreenBrightness", svc, "DisplayRemoved", this, SLOT(rescanDisplays()));
    bus.connect(svc, "/org/kde/ScreenBrightness", svc, "DisplaysChanged", this, SLOT(rescanDisplays()));
    bus.connect(svc, QString(), "org.kde.ScreenBrightness.Display", "BrightnessChanged", this,
                SLOT(onBrightnessSignal(QDBusMessage)));
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        for (QScreen *s : QGuiApplication::screens())
            watchScreen(s);
        connect(gui, &QGuiApplication::screenAdded, this, [this](QScreen *s) {
            watchScreen(s);
            m_screenDebounce.start();
        });
        connect(gui, &QGuiApplication::screenRemoved, this, [this] { m_screenDebounce.start(); });
    }
    // Without KDE: DDC/CI monitors through ddcutil (slow, so only at start and when screens change)
    m_ddcFlush.setSingleShot(true);
#ifdef Q_OS_WIN
    m_ddcFlush.setInterval(150);
#else
    m_ddcFlush.setInterval(250);
#endif
    connect(&m_ddcFlush, &QTimer::timeout, this, &SystemControl::flushDdc);
    enumerateDisplays();
    scanDdc();

    connect(&m_slow, &QTimer::timeout, this, [this] {
        readBattery();
        tickClock();
#ifdef Q_OS_WIN
        readBrightness(); // Windows' own slider can change the internal panel (cheap WMI read)
        QThreadPool::globalInstance()->start([] { BrightnessWin::reapplySoft(); }); // gamma ramps reset on sleep
#endif
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

#ifdef Q_OS_WIN
    // Volume: Core Audio callbacks (Windows slider, hardware keys, other apps, default device change)
    {
        QPointer<SystemControl> self(this);
        SysWin::audioInit([self](bool deviceChanged) {
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, deviceChanged] {
                if (!self)
                    return;
                if (deviceChanged)
                    SysWin::audioRebind();
                self->readVolume();
            }, Qt::QueuedConnection);
        });
    }
    // refresh rate / display mode follow screens being plugged in or switched
    connect(&m_screenDebounce, &QTimer::timeout, this, &SystemControl::readRefresh);
#endif

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
static const char *kBlService = "org.kde.ScreenBrightness";
static const char *kBlDisplayIface = "org.kde.ScreenBrightness.Display";

static bool isInternalScreenName(const QString &n)
{
    return n.startsWith("eDP", Qt::CaseInsensitive) || n.startsWith("LVDS", Qt::CaseInsensitive)
        || n.startsWith("DSI", Qt::CaseInsensitive);
}

void SystemControl::watchScreen(QScreen *screen)
{
    // a mode switch (extend <-> mirror) moves / resizes outputs without adding or removing one
    connect(screen, &QScreen::geometryChanged, &m_screenDebounce, qOverload<>(&QTimer::start));
}

void SystemControl::rescanDisplays()
{
#ifdef Q_OS_WIN
    winScan(true);
    return;
#endif
    enumerateDisplays();
    scanDdc();
}

void SystemControl::readBrightness()
{
#ifdef Q_OS_WIN
    winScan(false);
    return;
#endif
    enumerateDisplays();
}

#ifdef Q_OS_WIN
// Windows: displays come from BrightnessWin (WMI for the internal panel, DDC/CI for external
// monitors). The scan runs on a worker thread: DDC/CI reads take 50-500 ms per monitor.
// withDdc = false only re-reads the internal panel and keeps the known external values.
void SystemControl::winScan(bool withDdc)
{
    if (m_winScanning) {
        m_winRescan = true;
        m_winRescanDdc = m_winRescanDdc || withDdc;
        return;
    }
    m_winScanning = true;
    QPointer<SystemControl> self(this);
    QThreadPool::globalInstance()->start([self, withDdc] {
        const QVector<BrightnessWin::Monitor> res = BrightnessWin::enumerate(withDdc);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, res] {
            if (!self)
                return;
            self->winScanDone(res);
        }, Qt::QueuedConnection);
    });
}

void SystemControl::winScanDone(const QVector<BrightnessWin::Monitor> &res)
{
    m_winScanning = false;
    QVector<BrightnessWin::Monitor> merged;
    int ext = 0;
    for (BrightnessWin::Monitor m : res) {
        const BlDisplay *known = nullptr;
        for (const BlDisplay &b : m_all)
            if (b.id == m.id)
                known = &b;
        if (m.pct < 0) {             // external monitor not read this time
            if (!known)
                continue;
            m.pct = known->pct;
        } else if (known) {          // the value just written can still be on its way
            const auto it = m_lastSet.constFind(m.id);
            if (it != m_lastSet.constEnd() && it->isValid() && it->elapsed() < 1000)
                m.pct = known->pct;
        }
        if (!m.internal) {
            ++ext;
            if (m.label.isEmpty())
                m.label = tr("Monitor %1").arg(ext);
        }
        merged.push_back(m);
    }
    m_winMon = merged;
    enumerateDisplays();
    if (m_winRescan) {
        const bool d = m_winRescanDdc;
        m_winRescan = m_winRescanDdc = false;
        winScan(d);
    }
}
#endif

// Ask the service which displays it knows (all of them, active or not) and read their values.
void SystemControl::enumerateDisplays()
{
#ifdef Q_OS_WIN
    {
        std::vector<BlDisplay> all;
        for (const BrightnessWin::Monitor &m : std::as_const(m_winMon)) {
            BlDisplay b;
            b.id = m.id;
            b.path = m.id;
            b.gdi = m.gdi;
            b.label = m.label;
            b.internal = m.internal;
            b.soft = m.soft;
            b.max = 100;
            b.pct = m.pct;
            b.source = BlSource::Win;
            all.push_back(b);
        }
        m_all = all;
        updateActiveDisplays();
        return;
    }
#endif
    QStringList ids;
    {
        QDBusInterface root(kBlService, "/org/kde/ScreenBrightness", kBlService);
        if (root.isValid()) {
            const QVariant v = root.property("DisplaysDBusNames");
            if (v.canConvert<QDBusArgument>())
                ids = qdbus_cast<QStringList>(v.value<QDBusArgument>());
            else
                ids = v.toStringList();
        }
    }
    if (ids.isEmpty()) {
        // older service / no property: probe display0..7
        for (int i = 0; i < 8; ++i) {
            QDBusInterface d(kBlService, QString("/org/kde/ScreenBrightness/display%1").arg(i), kBlDisplayIface);
            if (d.isValid())
                ids << QString("display%1").arg(i);
            else if (i == 0)
                break;
        }
    }
    std::vector<BlDisplay> all;
    for (const QString &raw : std::as_const(ids)) {
        BlDisplay b;
        b.id = raw.section('/', -1);
        b.path = "/org/kde/ScreenBrightness/" + b.id;
        QDBusInterface d(kBlService, b.path, kBlDisplayIface);
        if (!d.isValid())
            continue;
        b.internal = d.property("IsInternal").toBool();
        b.label = d.property("Label").toString();
        b.max = qMax(1, d.property("MaxBrightness").toInt());
        b.pct = qRound(d.property("Brightness").toInt() * 100.0 / b.max);
        if (b.label.isEmpty())
            b.label = b.internal ? "Built-in" : b.id;
        all.push_back(b);
    }
    if (all.empty()) {
        // No KDE brightness service (GNOME, Game Mode, other desktops, older Plasma):
        // the internal panel through logind, external monitors through ddcutil.
        BlDisplay bl;
        if (readBacklight(bl))
            all.push_back(bl);
        all.insert(all.end(), m_ddc.begin(), m_ddc.end());
    }
    m_all = all;
    updateActiveDisplays();
}

// The internal panel's backlight in /sys/class/backlight (firmware > platform > raw, as the kernel
// documents). Written through logind, which lets the active session's user do it without root.
bool SystemControl::readBacklight(BlDisplay &out) const
{
    const QDir dir("/sys/class/backlight");
    QString best;
    int bestRank = 99;
    for (const QString &e : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString p = dir.absoluteFilePath(e);
        if (readFile(p + "/max_brightness").toInt() <= 0)
            continue;
        const QString type = readFile(p + "/type");
        const int rank = type == "firmware" ? 0 : type == "platform" ? 1 : 2;
        if (rank < bestRank) {
            bestRank = rank;
            best = e;
        }
    }
    if (best.isEmpty())
        return false;
    const QString p = dir.absoluteFilePath(best);
    out.id = "backlight";
    out.path = best;
    out.label = "Built-in";
    out.internal = true;
    out.source = BlSource::Backlight;
    out.max = qMax(1, readFile(p + "/max_brightness").toInt());
    // the value just written can still be on its way through logind: keep ours for a moment
    const auto it = m_lastSet.constFind(out.id);
    if (it != m_lastSet.constEnd() && it->isValid() && it->elapsed() < 1000)
        for (const BlDisplay &b : m_all)
            if (b.id == out.id) {
                out.pct = b.pct;
                return true;
            }
    out.pct = qBound(0, qRound(readFile(p + "/brightness").toInt() * 100.0 / out.max), 100);
    return true;
}

// External monitors with DDC/CI (VCP 0x10 = brightness), when ddcutil is installed. KDE's service
// already covers them on Plasma, and Game Mode shows one screen: only GNOME and other desktops.
void SystemControl::scanDdc()
{
#ifdef Q_OS_WIN
    winScan(true);
    return;
#endif
    const Desktop::Kind k = Desktop::current();
    if (k == Desktop::Kind::KDE || k == Desktop::Kind::Gamescope || m_ddcScanning
        || QStandardPaths::findExecutable("ddcutil").isEmpty())
        return;
    m_ddcScanning = true;
    run("ddcutil", {"detect", "--terse"}, [this](const QString &out) {
        // "Display 1 / I2C bus: /dev/i2c-4 / Monitor: DEL:DELL U2415:ABC123" ("Invalid display" skipped)
        struct Found { QString num, label; };
        std::vector<Found> found;
        QString num;
        for (const QString &raw : out.split('\n')) {
            const QString l = raw.trimmed();
            if (l.startsWith("Display ")) {
                num = l.section(' ', 1, 1);
            } else if (l.startsWith("Invalid display")) {
                num.clear();
            } else if (l.startsWith("Monitor:") && !num.isEmpty()) {
                const QString model = l.mid(8).trimmed().section(':', 1, 1).trimmed();
                found.push_back({num, model.isEmpty() ? tr("Monitor %1").arg(num) : model});
                num.clear();
            }
        }
        if (found.empty()) {
            m_ddcScanning = false;
            if (!m_ddc.empty()) {
                m_ddc.clear();
                enumerateDisplays();
            }
            return;
        }
        auto result = std::make_shared<std::vector<BlDisplay>>();
        auto left = std::make_shared<int>(int(found.size()));
        for (const Found &f : found) {
            run("ddcutil", {"--display", f.num, "--terse", "getvcp", "10"}, [this, f, result, left](const QString &v) {
                // "VCP 10 C 50 100"
                static const QRegularExpression re("VCP\\s+10\\s+C\\s+(\\d+)\\s+(\\d+)");
                const auto m = re.match(v);
                if (m.hasMatch()) {
                    BlDisplay d;
                    d.id = "ddc" + f.num;
                    d.path = f.num;
                    d.label = f.label;
                    d.source = BlSource::Ddc;
                    d.max = qMax(1, m.captured(2).toInt());
                    d.pct = qRound(m.captured(1).toInt() * 100.0 / d.max);
                    result->push_back(d);
                }
                if (--*left == 0) {
                    std::sort(result->begin(), result->end(),
                              [](const BlDisplay &a, const BlDisplay &b) { return a.id < b.id; });
                    m_ddc = *result;
                    m_ddcScanning = false;
                    enumerateDisplays();
                }
            });
        }
    });
}

// ddcutil takes ~0.1-0.5 s per write: send only the latest value, one write per monitor at a time.
void SystemControl::flushDdc()
{
#ifdef Q_OS_WIN
    for (auto it = m_ddcPending.begin(); it != m_ddcPending.end();) {
        const QString id = it.key();
        if (m_ddcBusy.value(id)) {
            ++it;
            continue;
        }
        const int pct = it.value();
        it = m_ddcPending.erase(it);
        BrightnessWin::Monitor mon;
        bool found = false;
        for (const BrightnessWin::Monitor &m : std::as_const(m_winMon))
            if (m.id == id) {
                mon = m;
                found = true;
            }
        if (!found)
            continue;
        m_ddcBusy[id] = true;
        QPointer<SystemControl> self(this);
        QThreadPool::globalInstance()->start([self, mon, pct, id] {
            BrightnessWin::setPercent(mon, pct);
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, id] {
                if (!self)
                    return;
                self->m_ddcBusy[id] = false;
                if (self->m_ddcPending.contains(id))
                    self->m_ddcFlush.start();
            }, Qt::QueuedConnection);
        });
    }
    return;
#endif
    for (auto it = m_ddcPending.begin(); it != m_ddcPending.end();) {
        const QString num = it.key();
        if (m_ddcBusy.value(num)) {
            ++it;
            continue;
        }
        const int value = it.value();
        it = m_ddcPending.erase(it);
        m_ddcBusy[num] = true;
        run("ddcutil", {"--display", num, "--noverify", "setvcp", "10", QString::number(value)},
            [this, num](const QString &) {
                m_ddcBusy[num] = false;
                if (m_ddcPending.contains(num))
                    m_ddcFlush.start();
            });
    }
}

void SystemControl::writeBrightness(const BlDisplay &d, int percent)
{
    const int raw = int(qRound(percent / 100.0 * d.max));
    switch (d.source) {
    case BlSource::Kde: {
        QDBusMessage msg = QDBusMessage::createMethodCall(kBlService, d.path, kBlDisplayIface, "SetBrightness");
        // flags = 1: do not show the KDE brightness OSD, our slider is the indicator.
        msg << raw << uint(1);
        QDBusConnection::sessionBus().asyncCall(msg);
        break;
    }
    case BlSource::Backlight: {
        // logind: the caller's session, or the user's graphical session for a systemd user service
        QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
                                                          "org.freedesktop.login1.Session", "SetBrightness");
        msg << QString("backlight") << d.path << uint(qMax(1, raw));
        QDBusConnection::systemBus().asyncCall(msg);
        break;
    }
    case BlSource::Win:
        m_ddcPending[d.id] = percent; // written on a worker thread, latest value only (flushDdc)
        if (!m_ddcFlush.isActive())
            m_ddcFlush.start();
        break;
    case BlSource::Ddc:
        m_ddcPending[d.path] = raw;
        if (!m_ddcFlush.isActive())
            m_ddcFlush.start();
        break;
    }
}

// Keep only the displays that are switched on (a disabled internal panel can still be listed),
// internal first, and merge them into one bar when the screens are mirrored.
void SystemControl::updateActiveDisplays()
{
    std::vector<BlDisplay> act;
    bool mirror = false;
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (!screens.isEmpty()) {
#ifdef Q_OS_WIN
        // Only displays on an active Windows source (QScreen::name() is the GDI name, e.g. DISPLAY1).
        // Duplicate mode: several entries share one source, they merge into one bar.
        QStringList names;
        for (QScreen *s : screens)
            names << s->name();
        for (const BlDisplay &b : m_all)
            if (b.gdi.isEmpty() || names.contains(b.gdi, Qt::CaseInsensitive))
                act.push_back(b);
        if (act.size() >= 2) {
            mirror = true;
            for (const BlDisplay &b : act)
                if (b.gdi.compare(act.front().gdi, Qt::CaseInsensitive) != 0)
                    mirror = false;
        }
#else
        int nInt = 0, nExt = 0;
        QStringList extModels;
        for (QScreen *s : screens) {
            if (isInternalScreenName(s->name())) {
                ++nInt;
            } else {
                ++nExt;
                extModels << s->model();
            }
        }
        std::vector<BlDisplay> ext;
        for (const BlDisplay &b : m_all) {
            if (b.internal) {
                if (nInt > 0)
                    act.push_back(b);
            } else {
                ext.push_back(b);
            }
        }
        // More external entries than external screens (stale DDC entry): prefer the ones whose
        // label matches a screen's model name, otherwise keep the first ones.
        if (int(ext.size()) > nExt) {
            std::vector<BlDisplay> match;
            for (const BlDisplay &b : ext)
                for (const QString &m : std::as_const(extModels))
                    if (!m.isEmpty() && b.label.contains(m, Qt::CaseInsensitive)) {
                        match.push_back(b);
                        break;
                    }
            ext = int(match.size()) == nExt ? match : std::vector<BlDisplay>(ext.begin(), ext.begin() + nExt);
        }
        act.insert(act.end(), ext.begin(), ext.end());
#endif

        // Mirror: two or more active screens with identical geometry
        if (screens.size() >= 2 && !mirror) {
            mirror = true;
            for (QScreen *s : screens)
                if (s->geometry().isEmpty() || s->geometry() != screens.first()->geometry())
                    mirror = false;
        }
    }
    if (act.empty())            // nothing matched (unusual screen names, no screens): trust the service
        act = m_all;
    if (act.size() < 2)
        mirror = false;
    std::stable_sort(act.begin(), act.end(),
                     [](const BlDisplay &a, const BlDisplay &b) { return a.internal && !b.internal; });

    QVariantList var;
    if (mirror) {
        var << QVariantMap{{"id", "mirror"}, {"label", tr("Both screens")},
                           {"internal", act.front().internal}, {"brightness", act.front().pct},
                           {"soft", act.front().soft}};
    } else {
        for (const BlDisplay &b : act)
            var << QVariantMap{{"id", b.id}, {"label", b.label}, {"internal", b.internal}, {"brightness", b.pct},
                         {"soft", b.soft}};
    }
    const bool availBefore = !m_active.empty();
    const int primaryBefore = m_brightness;
    m_active = act;
    m_mirror = mirror;
    if (!act.empty())
        m_brightness = act.front().pct;
    if (var != m_displaysVar) {
        m_displaysVar = var;
        emit displaysChanged();
    }
    if (availBefore != !m_active.empty() || primaryBefore != m_brightness)
        emit brightnessChanged();
}

void SystemControl::onBrightnessSignal(const QDBusMessage &msg)
{
    // BrightnessChanged(int brightness, QString sourceClientName) from /org/kde/ScreenBrightness/displayN
    const QString id = msg.path().section('/', -1);
    if (msg.arguments().isEmpty())
        return;
    const auto it = m_lastSet.constFind(id);
    if (it != m_lastSet.constEnd() && it->isValid() && it->elapsed() < 500)
        return;
    bool changed = false;
    for (BlDisplay &b : m_all)
        if (b.id == id) {
            const int pct = qRound(msg.arguments().first().toInt() * 100.0 / b.max);
            if (pct != b.pct) {
                b.pct = pct;
                changed = true;
            }
        }
    if (changed)
        updateActiveDisplays();
}

void SystemControl::setDisplayBrightness(const QString &id, int percent)
{
    percent = qBound(1, percent, 100);
    for (BlDisplay &a : m_active) {
        if (id != "mirror" && a.id != id)
            continue;
        a.pct = percent;
        for (BlDisplay &b : m_all)
            if (b.id == a.id)
                b.pct = percent;
        m_lastSet[a.id].start();
        writeBrightness(a, percent);
    }
    updateActiveDisplays();
}

// Primary target: the internal panel if it is on, otherwise the first external monitor
// (a mirrored pair is driven together).
void SystemControl::setBrightness(int percent)
{
    percent = qBound(1, percent, 100);
    if (m_active.empty()) {
        m_brightness = percent;
        emit brightnessChanged();
        return;
    }
    setDisplayBrightness(m_mirror ? QString("mirror") : m_active.front().id, percent);
}

// ---------- volume ----------
void SystemControl::readVolume()
{
#ifdef Q_OS_WIN
    const SysWin::Volume v = SysWin::audioGet();
    if (v.ok && (v.percent != m_volume || v.muted != m_muted)) {
        m_volume = v.percent;
        m_muted = v.muted;
        emit volumeChanged();
    }
    return;
#endif
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
#ifdef Q_OS_WIN
    SysWin::audioSetVolume(m_volume);
    return;
#endif
    m_pendingVolume = m_volume;
    m_volumeDebounce.start();
}

void SystemControl::toggleMute()
{
    m_muted = !m_muted;
    emit volumeChanged();
#ifdef Q_OS_WIN
    SysWin::audioSetMute(m_muted);
    return;
#endif
    run("wpctl", {"set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
}

// ---------- battery / clock ----------
void SystemControl::readBattery()
{
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps) || (sps.BatteryFlag & 128) /* no battery */ || sps.BatteryLifePercent > 100)
        return;
    const int wcap = sps.BatteryLifePercent;
    const bool wchg = (sps.BatteryFlag & 8) || sps.ACLineStatus == 1; // charging or on AC
    if (wcap != m_battery || wchg != m_charging) {
        m_battery = wcap;
        m_charging = wchg;
        emit batteryChanged();
    }
    return;
#else
    const int cap = readFile("/sys/class/power_supply/BAT0/capacity").toInt();
    const QString st = readFile("/sys/class/power_supply/BAT0/status");
    // on the charger: any mains supply (the Ally's is not always called AC0), as the daemon checks it
    bool ac = false;
    const QDir ps("/sys/class/power_supply");
    for (const QString &e : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (readFile(ps.absoluteFilePath(e) + "/type") == "Mains" && readFile(ps.absoluteFilePath(e) + "/online") == "1") {
            ac = true;
            break;
        }
    const bool chg = st == "Charging" || st == "Full" || ac;
    if (cap != m_battery || chg != m_charging) {
        m_battery = cap;
        m_charging = chg;
        emit batteryChanged();
    }
#endif
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
#ifdef Q_OS_WIN
    // WinRT calls block: worker thread, then back to the GUI thread
    QPointer<SystemControl> self(this);
    std::thread([self] {
        const SysWin::Radios r = SysWin::radiosRead();
        if (!self || !r.ok)
            return;
        QMetaObject::invokeMethod(self.data(), [self, r] {
            if (!self)
                return;
            if (r.wifi != self->m_wifi || r.bluetooth != self->m_bluetooth) {
                self->m_wifi = r.wifi;
                self->m_bluetooth = r.bluetooth;
                emit self->radiosChanged();
            }
        }, Qt::QueuedConnection);
    }).detach();
    return;
#endif
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
#ifdef Q_OS_WIN
    winSetRadio(true, on);
    return;
#endif
    run("nmcli", {"radio", "wifi", on ? "on" : "off"}, [this](const QString &) { readRadios(); });
}

void SystemControl::setBluetooth(bool on)
{
    m_bluetooth = on;
    emit radiosChanged();
#ifdef Q_OS_WIN
    winSetRadio(false, on);
    return;
#endif
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
    if (!Desktop::caps().refreshRate && !Desktop::caps().displayMode)
        return; // kscreen-doctor is KDE's
#ifdef Q_OS_WIN
    {
        const SysWin::DisplayState d = SysWin::displayRead();
        if (!d.ok)
            return;
        m_extName = d.hasExternal ? QString("external") : QString();
        m_displayMode = d.mode;
        emit displayChanged();
        if (d.refresh > 0 && d.refresh != m_refresh) {
            m_refresh = d.refresh;
            emit refreshChanged();
        }
        return;
    }
#endif
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
    if (!Desktop::caps().refreshRate)
        return;
#ifdef Q_OS_WIN
    {
        // 60 <-> 120 Hz on the built-in panel, at its current resolution
        QPointer<SystemControl> self(this);
        std::thread([self] {
            const SysWin::DisplayState d = SysWin::displayRead();
            int target = 0;
            if (d.ok && d.rates.size() >= 2) {
                const int want = d.refresh > 90 ? 60 : 120;
                int bestDiff = 1 << 30;
                for (int r : d.rates)
                    if (qAbs(r - want) < bestDiff) {
                        bestDiff = qAbs(r - want);
                        target = r;
                    }
                if (target == d.refresh)
                    target = 0;
            }
            if (target > 0 && self) {
                QMetaObject::invokeMethod(self.data(), [self, target] {
                    if (self) {
                        self->m_refresh = target;
                        emit self->refreshChanged();
                    }
                }, Qt::QueuedConnection);
                SysWin::displaySetRefresh(target);
            }
            if (self)
                QMetaObject::invokeMethod(self.data(), [self] { if (self) self->readRefresh(); }, Qt::QueuedConnection);
        }).detach();
        return;
    }
#endif
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
    if (!Desktop::caps().displayMode)
        return;
#ifdef Q_OS_WIN
    {
        if (mode != "handheld" && !hasExternal())
            return; // no external screen: nothing to switch
        m_displayMode = mode;
        emit displayChanged();
        QPointer<SystemControl> self(this);
        std::thread([self, mode] {
            SysWin::displaySetMode(mode);
            if (self)
                QMetaObject::invokeMethod(self.data(), [self] {
                    if (self)
                        QTimer::singleShot(800, self.data(), &SystemControl::readRefresh);
                }, Qt::QueuedConnection);
        }).detach();
        return;
    }
#endif
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
#ifdef Q_OS_WIN
    {
        const SysWin::Stats w = SysWin::statsRead();
        if (w.cpuLoad >= 0)
            m_cpuLoad = w.cpuLoad;
        if (w.gpuLoad >= 0)
            m_gpuLoad = w.gpuLoad;
        m_cpuTemp = w.cpuTemp;
        m_gpuTemp = w.gpuTemp;
        m_socWatts = w.socWatts;
        m_fanRpm = w.fanRpm;
        m_batWatts = w.batteryWatts;
        m_ramMb = w.ramUsedMb;
        emit statsChanged();
        return;
    }
#endif
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
#ifdef Q_OS_WIN
    SysWin::showDesktop();
    return;
#endif
    if (!Desktop::caps().showDesktop)
        return; // only KWin has a "Show Desktop" to call
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.kglobalaccel", "/component/kwin", "org.kde.kglobalaccel.Component", "invokeShortcut");
    msg << QString("Show Desktop");
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SystemControl::openKeyboard()
{
#ifdef Q_OS_WIN
    std::thread([] { SysWin::openKeyboard(); }).detach();
    return;
#endif
    QDBusInterface vk("org.kde.KWin", "/VirtualKeyboard", "org.kde.kwin.VirtualKeyboard");
    if (vk.isValid() && vk.property("available").toBool()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.kde.KWin", "/VirtualKeyboard", "org.freedesktop.DBus.Properties", "Set");
        msg << QString("org.kde.kwin.VirtualKeyboard") << QString("active")
            << QVariant::fromValue(QDBusVariant(true));
        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }
    if (Desktop::current() == Desktop::Kind::GNOME && !QStandardPaths::findExecutable("gsettings").isEmpty()) {
        // GNOME's keyboard opens by itself on text fields once enabled: switch it on / off.
        const QString schema = "org.gnome.desktop.a11y.applications", key = "screen-keyboard-enabled";
        run("gsettings", {"get", schema, key}, [this, schema, key](const QString &out) {
            run("gsettings", {"set", schema, key, out.trimmed() == "true" ? "false" : "true"});
        });
        return;
    }
    // Fallback (Game Mode, other desktops): the Steam on-screen keyboard (works when Steam runs).
    QProcess::startDetached("xdg-open", {"steam://open/keyboard"});
}

void SystemControl::screenshot()
{
#ifdef Q_OS_WIN
    // Wait for the panel to slide away, then save every screen as one PNG in Pictures/Screenshots.
    QTimer::singleShot(450, this, [] {
        const QList<QScreen *> screens = QGuiApplication::screens();
        if (screens.isEmpty())
            return;
        QRect vg;
        qreal scale = 1.0;
        for (QScreen *s : screens) {
            vg = vg.united(s->geometry());
            scale = qMax(scale, s->devicePixelRatio());
        }
        QImage img(QSize(qRound(vg.width() * scale), qRound(vg.height() * scale)), QImage::Format_RGB32);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            for (QScreen *s : screens) {
                const QPixmap px = s->grabWindow(0);
                const QRect g = s->geometry();
                p.drawPixmap(QRect(qRound((g.x() - vg.x()) * scale), qRound((g.y() - vg.y()) * scale),
                                   qRound(g.width() * scale), qRound(g.height() * scale)), px);
            }
        }
        QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        dir += "/Screenshots";
        QDir().mkpath(dir);
        const QString file = dir + "/XPanel_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".png";
        if (!img.save(file, "PNG"))
            qWarning("XPanel: cannot save screenshot %s", qPrintable(file));
    });
    return;
#endif
    // Wait for the panel to slide away first.
    const QString tool = Desktop::caps().screenshotTool;
    if (tool.isEmpty())
        return;
    QTimer::singleShot(450, this, [tool] {
        if (tool == "spectacle")
            QProcess::startDetached("spectacle", {"--background", "--fullscreen", "--nonotify"});
        else
            QProcess::startDetached(tool, {}); // gnome-screenshot, grim: the whole screen into Pictures
    });
}

void SystemControl::power(const QString &what)
{
#ifdef Q_OS_WIN
    if (what == "suspend" || what == "reboot" || what == "poweroff" || what == "logout")
        std::thread([what] { SysWin::power(what); }).detach(); // sleep blocks until resume
    return;
#endif
    if (what == "logout") {
        if (!Desktop::caps().logout)
            return;
        switch (Desktop::current()) {
        case Desktop::Kind::KDE:
            QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
                "org.kde.Shutdown", "/Shutdown", "org.kde.Shutdown", "logout"));
            break;
        case Desktop::Kind::GNOME: {
            QDBusMessage msg = QDBusMessage::createMethodCall(
                "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", "Logout");
            msg << uint(0); // 0: normal, with GNOME's confirmation dialog
            QDBusConnection::sessionBus().asyncCall(msg);
            break;
        }
        default:
            QProcess::startDetached("loginctl", {"terminate-session", qEnvironmentVariable("XDG_SESSION_ID")});
            break;
        }
        return;
    }
    if (what == "suspend" || what == "reboot" || what == "poweroff")
        QProcess::startDetached("systemctl", {what});
}

#ifdef Q_OS_WIN
// Wi-Fi / Bluetooth switch through Windows.Devices.Radios. When Windows refuses (no access),
// the matching Settings page opens instead.
void SystemControl::winSetRadio(bool wifi, bool on)
{
    QPointer<SystemControl> self(this);
    std::thread([self, wifi, on] {
        const bool ok = SysWin::radioSet(wifi, on);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, wifi, ok] {
            if (!self)
                return;
            if (!ok)
                QDesktopServices::openUrl(QUrl(wifi ? "ms-settings:network-wifi" : "ms-settings:bluetooth"));
            QTimer::singleShot(500, self.data(), &SystemControl::readRadios);
        }, Qt::QueuedConnection);
    }).detach();
}
#endif
