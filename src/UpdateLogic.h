#pragma once

// Pure helpers of the XPanel self-updater (src/AppUpdate.cpp), header-only so the unit tests
// (tests/tst_appupdate.cpp) can use them without the network or a GUI.

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace UpdateLogic {

// "v1.10.0" -> {1, 10, 0}. A suffix after '-' or '+' (pre-release, build) is ignored.
inline QList<int> versionParts(QString v)
{
    v = v.trimmed();
    if (v.startsWith('v') || v.startsWith('V'))
        v.remove(0, 1);
    const qsizetype cut = v.indexOf(QRegularExpression("[-+ ]"));
    if (cut >= 0)
        v.truncate(cut);
    QList<int> parts;
    for (const QString &p : v.split('.')) {
        bool ok = false;
        const int n = p.toInt(&ok);
        parts << (ok && n >= 0 ? n : 0);
    }
    while (parts.size() > 1 && parts.last() == 0) // 1.4 == 1.4.0
        parts.removeLast();
    return parts;
}

// <0: a older than b, 0: same, >0: a newer. Numeric per part: 1.10.0 > 1.9.0.
inline int compareVersions(const QString &a, const QString &b)
{
    const QList<int> x = versionParts(a), y = versionParts(b);
    for (qsizetype i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}

// Looks like a release version ("1.4.0", "v2.0"): digits and dots only after the 'v'.
inline bool isVersion(const QString &v)
{
    static const QRegularExpression re("^[vV]?\\d+(\\.\\d+){0,3}$");
    return re.match(v.trimmed()).hasMatch();
}

// The SHA-256 of `fileName` from a sha256sum file ("<hex>  name" or "<hex> *name" per line),
// lower case; empty when the file is not listed or the line is malformed.
inline QByteArray sha256For(const QByteArray &sums, const QString &fileName)
{
    static const QRegularExpression re("^([0-9a-fA-F]{64}) [ *](.+)$");
    for (QByteArray line : sums.split('\n')) {
        line = line.trimmed();
        const QRegularExpressionMatch m = re.match(QString::fromUtf8(line));
        if (m.hasMatch() && m.captured(2).trimmed() == fileName)
            return m.captured(1).toLower().toLatin1();
    }
    return {};
}

// The release asset an install kind updates from (empty: no automatic update).
inline QString assetName(const QString &installKind, const QString &version)
{
    if (installKind == "appimage-installed" || installKind == "appimage-portable")
        return QString("XPanel-%1-x86_64.AppImage").arg(version);
    if (installKind == "windows")
        return QString("XPanel-%1-Windows-x64-Setup.exe").arg(version);
    return {};
}

// How XPanel was installed, from what the running process sees (same detection as
// DeviceClient::installHint()):
//   windows             the Inno Setup installer (unins000.exe next to XPanel.exe)
//   appimage-installed  /opt/xpanel (sudo XPanel.AppImage --install)
//   appimage-portable   a user's AppImage file ($APPIMAGE) in a writable folder
//   manual              anything else: source build, release archive, distribution package
inline QString installKind(bool windows, const QString &appDir, const QString &appImage,
                           bool hasUninstaller, bool appImageDirWritable)
{
    if (windows)
        return hasUninstaller ? "windows" : "manual";
    if (appDir.startsWith("/opt/xpanel/") || appDir == "/opt/xpanel")
        return "appimage-installed";
    if (!appImage.isEmpty() && !appImage.startsWith("/opt/xpanel/") && appImageDirWritable)
        return "appimage-portable";
    return "manual";
}

// Downloads come only from GitHub over https. `allowLocal` (only when XPANEL_UPDATE_URL is set,
// for tests) also accepts http://127.0.0.1, http://localhost and file:// URLs.
inline bool urlAllowed(const QUrl &url, bool allowLocal = false)
{
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower(), host = url.host().toLower();
    if (allowLocal) {
        if (scheme == "file")
            return true;
        if (scheme == "http" && (host == "127.0.0.1" || host == "localhost"))
            return true;
    }
    if (scheme != "https" || !url.userInfo().isEmpty())
        return false;
    return host == "github.com" || host == "api.github.com" || host == "objects.githubusercontent.com"
        || host.endsWith(".githubusercontent.com");
}

// Release asset names are plain file names; never let one point elsewhere on disk.
inline bool safeFileName(const QString &name)
{
    static const QRegularExpression re("^[A-Za-z0-9][A-Za-z0-9._+-]*$");
    return re.match(name).hasMatch() && !name.contains("..");
}

} // namespace UpdateLogic
