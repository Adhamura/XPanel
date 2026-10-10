// Unit tests for the model table and detection (daemon/Model.h).
#include "Model.h"

#include <QTest>

class TestModel : public QObject {
    Q_OBJECT

private slots:
    void detect_data()
    {
        QTest::addColumn<QString>("board");
        QTest::addColumn<int>("pid");
        QTest::addColumn<QString>("id");
        // by DMI board name (the controller id does not matter then)
        QTest::newRow("Ally") << "RC71L" << 0x1abe << "ally";
        QTest::newRow("Ally, no pad") << "RC71L" << 0 << "ally";
        QTest::newRow("Ally X") << "RC72LA" << 0x1b4c << "allyx";
        QTest::newRow("Xbox Ally") << "RC73YA" << 0x1b4c << "xboxally";
        QTest::newRow("Xbox Ally X") << "RC73XA" << 0x1b4c << "xboxallyx";
        QTest::newRow("Xbox Ally X, no pad") << "RC73XA" << 0 << "xboxallyx";
        QTest::newRow("spaces, lower case") << "  rc73ya\n" << 0 << "xboxally";
        // by the controller when the board name is unknown or missing
        QTest::newRow("pid 1abe") << "" << 0x1abe << "ally";
        QTest::newRow("pid 1b4c = Ally X") << "" << 0x1b4c << "allyx";
        QTest::newRow("other board, pid 1b4c") << "Jupiter" << 0x1b4c << "allyx";
        // nothing known
        QTest::newRow("unknown") << "Jupiter" << 0 << "unknown";
        QTest::newRow("empty") << "" << 0 << "unknown";
        QTest::newRow("other pid") << "" << 0x1866 << "unknown";
        QTest::newRow("RC73 alone") << "RC73" << 0 << "unknown";
    }
    void detect()
    {
        QFETCH(QString, board);
        QFETCH(int, pid);
        QFETCH(QString, id);
        QCOMPARE(QString::fromLatin1(Model::detect(board, quint16(pid)).id), id);
    }

    void names()
    {
        QCOMPARE(QString::fromLatin1(Model::detect("RC73YA", 0).name), QString("ROG Xbox Ally"));
        QCOMPARE(QString::fromLatin1(Model::detect("RC73XA", 0).name), QString("ROG Xbox Ally X"));
    }

    void isController_data()
    {
        QTest::addColumn<int>("vendor");
        QTest::addColumn<int>("product");
        QTest::addColumn<bool>("yes");
        QTest::newRow("Ally") << 0x0b05 << 0x1abe << true;
        QTest::newRow("Ally X / Xbox Ally") << 0x0b05 << 0x1b4c << true;
        QTest::newRow("XPanel virtual pad") << 0x045e << 0x028e << false;
        QTest::newRow("Microsoft pad with an Ally id") << 0x045e << 0x1b4c << false;
        QTest::newRow("ASUS keyboard") << 0x0b05 << 0x1866 << false;
        QTest::newRow("ASUS Raikiri pad") << 0x0b05 << 0x1a38 << false;
        QTest::newRow("ASUS, product 0") << 0x0b05 << 0 << false;
        QTest::newRow("nothing") << 0 << 0 << false;
    }
    void isController()
    {
        QFETCH(int, vendor);
        QFETCH(int, product);
        QFETCH(bool, yes);
        QCOMPARE(Model::isController(quint16(vendor), quint16(product)), yes);
    }

    // Each model's Operating Mode SPL lies in its own range (boost limits are clamped by the
    // caller), and every range is ordered.
    void consistent()
    {
        int n = 0;
        const Model::Info *t = Model::models(&n);
        QVERIFY(n >= 4);
        auto in = [](int w, const Model::Range &r) { return r.min <= w && w <= r.max; };
        for (int i = 0; i < n; ++i) {
            const Model::Info &m = t[i];
            QVERIFY2(m.controllerPid != 0, m.id);
            for (const Model::Ranges *r : {&m.ac, &m.battery})
                for (const Model::Range *x : {&r->spl, &r->sppt, &r->fppt})
                    QVERIFY2(0 < x->min && x->min <= x->max, m.id);
            for (const Model::Watts *w : {&m.silent, &m.performance, &m.turboBattery}) {
                QVERIFY2(w->spl <= w->sppt && w->sppt <= w->fppt, m.id);
                QVERIFY2(in(w->spl, m.battery.spl), m.id);
            }
            const Model::Watts &w = m.turboAc;
            QVERIFY2(w.spl <= w.sppt && w.sppt <= w.fppt, m.id);
            QVERIFY2(in(w.spl, m.ac.spl), m.id);
        }
    }

    void modeWatts()
    {
        const Model::Info m = Model::detect("RC73XA", 0);
        QCOMPARE(Model::modeWatts(m, "silent", false).spl, 13);
        QCOMPARE(Model::modeWatts(m, "performance", true).spl, 17);
        QCOMPARE(Model::modeWatts(m, "turbo", false).spl, 25);
        QCOMPARE(Model::modeWatts(m, "turbo", true).spl, 35);
        const Model::Info a = Model::detect("RC73YA", 0);
        QCOMPARE(Model::modeWatts(a, "silent", false).spl, 10);
        QCOMPARE(Model::modeWatts(a, "performance", false).spl, 15);
        QCOMPARE(Model::modeWatts(a, "turbo", true).spl, 20);
    }
};

QTEST_APPLESS_MAIN(TestModel)
#include "tst_model.moc"
