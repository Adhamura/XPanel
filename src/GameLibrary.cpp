#include "GameLibrary.h"
#include "Config.h"
#include "ProfileRules.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

static QString readFile(const QString &fn)
{
    QFile f(fn);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

GameLibrary::GameLibrary(Config *config, QObject *parent) : QObject(parent), m_config(config)
{
    rescan();
}

QVariantList GameLibrary::scanSteam()
{
    QVariantList out;
    m_steamNames.clear();
    const QString steam = QDir::homePath() + "/.local/share/Steam";
    // every library folder: "path" lines in libraryfolders.vdf
    QStringList libs{steam};
    static const QRegularExpression pathRe("\"path\"\\s+\"([^\"]+)\"");
    auto it = pathRe.globalMatch(readFile(steam + "/steamapps/libraryfolders.vdf"));
    while (it.hasNext()) {
        const QString p = it.next().captured(1);
        if (!libs.contains(p)) libs << p;
    }
    static const QRegularExpression idRe("\"appid\"\\s+\"(\\d+)\"");
    static const QRegularExpression nameRe("\"name\"\\s+\"([^\"]+)\"");
    for (const QString &lib : libs) {
        const QDir dir(lib + "/steamapps");
        for (const QString &fn : dir.entryList({"appmanifest_*.acf"}, QDir::Files)) {
            const QString acf = readFile(dir.absoluteFilePath(fn));
            const QString id = idRe.match(acf).captured(1), name = nameRe.match(acf).captured(1);
            if (id.isEmpty() || name.isEmpty())
                continue;
            m_steamNames.insert(id, name);
            if (ProfileRules::isSteamToolName(name)) // tools that are not games
                continue;
            // cover art: new layout (librarycache/<id>/library_600x900.jpg) or old (<id>_library_600x900.jpg)
            QString cover;
            for (const QString &c : {steam + "/appcache/librarycache/" + id + "/library_600x900.jpg",
                                     steam + "/appcache/librarycache/" + id + "_library_600x900.jpg"})
                if (QFile::exists(c)) { cover = "file://" + c; break; }
            QString hero;
            for (const QString &c : {steam + "/appcache/librarycache/" + id + "/library_hero.jpg",
                                     steam + "/appcache/librarycache/" + id + "_library_hero.jpg"})
                if (QFile::exists(c)) { hero = "file://" + c; break; }
            out << QVariantMap{{"id", "steam:" + id}, {"title", name}, {"platform", "Steam"},
                               {"cover", cover}, {"hero", hero}, {"icon", ""}};
        }
    }
    return out;
}

QVariantMap GameLibrary::desktopEntry(const QString &path) const
{
    QVariantMap m;
    bool inMain = false;
    for (const QString &l : readFile(path).split('\n')) {
        if (l.startsWith('[')) { inMain = l.trimmed() == "[Desktop Entry]"; continue; }
        if (!inMain) continue;
        const QString k = l.section('=', 0, 0).trimmed(), v = l.section('=', 1).trimmed();
        if (k == "Name" || k == "Icon" || k == "Categories" || k == "NoDisplay" || k == "Hidden" || k == "Type")
            m[k] = v;
    }
    return m;
}

QVariantList GameLibrary::installedApps() const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = QDir(dir).relativeFilePath(path).replace('/', '-');
            if (seen.contains(id)) continue; // user files override system ones
            seen.insert(id);
            const QVariantMap e = desktopEntry(path);
            if (e["Type"] != "Application" || e["NoDisplay"] == "true" || e["Hidden"] == "true" || e["Name"].toString().isEmpty())
                continue;
            const QString icon = e["Icon"].toString();
            out << QVariantMap{{"desktopId", id}, {"title", e["Name"]}, {"icon", icon},
                               {"isGame", e["Categories"].toString().split(';').contains("Game")}};
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["title"].toString().compare(b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

void GameLibrary::rescan()
{
    QVariantList list = scanSteam();
    const QStringList added = m_config->get("library.added").toStringList();
    const QStringList hidden = m_config->get("library.hidden").toStringList();
    QSet<QString> steamNames;
    for (const QVariant &g : std::as_const(list))
        steamNames.insert(g.toMap()["title"].toString());
    for (const QVariant &a : installedApps()) {
        const QVariantMap app = a.toMap();
        const QString id = "app:" + app["desktopId"].toString();
        if (!app["isGame"].toBool() && !added.contains(id))
            continue;
        if (steamNames.contains(app["title"].toString()))
            continue; // the Steam shortcut of a game we already list
        if (app["desktopId"].toString().startsWith("steam") && !added.contains(id))
            continue; // Steam itself is a launcher, not a game
        list << QVariantMap{{"id", id}, {"title", app["title"]}, {"platform", "Desktop"},
                            {"cover", ""}, {"hero", ""}, {"icon", app["icon"]}};
    }
    QVariantList visible;
    for (const QVariant &g : std::as_const(list))
        if (!hidden.contains(g.toMap()["id"].toString()))
            visible << g;
    std::sort(visible.begin(), visible.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["title"].toString().compare(b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });
    m_games = visible;
    emit gamesChanged();
}

void GameLibrary::addApp(const QString &desktopId)
{
    const QString id = "app:" + desktopId;
    QStringList added = m_config->get("library.added").toStringList();
    QStringList hidden = m_config->get("library.hidden").toStringList();
    if (!added.contains(id)) added << id;
    hidden.removeAll(id);
    m_config->set("library.added", added);
    m_config->set("library.hidden", hidden);
    rescan();
}

void GameLibrary::removeGame(const QString &id)
{
    QStringList hidden = m_config->get("library.hidden").toStringList();
    QStringList added = m_config->get("library.added").toStringList();
    if (!hidden.contains(id)) hidden << id;
    added.removeAll(id);
    m_config->set("library.hidden", hidden);
    m_config->set("library.added", added);
    rescan();
}

void GameLibrary::launch(const QString &id)
{
    emit launched(id);
    if (id.startsWith("steam:"))
        QProcess::startDetached("steam", {"steam://rungameid/" + id.mid(6)});
    else if (id.startsWith("app:")) {
        // XPANEL_GAME marks the game's processes, so GameWatcher can tell when it ends
        QProcess p;
        p.setProgram("gtk-launch");
        p.setArguments({id.mid(4)});
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("XPANEL_GAME", id);
        p.setProcessEnvironment(env);
        p.startDetached();
    }
}

QString GameLibrary::titleOf(const QString &id) const
{
    if (id.startsWith("steam:") && m_steamNames.contains(id.mid(6)))
        return m_steamNames.value(id.mid(6));
    for (const QVariant &g : m_games)
        if (g.toMap().value("id").toString() == id)
            return g.toMap().value("title").toString();
    return {};
}
