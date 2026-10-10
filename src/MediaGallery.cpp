#include "MediaGallery.h"

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
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
    for (const QString &dir : {QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                               QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)}) {
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
    QStringList args{"trash"};
    for (const QString &p : paths)
        if (p.startsWith(QDir::homePath() + "/")) // only the user's own files
            args << p;
    if (args.size() > 1)
        QProcess::execute("gio", args);
    rescan();
}

void MediaGallery::openFile(const QString &path)
{
    QProcess::startDetached("xdg-open", {path});
}

void MediaGallery::showInFolder(const QString &path)
{
    QProcess::startDetached("dolphin", {"--select", path});
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
