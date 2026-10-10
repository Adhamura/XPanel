// Unit tests for the pure parts of XPanel's self-updater (src/UpdateLogic.h).
#include "UpdateLogic.h"

#include <QTest>

using namespace UpdateLogic;

class TestAppUpdate : public QObject {
    Q_OBJECT

private slots:
    void compare_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        QTest::newRow("same") << "1.4.0" << "1.4.0" << 0;
        QTest::newRow("tag prefix") << "v1.4.0" << "1.4.0" << 0;
        QTest::newRow("numeric, not text") << "1.10.0" << "1.9.0" << 1;
        QTest::newRow("older") << "1.3.2" << "1.4.0" << -1;
        QTest::newRow("patch") << "1.4.1" << "1.4.0" << 1;
        QTest::newRow("missing part is zero") << "1.4" << "1.4.0" << 0;
        QTest::newRow("longer") << "1.4.0.1" << "1.4.0" << 1;
        QTest::newRow("major") << "2.0.0" << "1.99.99" << 1;
        QTest::newRow("pre-release suffix ignored") << "1.5.0-rc1" << "1.5.0" << 0;
        QTest::newRow("upper V") << "V2.0" << "1.9" << 1;
    }
    void compare()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(compareVersions(a, b), expected);
        QCOMPARE(compareVersions(b, a), -expected);
    }

    void isVersion_data()
    {
        QTest::addColumn<QString>("v");
        QTest::addColumn<bool>("ok");
        QTest::newRow("tag") << "v1.4.0" << true;
        QTest::newRow("plain") << "1.4.0" << true;
        QTest::newRow("two parts") << "2.0" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("text") << "latest" << false;
        QTest::newRow("pre-release") << "v1.5.0-rc1" << false;
        QTest::newRow("injection") << "1.4.0; rm -rf /" << false;
    }
    void isVersion()
    {
        QFETCH(QString, v);
        QFETCH(bool, ok);
        QCOMPARE(UpdateLogic::isVersion(v), ok);
    }

    void sums()
    {
        const QByteArray file =
            "479f324088c647267a5d13e43f86505ce2d8af124f60e486569e58619ddc1613  XPanel-1.4.0-x86_64.AppImage\n"
            "97fec91af785445534ad02e43b9f2622f6d9cd4a2dbaeaaa4723511ce4d8c8c0  xpanel-1.4.0-linux-x86_64.tar.gz\r\n"
            "6E73912B331DC3446456A5A3773713EECDB2A2E1CDBAE163B84B308218901D6F *XPanel-1.4.0-Windows-x64-Setup.exe\n"
            "garbage line\n"
            "1234  short-hash.bin\n";
        QCOMPARE(sha256For(file, "XPanel-1.4.0-x86_64.AppImage"),
                 QByteArray("479f324088c647267a5d13e43f86505ce2d8af124f60e486569e58619ddc1613"));
        QCOMPARE(sha256For(file, "xpanel-1.4.0-linux-x86_64.tar.gz"),
                 QByteArray("97fec91af785445534ad02e43b9f2622f6d9cd4a2dbaeaaa4723511ce4d8c8c0"));
        // binary mode marker, upper case hex -> lower case
        QCOMPARE(sha256For(file, "XPanel-1.4.0-Windows-x64-Setup.exe"),
                 QByteArray("6e73912b331dc3446456a5a3773713eecdb2a2e1cdbae163b84b308218901d6f"));
        QVERIFY(sha256For(file, "XPanel-1.5.0-x86_64.AppImage").isEmpty()); // not listed
        QVERIFY(sha256For(file, "short-hash.bin").isEmpty());                // malformed
        QVERIFY(sha256For(file, "x86_64.AppImage").isEmpty());               // no partial match
        QVERIFY(sha256For({}, "XPanel-1.4.0-x86_64.AppImage").isEmpty());
    }

    void assets()
    {
        QCOMPARE(assetName("appimage-installed", "1.5.0"), QString("XPanel-1.5.0-x86_64.AppImage"));
        QCOMPARE(assetName("appimage-portable", "1.5.0"), QString("XPanel-1.5.0-x86_64.AppImage"));
        QCOMPARE(assetName("windows", "1.5.0"), QString("XPanel-1.5.0-Windows-x64-Setup.exe"));
        QVERIFY(assetName("manual", "1.5.0").isEmpty());
        QVERIFY(safeFileName("XPanel-1.5.0-x86_64.AppImage"));
        QVERIFY(!safeFileName("../XPanel.AppImage"));
        QVERIFY(!safeFileName("a/b"));
        QVERIFY(!safeFileName(""));
        QVERIFY(!safeFileName(".hidden"));
    }

    void kind()
    {
        QCOMPARE(installKind(true, "C:/Program Files/XPanel", {}, true, false), QString("windows"));
        QCOMPARE(installKind(true, "C:/Users/me/xpanel", {}, false, false), QString("manual"));
        QCOMPARE(installKind(false, "/opt/xpanel/usr/bin", {}, false, false), QString("appimage-installed"));
        QCOMPARE(installKind(false, "/tmp/.mount_XPanelab/usr/bin", "/home/me/Apps/XPanel-1.4.0-x86_64.AppImage", false, true),
                 QString("appimage-portable"));
        QCOMPARE(installKind(false, "/tmp/.mount_XPanelab/usr/bin", "/usr/share/XPanel.AppImage", false, false),
                 QString("manual"));
        QCOMPARE(installKind(false, "/usr/bin", {}, false, false), QString("manual"));
        QCOMPARE(installKind(false, "/home/me/XPanel/build/bin", {}, false, false), QString("manual"));
        QCOMPARE(installKind(false, "/opt/xpanelfoo/bin", {}, false, false), QString("manual"));
    }

    void urls_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("local");
        QTest::addColumn<bool>("ok");
        QTest::newRow("api") << "https://api.github.com/repos/Adhamura/XPanel/releases/latest" << false << true;
        QTest::newRow("download") << "https://github.com/Adhamura/XPanel/releases/download/v1.4.0/SHA256SUMS" << false << true;
        QTest::newRow("objects") << "https://objects.githubusercontent.com/github-production-release-asset/1" << false << true;
        QTest::newRow("release-assets") << "https://release-assets.githubusercontent.com/x" << false << true;
        QTest::newRow("http") << "http://github.com/Adhamura/XPanel" << false << false;
        QTest::newRow("other host") << "https://example.com/XPanel.AppImage" << false << false;
        QTest::newRow("look-alike") << "https://github.com.evil.example/x" << false << false;
        QTest::newRow("suffix look-alike") << "https://evilgithubusercontent.com/x" << false << false;
        QTest::newRow("user info") << "https://github.com@evil.example/x" << false << false;
        QTest::newRow("credentials") << "https://user:pw@github.com/x" << false << false;
        QTest::newRow("local without test url") << "http://127.0.0.1:8000/latest.json" << false << false;
        QTest::newRow("file without test url") << "file:///tmp/latest.json" << false << false;
        QTest::newRow("local with test url") << "http://127.0.0.1:8000/latest.json" << true << true;
        QTest::newRow("file with test url") << "file:///tmp/latest.json" << true << true;
        QTest::newRow("other http with test url") << "http://example.com/x" << true << false;
        QTest::newRow("garbage") << "not a url" << false << false;
    }
    void urls()
    {
        QFETCH(QString, url);
        QFETCH(bool, local);
        QFETCH(bool, ok);
        QCOMPARE(urlAllowed(QUrl(url), local), ok);
    }
};

QTEST_APPLESS_MAIN(TestAppUpdate)
#include "tst_appupdate.moc"
