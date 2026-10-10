// Unit tests for the desktop detection and capabilities (src/Desktop.*).
#include "Desktop.h"

#include <QSet>
#include <QTest>

using Desktop::Kind;

Q_DECLARE_METATYPE(Desktop::Kind)

// "KEY=value;KEY2=value2" -> environment
static QProcessEnvironment env(const QString &spec)
{
    QProcessEnvironment e;
    for (const QString &kv : spec.split(';', Qt::SkipEmptyParts))
        e.insert(kv.section('=', 0, 0), kv.section('=', 1));
    return e;
}

class TestDesktop : public QObject {
    Q_OBJECT

private slots:
    void detect_data()
    {
        QTest::addColumn<QString>("environment");
        QTest::addColumn<Kind>("kind");
        QTest::newRow("empty") << "" << Kind::Other;
        QTest::newRow("kde") << "XDG_CURRENT_DESKTOP=KDE" << Kind::KDE;
        QTest::newRow("kde session only") << "XDG_SESSION_DESKTOP=KDE" << Kind::KDE;
        QTest::newRow("plasma session") << "DESKTOP_SESSION=plasma" << Kind::KDE;
        QTest::newRow("plasma session path") << "DESKTOP_SESSION=/usr/share/wayland-sessions/plasmawayland" << Kind::KDE;
        QTest::newRow("gnome") << "XDG_CURRENT_DESKTOP=GNOME" << Kind::GNOME;
        QTest::newRow("ubuntu") << "XDG_CURRENT_DESKTOP=ubuntu:GNOME" << Kind::GNOME;
        QTest::newRow("gnome classic") << "XDG_CURRENT_DESKTOP=GNOME-Classic:GNOME" << Kind::GNOME;
        QTest::newRow("gnome lowercase") << "XDG_CURRENT_DESKTOP=gnome" << Kind::GNOME;
        QTest::newRow("gnome session") << "XDG_SESSION_DESKTOP=gnome" << Kind::GNOME;
        QTest::newRow("ubuntu session") << "DESKTOP_SESSION=ubuntu" << Kind::GNOME;
        QTest::newRow("gamescope") << "XDG_CURRENT_DESKTOP=gamescope" << Kind::Gamescope;
        QTest::newRow("gamescope with display") << "XDG_CURRENT_DESKTOP=gamescope;GAMESCOPE_WAYLAND_DISPLAY=gamescope-0"
                                                << Kind::Gamescope;
        QTest::newRow("gamescope display only") << "GAMESCOPE_WAYLAND_DISPLAY=gamescope-0" << Kind::Gamescope;
        QTest::newRow("gamescope session") << "XDG_SESSION_DESKTOP=gamescope-wayland" << Kind::Gamescope;
        // a known desktop wins over a stale / nested gamescope variable
        QTest::newRow("kde nested gamescope") << "XDG_CURRENT_DESKTOP=KDE;GAMESCOPE_WAYLAND_DISPLAY=gamescope-0" << Kind::KDE;
        QTest::newRow("gnome nested gamescope") << "XDG_CURRENT_DESKTOP=GNOME;GAMESCOPE_WAYLAND_DISPLAY=gamescope-0"
                                                << Kind::GNOME;
        QTest::newRow("sway") << "XDG_CURRENT_DESKTOP=sway" << Kind::Other;
        // XDG_CURRENT_DESKTOP is the authority: the session name is only a fallback
        QTest::newRow("unknown current, gnome session") << "XDG_CURRENT_DESKTOP=Hyprland;XDG_SESSION_DESKTOP=gnome"
                                                        << Kind::Other;
        QTest::newRow("override gnome") << "XPANEL_DESKTOP=gnome;XDG_CURRENT_DESKTOP=KDE" << Kind::GNOME;
        QTest::newRow("override gamescope") << "XPANEL_DESKTOP=GameScope;XDG_CURRENT_DESKTOP=KDE" << Kind::Gamescope;
        QTest::newRow("override other") << "XPANEL_DESKTOP=other;XDG_CURRENT_DESKTOP=KDE" << Kind::Other;
        QTest::newRow("override kde") << "XPANEL_DESKTOP=kde;XDG_CURRENT_DESKTOP=GNOME" << Kind::KDE;
        QTest::newRow("bad override ignored") << "XPANEL_DESKTOP=foo;XDG_CURRENT_DESKTOP=KDE" << Kind::KDE;
    }
    void detect()
    {
        QFETCH(QString, environment);
        QFETCH(Kind, kind);
        QCOMPARE(Desktop::detect(env(environment)), kind);
    }

    void names()
    {
        QCOMPARE(Desktop::id(Kind::KDE), QString("kde"));
        QCOMPARE(Desktop::id(Kind::GNOME), QString("gnome"));
        QCOMPARE(Desktop::id(Kind::Gamescope), QString("gamescope"));
        QCOMPARE(Desktop::id(Kind::Other), QString("other"));
        // ids round-trip through the override
        for (Kind k : {Kind::KDE, Kind::GNOME, Kind::Gamescope, Kind::Other})
            QCOMPARE(Desktop::detect(env("XPANEL_DESKTOP=" + Desktop::id(k))), k);
    }

    void capabilities()
    {
        const auto all = [](const QString &) { return true; };
        const auto none = [](const QString &) { return false; };

        const Desktop::Caps kde = Desktop::capabilities(Kind::KDE, none, false);
        QVERIFY(kde.refreshRate && kde.displayMode && kde.colorPresets && kde.wideColor && kde.shortcuts);
        QVERIFY(kde.showDesktop && kde.screenshot && kde.logout && kde.userSettings && kde.tray);
        QCOMPARE(kde.screenshotTool, QString("spectacle"));

        const Desktop::Caps gnome = Desktop::capabilities(Kind::GNOME, all, true);
        QVERIFY(!gnome.refreshRate && !gnome.displayMode && !gnome.wideColor && !gnome.shortcuts && !gnome.showDesktop);
        QVERIFY(gnome.colorPresets && gnome.screenshot && gnome.logout && gnome.userSettings && gnome.tray);
        QCOMPARE(gnome.screenshotTool, QString("gnome-screenshot"));
        const Desktop::Caps bareGnome = Desktop::capabilities(Kind::GNOME, none, true);
        QVERIFY(!bareGnome.colorPresets && !bareGnome.screenshot && !bareGnome.userSettings && bareGnome.logout);

        const Desktop::Caps gs = Desktop::capabilities(Kind::Gamescope, all, true);
        QVERIFY(!gs.refreshRate && !gs.displayMode && !gs.colorPresets && !gs.wideColor && !gs.shortcuts);
        QVERIFY(!gs.showDesktop && !gs.screenshot && !gs.logout && !gs.userSettings && !gs.tray);

        const Desktop::Caps other = Desktop::capabilities(Kind::Other, [](const QString &t) { return t == "grim" || t == "loginctl"; }, true);
        QVERIFY(other.screenshot && other.logout && !other.colorPresets && !other.refreshRate && other.tray);
        QCOMPARE(other.screenshotTool, QString("grim"));
        QVERIFY(!Desktop::capabilities(Kind::Other, all, false).logout); // no session to end
    }

    void terminal()
    {
        const QStringList cmd{"fwupdmgr", "update"};
        QCOMPARE(Desktop::terminalCommand("konsole", cmd), (QStringList{"--hold", "-e", "fwupdmgr", "update"}));
        QCOMPARE(Desktop::terminalCommand("xterm", cmd), (QStringList{"-hold", "-e", "fwupdmgr", "update"}));
        const QStringList kgx = Desktop::terminalCommand("kgx", cmd);
        QCOMPARE(kgx.first(), QString("--"));
        QCOMPARE(kgx.mid(1, 2), (QStringList{"sh", "-c"}));
        QCOMPARE(kgx.mid(kgx.size() - 3), (QStringList{"sh", "fwupdmgr", "update"})); // $0 then the arguments
        QCOMPARE(Desktop::terminalCommand("x-terminal-emulator", cmd).first(), QString("-e"));
    }
};

QTEST_GUILESS_MAIN(TestDesktop)
#include "tst_desktop.moc"
