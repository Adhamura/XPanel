// Unit tests for the per-game profile rules (src/ProfileRules.*).
#include "ProfileRules.h"

#include <QTest>

using namespace ProfileRules;

static QByteArray nul(std::initializer_list<const char *> args)
{
    QByteArray out;
    for (const char *a : args) {
        out += a;
        out += '\0';
    }
    return out;
}

class TestProfileRules : public QObject {
    Q_OBJECT

private slots:
    void cmdline_data()
    {
        QTest::addColumn<QByteArray>("cmdline");
        QTest::addColumn<QString>("appid");
        const char *rp = "/home/u/.local/share/Steam/ubuntu12_32/reaper";
        QTest::newRow("native") << nul({rp, "SteamLaunch", "AppId=620", "--", "/games/portal2.sh"}) << "620";
        QTest::newRow("proton") << nul({rp, "SteamLaunch", "AppId=1245620", "--",
                                          "/Steam/ubuntu12_32/steam-launch-wrapper", "--", "proton", "waitforexitandrun"})
                                << "1245620";
        QTest::newRow("bare name") << nul({"reaper", "SteamLaunch", "AppId=12345", "--", "sleep"}) << "12345";
        QTest::newRow("script") << nul({"/bin/sh", "/tmp/x/reaper", "SteamLaunch", "AppId=12345", "--"}) << "12345";
        QTest::newRow("no SteamLaunch") << nul({rp, "AppId=620", "--", "game"}) << "";
        QTest::newRow("no reaper") << nul({"/usr/bin/game", "SteamLaunch", "AppId=620"}) << "";
        QTest::newRow("after --") << nul({rp, "--", "SteamLaunch", "AppId=620"}) << "";
        QTest::newRow("not a number") << nul({rp, "SteamLaunch", "AppId=abc", "--"}) << "";
        QTest::newRow("steam client") << nul({"/usr/lib/steam/steam", "-silent"}) << "";
        QTest::newRow("empty") << QByteArray() << "";
    }
    void cmdline()
    {
        QFETCH(QByteArray, cmdline);
        QFETCH(QString, appid);
        QCOMPARE(steamAppIdFromCmdline(cmdline), appid);
    }

    void environ_data()
    {
        QTest::addColumn<QByteArray>("env");
        QTest::addColumn<QString>("id");
        QTest::newRow("SteamAppId") << nul({"HOME=/home/u", "SteamAppId=620", "SteamGameId=620"}) << "steam:620";
        QTest::newRow("SteamGameId") << nul({"SteamGameId=620"}) << "steam:620";
        QTest::newRow("shortcut") << nul({"SteamAppId=0", "SteamGameId=12464663414870278144"}) << "";
        QTest::newRow("xpanel") << nul({"PATH=/usr/bin", "XPANEL_GAME=app:org.example.Game.desktop"})
                                << "app:org.example.Game.desktop";
        QTest::newRow("steam wins") << nul({"XPANEL_GAME=app:launcher.desktop", "SteamAppId=620"}) << "steam:620";
        QTest::newRow("bad marker") << nul({"XPANEL_GAME=whatever"}) << "";
        QTest::newRow("none") << nul({"HOME=/home/u", "PATH=/usr/bin"}) << "";
    }
    void environ()
    {
        QFETCH(QByteArray, env);
        QFETCH(QString, id);
        QCOMPARE(gameIdFromEnviron(env), id);
    }

    void tools()
    {
        QVERIFY(isSteamToolName("Proton Experimental"));
        QVERIFY(isSteamToolName("Proton 9.0"));
        QVERIFY(isSteamToolName("Steam Linux Runtime 3.0 (sniper)"));
        QVERIFY(isSteamToolName("Steamworks Common Redistributables"));
        QVERIFY(!isSteamToolName("Portal 2"));
        QVERIFY(!isSteamToolName("The Proton Game"));
        QVERIFY(isIgnoredSteamAppId("0"));
        QVERIFY(isIgnoredSteamAppId("769"));
        QVERIFY(isIgnoredSteamAppId("1628350"));
        QVERIFY(isIgnoredSteamAppId("1493710"));
        QVERIFY(isIgnoredSteamAppId(""));
        QVERIFY(!isIgnoredSteamAppId("620"));
    }

    void settings()
    {
        QCOMPARE(profileSettings({{"opmode", ""}, {"control", ""}, {"tdp", 0}}), QVariantMap());
        QVERIFY(!hasSettings({{"opmode", ""}, {"tdp", 12}})); // TDP alone needs Manual
        QCOMPARE(profileSettings({{"opmode", "silent"}, {"tdp", 12}, {"control", "desktop"}, {"fan", "quiet"}}),
                 (QVariantMap{{"opmode", "silent"}, {"preset", "desktop"}, {"fan", "quiet"}}));
        QCOMPARE(profileSettings({{"opmode", "manual"}, {"tdp", 12}, {"tdpFast", 20}}),
                 (QVariantMap{{"opmode", "manual"}, {"tdp", 12}, {"tdpFast", 20}}));
    }

    void restore()
    {
        const QVariantMap snapshot{{"opmode", "turbo"}, {"tdp", 25}, {"fan", "auto"}, {"preset", "gamepad"}};
        const QVariantMap applied{{"opmode", "manual"}, {"tdp", 12}, {"fan", "quiet"}, {"preset", "desktop"}};
        // nothing changed during the game: everything goes back (15.0 from JSON == 15)
        QCOMPARE(restoreChanges(snapshot, applied,
                                {{"opmode", "manual"}, {"tdp", 12.0}, {"fan", "quiet"}, {"preset", "desktop"}}),
                 snapshot);
        // the user picked Max fan and 18 W during the game: those stay
        QCOMPARE(restoreChanges(snapshot, applied,
                                {{"opmode", "manual"}, {"tdp", 18}, {"fan", "max"}, {"preset", "desktop"}}),
                 (QVariantMap{{"opmode", "turbo"}, {"preset", "gamepad"}}));
        // unknown current value (daemon did not report it): restore
        QCOMPARE(restoreChanges({{"fan", "auto"}}, {{"fan", "quiet"}}, {}), (QVariantMap{{"fan", "auto"}}));
        // no snapshot value: cannot restore
        QCOMPARE(restoreChanges({}, {{"fan", "quiet"}}, {{"fan", "quiet"}}), QVariantMap());
        // already back to the old value: nothing to send
        QCOMPARE(restoreChanges({{"fan", "auto"}}, {{"fan", "quiet"}}, {{"fan", "auto"}}), QVariantMap());
    }
};

QTEST_GUILESS_MAIN(TestProfileRules)
#include "tst_profilerules.moc"
