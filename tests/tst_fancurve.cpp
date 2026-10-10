// Unit tests for the custom fan curve validation and conversion (daemon/FanCurve.h).
#include "FanCurve.h"

#include <QJsonDocument>
#include <QTest>

using namespace FanCurve;

static QJsonValue json(const char *text)
{
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray("{\"v\":") + text + "}", &err);
    Q_ASSERT(err.error == QJsonParseError::NoError);
    return d.object()["v"];
}

static const char *kGood = "[[40,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]";

class TestFanCurve : public QObject {
    Q_OBJECT

private slots:
    void accepts_data()
    {
        QTest::addColumn<QString>("curve");
        QTest::addColumn<bool>("ok");
        QTest::newRow("good") << kGood << true;
        QTest::newRow("whole floats") << "[[40.0,0.0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << true;
        QTest::newRow("flat duty") << "[[30,50],[40,50],[50,50],[60,50],[70,50],[80,50],[90,50],[100,50]]" << true;
        QTest::newRow("range ends") << "[[20,0],[30,0],[40,0],[50,0],[60,0],[70,0],[80,100],[105,100]]" << true;
        QTest::newRow("7 points") << "[[40,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100]]" << false;
        QTest::newRow("9 points") << "[[40,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100],[100,100]]" << false;
        QTest::newRow("empty") << "[]" << false;
        QTest::newRow("temp too low") << "[[19,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("temp too high") << "[[40,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[106,100]]" << false;
        QTest::newRow("temp equal") << "[[40,0],[50,20],[50,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("temp falling") << "[[40,0],[50,20],[60,40],[55,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("duty falling") << "[[40,0],[50,20],[60,40],[70,60],[78,55],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("duty above 100") << "[[40,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,101]]" << false;
        QTest::newRow("duty below 0") << "[[40,-1],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("fractional temp") << "[[40.5,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("fractional duty") << "[[40,0],[50,20.5],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("string number") << "[[\"40\",0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("bool") << "[[true,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("point of 3") << "[[40,0,1],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("point of 1") << "[[40],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("point not an array") << "[40,[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
        QTest::newRow("object") << "{\"a\":1}" << false;
        QTest::newRow("null") << "null" << false;
        QTest::newRow("huge") << "[[1e300,0],[50,20],[60,40],[70,60],[78,75],[85,90],[90,100],[95,100]]" << false;
    }
    void accepts()
    {
        QFETCH(QString, curve);
        QFETCH(bool, ok);
        QCOMPARE(parseCurve(json(curve.toUtf8().constData())), ok);
    }

    void parse_values()
    {
        Curve c{};
        QVERIFY(parseCurve(json(kGood), &c));
        QCOMPARE(c[0].temp, 40);
        QCOMPARE(c[0].percent, 0);
        QCOMPARE(c[4].temp, 78);
        QCOMPARE(c[4].percent, 75);
        QCOMPARE(c[7].temp, 95);
        QCOMPARE(c[7].percent, 100);
    }

    void fanCurveKey()
    {
        const QByteArray good = kGood;
        QVERIFY(valid(json("{\"cpu\":" + good + "}")));
        QVERIFY(valid(json("{\"cpu\":" + good + ",\"gpu\":" + good + "}")));
        QVERIFY(!valid(json("{}")));
        QVERIFY(!valid(json("{\"gpu\":" + good + "}")));              // cpu is required
        QVERIFY(!valid(json("{\"cpu\":" + good + ",\"gpu\":[]}")));    // a given gpu must be valid
        QVERIFY(!valid(json("{\"cpu\":" + good + ",\"mid\":" + good + "}"))); // unknown fan
        QVERIFY(!valid(json(good)));                                  // not an object
        QVERIFY(!valid(json("\"custom\"")));
        QVERIFY(!valid(json("null")));
    }

    void gpu_follows_cpu()
    {
        const QByteArray good = kGood;
        const QJsonObject shared = json("{\"cpu\":" + good + "}").toObject();
        const Curve c = cpu(shared), g = gpu(shared);
        for (int i = 0; i < kPoints; ++i) {
            QCOMPARE(g[size_t(i)].temp, c[size_t(i)].temp);
            QCOMPARE(g[size_t(i)].percent, c[size_t(i)].percent);
        }
        const QJsonObject both = json("{\"cpu\":" + good
                                      + ",\"gpu\":[[30,5],[40,10],[50,15],[60,20],[70,25],[80,30],[90,35],[100,40]]}").toObject();
        QCOMPARE(gpu(both)[0].temp, 30);
        QCOMPARE(gpu(both)[7].percent, 40);
        QCOMPARE(cpu(both)[0].temp, 40);
    }

    void fallback_is_balanced_and_valid()
    {
        const Curve b = balanced();
        QJsonArray a;
        for (const Point &p : b)
            a.append(QJsonArray{p.temp, p.percent});
        QVERIFY(parseCurve(a));
        QCOMPARE(cpu({})[3].temp, b[3].temp);
        QCOMPARE(gpu({{"cpu", QJsonArray()}})[3].percent, b[3].percent);
    }

    void pwm_conversion_data()
    {
        QTest::addColumn<int>("percent");
        QTest::addColumn<int>("pwm");
        QTest::newRow("0") << 0 << 0;
        QTest::newRow("1") << 1 << 3;
        QTest::newRow("8") << 8 << 20;     // the built-in Quiet curve is 20 /255 = 8 %
        QTest::newRow("25") << 25 << 64;
        QTest::newRow("50") << 50 << 128;
        QTest::newRow("82") << 82 << 209;
        QTest::newRow("99") << 99 << 252;
        QTest::newRow("100") << 100 << 255;
        QTest::newRow("clamped low") << -5 << 0;
        QTest::newRow("clamped high") << 150 << 255;
    }
    void pwm_conversion()
    {
        QFETCH(int, percent);
        QFETCH(int, pwm);
        QCOMPARE(toPwm(percent), pwm);
    }

    void pwm_is_monotonic()
    {
        int last = 0;
        for (int p = 0; p <= 100; ++p) {
            QVERIFY(toPwm(p) >= last);
            last = toPwm(p);
        }
    }
};

QTEST_GUILESS_MAIN(TestFanCurve)
#include "tst_fancurve.moc"
