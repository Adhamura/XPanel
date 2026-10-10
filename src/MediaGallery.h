#pragma once

#include <QObject>
#include <QVariantList>

// Media Gallery: screenshots and videos in ~/Pictures and ~/Videos.
class MediaGallery : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    using QObject::QObject;
    QVariantList items() const { return m_items; }

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void trash(const QStringList &paths);  // to the desktop trash (can be restored)
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void showInFolder(const QString &path);
    Q_INVOKABLE void copyToClipboard(const QStringList &paths); // as files, paste in chat / file manager

signals:
    void itemsChanged();

private:
    QVariantList m_items;
};
