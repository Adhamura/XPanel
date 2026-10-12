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
#include <QCryptographicHash>
#include <QImage>
#include <QDesktopServices>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

static QString readFile(const QString &fn)
{
    QFile f(fn);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QHash<QString, QString> GameLibrary::s_exeIds;

// Steam install folder ("" when Steam is not installed).
static QString steamRoot()
{
#ifdef Q_OS_WIN
    QString p = QSettings("HKEY_CURRENT_USER/Software/Valve/Steam", QSettings::NativeFormat).value("SteamPath").toString();
    if (p.isEmpty() || !QDir(p).exists())
        p = QSettings("HKEY_LOCAL_MACHINE/SOFTWARE/WOW6432Node/Valve/Steam", QSettings::NativeFormat).value("InstallPath").toString();
    if (p.isEmpty() || !QDir(p).exists())
        p = "C:/Program Files (x86)/Steam";
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
#else
    // native, the ~/.steam/steam link of Debian / Ubuntu packages, Flatpak
    const QString home = QDir::homePath();
    for (const QString &p : {home + "/.local/share/Steam", home + "/.steam/steam",
                             home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"})
        if (QFile::exists(p + "/steamapps"))
            return QFileInfo(p).canonicalFilePath();
    return home + "/.local/share/Steam";
#endif
}

// first existing file of a Steam librarycache entry: flat (old), per-app folder, or hashed sub folder
static QString cacheArt(const QString &steam, const QString &id, const QString &file)
{
    const QString base = steam + "/appcache/librarycache/";
    for (const QString &c : {base + id + "/" + file, base + id + "_" + file})
        if (QFile::exists(c))
            return QUrl::fromLocalFile(c).toString();
    const QDir d(base + id);
    for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString c = d.absoluteFilePath(sub + "/" + file);
        if (QFile::exists(c))
            return QUrl::fromLocalFile(c).toString();
    }
    return {};
}

GameLibrary::GameLibrary(Config *config, QObject *parent) : QObject(parent), m_config(config)
{
    rescan();
}

QVariantList GameLibrary::scanSteam()
{
    QVariantList out;
    m_steamNames.clear();
    const QString steam = steamRoot();
    // every library folder: "path" lines in libraryfolders.vdf
    QStringList libs{steam};
    static const QRegularExpression pathRe("\"path\"\\s+\"([^\"]+)\"");
    auto it = pathRe.globalMatch(readFile(steam + "/steamapps/libraryfolders.vdf"));
    while (it.hasNext()) {
        QString p = it.next().captured(1);
        p.replace("\\\\", "\\"); // vdf escapes backslashes
        p = QDir::cleanPath(QDir::fromNativeSeparators(p));
        if (!libs.contains(p, Qt::CaseInsensitive)) libs << p;
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
            // some games have no portrait cover: fall back to the capsule or header art
            QString cover = cacheArt(steam, id, "library_600x900.jpg");
            for (const char *alt : {"library_capsule.jpg", "header.jpg", "library_header.jpg"})
                if (cover.isEmpty())
                    cover = cacheArt(steam, id, alt);
            const QString hero = cacheArt(steam, id, "library_hero.jpg");
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

#ifdef Q_OS_WIN
static QStringList startMenuRoots()
{
    QStringList r;
    for (const char *v : {"ProgramData", "AppData"}) {
        const QString base = qEnvironmentVariable(v);
        if (!base.isEmpty())
            r << QDir::cleanPath(QDir::fromNativeSeparators(base)) + "/Microsoft/Windows/Start Menu/Programs";
    }
    return r;
}

struct LnkInfo { QString target, args, iconFile; };

static QString expandEnv(const wchar_t *in)
{
    wchar_t out[2048];
    const DWORD n = ExpandEnvironmentStringsW(in, out, 2048);
    return (n > 0 && n <= 2048) ? QString::fromWCharArray(out) : QString::fromWCharArray(in);
}

static bool resolveLnk(const QString &lnk, LnkInfo &info)
{
    IShellLinkW *sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl))) || !sl)
        return false;
    bool ok = false;
    IPersistFile *pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && pf) {
        if (SUCCEEDED(pf->Load(reinterpret_cast<LPCOLESTR>(QDir::toNativeSeparators(lnk).utf16()), STGM_READ))) {
            wchar_t buf[2048] = {};
            if (SUCCEEDED(sl->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
                info.target = QDir::fromNativeSeparators(expandEnv(buf));
            wchar_t args[2048] = {};
            if (SUCCEEDED(sl->GetArguments(args, 2048)))
                info.args = QString::fromWCharArray(args);
            wchar_t ico[2048] = {};
            int idx = 0;
            if (SUCCEEDED(sl->GetIconLocation(ico, MAX_PATH, &idx)) && ico[0])
                info.iconFile = QDir::fromNativeSeparators(expandEnv(ico));
            ok = !info.target.isEmpty();
        }
        pf->Release();
    }
    sl->Release();
    return ok;
}

// PNG of the file's icon in the cache folder; returns a file:/// URL ("" on failure)
static QString winIconUrl(const QString &file)
{
    if (file.isEmpty() || !QFile::exists(file))
        return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/icons";
    QDir().mkpath(dir);
    const QString png = dir + "/" + QCryptographicHash::hash(file.toLower().toUtf8(), QCryptographicHash::Md5).toHex() + ".png";
    if (QFile::exists(png))
        return QUrl::fromLocalFile(png).toString();
    IShellItemImageFactory *f = nullptr;
    if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(QDir::toNativeSeparators(file).utf16()), nullptr,
                                           IID_PPV_ARGS(&f))) || !f)
        return {};
    HBITMAP hb = nullptr;
    const HRESULT hr = f->GetImage(SIZE{128, 128}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb);
    f->Release();
    if (FAILED(hr) || !hb)
        return {};
    QString url;
    BITMAP bm{};
    if (GetObject(hb, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
        HDC dc = GetDC(nullptr);
        const int lines = GetDIBits(dc, hb, 0, bm.bmHeight, img.bits(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        if (lines > 0) {
            bool anyAlpha = false;
            for (int y = 0; y < img.height() && !anyAlpha; ++y) {
                const QRgb *l = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(l[x])) { anyAlpha = true; break; }
            }
            if (!anyAlpha)
                img = img.convertToFormat(QImage::Format_RGB32);
            if (img.save(png, "PNG"))
                url = QUrl::fromLocalFile(png).toString();
        }
    }
    DeleteObject(hb);
    return url;
}

static QString lnkPathOf(const QString &relId)
{
    for (const QString &root : startMenuRoots())
        if (QFile::exists(root + "/" + relId))
            return root + "/" + relId;
    return {};
}

static bool skipLnkName(const QString &name, const QString &target)
{
    static const QRegularExpression bad("uninstall|uninst|\\bhelp\\b|readme|read me|manual|documentation|license|release notes|"
                                        "\\bwebsite\\b|\\bsupport\\b|changelog|\\bfaq\\b",
                                        QRegularExpression::CaseInsensitiveOption);
    if (bad.match(name).hasMatch())
        return true;
    const QString t = QFileInfo(target).fileName().toLower();
    if (t.startsWith("unins") || t == "msiexec.exe" || t == "cmd.exe")
        return true;
    const QString ext = QFileInfo(target).suffix().toLower();
    return ext != "exe" && ext != "bat" && ext != "com" && ext != "cmd";
}

QVariantList GameLibrary::scanApps(bool withIcons) const
{
    QVariantList out;
    QSet<QString> seen;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const QString &root : startMenuRoots()) {
        QDirIterator it(root, {"*.lnk"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = QDir(root).relativeFilePath(path);
            const QString title = QFileInfo(path).completeBaseName();
            LnkInfo li;
            if (!resolveLnk(path, li) || skipLnkName(title, li.target) || li.target.contains("/steamapps/", Qt::CaseInsensitive))
                continue;
            const QString key = title.toLower() + "|" + li.target.toLower();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            out << QVariantMap{{"desktopId", id}, {"title", title}, {"icon", withIcons ? winIconUrl(li.target) : QString()},
                               {"isGame", false}, {"target", li.target}, {"args", li.args}};
        }
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["title"].toString().compare(b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });
    return out;
}
#endif

QVariantList GameLibrary::installedApps() const
{
#ifdef Q_OS_WIN
    return scanApps(true);
#else
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
#endif
}

void GameLibrary::rescan()
{
    QVariantList list = scanSteam();
    const QStringList added = m_config->get("library.added").toStringList();
    const QStringList hidden = m_config->get("library.hidden").toStringList();
    QSet<QString> steamNames;
    for (const QVariant &g : std::as_const(list))
        steamNames.insert(g.toMap()["title"].toString());
#ifdef Q_OS_WIN
    const QVariantList apps = scanApps(false);
    QHash<QString, QString> exeIds;
#else
    const QVariantList apps = installedApps();
#endif
    for (const QVariant &a : apps) {
        const QVariantMap app = a.toMap();
        const QString id = "app:" + app["desktopId"].toString();
        if (!app["isGame"].toBool() && !added.contains(id))
            continue;
        if (steamNames.contains(app["title"].toString()))
            continue; // the Steam shortcut of a game we already list
        if (app["desktopId"].toString().startsWith("steam") && !added.contains(id))
            continue; // Steam itself is a launcher, not a game
#ifdef Q_OS_WIN
        const QString exe = QFileInfo(app["target"].toString()).fileName().toLower();
        if (!exe.isEmpty())
            exeIds.insert(exe, id);
        list << QVariantMap{{"id", id}, {"title", app["title"]}, {"platform", "Desktop"},
                            {"cover", ""}, {"hero", ""}, {"icon", ""}, {"target", app["target"]}};
#else
        list << QVariantMap{{"id", id}, {"title", app["title"]}, {"platform", "Desktop"},
                            {"cover", ""}, {"hero", ""}, {"icon", app["icon"]}};
#endif
    }
#ifdef Q_OS_WIN
    s_exeIds = exeIds; // the library apps, for GameWatcher
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (QVariant &g : list) { // icons only for the apps that are in the library
            QVariantMap m = g.toMap();
            if (m["platform"].toString() == "Desktop") {
                m["icon"] = winIconUrl(m["target"].toString());
                g = m;
            }
        }
        if (SUCCEEDED(co))
            CoUninitialize();
    }
#endif
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
#ifdef Q_OS_WIN
        QDesktopServices::openUrl(QUrl("steam://rungameid/" + id.mid(6)));
#else
        QProcess::startDetached("steam", {"steam://rungameid/" + id.mid(6)});
#endif
    else if (id.startsWith("app:")) {
#ifdef Q_OS_WIN
        const QString lnk = lnkPathOf(id.mid(4));
        if (!lnk.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(lnk));
#else
        // XPANEL_GAME marks the game's processes, so GameWatcher can tell when it ends
        QProcess p;
        p.setProgram("gtk-launch");
        p.setArguments({id.mid(4)});
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("XPANEL_GAME", id);
        p.setProcessEnvironment(env);
        p.startDetached();
#endif
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
