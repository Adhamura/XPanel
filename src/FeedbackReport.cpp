#include "FeedbackReport.h"

#include <QStringList>
#include <QUrl>

namespace FeedbackReport {

static const char kNewIssue[] = "https://github.com/Adhamura/XPanel/issues/new";
static const char kTruncNote[] = "\n\n[text shortened: too long for a link]";

Type typeFromString(const QString &s)
{
    if (s == "Idea") return Type::Idea;
    if (s == "Problem") return Type::Problem;
    return Type::Other;
}

QString templateFile(Type t)
{
    switch (t) {
    case Type::Idea: return "feature_request.yml";
    case Type::Problem: return "bug_report.yml";
    case Type::Other: break;
    }
    return {};
}

QString effectiveTitle(const QString &title, const QString &text)
{
    QString t = title.simplified();
    if (t.isEmpty())
        t = text.trimmed().section('\n', 0, 0).simplified();
    if (t.size() > kMaxTitleLength)
        t = t.left(kMaxTitleLength - 1).trimmed() + QChar(0x2026);
    return t;
}

QList<QPair<QString, QString>> infoLines(const Info &i)
{
    QList<QPair<QString, QString>> out;
    auto add = [&out](const char *label, const QString &v) {
        if (!v.trimmed().isEmpty())
            out.append({QString::fromLatin1(label), v.trimmed()});
    };
    add("XPanel", i.version);
    add("Device", i.device);
    add("BIOS", i.bios);
    add("Desktop", i.desktop);
    add("Distro", i.distro);
    add("Kernel", i.kernel);
    add("Qt", i.qt);
    add("Language", i.language);
    return out;
}

static Info visibleInfo(const Info &i, bool includeInfo)
{
    if (includeInfo)
        return i;
    Info v;
    v.version = i.version; // always: it tells which version the report is about
    return v;
}

static QString infoBlock(const Info &i)
{
    QString s;
    for (const auto &l : infoLines(i))
        s += "- " + l.first + ": " + l.second + "\n";
    return s.isEmpty() ? s : "---\n" + s;
}

static QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

// "ROG Ally X (RC72LA), BIOS 330"
static QString deviceField(const Info &i)
{
    QString d = i.device.trimmed();
    if (!i.bios.trimmed().isEmpty())
        d += (d.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral("BIOS ") + i.bios.trimmed();
    return d;
}

// "CachyOS Linux, KDE Plasma, kernel 6.17.1, Qt 6.8.2, language de" (the bug form asks for distro, desktop, kernel)
static QString distroField(const Info &i)
{
    QStringList p;
    if (!i.distro.trimmed().isEmpty()) p << i.distro.trimmed();
    if (!i.desktop.trimmed().isEmpty()) p << i.desktop.trimmed();
    if (!i.kernel.trimmed().isEmpty()) p << "kernel " + i.kernel.trimmed();
    if (!i.qt.trimmed().isEmpty()) p << "Qt " + i.qt.trimmed();
    if (!i.language.trimmed().isEmpty()) p << "language " + i.language.trimmed();
    return p.join(", ");
}

static QString versionField(const Info &i)
{
    return i.version.trimmed().isEmpty() ? QString() : "XPanel " + i.version.trimmed();
}

static QString assemble(const QList<QPair<QString, QString>> &params)
{
    QString url = QString::fromLatin1(kNewIssue);
    QChar sep = QLatin1Char('?');
    for (const auto &p : params) {
        if (p.second.isEmpty())
            continue;
        url += sep + p.first + "=" + enc(p.second);
        sep = QLatin1Char('&');
    }
    return url;
}

// text = the user's text (may be empty); the device fields only where the template has them
static QString build(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo)
{
    const Info i = visibleInfo(info, includeInfo);
    QList<QPair<QString, QString>> p;
    if (!templateFile(type).isEmpty())
        p.append({"template", templateFile(type)});
    p.append({"title", title});
    switch (type) {
    case Type::Problem:
        p.append({"device", deviceField(i)});
        p.append({"distro", distroField(i)});
        p.append({"version", versionField(i)});
        p.append({"what", text});
        break;
    case Type::Idea: // the feature form has no device fields: the context goes below the text
        p.append({"what", (text.isEmpty() ? QString() : text + "\n\n") + infoBlock(i)});
        break;
    case Type::Other:
        p.append({"body", (text.isEmpty() ? QString() : text + "\n\n") + infoBlock(i)});
        break;
    }
    return assemble(p);
}

Link issueUrl(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo, int maxLength)
{
    Link l;
    const QString t = effectiveTitle(title, text);
    QString body = text.trimmed();
    l.url = build(type, t, body, info, includeInfo);
    if (l.url.size() > maxLength) {
        // percent-encoding makes a non-ASCII character 6-9 characters long: find the longest text that fits
        l.truncated = true;
        auto cut = [&](int keep) {
            QString b = body.left(keep);
            if (!b.isEmpty() && b.at(b.size() - 1).isHighSurrogate())
                b.chop(1);
            return b.trimmed() + QString::fromLatin1(kTruncNote);
        };
        int lo = 0, hi = body.size(); // cut(lo) fits (or nothing fits), cut(hi) does not
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (build(type, t, cut(mid), info, includeInfo).size() <= maxLength) lo = mid; else hi = mid;
        }
        l.url = build(type, t, cut(lo), info, includeInfo);
    }
    return l;
}

Link shortUrl(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo)
{
    Link l;
    l.url = build(type, effectiveTitle(title, text), QString(), info, includeInfo);
    return l;
}

QString reportText(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo)
{
    const QString kind = type == Type::Idea ? "Idea" : type == Type::Problem ? "Problem" : "Other";
    QString s = "## " + effectiveTitle(title, text) + "\n\n**Type:** " + kind + "\n\n";
    if (!text.trimmed().isEmpty())
        s += text.trimmed() + "\n\n";
    s += infoBlock(visibleInfo(info, includeInfo));
    return s;
}

} // namespace FeedbackReport
