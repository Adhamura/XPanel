// xpanel-daemon - the root half of XPanel.
//
// Owns the settings that need root on the ROG Ally / Ally X (TDP, fan curve,
// charge limit, lighting), keeps them in /var/lib/xpanel/state.json, applies them
// at start, and applies them again when the charger state changes or when
// something else (firmware, another tool) changes the TDP. The numbers that differ
// between the models (Operating Mode watts, power ranges) are in Model.h.
//
// Protocol: UNIX socket /run/xpanel/xpanel.sock, one JSON object per line.
// Access: root and members of the group "xpanel" (XPANEL_GROUP); checked per connection with
// SO_PEERCRED against the current group database, so a new member needs no new login.
// Others get {"error": "denied", "message": "..."} and are disconnected.
//   client -> {"get": true}
//   client -> {"set": {"opmode": "turbo"}}          (any of the keys below)
//   daemon -> {"state": {...}}                      (reply, and pushed to every
//                                                    client on each change)
//   client -> {"input": {"grab": true}}             (the pad drives XPanel, not the game;
//                                                    ends by itself when that client goes)
//   client -> {"input": {"calibrate": "gyro"}}      (~1 s at rest; result in "gyroCalibration",
//                                                    offset saved as "gyroBias" [x, y, z] rad/s)
//   daemon -> {"event": "special" | "nav" | "action", "name": "..."}   (input layer)
// Key "control": the resolved Control Mode settings from the app (not echoed in "state").
// Keys: opmode (silent|performance|turbo|manual), tdp / tdpSlow / tdpFast (W, manual;
//       tdpSlow / tdpFast 0 = derived from tdp), fan (auto|quiet|max),
//       charge (50..100, 100 = off), eco (bool), gpuClock (MHz, 0 = auto),
//       lighting (off|static|breathing|colorcycle|rainbow), lightColor ("#rrggbb"),
//       lightBrightness (0..3), lightSpeed (low|medium|high), bootSound, mcuPowersave (bool)
// Read-only state keys include: model (ally|allyx|unknown), modelName, tdpMin/tdpMax (SPL),
//       spptMin/spptMax, fpptMin/fpptMax (W), modeWatts {silent, performance, turbo, turboAc} (SPL W)

#include "Hardware.h"
#ifdef Q_OS_WIN
#include "InputLayerStub.h"
#else
#include "InputLayer.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <QtGlobal> // Q_OS_*
#ifndef Q_OS_WIN
#include <grp.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
// Windows: runs in the user's session (started by XPanel), named pipe \\.\pipe\xpanel.
static const QString kStateDir = qEnvironmentVariable("STATE_DIRECTORY", qEnvironmentVariable("LOCALAPPDATA") + "/XPanel/daemon");
static const QString kSocketPath = qEnvironmentVariable("XPANEL_SOCKET", "xpanel");
#else
static const QString kStateDir = qEnvironmentVariable("STATE_DIRECTORY", "/var/lib/xpanel");
static const QString kSocketPath = qEnvironmentVariable("XPANEL_SOCKET", "/run/xpanel/xpanel.sock");
#endif
static const QByteArray kGroup = qEnvironmentVariable("XPANEL_GROUP", "xpanel").toLocal8Bit();

static const QStringList kOpModes = {"silent", "performance", "turbo", "manual"};
static const QStringList kFans = {"auto", "quiet", "max"};
static const QStringList kLights = {"off", "static", "breathing", "colorcycle", "rainbow"};
static const QStringList kSpeeds = {"low", "medium", "high"};

class Daemon : public QObject {
public:
    Daemon()
    {
        load();
        applyAll();

#ifdef Q_OS_WIN
        // The pipe keeps Windows' default security: only this user (and admins) can write to it.
        if (!m_server.listen(kSocketPath))
            qFatal("xpanel-daemon: cannot listen on %s (already running?)", qPrintable(kSocketPath));
#else
        QDir().mkpath(QFileInfo(kSocketPath).absolutePath());
        QLocalServer::removeServer(kSocketPath);
        // Anyone may connect; onConnection() lets only root and the xpanel group in. A file
        // mode of 0660 would need a new login before a newly added member could connect.
        m_server.setSocketOptions(QLocalServer::WorldAccessOption);
        if (!m_server.listen(kSocketPath))
            qFatal("xpanel-daemon: cannot listen on %s", qPrintable(kSocketPath));
        ::chmod(kSocketPath.toLocal8Bit().constData(), 0666);
#endif
        connect(&m_server, &QLocalServer::newConnection, this, &Daemon::onConnection);

        // Our own input layer: the Ally pad -> virtual Xbox pad / keyboard / mouse.
        m_input.setControl(m_state["control"].toObject());
        m_input.setGyroBias(m_state["gyroBias"].toArray());
        connect(&m_input, &InputLayer::gyroBiasChanged, this, [this](const QJsonArray &b) {
            m_state["gyroBias"] = b;
            save();
            broadcast();
        });
        auto sendEvent = [this](const QString &kind, const QString &name) {
            const QByteArray msg = QJsonDocument(QJsonObject{{"event", kind}, {"name", name}}).toJson(QJsonDocument::Compact) + "\n";
            for (QLocalSocket *c : std::as_const(m_clients)) c->write(msg);
        };
        connect(&m_input, &InputLayer::special, this, [sendEvent](const QString &n) { sendEvent("special", n); });
        connect(&m_input, &InputLayer::nav, this, [sendEvent](const QString &n) { sendEvent("nav", n); });
        connect(&m_input, &InputLayer::action, this, [sendEvent](const QString &n) { sendEvent("action", n); });
        connect(&m_input, &InputLayer::statusChanged, this, &Daemon::broadcast);

        // Watchdog: charger plugged / unplugged, or the TDP changed under us.
        m_lastAc = Hardware::onAc();
        connect(&m_watch, &QTimer::timeout, this, &Daemon::watch);
        m_watch.start(3000);
        qInfo("xpanel-daemon: ready on %s", qPrintable(kSocketPath));
    }

private:
    // ---------- state ----------
    void load()
    {
        m_state = QJsonObject{
            {"opmode", "performance"}, {"tdp", 15}, {"fan", "auto"},
            {"charge", Hardware::readChargeLimit()}, {"lighting", "static"},
            {"tdpSlow", 0}, {"tdpFast", 0}, {"eco", false}, {"gpuClock", 0},
            {"lightColor", "#00c8ff"}, {"lightBrightness", 3}, {"lightSpeed", "medium"},
            {"bootSound", Hardware::readAttr("boot_sound") == 1},
            {"mcuPowersave", Hardware::readAttr("mcu_powersave") == 1},
        };
        QFile f(kStateDir + "/state.json");
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
            for (auto it = saved.begin(); it != saved.end(); ++it)
                if (valid(it.key(), it.value()))
                    m_state[it.key()] = it.value();
        }
    }

    void save()
    {
        QDir().mkpath(kStateDir);
        QSaveFile f(kStateDir + "/state.json");
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(m_state).toJson());
            f.commit();
        }
    }

    // Accepted watts for a manual limit: the firmware's range now, widened by the model's
    // ranges on both power sources, so a value set on the charger survives a restart on battery
    // (Hardware::applyTdp() clamps to what the firmware takes at the moment).
    static Model::Range tdpRange(const QString &key)
    {
        const QString k = key == "tdp" ? "tdp" : (key == "tdpSlow" ? "sppt" : "fppt");
        const QJsonObject l = Hardware::limits();
        const Model::Info &m = Hardware::model();
        auto pick = [&key](const Model::Ranges &r) { return key == "tdp" ? r.spl : (key == "tdpSlow" ? r.sppt : r.fppt); };
        const Model::Range ac = pick(m.ac), bat = pick(m.battery);
        return {qMin(l[k + "Min"].toInt(), qMin(ac.min, bat.min)), qMax(l[k + "Max"].toInt(), qMax(ac.max, bat.max))};
    }

    static bool valid(const QString &key, const QJsonValue &v)
    {
        if (key == "opmode") return kOpModes.contains(v.toString());
        if (key == "fan") return kFans.contains(v.toString());
        if (key == "lighting") return kLights.contains(v.toString());
        if (key == "tdp" || key == "tdpSlow" || key == "tdpFast") {
            if (!v.isDouble())
                return false;
            // tdpSlow / tdpFast: 0 = derived from tdp; below the minimum is raised when applied
            const int w = v.toInt();
            const Model::Range r = tdpRange(key);
            return w >= (key == "tdp" ? r.min : 0) && w <= r.max;
        }
        if (key == "gpuClock") return v.isDouble() && v.toInt() >= 0 && v.toInt() <= 4000;
        if (key == "lightBrightness") return v.isDouble() && v.toInt() >= 0 && v.toInt() <= 3;
        if (key == "lightSpeed") return kSpeeds.contains(v.toString());
        if (key == "lightColor") return QRegularExpression("^#[0-9a-fA-F]{6}$").match(v.toString()).hasMatch();
        if (key == "eco" || key == "bootSound" || key == "mcuPowersave") return v.isBool();
        if (key == "control") return v.isObject() && QJsonDocument(v.toObject()).toJson().size() < 65536;
        if (key == "gyroBias") { // [x, y, z] rad/s, from the gyro calibration
            const QJsonArray a = v.toArray();
            return v.isArray() && a.size() == 3
                   && std::all_of(a.begin(), a.end(), [](const QJsonValue &x) { return x.isDouble() && qAbs(x.toDouble()) < 0.5; });
        }
        if (key == "charge") return v.isDouble() && v.toInt() >= 50 && v.toInt() <= 100;
        return false;
    }

    // ---------- applying ----------
    // Watts for each mode, from the model table (daemon/Model.h). Turbo is higher on the charger.
    Hardware::Tdp wantedTdp() const
    {
        const QString mode = m_state["opmode"].toString();
        Hardware::Tdp t;
        if (mode == "silent" || mode == "performance" || mode == "turbo") {
            const Model::Watts w = Model::modeWatts(Hardware::model(), mode, Hardware::onAc());
            t = {w.spl, w.sppt, w.fppt,
                 mode == "silent" ? "low-power" : (mode == "performance" ? "balanced" : "performance")};
        } else {
            const int w = m_state["tdp"].toInt();
            const int slow = m_state["tdpSlow"].toInt(), fast = m_state["tdpFast"].toInt();
            t = {w, slow > 0 ? slow : w + 5, fast > 0 ? fast : w + 10,
                 w <= 13 ? "low-power" : (w <= 20 ? "balanced" : "performance")};
        }
        return t;
    }

    void applyLighting()
    {
        Hardware::applyLighting(m_state["lighting"].toString(), m_state["lightColor"].toString(),
                                m_state["lightBrightness"].toInt(), m_state["lightSpeed"].toString());
    }

    void applyTdp()
    {
        Hardware::applyTdp(wantedTdp());
        Hardware::applyCpu(m_state["eco"].toBool(), m_state["opmode"].toString());
        // A profile change can reset the fan curve: put ours back.
        if (m_state["fan"].toString() != "auto")
            Hardware::applyFan(m_state["fan"].toString());
    }

    void applyAll()
    {
        applyTdp();
        Hardware::applyFan(m_state["fan"].toString());
        Hardware::applyChargeLimit(m_state["charge"].toInt());
        applyLighting();
        Hardware::applyGpuClock(m_state["gpuClock"].toInt());
        Hardware::writeAttr("boot_sound", m_state["bootSound"].toBool() ? 1 : 0);
        Hardware::writeAttr("mcu_powersave", m_state["mcuPowersave"].toBool() ? 1 : 0);
    }

    void watch()
    {
        const bool ac = Hardware::onAc();
        const Hardware::Tdp want = wantedTdp();
        const Hardware::Tdp have = Hardware::readTdp();
        const bool drift = have.spl != want.spl || have.profile != want.profile;
        if (ac != m_lastAc || drift) {
            if (drift && ac == m_lastAc)
                qInfo("xpanel-daemon: TDP changed outside XPanel (%d W, %s), applying %d W again",
                      have.spl, qPrintable(have.profile), want.spl);
            m_lastAc = ac;
            applyTdp();
            broadcast();
        }
    }

    // ---------- clients ----------
    QJsonObject fullState() const
    {
        QJsonObject s = m_state;
        s.remove("control"); // large, and the app already has it
        const QJsonObject in = m_input.status();
        for (auto it = in.begin(); it != in.end(); ++it)
            s[it.key()] = it.value();
        const Hardware::Tdp t = Hardware::readTdp();
        s["ac"] = Hardware::onAc();
        s["spl"] = t.spl;
        s["sppt"] = t.sppt;
        s["fppt"] = t.fppt;
        s["profile"] = t.profile;
        const Model::Info &m = Hardware::model();
        s["modeWatts"] = QJsonObject{{"silent", m.silent.spl}, {"performance", m.performance.spl},
                                     {"turbo", m.turboBattery.spl}, {"turboAc", m.turboAc.spl}};
        for (const QJsonObject &extra : {Hardware::limits(), Hardware::gpuRange(), m_info})
            for (auto it = extra.begin(); it != extra.end(); ++it)
                s[it.key()] = it.value();
        return s;
    }

    static QByteArray line(const QJsonObject &state)
    {
        return QJsonDocument(QJsonObject{{"state", state}}).toJson(QJsonDocument::Compact) + "\n";
    }

    void broadcast()
    {
        const QByteArray msg = line(fullState());
        for (QLocalSocket *c : std::as_const(m_clients))
            c->write(msg);
    }

#ifndef Q_OS_WIN
    // Who may change the hardware: root, or a member of kGroup (looked up now, not at login).
    static bool authorized(uid_t uid, QString *why)
    {
        if (uid == 0)
            return true;
        const group *gr = getgrnam(kGroup.constData());
        const passwd *pw = getpwuid(uid);
        if (!gr || !pw) {
            *why = !gr ? QString("the group \"%1\" does not exist; create it: sudo groupadd --system %1").arg(QString(kGroup))
                       : QString("unknown user id %1").arg(uid);
            return false;
        }
        const gid_t want = gr->gr_gid;
        int n = 64;
        QList<gid_t> groups(n);
        if (getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &n) < 0) {
            groups.resize(n);
            getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &n);
        }
        for (int i = 0; i < n && i < groups.size(); ++i)
            if (groups[i] == want)
                return true;
        *why = QString("user %1 is not in the group \"%2\"; run: sudo usermod -aG %2 %1")
                   .arg(QString::fromLocal8Bit(pw->pw_name), QString(kGroup));
        return false;
    }

#endif

    void onConnection()
    {
        while (QLocalSocket *c = m_server.nextPendingConnection()) {
#ifndef Q_OS_WIN
            ucred cred{};
            cred.uid = uid_t(-1); // stays "nobody" in the log if the lookup fails
            socklen_t len = sizeof(cred);
            QString why = "cannot read the peer credentials";
            if (getsockopt(int(c->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0
                || !authorized(cred.uid, &why)) {
                if (!m_deniedLogged.contains(cred.uid)) { // once per user, the app retries every few seconds
                    m_deniedLogged.insert(cred.uid);
                    qWarning("xpanel-daemon: refused uid %u (pid %d): %s", cred.uid, cred.pid, qPrintable(why));
                }
                c->write(QJsonDocument(QJsonObject{{"error", "denied"}, {"message", why}}).toJson(QJsonDocument::Compact) + "\n");
                c->flush();
                connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
                c->disconnectFromServer();
                if (c->state() == QLocalSocket::UnconnectedState)
                    c->deleteLater();
                continue;
            }
#endif
            m_clients.append(c);
            connect(c, &QLocalSocket::readyRead, this, [this, c] { onRead(c); });
            connect(c, &QLocalSocket::disconnected, this, [this, c] {
                if (m_grabOwner == c) { m_grabOwner = nullptr; m_input.setGrab(false); }
                m_clients.removeAll(c);
                c->deleteLater();
            });
        }
    }

    void onRead(QLocalSocket *c)
    {
        while (c->canReadLine()) {
            const QByteArray raw = c->readLine().trimmed();
            if (raw.size() > (1 << 17)) // "control" alone may be up to 64 KB (see valid())
                continue;
            const QJsonObject req = QJsonDocument::fromJson(raw).object();
            if (req["input"].toObject()["calibrate"].toString() == "gyro") {
                m_input.calibrateGyro();
                continue;
            }
            if (req.contains("input")) {
                const bool g = req["input"].toObject()["grab"].toBool(false);
                m_grabOwner = g ? c : nullptr;
                m_input.setGrab(g);
                continue;
            }
            const QJsonObject set = req["set"].toObject();
            bool changed = false;
            QStringList apply;
            for (auto it = set.begin(); it != set.end(); ++it) {
                if (!valid(it.key(), it.value()) || m_state[it.key()] == it.value())
                    continue;
                m_state[it.key()] = it.value();
                apply << it.key();
                changed = true;
            }
            if (apply.contains("opmode") || apply.contains("tdp") || apply.contains("tdpSlow") ||
                apply.contains("tdpFast") || apply.contains("eco")) applyTdp();
            if (apply.contains("gpuClock")) Hardware::applyGpuClock(m_state["gpuClock"].toInt());
            if (apply.contains("bootSound")) Hardware::writeAttr("boot_sound", m_state["bootSound"].toBool() ? 1 : 0);
            if (apply.contains("mcuPowersave")) Hardware::writeAttr("mcu_powersave", m_state["mcuPowersave"].toBool() ? 1 : 0);
            if (apply.contains("fan")) Hardware::applyFan(m_state["fan"].toString());
            if (apply.contains("charge")) Hardware::applyChargeLimit(m_state["charge"].toInt());
            if (apply.contains("control")) m_input.setControl(m_state["control"].toObject());
            if (apply.contains("gyroBias")) m_input.setGyroBias(m_state["gyroBias"].toArray());
            for (const char *k : {"lighting", "lightColor", "lightBrightness", "lightSpeed"})
                if (apply.contains(k)) { applyLighting(); break; }
            if (changed) {
                save();
                broadcast();
            } else {
                c->write(line(fullState()));
            }
        }
    }

    QJsonObject m_state;
    const QJsonObject m_info = Hardware::deviceInfo(); // read once (serial needs root)
    QLocalServer m_server;
    QList<QLocalSocket *> m_clients;
#ifndef Q_OS_WIN
    QSet<uid_t> m_deniedLogged;
#endif
    InputLayer m_input;
    QLocalSocket *m_grabOwner = nullptr;
    QTimer m_watch;
    bool m_lastAc = false;
};

#ifndef Q_OS_WIN
static int g_sig[2];
#endif

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--version")) {
            printf("xpanel-daemon %s\n", XPANEL_VERSION);
            return 0;
        }
    QCoreApplication app(argc, argv);
#ifndef Q_OS_WIN
    if (geteuid() != 0)
        qWarning("xpanel-daemon: not running as root, hardware writes will fail");

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sig);
    QSocketNotifier sn(g_sig[1], QSocketNotifier::Read);
    QObject::connect(&sn, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    auto handler = [](int) { const char c = 1; (void)!::write(g_sig[0], &c, 1); };
    std::signal(SIGTERM, handler);
    std::signal(SIGINT, handler);
#endif

    Daemon d;
    const int rc = app.exec();
    QLocalServer::removeServer(kSocketPath);
    return rc;
}
