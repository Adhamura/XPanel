// xpanel-daemon - the root half of XPanel.
//
// Owns the settings that need root on the ROG Ally / Ally X (TDP, fan curve,
// charge limit, lighting), keeps them in /var/lib/xpanel/state.json, applies them
// at start, and applies them again when the charger state changes or when
// something else (firmware, another tool) changes the TDP.
//
// Protocol: UNIX socket /run/xpanel/xpanel.sock, one JSON object per line.
//   client -> {"get": true}
//   client -> {"set": {"opmode": "turbo"}}          (any of the keys below)
//   daemon -> {"state": {...}}                      (reply, and pushed to every
//                                                    client on each change)
//   client -> {"input": {"grab": true}}             (the pad drives XPanel, not the game;
//                                                    ends by itself when that client goes)
//   daemon -> {"event": "special" | "nav" | "action", "name": "..."}   (input layer)
// Key "control": the resolved Control Mode settings from the app (not echoed in "state").
// Keys: opmode (silent|performance|turbo|manual), tdp / tdpSlow / tdpFast (W, manual;
//       tdpSlow / tdpFast 0 = derived from tdp), fan (auto|quiet|max),
//       charge (50..100, 100 = off), eco (bool), gpuClock (MHz, 0 = auto),
//       lighting (off|static|breathing|colorcycle|rainbow), lightColor ("#rrggbb"),
//       lightBrightness (0..3), lightSpeed (low|medium|high), bootSound, mcuPowersave (bool)

#include "Hardware.h"
#include "InputLayer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const QString kStateDir = qEnvironmentVariable("STATE_DIRECTORY", "/var/lib/xpanel");
static const QString kSocketPath = qEnvironmentVariable("XPANEL_SOCKET", "/run/xpanel/xpanel.sock");

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

        QDir().mkpath(QFileInfo(kSocketPath).absolutePath());
        QLocalServer::removeServer(kSocketPath);
        m_server.setSocketOptions(QLocalServer::WorldAccessOption);
        if (!m_server.listen(kSocketPath))
            qFatal("xpanel-daemon: cannot listen on %s", qPrintable(kSocketPath));
        ::chmod(kSocketPath.toLocal8Bit().constData(), 0666);
        connect(&m_server, &QLocalServer::newConnection, this, &Daemon::onConnection);

        // Our own input layer: the Ally pad -> virtual Xbox pad / keyboard / mouse.
        m_input.setControl(m_state["control"].toObject());
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

    static bool valid(const QString &key, const QJsonValue &v)
    {
        if (key == "opmode") return kOpModes.contains(v.toString());
        if (key == "fan") return kFans.contains(v.toString());
        if (key == "lighting") return kLights.contains(v.toString());
        if (key == "tdp") return v.isDouble() && v.toInt() >= 5 && v.toInt() <= 40;
        if (key == "tdpSlow" || key == "tdpFast") return v.isDouble() && v.toInt() >= 0 && v.toInt() <= 60;
        if (key == "gpuClock") return v.isDouble() && v.toInt() >= 0 && v.toInt() <= 4000;
        if (key == "lightBrightness") return v.isDouble() && v.toInt() >= 0 && v.toInt() <= 3;
        if (key == "lightSpeed") return kSpeeds.contains(v.toString());
        if (key == "lightColor") return QRegularExpression("^#[0-9a-fA-F]{6}$").match(v.toString()).hasMatch();
        if (key == "eco" || key == "bootSound" || key == "mcuPowersave") return v.isBool();
        if (key == "control") return v.isObject() && QJsonDocument(v.toObject()).toJson().size() < 65536;
        if (key == "charge") return v.isDouble() && v.toInt() >= 50 && v.toInt() <= 100;
        return false;
    }

    // ---------- applying ----------
    // Watts for each mode on the Ally X. Turbo is higher on the charger.
    Hardware::Tdp wantedTdp() const
    {
        const QString mode = m_state["opmode"].toString();
        const bool ac = Hardware::onAc();
        Hardware::Tdp t;
        if (mode == "silent") t = {13, 15, 20, "low-power"};
        else if (mode == "performance") t = {17, 22, 30, "balanced"};
        else if (mode == "turbo") t = ac ? Hardware::Tdp{30, 35, 43, "performance"}
                                         : Hardware::Tdp{25, 30, 35, "performance"};
        else {
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

    void onConnection()
    {
        while (QLocalSocket *c = m_server.nextPendingConnection()) {
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
            if (raw.size() > 4096)
                continue;
            const QJsonObject req = QJsonDocument::fromJson(raw).object();
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
    InputLayer m_input;
    QLocalSocket *m_grabOwner = nullptr;
    QTimer m_watch;
    bool m_lastAc = false;
};

static int g_sig[2];

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (geteuid() != 0)
        qWarning("xpanel-daemon: not running as root, hardware writes will fail");

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sig);
    QSocketNotifier sn(g_sig[1], QSocketNotifier::Read);
    QObject::connect(&sn, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    auto handler = [](int) { const char c = 1; (void)!::write(g_sig[0], &c, 1); };
    std::signal(SIGTERM, handler);
    std::signal(SIGINT, handler);

    Daemon d;
    const int rc = app.exec();
    QLocalServer::removeServer(kSocketPath);
    return rc;
}
