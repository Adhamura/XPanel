#include "MediaGallery.h"
#include "Desktop.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

void MediaGallery::rescan()
{
    static const QStringList pics{"*.png", "*.jpg", "*.jpeg", "*.webp", "*.avif"};
    static const QStringList vids{"*.mp4", "*.mkv", "*.webm", "*.mov"};
    QList<QFileInfo> files;
    QStringList dirs{QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                     QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)};
#ifdef Q_OS_WIN
    // Xbox Game Bar captures live in Videos\Captures; Pictures\Screenshots is under Pictures already
    const QString cap = dirs.at(1) + "/Captures";
    if (QDir(cap).exists() && !cap.startsWith(dirs.at(1) + "/", Qt::CaseInsensitive))
        dirs << cap;
#endif
    for (const QString &dir : std::as_const(dirs)) {
        QDirIterator it(dir, pics + vids, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            files << it.fileInfo();
        }
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    QVariantList out;
    for (const QFileInfo &f : std::as_const(files)) {
        const QString ext = "*." + f.suffix().toLower();
        out << QVariantMap{{"path", f.absoluteFilePath()}, {"url", QUrl::fromLocalFile(f.absoluteFilePath()).toString()},
                           {"name", f.fileName()}, {"video", vids.contains(ext)},
                           {"screenshot", f.absolutePath().contains("Screenshot", Qt::CaseInsensitive)
                                              || f.fileName().startsWith("Screenshot", Qt::CaseInsensitive)},
                           {"date", f.lastModified().toString("dd.MM.yyyy HH:mm")},
                           {"size", double(f.size())}};
    }
    m_items = out;
    emit itemsChanged();
}

void MediaGallery::trash(const QStringList &paths)
{
#ifdef Q_OS_WIN
    const QString home = QDir::fromNativeSeparators(QDir::homePath()) + "/";
    for (const QString &p : paths)
        if (QDir::fromNativeSeparators(p).startsWith(home, Qt::CaseInsensitive)) // only the user's own files
            QFile::moveToTrash(p);
#else
    QStringList args{"trash"};
    for (const QString &p : paths)
        if (p.startsWith(QDir::homePath() + "/")) // only the user's own files
            args << p;
    if (args.size() > 1)
        QProcess::execute("gio", args);
#endif
    rescan();
}

void MediaGallery::openFile(const QString &path)
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#else
    QProcess::startDetached("xdg-open", {path});
#endif
}

void MediaGallery::showInFolder(const QString &path)
{
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(path)});
#else
    Desktop::showInFileManager(path, true);
#endif
}

void MediaGallery::copyToClipboard(const QStringList &paths)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);
    QGuiApplication::clipboard()->setMimeData(mime);
}
