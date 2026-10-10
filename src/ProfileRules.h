#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

// Pure rules of the per-game profiles (no I/O, unit tested in tests/).
namespace ProfileRules {

// Steam starts every game through "reaper SteamLaunch AppId=<id> -- ...".
// cmdline: /proc/<pid>/cmdline (NUL-separated). Returns the app id, or "" for other processes.
QString steamAppIdFromCmdline(const QByteArray &cmdline);

// Game id from /proc/<pid>/environ (NUL-separated): SteamAppId / SteamGameId -> "steam:<id>",
// else XPANEL_GAME=<id> (set by XPanel's own launches). "" when none.
QString gameIdFromEnviron(const QByteArray &environ);

// Steam tools that are not games (Proton, Steam Linux Runtime, redistributables) by name.
bool isSteamToolName(const QString &name);
// App ids that are never a game: 0, the Steam client, Big Picture, runtimes, Proton.
bool isIgnoredSteamAppId(const QString &appid);

// What a "game.<id>" profile changes: keys opmode, tdp, tdpSlow, tdpFast, fan (daemon) and
// preset (Config "control.preset"). Empty values mean "keep".
QVariantMap profileSettings(const QVariantMap &profile);
// true when the profile changes anything
inline bool hasSettings(const QVariantMap &profile) { return !profileSettings(profile).isEmpty(); }

// JSON-equal (15 == 15.0)
bool sameValue(const QVariant &a, const QVariant &b);

// At game end: the snapshot value of every key the profile set, except keys whose current
// value is no longer what the profile applied (the user changed them while the game ran).
QVariantMap restoreChanges(const QVariantMap &snapshot, const QVariantMap &applied,
                           const QVariantMap &current);

} // namespace ProfileRules
