#pragma once

#include "FeedbackReport.h"

#include <QObject>
#include <QQuickImageProvider>
#include <QVariantList>
#include <QVariantMap>

class DeviceClient;
class Translations;

// Settings > Feedback ("feedback" context property): the report for a GitHub issue and its QR code.
// XPanel sends nothing itself and carries no GitHub token: the pre-filled "new issue" page opens in
// the browser (or is scanned from the QR code with a phone) and the user submits it with their own
// GitHub account. See FeedbackReport.h for the link, and what is (not) in it.
class Feedback : public QObject {
    Q_OBJECT
public:
    Feedback(DeviceClient *device, Translations *i18n, QObject *parent = nullptr);

    // What is filled in automatically: [{label, value}] (privacy: no serial number, user or host name)
    Q_INVOKABLE QVariantList infoLines(bool includeInfo) const;
    // type: "Idea" | "Problem" | "Other"
    Q_INVOKABLE QString issueUrl(const QString &type, const QString &title, const QString &text, bool includeInfo) const;
    // The link for the QR code: the full link, or the short one (no text) when that is too long to scan
    Q_INVOKABLE QString qrUrl(const QString &type, const QString &title, const QString &text, bool includeInfo) const;
    Q_INVOKABLE bool qrIsShort(const QString &type, const QString &title, const QString &text, bool includeInfo) const;
    Q_INVOKABLE QString qrSource(const QString &url) const; // "image://qr/..." for an Image
    Q_INVOKABLE QString reportText(const QString &type, const QString &title, const QString &text, bool includeInfo) const;
    Q_INVOKABLE bool hasContent(const QString &title, const QString &text) const;
    Q_INVOKABLE void copy(const QString &type, const QString &title, const QString &text, bool includeInfo) const;
    // Opens the browser. {opened, truncated}; a text that did not fit the link is also copied.
    Q_INVOKABLE QVariantMap openInBrowser(const QString &type, const QString &title, const QString &text, bool includeInfo) const;

    FeedbackReport::Info info() const;

private:
    DeviceClient *m_device;
    Translations *m_i18n;
};

// "image://qr/<base64url of the text>": the QR code as a white square with quiet zone.
// Dark modules are drawn in whole pixels so that the code stays sharp when scanned from a screen.
class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
