// Unit tests for the feedback link (src/FeedbackReport.*) and the QR code (src/QrMatrix.*).
#include "FeedbackReport.h"
#include "QrMatrix.h"

#include <QTest>
#include <QUrl>
#include <QUrlQuery>

using namespace FeedbackReport;

static Info sample()
{
    Info i;
    i.version = "1.4.0";
    i.device = "ROG Ally X (RC72LA)";
    i.bios = "330";
    i.desktop = "KDE Plasma";
    i.distro = "CachyOS Linux";
    i.kernel = "6.17.1-cachyos";
    i.qt = "6.8.2";
    i.language = "de";
    return i;
}

static QUrlQuery queryOf(const QString &url)
{
    return QUrlQuery(QUrl(url).query(QUrl::FullyEncoded));
}

class TestFeedback : public QObject {
    Q_OBJECT

private slots:
    void typeMapping()
    {
        QCOMPARE(typeFromString("Problem"), Type::Problem);
        QCOMPARE(typeFromString("Idea"), Type::Idea);
        QCOMPARE(typeFromString("Other"), Type::Other);
        QCOMPARE(typeFromString("something"), Type::Other);
        QCOMPARE(templateFile(Type::Problem), QString("bug_report.yml"));
        QCOMPARE(templateFile(Type::Idea), QString("feature_request.yml"));
        QCOMPARE(templateFile(Type::Other), QString());
    }
    void problemUsesTheBugFormFieldIds()
    {
        const Link l = issueUrl(Type::Problem, "Fan is loud", "It spins up at idle", sample(), true);
        QVERIFY(!l.truncated);
        QVERIFY(l.url.startsWith("https://github.com/Adhamura/XPanel/issues/new?"));
        const QUrlQuery q = queryOf(l.url);
        QCOMPARE(q.queryItemValue("template"), QString("bug_report.yml"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("title").toUtf8()), QString("Fan is loud"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("what").toUtf8()), QString("It spins up at idle"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("version").toUtf8()), QString("XPanel 1.4.0"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("device").toUtf8()), QString("ROG Ally X (RC72LA), BIOS 330"));
        const QString distro = QUrl::fromPercentEncoding(q.queryItemValue("distro").toUtf8());
        QVERIFY(distro.contains("CachyOS Linux"));
        QVERIFY(distro.contains("KDE Plasma"));
        QVERIFY(distro.contains("6.17.1-cachyos"));
        QVERIFY(!q.hasQueryItem("labels")); // the form template sets its own label
    }
    void ideaUsesTheFeatureFormField()
    {
        const Link l = issueUrl(Type::Idea, "Per-game fan curve", "Please", sample(), true);
        const QUrlQuery q = queryOf(l.url);
        QCOMPARE(q.queryItemValue("template"), QString("feature_request.yml"));
        const QString what = QUrl::fromPercentEncoding(q.queryItemValue("what").toUtf8());
        QVERIFY(what.startsWith("Please"));
        QVERIFY(what.contains("ROG Ally X"));
        QVERIFY(!q.hasQueryItem("device"));
    }
    void otherHasNoTemplate()
    {
        const Link l = issueUrl(Type::Other, "Hello", "Nice app", sample(), true);
        const QUrlQuery q = queryOf(l.url);
        QVERIFY(!q.hasQueryItem("template"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("title").toUtf8()), QString("Hello"));
        const QString body = QUrl::fromPercentEncoding(q.queryItemValue("body").toUtf8());
        QVERIFY(body.startsWith("Nice app"));
        QVERIFY(body.contains("Kernel: 6.17.1-cachyos"));
    }
    void encoding()
    {
        const Link l = issueUrl(Type::Other, "a & b = c?", "x&y=z#frag %20 äö 日本\n\nline", sample(), false);
        // the link has exactly the parameters title and body: nothing in the text leaks into the structure
        const QUrlQuery q = queryOf(l.url);
        QCOMPARE(q.queryItems().size(), 2);
        QVERIFY(!l.url.contains('#'));
        QVERIFY(!l.url.contains(' '));
        QVERIFY(!l.url.contains('\n'));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("title").toUtf8()), QString("a & b = c?"));
        const QString body = QUrl::fromPercentEncoding(q.queryItemValue("body").toUtf8());
        QVERIFY(body.startsWith("x&y=z#frag %20 äö 日本\n\nline"));
        QVERIFY(QUrl(l.url, QUrl::StrictMode).isValid());
    }
    void titleFromTheTextWhenEmpty()
    {
        QCOMPARE(effectiveTitle("", "  First line here\nsecond"), QString("First line here"));
        QCOMPARE(effectiveTitle("  My   title ", "x"), QString("My title"));
        QCOMPARE(effectiveTitle(QString(300, 'a'), "").size(), kMaxTitleLength);
    }
    void deviceInfoCanBeLeftOut()
    {
        const Link l = issueUrl(Type::Problem, "t", "text", sample(), false);
        const QUrlQuery q = queryOf(l.url);
        QVERIFY(!q.hasQueryItem("device"));
        QVERIFY(!q.hasQueryItem("distro"));
        QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue("version").toUtf8()), QString("XPanel 1.4.0"));
        QVERIFY(!l.url.contains("Cachy"));
        QCOMPARE(infoLines(Info{"1.4.0", {}, {}, {}, {}, {}, {}, {}}).size(), 1);
    }
    void longTextIsCutToFit()
    {
        const QString text = QString("abc äöü ").repeated(2000);
        for (const Type t : {Type::Problem, Type::Idea, Type::Other}) {
            const Link l = issueUrl(t, "Long one", text, sample(), true);
            QVERIFY(l.truncated);
            QVERIFY2(l.url.size() <= kMaxUrlLength, qPrintable(QString::number(l.url.size())));
            QVERIFY(l.url.size() > kMaxUrlLength - 400); // not cut more than needed
            QVERIFY(QUrl::fromPercentEncoding(l.url.toUtf8()).contains("text shortened"));
        }
        const Link small = issueUrl(Type::Problem, "t", "short", sample(), true);
        QVERIFY(!small.truncated);
    }
    void shortLinkHasNoUserText()
    {
        const Link l = shortUrl(Type::Problem, "Crash", "secret long text", sample(), true);
        QVERIFY(!l.url.contains("secret"));
        const QUrlQuery q = queryOf(l.url);
        QCOMPARE(q.queryItemValue("template"), QString("bug_report.yml"));
        QVERIFY(q.hasQueryItem("device"));
        QVERIFY(!q.hasQueryItem("what"));
        QVERIFY(l.url.toUtf8().size() < kMaxQrLength);
    }
    void noSerialOrUser()
    {
        // The report is built only from Info: there is no serial / user / host / path field at all.
        const Link l = issueUrl(Type::Problem, "t", "text", sample(), true);
        const QString all = QUrl::fromPercentEncoding(l.url.toUtf8()) + reportText(Type::Problem, "t", "text", sample(), true);
        QVERIFY(!all.contains("serial", Qt::CaseInsensitive));
        QVERIFY(!all.contains("/home/"));
        for (const auto &line : infoLines(sample()))
            QVERIFY(line.first != "Serial" && line.first != "User" && line.first != "Host");
    }
    void reportTextIsMarkdown()
    {
        const QString r = reportText(Type::Idea, "A title", "Body text", sample(), true);
        QVERIFY(r.startsWith("## A title"));
        QVERIFY(r.contains("**Type:** Idea"));
        QVERIFY(r.contains("Body text"));
        QVERIFY(r.contains("- Device: ROG Ally X (RC72LA)"));
        QVERIFY(r.contains("- Kernel: 6.17.1-cachyos"));
    }

    // ---------- QR code ----------
    void qrIsSquareAndHasFinderPatterns()
    {
        const QString url = issueUrl(Type::Problem, "Sample", "A sample report", sample(), true).url;
        const QrMatrix m = qrEncode(url);
        QVERIFY(!m.isNull());
        QVERIFY(m.size >= 21 && m.size <= 177 && (m.size - 21) % 4 == 0); // versions 1..40
        QCOMPARE(m.cells.size(), m.size * m.size);
        // finder pattern: 7x7 ring, 3x3 centre, in three corners
        const int n = m.size;
        for (const auto &c : {QPoint(0, 0), QPoint(n - 7, 0), QPoint(0, n - 7)}) {
            for (int i = 0; i < 7; ++i) {
                QVERIFY(m.dark(c.x() + i, c.y()));
                QVERIFY(m.dark(c.x() + i, c.y() + 6));
                QVERIFY(m.dark(c.x(), c.y() + i));
                QVERIFY(m.dark(c.x() + 6, c.y() + i));
            }
            QVERIFY(!m.dark(c.x() + 1, c.y() + 1));
            QVERIFY(m.dark(c.x() + 3, c.y() + 3));
        }
        QVERIFY(!m.dark(-1, 0) && !m.dark(n, 0));
    }
    void qrIsDeterministicAndDiffersPerText()
    {
        const QrMatrix a = qrEncode("https://example.org/a"), b = qrEncode("https://example.org/a"),
                       c = qrEncode("https://example.org/b");
        QCOMPARE(a.cells, b.cells);
        QVERIFY(a.cells != c.cells);
    }
    void qrLimits()
    {
        QVERIFY(qrEncode("").isNull());
        const QrMatrix big = qrEncode(QString(1500, 'x'));
        QVERIFY(!big.isNull());
        QVERIFY(qrEncode(QString(4000, 'x')).isNull()); // more than version 40 holds
        // the short link of the longest report fits a QR code comfortably
        const Link s = shortUrl(Type::Problem, QString(300, 'T'), "", sample(), true);
        QVERIFY(!qrEncode(s.url).isNull());
    }
};

QTEST_GUILESS_MAIN(TestFeedback)
#include "tst_feedback.moc"
