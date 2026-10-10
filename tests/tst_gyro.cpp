// Unit tests for the gyro: the pure math (daemon/GyroMath.*) and the IIO reader (daemon/Imu.*)
// against a fake sysfs tree (XPANEL_IIO_ROOT / XPANEL_IIO_DEV), by polling and by buffer.
#include "GyroMath.h"
#include "Imu.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstring>

using namespace Gyro;

static constexpr double kRad = 3.14159265358979323846 / 180.0; // 1 deg in rad

static void put(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

static QByteArray get(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray("<missing>");
}

class TestGyro : public QObject {
    Q_OBJECT

private slots:
    void mountMatrix()
    {
        Mat3 m;
        QVERIFY(parseMountMatrix("0, 1, 0; -1, 0, 0; 0, 0, 1\n", &m));
        const Vec3 v = apply(m, {1, 2, 3});
        QCOMPARE(v.x, 2.0);
        QCOMPARE(v.y, -1.0);
        QCOMPARE(v.z, 3.0);
        QVERIFY(parseMountMatrix("1.5,0,0;0,1,0;0,0,1", &m));
        QCOMPARE(apply(m, {2, 0, 0}).x, 3.0);
        Mat3 keep;
        QVERIFY(!parseMountMatrix("1, 0, 0; 0, 1, 0", &keep));
        QVERIFY(!parseMountMatrix("1, 0; 0, 1, 0; 0, 0, 1", &keep));
        QVERIFY(!parseMountMatrix("a, 0, 0; 0, 1, 0; 0, 0, 1", &keep));
        QVERIFY(!parseMountMatrix("", &keep));
        QCOMPARE(apply(keep, {1, 2, 3}).y, 2.0); // unchanged: identity
    }

    void scanType()
    {
        ScanType t = parseScanType("le:s16/16>>0\n");
        QVERIFY(t.valid);
        QVERIFY(!t.bigEndian);
        QVERIFY(t.isSigned);
        QCOMPARE(t.realBits, 16);
        QCOMPARE(t.storageBits, 16);
        t = parseScanType("be:u12/16>>4");
        QVERIFY(t.valid && t.bigEndian && !t.isSigned);
        QCOMPARE(t.shift, 4);
        QVERIFY(parseScanType("le:s64/64>>0").valid);
        QVERIFY(!parseScanType("le:s16/12>>0").valid);
        QVERIFY(!parseScanType("le:s16/16X2>>0").valid);
        QVERIFY(!parseScanType("garbage").valid);
    }

    void decodeValues()
    {
        const unsigned char ffff[2] = {0xff, 0xff};
        QCOMPARE(decode(ffff, parseScanType("le:s16/16>>0")), qint64(-1));
        QCOMPARE(decode(ffff, parseScanType("le:u16/16>>0")), qint64(65535));
        const unsigned char le[2] = {0x34, 0x12};
        QCOMPARE(decode(le, parseScanType("le:s16/16>>0")), qint64(0x1234));
        const unsigned char be[2] = {0x12, 0x34};
        QCOMPARE(decode(be, parseScanType("be:u12/16>>4")), qint64(0x123));
        const unsigned char neg[2] = {0xff, 0xf0};
        QCOMPARE(decode(neg, parseScanType("be:s12/16>>4")), qint64(-1));
        const unsigned char ts[8] = {0x00, 0xca, 0x9a, 0x3b, 0, 0, 0, 0}; // 1e9 little endian
        QCOMPARE(decode(ts, parseScanType("le:s64/64>>0")), qint64(1000000000));
    }

    void layout()
    {
        const ScanType s16 = parseScanType("le:s16/16>>0"), s64 = parseScanType("le:s64/64>>0");
        QList<ScanChannel> c{{6, s64, 0}, {4, s16, 0}, {3, s16, 0}, {5, s16, 0}};
        QCOMPARE(layoutScan(c), 16);
        QCOMPARE(c[0].index, 3);
        QCOMPARE(c[0].offset, 0);
        QCOMPARE(c[1].offset, 2);
        QCOMPARE(c[2].offset, 4);
        QCOMPARE(c[3].offset, 8); // aligned to 8
        QList<ScanChannel> three{{0, s16, 0}, {1, s16, 0}, {2, s16, 0}};
        QCOMPARE(layoutScan(three), 6);
        QList<ScanChannel> bad{{0, ScanType(), 0}};
        QCOMPARE(layoutScan(bad), 0);
    }

    void frequency()
    {
        const QString bmi = "0.781250 1.562500 3.125000 6.250000 12.500000 25.000000 50.000000 100.000000 "
                            "200.000000 400.000000 800.000000 1600.000000 3200.000000 6400.000000";
        QCOMPARE(chooseFrequency(bmi), 200.0);
        QCOMPARE(chooseFrequency("104 208 416 833"), 208.0);
        QCOMPARE(chooseFrequency("25 50 100"), 100.0); // none >= 200: the fastest
        QCOMPARE(chooseFrequency(""), 0.0);
    }

    void axes()
    {
        Settings s;
        const Vec3 zero;
        // yaw: +y rotation (turning left) -> aim left
        Aim a = aimRate({0, 10 * kRad, 0}, zero, s);
        QVERIFY(qAbs(a.h + 10) < 1e-9);
        QVERIFY(qAbs(a.v) < 1e-9);
        // roll is ignored with Yaw, used with Roll
        QVERIFY(qAbs(aimRate({0, 0, 10 * kRad}, zero, s).h) < 1e-9);
        s.axis = TurnAxis::Roll;
        QVERIFY(qAbs(aimRate({0, 0, 10 * kRad}, zero, s).h + 10) < 1e-9);
        QVERIFY(qAbs(aimRate({0, 10 * kRad, 0}, zero, s).h) < 1e-9);
        // pitch: +x rotation (top away from the player) -> aim up (negative v)
        s.axis = TurnAxis::Yaw;
        QVERIFY(qAbs(aimRate({10 * kRad, 0, 0}, zero, s).v + 10) < 1e-9);
        // inverted
        s.invertX = s.invertY = true;
        a = aimRate({10 * kRad, 10 * kRad, 0}, zero, s);
        QVERIFY(qAbs(a.h - 10) < 1e-9);
        QVERIFY(qAbs(a.v - 10) < 1e-9);
        // bias removed: a resting sensor with an offset gives nothing
        s = Settings();
        const Vec3 bias{0.01, -0.02, 0.005};
        a = aimRate(bias, bias, s);
        QCOMPARE(a.h, 0.0);
        QCOMPARE(a.v, 0.0);
        // noise filter: below 0.5 deg/s nothing, faded in up to 1 deg/s
        QCOMPARE(aimRate({0, 0.4 * kRad, 0}, zero, s).h, 0.0);
        QVERIFY(qAbs(aimRate({0, 0.75 * kRad, 0}, zero, s).h + 0.375) < 1e-9);
        QVERIFY(qAbs(aimRate({0, 2 * kRad, 0}, zero, s).h + 2) < 1e-9);
    }

    void mouse()
    {
        Settings s;
        MouseAccumulator acc;
        // 2 deg/s for 64 samples of 1/128 s = 1 deg -> 20 px at 20 px/deg (0.3125 px per sample add up)
        int sumX = 0, sumY = 0;
        for (int i = 0; i < 64; ++i) {
            int dx = 0, dy = 0;
            acc.add({2, -2}, 1.0 / 128, s, &dx, &dy);
            QVERIFY(qAbs(dx) <= 1 && qAbs(dy) <= 1);
            sumX += dx;
            sumY += dy;
        }
        QCOMPARE(sumX, 20);
        QCOMPARE(sumY, -20);
        // sensitivity scales each axis on its own
        s.sensX = 200;
        s.sensY = 50;
        acc.reset();
        int dx = 0, dy = 0;
        acc.add({10, 10}, 0.5, s, &dx, &dy);
        QCOMPARE(dx, 200);
        QCOMPARE(dy, 50);
    }

    void stick()
    {
        Settings s;
        Stick st = toStick({kStickFullDegPerSec / 2, 0}, s);
        QVERIFY(qAbs(st.x - 0.5) < 1e-9);
        QCOMPARE(st.y, 0.0);
        s.sensX = 200;
        QVERIFY(qAbs(toStick({kStickFullDegPerSec / 2, 0}, s).x - 1.0) < 1e-9);
        st = toStick({1000, 1000}, s); // clamped to the circle
        QVERIFY(qAbs(std::hypot(st.x, st.y) - 1.0) < 1e-9);
        QVERIFY(st.x > 0 && st.y > 0);
        st = combine({0.8, 0}, {0.5, 0});
        QVERIFY(qAbs(st.x - 1.0) < 1e-9);
        st = combine({0.5, 0}, {-0.25, 0.1});
        QVERIFY(qAbs(st.x - 0.25) < 1e-9);
        QVERIFY(qAbs(st.y - 0.1) < 1e-9);
    }

    // ---- the IIO reader against a fake sysfs tree ----

    void noDevice()
    {
        QTemporaryDir root;
        qputenv("XPANEL_IIO_ROOT", root.path().toLocal8Bit());
        put(root.path() + "/iio:device0/name", "accel-only");
        put(root.path() + "/iio:device0/in_accel_x_raw", "1");
        Imu imu;
        QCOMPARE(imu.stateName(), QString("no device"));
        imu.setRunning(true);
        QCOMPARE(imu.stateName(), QString("no device"));
        imu.setRunning(false);
    }

    void polling()
    {
        QTemporaryDir root;
        qputenv("XPANEL_IIO_ROOT", root.path().toLocal8Bit());
        qputenv("XPANEL_IIO_DEV", (root.path() + "/dev").toLocal8Bit());
        // an unrelated gyro first: the BMI323 is preferred
        const QString other = root.path() + "/iio:device0/";
        put(other + "name", "some-gyro");
        put(other + "in_anglvel_x_raw", "0");
        const QString d = root.path() + "/iio:device1/";
        put(d + "name", "bmi323-imu\n");
        put(d + "in_anglvel_x_raw", "100\n");
        put(d + "in_anglvel_y_raw", "-200\n");
        put(d + "in_anglvel_z_raw", "300\n");
        put(d + "in_anglvel_scale", "0.001065264\n");
        put(d + "in_mount_matrix", "0, 1, 0; -1, 0, 0; 0, 0, 1\n");
        put(d + "in_anglvel_sampling_frequency", "25.000000\n");
        put(d + "in_anglvel_sampling_frequency_available", "12.500000 25.000000 50.000000 100.000000 200.000000 400.000000\n");

        Imu imu;
        QCOMPARE(imu.stateName(), QString("ready"));
        QSignalSpy spy(&imu, &Imu::sample);
        imu.setRunning(true);
        QCOMPARE(imu.stateName(), QString("active"));
        QCOMPARE(imu.method(), QString("poll"));
        QCOMPARE(get(d + "in_anglvel_sampling_frequency"), QByteArray("200"));
        QVERIFY(spy.wait(500));
        const QList<QVariant> s = spy.first();
        const double k = 0.001065264;
        // mount matrix: x' = y, y' = -x, z' = z
        QVERIFY(qAbs(s[0].toDouble() - (-200 * k)) < 1e-12);
        QVERIFY(qAbs(s[1].toDouble() - (-100 * k)) < 1e-12);
        QVERIFY(qAbs(s[2].toDouble() - (300 * k)) < 1e-12);
        QVERIFY(s[3].toDouble() >= 0 && s[3].toDouble() <= 0.05);
        // new values are read on the next poll (the files stay open)
        put(d + "in_anglvel_x_raw", "-7\n");
        spy.clear();
        QVERIFY(spy.wait(500));
        QVERIFY(qAbs(spy.last()[1].toDouble() - 7 * k) < 1e-12);
        imu.setRunning(false);
        QCOMPARE(imu.stateName(), QString("ready"));
        QCOMPARE(get(d + "in_anglvel_sampling_frequency"), QByteArray("25.000000")); // put back
    }

    void buffer()
    {
        QTemporaryDir root;
        qputenv("XPANEL_IIO_ROOT", root.path().toLocal8Bit());
        qputenv("XPANEL_IIO_DEV", (root.path() + "/dev").toLocal8Bit());
        const QString d = root.path() + "/iio:device0/";
        put(d + "name", "bmi323-imu");
        put(d + "in_anglvel_x_raw", "0");
        put(d + "in_anglvel_y_raw", "0");
        put(d + "in_anglvel_z_raw", "0");
        put(d + "in_anglvel_scale", "0.001");
        put(d + "in_anglvel_sampling_frequency", "25.000000");
        put(d + "in_anglvel_sampling_frequency_available", "25.000000 50.000000 100.000000 200.000000 400.000000");
        put(d + "buffer/enable", "1"); // stale: nobody has the device open
        put(d + "buffer/length", "4");
        put(d + "trigger/current_trigger", "");
        const QString se = d + "scan_elements/";
        put(se + "in_accel_x_en", "1");
        put(se + "in_accel_x_index", "0");
        put(se + "in_accel_x_type", "le:s16/16>>0");
        const char *ax[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            put(se + QString("in_anglvel_%1_en").arg(ax[i]), "0");
            put(se + QString("in_anglvel_%1_index").arg(ax[i]), QByteArray::number(3 + i));
            put(se + QString("in_anglvel_%1_type").arg(ax[i]), "le:s16/16>>0");
        }
        put(se + "in_timestamp_en", "0");
        put(se + "in_timestamp_index", "6");
        put(se + "in_timestamp_type", "le:s64/64>>0");
        put(root.path() + "/trigger0/name", "something-else");
        put(root.path() + "/trigger1/name", "bmi323-imu-trig-0");
        QDir().mkpath(root.path() + "/dev");
        const QByteArray fifo = (root.path() + "/dev/iio:device0").toLocal8Bit();
        QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
        const int w = ::open(fifo.constData(), O_RDWR | O_NONBLOCK); // keeps a writer on the pipe

        Imu imu;
        QSignalSpy spy(&imu, &Imu::sample);
        imu.setRunning(true);
        QCOMPARE(imu.stateName(), QString("active"));
        QCOMPARE(imu.method(), QString("buffer"));
        QCOMPARE(get(d + "buffer/enable"), QByteArray("1"));
        QCOMPARE(get(d + "buffer/length"), QByteArray("64"));
        QCOMPARE(get(d + "trigger/current_trigger"), QByteArray("bmi323-imu-trig-0"));
        QCOMPARE(get(se + "in_accel_x_en"), QByteArray("0"));
        QCOMPARE(get(se + "in_anglvel_y_en"), QByteArray("1"));
        QCOMPARE(get(se + "in_timestamp_en"), QByteArray("1"));
        QCOMPARE(get(d + "in_anglvel_sampling_frequency"), QByteArray("200"));

        // two records: x, y, z (s16), padding, timestamp (s64) = 16 bytes each
        auto record = [](qint16 x, qint16 y, qint16 z, qint64 ts) {
            QByteArray r(16, 0);
            memcpy(r.data(), &x, 2);
            memcpy(r.data() + 2, &y, 2);
            memcpy(r.data() + 4, &z, 2);
            memcpy(r.data() + 8, &ts, 8);
            return r;
        };
        const QByteArray data = record(1000, -2000, 3, 1000000000) + record(10, 20, 30, 1005000000);
        QCOMPARE(::write(w, data.constData(), size_t(data.size())), ssize_t(data.size()));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 1000);
        QVERIFY(qAbs(spy[0][0].toDouble() - 1.0) < 1e-12);
        QVERIFY(qAbs(spy[0][1].toDouble() + 2.0) < 1e-12);
        QVERIFY(qAbs(spy[0][2].toDouble() - 0.003) < 1e-12);
        QVERIFY(qAbs(spy[0][3].toDouble() - 1.0 / 200) < 1e-12); // first record: 1 / rate
        QVERIFY(qAbs(spy[1][0].toDouble() - 0.01) < 1e-12);
        QVERIFY(qAbs(spy[1][3].toDouble() - 0.005) < 1e-12);     // from the timestamps

        imu.setRunning(false);
        ::close(w);
        // everything put back
        QCOMPARE(get(d + "buffer/enable"), QByteArray("0"));
        QCOMPARE(get(d + "buffer/length"), QByteArray("4"));
        QCOMPARE(get(d + "trigger/current_trigger"), QByteArray(""));
        QCOMPARE(get(se + "in_accel_x_en"), QByteArray("1"));
        QCOMPARE(get(se + "in_anglvel_y_en"), QByteArray("0"));
        QCOMPARE(get(se + "in_timestamp_en"), QByteArray("0"));
        QCOMPARE(get(d + "in_anglvel_sampling_frequency"), QByteArray("25.000000"));
    }

    void bufferWithoutTrigger()
    {
        // no trigger (IRQ not wired): falls back to polling and leaves the scan elements alone
        QTemporaryDir root;
        qputenv("XPANEL_IIO_ROOT", root.path().toLocal8Bit());
        qputenv("XPANEL_IIO_DEV", (root.path() + "/dev").toLocal8Bit());
        const QString d = root.path() + "/iio:device0/";
        put(d + "name", "bmi323-imu");
        for (const char *a : {"x", "y", "z"})
            put(d + QString("in_anglvel_%1_raw").arg(a), "5");
        put(d + "in_anglvel_scale", "0.5");
        put(d + "buffer/enable", "0");
        put(d + "trigger/current_trigger", "");
        put(d + "scan_elements/in_anglvel_x_en", "0");
        QDir().mkpath(root.path() + "/dev");
        const QByteArray fifo = (root.path() + "/dev/iio:device0").toLocal8Bit();
        QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
        Imu imu;
        QSignalSpy spy(&imu, &Imu::sample);
        imu.setRunning(true);
        QCOMPARE(imu.method(), QString("poll"));
        QVERIFY(spy.wait(500));
        QCOMPARE(spy.first()[0].toDouble(), 2.5);
        QCOMPARE(get(d + "scan_elements/in_anglvel_x_en"), QByteArray("0"));
        imu.setRunning(false);
    }
};

QTEST_GUILESS_MAIN(TestGyro)
#include "tst_gyro.moc"
