#pragma once

#include <QList>
#include <QPair>
#include <QString>

// Settings > Feedback: builds the pre-filled GitHub "new issue" link and the plain-text report.
// Qt Core only, no I/O (unit tested in tests/tst_feedback.cpp). Nothing is sent by XPanel itself:
// the link opens in the user's browser, where they submit it with their own GitHub account.
//
// The issue forms in .github/ISSUE_TEMPLATE/*.yml take their fields from query parameters named
// after the field `id` (GitHub docs, "Creating an issue from a URL query"):
//   Problem -> bug_report.yml:      device, distro, version, what
//   Idea    -> feature_request.yml: what
//   Other   -> no template: title + body
// No serial number, user name, host name or path is ever part of the report.
namespace FeedbackReport {

enum class Type { Idea, Problem, Other };

// What is filled in automatically (all optional, empty = left out).
struct Info {
    QString version;  // XPanel version
    QString device;   // "ROG Ally X (RC72LA)"
    QString bios;     // BIOS version
    QString desktop;  // "KDE Plasma", "GNOME", "Steam Game Mode"
    QString distro;   // PRETTY_NAME of /etc/os-release
    QString kernel;
    QString qt;       // Qt version
    QString language; // UI language, "de"
};

constexpr int kMaxUrlLength = 7000;    // GitHub answers 414 above roughly 8 KB
constexpr int kMaxQrLength = 1500;     // above this the QR code carries the short link
constexpr int kMaxTitleLength = 120;

Type typeFromString(const QString &s); // "Idea" | "Problem" | anything else = Other
QString templateFile(Type t);          // "feature_request.yml", "bug_report.yml", "" for Other

// The title as sent: the given one, or the first line of the text when empty.
QString effectiveTitle(const QString &title, const QString &text);

// Preview lines [label, value] of what the automatic part contains (empty values left out).
QList<QPair<QString, QString>> infoLines(const Info &info);

struct Link {
    QString url;
    bool truncated = false; // the text was shortened to fit maxLength
};
// The full link. includeInfo=false leaves out everything but the XPanel version.
Link issueUrl(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo,
              int maxLength = kMaxUrlLength);
// A short link for the QR code: title + template + device fields, without the user's text.
Link shortUrl(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo);

// The whole report as Markdown (clipboard, "Save locally").
QString reportText(Type type, const QString &title, const QString &text, const Info &info, bool includeInfo);

} // namespace FeedbackReport
