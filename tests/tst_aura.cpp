// Unit tests for the Aura lighting packets and the effects that follow the system (daemon/Aura.h).
#include "Aura.h"

#include <QTest>

using namespace Aura;

static QByteArray hex(const QByteArray &b)
{
    return b.left(14).toHex(' ');
}

class TestAura : public QObject {
    Q_OBJECT

    static Params params(const char *effect)
    {
        Params p;
        p.effect = effect;
        p.color = "#102030";
        p.color2 = "#a0b0c0";
        p.brightness = 2;
        p.speed = "high";
        return p;
    }

private slots:
    void sequence()
    {
        const QList<QByteArray> c = packets(params("static"));
        QCOMPARE(c.size(), 5);
        for (const QByteArray &b : c) {
            QCOMPARE(b.size(), 64);
            QCOMPARE(uchar(b[0]), uchar(0x5A));
        }
        QCOMPARE(c[0].mid(1, 14), QByteArray("ASUS Tech.Inc."));
        QCOMPARE(hex(c[1]), QByteArray("5a ba c5 c4 02 00 00 00 00 00 00 00 00 00"));
        QCOMPARE(uchar(c[3][1]), uchar(0xB5));
        QCOMPARE(uchar(c[4][1]), uchar(0xB4));
    }

    // The effects that worked before 1.2 keep their exact bytes.
    void existing_effects_unchanged()
    {
        QCOMPARE(hex(packets(params("static"))[2]), QByteArray("5a b3 00 00 10 20 30 00 00 00 00 00 00 00"));
        QCOMPARE(hex(packets(params("breathing"))[2]), QByteArray("5a b3 00 01 10 20 30 f5 00 00 00 00 00 00"));
        QCOMPARE(hex(packets(params("colorcycle"))[2]), QByteArray("5a b3 00 02 10 20 30 f5 00 00 00 00 00 00"));
        QCOMPARE(hex(packets(params("rainbow"))[2]), QByteArray("5a b3 00 03 00 00 00 f5 00 00 00 00 00 00"));
        Params slow = params("breathing");
        slow.speed = "low";
        QCOMPARE(uchar(packets(slow)[2][7]), uchar(0xE1));
        slow.speed = "medium";
        QCOMPARE(uchar(packets(slow)[2][7]), uchar(0xEB));
        // rainbow wave "right" is direction 0, as before
        Params r = params("rainbow");
        r.direction = "right";
        QCOMPARE(uchar(packets(r)[2][8]), uchar(0));
    }

    void off()
    {
        const QList<QByteArray> c = packets(params("off"));
        QCOMPARE(c.size(), 2); // handshake + brightness 0, no mode
        QCOMPARE(hex(c[1]), QByteArray("5a ba c5 c4 00 00 00 00 00 00 00 00 00 00"));
    }

    void dual_breathing()
    {
        const QByteArray m = packets(params("dualbreathing"))[2];
        QCOMPARE(hex(m), QByteArray("5a b3 00 01 10 20 30 f5 00 01 a0 b0 c0 00"));
    }

    void strobe()
    {
        QCOMPARE(hex(packets(params("strobe"))[2]), QByteArray("5a b3 00 0a 10 20 30 f5 00 00 00 00 00 00"));
    }

    void rainbow_directions_data()
    {
        QTest::addColumn<QString>("direction");
        QTest::addColumn<int>("byte");
        QTest::newRow("right") << "right" << 0;
        QTest::newRow("left") << "left" << 1;
        QTest::newRow("up") << "up" << 2;
        QTest::newRow("down") << "down" << 3;
        QTest::newRow("unknown") << "sideways" << 0;
    }
    void rainbow_directions()
    {
        QFETCH(QString, direction);
        QFETCH(int, byte);
        Params p = params("rainbow");
        p.direction = direction;
        QCOMPARE(int(packets(p)[2][8]), byte);
    }

    void direction_only_for_rainbow()
    {
        Params p = params("breathing");
        p.direction = "left";
        QCOMPARE(int(packets(p)[2][8]), 0);
    }

    void brightness_is_clamped()
    {
        Params p = params("static");
        p.brightness = 9;
        QCOMPARE(int(packets(p)[1][4]), 3);
        p.brightness = -2;
        QCOMPARE(packets(p).size(), 2); // brightness 0: no mode packet
    }

    void unknown_effects_have_no_packets()
    {
        QVERIFY(packets(params("disco")).isEmpty());
        QVERIFY(packets(params("battery")).isEmpty()); // resolve() first
        QVERIFY(packets(params("followmode")).isEmpty());
    }

    void parse_and_format_colors()
    {
        QCOMPARE(parseColor("#ff8000"), (Rgb{255, 128, 0}));
        QCOMPARE(toHex({255, 128, 0}), QString("#ff8000"));
        QCOMPARE(toHex({0, 5, 10}), QString("#00050a"));
        QCOMPARE(toHex(parseColor("#00c8ff")), QString("#00c8ff"));
    }

    void battery_gradient()
    {
        QCOMPARE(batteryColor(0), (Rgb{255, 0, 0}));
        QCOMPARE(batteryColor(50), (Rgb{255, 255, 0}));
        QCOMPARE(batteryColor(100), (Rgb{0, 255, 0}));
        QCOMPARE(batteryColor(25), (Rgb{255, 127, 0}));
        QCOMPARE(batteryColor(75), (Rgb{127, 255, 0}));
        QCOMPARE(batteryColor(-10), batteryColor(0));
        QCOMPARE(batteryColor(130), batteryColor(100));
        for (int p = 1; p <= 100; ++p) { // never redder after more charge
            QVERIFY(batteryColor(p).r <= batteryColor(p - 1).r);
            QVERIFY(batteryColor(p).g >= batteryColor(p - 1).g);
        }
    }

    void resolve_battery()
    {
        Params p = params("battery");
        const Params r = resolve(p, 100, "turbo");
        QCOMPARE(r.effect, QString("static"));
        QCOMPARE(r.color, QString("#00ff00"));
        QCOMPARE(r.brightness, p.brightness);
        QCOMPARE(resolve(p, 0, "turbo").color, QString("#ff0000"));
        // no battery: keep the chosen colour
        QCOMPARE(resolve(p, -1, "turbo").color, p.color);
        // the packet is a static mode packet in that colour
        QCOMPARE(hex(packets(r)[2]), QByteArray("5a b3 00 00 00 ff 00 00 00 00 00 00 00 00"));
    }

    void resolve_followmode()
    {
        const Params p = params("followmode");
        QCOMPARE(resolve(p, 50, "silent").color, QString("#0040ff"));
        QCOMPARE(resolve(p, 50, "performance").color, QString("#ffffff"));
        QCOMPARE(resolve(p, 50, "turbo").color, QString("#ff0014"));
        QCOMPARE(resolve(p, 50, "manual").color, p.color); // the chosen colour
        QCOMPARE(resolve(p, 50, "turbo").effect, QString("static"));
    }

    void resolve_leaves_other_effects()
    {
        for (const char *e : {"off", "static", "breathing", "dualbreathing", "colorcycle", "rainbow", "strobe"}) {
            const Params p = params(e), r = resolve(p, 10, "silent");
            QCOMPARE(r.effect, p.effect);
            QCOMPARE(r.color, p.color);
        }
        QVERIFY(isDynamic("battery"));
        QVERIFY(isDynamic("followmode"));
        QVERIFY(!isDynamic("static"));
    }

    void battery_reapply_threshold()
    {
        QVERIFY(!batteryMoved(80, 80));
        QVERIFY(!batteryMoved(80, 84));
        QVERIFY(!batteryMoved(80, 76));
        QVERIFY(batteryMoved(80, 85));
        QVERIFY(batteryMoved(80, 75));
        QVERIFY(batteryMoved(-1, 50)); // never applied yet
        QVERIFY(!batteryMoved(-1, -1));
    }
};

QTEST_GUILESS_MAIN(TestAura)
#include "tst_aura.moc"
