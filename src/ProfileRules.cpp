#include "ProfileRules.h"

#include <QJsonValue>
#include <QList>
#include <QRegularExpression>
#include <QSet>

namespace ProfileRules {

static QList<QByteArray> split0(const QByteArray &data)
{
    QList<QByteArray> out = data.split('\0');
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}

static bool allDigits(const QByteArray &s)
{
    if (s.isEmpty() || s.size() > 20)
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

QString steamAppIdFromCmdline(const QByteArray &cmdline)
{
    const QList<QByteArray> args = split0(cmdline);
    if (args.isEmpty())
        return {};
    // "reaper" (maybe a full path; an interpreter can come first), then the SteamLaunch
    // marker and AppId=<n>, all before the "--" that starts the game's own command line
    bool reaper = false, launch = false;
    QByteArray id;
    for (const QByteArray &a : args) {
        if (a == "--")
            break;
        if (a == "SteamLaunch")
            launch = reaper;
        else if (a.startsWith("AppId="))
            id = a.mid(6);
        else if (!launch && a.mid(a.lastIndexOf('/') + 1) == "reaper")
            reaper = true;
    }
    if (!launch || !allDigits(id))
        return {};
    return QString::fromLatin1(id);
}

QString gameIdFromEnviron(const QByteArray &environ)
{
    QByteArray appId, gameId, xpanel;
    for (const QByteArray &kv : split0(environ)) {
        if (kv.startsWith("XPANEL_GAME="))
            xpanel = kv.mid(12);
        else if (kv.startsWith("SteamAppId="))
            appId = kv.mid(11);
        else if (kv.startsWith("SteamGameId="))
            gameId = kv.mid(12);
    }
    // a Steam game started by something XPanel launched (e.g. a launcher) is still that game
    if (allDigits(appId) && appId.toULongLong() != 0)
        return "steam:" + QString::fromLatin1(appId);
    // non-Steam shortcuts have a 64-bit SteamGameId: not a Steam app
    if (allDigits(gameId) && gameId.toULongLong() != 0 && gameId.toULongLong() < (1ULL << 32))
        return "steam:" + QString::fromLatin1(gameId);
    if (xpanel.startsWith("app:") || xpanel.startsWith("steam:"))
        return QString::fromUtf8(xpanel);
    return {};
}

bool isSteamToolName(const QString &name)
{
    static const QRegularExpression toolRe("^(Proton|Steam Linux Runtime|Steamworks Common)");
    return toolRe.match(name).hasMatch();
}

bool isIgnoredSteamAppId(const QString &appid)
{
    static const QSet<QString> ignored = {
        "0", "7", "769",                                 // Steam client, Big Picture
        "228980",                                        // Steamworks Common Redistributables
        "1070560", "1391110", "1628350",                 // Steam Linux Runtime (scout, soldier, sniper)
        "858280", "930400", "961940", "996510", "1054830", "1113280", "1245040", "1420170",
        "1580130", "1887720", "2348590", "2805730", "1493710", "2180100", // Proton
        "1161040", "1826330",                            // BattlEye / EasyAntiCheat runtimes
    };
    return appid.isEmpty() || ignored.contains(appid);
}

QVariantMap profileSettings(const QVariantMap &profile)
{
    QVariantMap out;
    const QString opmode = profile.value("opmode").toString();
    if (!opmode.isEmpty()) {
        out["opmode"] = opmode;
        if (opmode == "manual") {
            if (profile.value("tdp").toInt() > 0)
                out["tdp"] = profile.value("tdp").toInt();
            for (const char *k : {"tdpSlow", "tdpFast"})
                if (profile.contains(k))
                    out[k] = profile.value(k).toInt();
        }
    }
    const QString fan = profile.value("fan").toString();
    if (!fan.isEmpty())
        out["fan"] = fan;
    const QString preset = profile.value("control").toString();
    if (!preset.isEmpty())
        out["preset"] = preset;
    return out;
}

bool sameValue(const QVariant &a, const QVariant &b)
{
    return QJsonValue::fromVariant(a) == QJsonValue::fromVariant(b);
}

QVariantMap restoreChanges(const QVariantMap &snapshot, const QVariantMap &applied,
                           const QVariantMap &current)
{
    QVariantMap out;
    for (auto it = applied.begin(); it != applied.end(); ++it) {
        const QString &k = it.key();
        if (!snapshot.contains(k))
            continue; // the value before the game is not known
        if (current.contains(k)) {
            if (!sameValue(current.value(k), it.value()))
                continue; // changed by the user while the game ran: keep it
            if (sameValue(current.value(k), snapshot.value(k)))
                continue; // nothing to do
        }
        out[k] = snapshot.value(k);
    }
    return out;
}

} // namespace ProfileRules
