#include "Feedback.h"

#include "Desktop.h"
#include "DeviceClient.h"
#include "QrMatrix.h"
#include "Translations.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>

using namespace FeedbackReport;

static QString dmi(const char *name)
{
    QFile f(QStringLiteral("/sys/class/dmi/id/") + QLatin1String(name)); // world-readable, unlike product_serial
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

Feedback::Feedback(DeviceClient *device, Translations *i18n, QObject *parent)
    : QObject(parent), m_device(device), m_i18n(i18n)
{
}

Info Feedback::info() const
{
    Info i;
    i.version = QStringLiteral(XPANEL_VERSION);
    // "ROG Ally X (RC72LA)": the daemon knows the model; without it the DMI names are the best there is
    const QString product = m_device && !m_device->value("product").toString().isEmpty()
                                ? m_device->value("product").toString() : dmi("product_name");
    const bool known = m_device && m_device->value("model").toString() != "unknown"
                       && !m_device->value("modelName").toString().isEmpty();
    if (known)
        i.device = m_device->value("modelName").toString() + (product.isEmpty() ? QString() : " (" + product + ")");
    else
        i.device = (dmi("sys_vendor") + " " + product).trimmed();
    i.bios = m_device && !m_device->value("bios").toString().isEmpty() ? m_device->value("bios").toString()
                                                                       : dmi("bios_version");
    i.desktop = Desktop::label(Desktop::current());
    i.distro = QSysInfo::prettyProductName(); // PRETTY_NAME of /etc/os-release
    i.kernel = QSysInfo::kernelVersion();
    i.qt = QString::fromLatin1(qVersion());
    i.language = m_i18n ? m_i18n->current() : QString();
    return i;
}

QVariantList Feedback::infoLines(bool includeInfo) const
{
    Info i = info();
    if (!includeInfo)
        i = Info{i.version, {}, {}, {}, {}, {}, {}, {}};
    QVariantList out;
    for (const auto &l : FeedbackReport::infoLines(i))
        out << QVariantMap{{"label", l.first}, {"value", l.second}};
    return out;
}

QString Feedback::issueUrl(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    return FeedbackReport::issueUrl(typeFromString(type), title, text, info(), includeInfo).url;
}

QString Feedback::qrUrl(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    const QString full = issueUrl(type, title, text, includeInfo);
    if (full.toUtf8().size() <= kMaxQrLength)
        return full;
    return shortUrl(typeFromString(type), title, text, info(), includeInfo).url;
}

bool Feedback::qrIsShort(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    return issueUrl(type, title, text, includeInfo).toUtf8().size() > kMaxQrLength;
}

QString Feedback::qrSource(const QString &url) const
{
    return "image://qr/" + QString::fromLatin1(url.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString Feedback::reportText(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    return FeedbackReport::reportText(typeFromString(type), title, text, info(), includeInfo);
}

bool Feedback::hasContent(const QString &title, const QString &text) const
{
    return !title.trimmed().isEmpty() || !text.trimmed().isEmpty();
}

void Feedback::copy(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    if (QClipboard *c = QGuiApplication::clipboard())
        c->setText(reportText(type, title, text, includeInfo));
}

QVariantMap Feedback::openInBrowser(const QString &type, const QString &title, const QString &text, bool includeInfo) const
{
    const Link l = FeedbackReport::issueUrl(typeFromString(type), title, text, info(), includeInfo);
    if (l.truncated)
        copy(type, title, text, includeInfo); // the full text, to paste into the issue
    const bool opened = QDesktopServices::openUrl(QUrl(l.url, QUrl::StrictMode));
    return {{"opened", opened}, {"truncated", l.truncated}};
}

QImage QrImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QByteArray text = QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QrMatrix qr = qrEncode(QString::fromUtf8(text));
    if (qr.isNull()) {
        QImage blank(1, 1, QImage::Format_RGB32);
        blank.fill(Qt::white);
        if (size) *size = blank.size();
        return blank;
    }
    const int border = 4; // quiet zone in modules (QR specification)
    const int total = qr.size + 2 * border;
    const int want = requestedSize.isValid() ? std::min(requestedSize.width(), requestedSize.height()) : 400;
    const int scale = std::max(1, want / total); // whole pixels per module: sharp edges
    const int side = std::max(want, total * scale);
    QImage img(side, side, QImage::Format_RGB32);
    img.fill(Qt::white);
    const int off = (side - qr.size * scale) / 2;
    for (int y = 0; y < qr.size; ++y)
        for (int x = 0; x < qr.size; ++x)
            if (qr.dark(x, y))
                for (int dy = 0; dy < scale; ++dy) {
                    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(off + y * scale + dy));
                    for (int dx = 0; dx < scale; ++dx)
                        line[off + x * scale + dx] = qRgb(0, 0, 0);
                }
    if (size) *size = img.size();
    return img;
}
