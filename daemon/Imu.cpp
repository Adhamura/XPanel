#include "Imu.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

constexpr double kWantHz = 200;  // buffered sampling rate we ask for (lowest available >= this)
constexpr int kPollMs = 8;       // 125 Hz when polling the *_raw files
constexpr int kBusyRetryMs = 5000;
constexpr int kAbsentRetryMs = 15000;
const char *const kAxes[3] = {"x", "y", "z"};

} // namespace

Imu::Imu(QObject *parent)
    : QObject(parent),
      m_root(qEnvironmentVariable("XPANEL_IIO_ROOT", "/sys/bus/iio/devices")),
      m_devRoot(qEnvironmentVariable("XPANEL_IIO_DEV", "/dev"))
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_wanted && m_state != State::Active)
            setRunning(true);
    });
    m_poll.setTimerType(Qt::PreciseTimer);
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &Imu::poll);
    setState(find() ? State::Ready : State::NoDevice);
}

Imu::~Imu()
{
    stop();
}

QString Imu::stateName() const
{
    switch (m_state) {
    case State::Ready: return "ready";
    case State::Active: return "active";
    case State::Busy: return "busy";
    case State::NoDevice: break;
    }
    return "no device";
}

void Imu::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
}

void Imu::setRunning(bool on)
{
    m_wanted = on;
    if (!on) {
        m_retry.stop();
        stop();
        setState(find() ? State::Ready : State::NoDevice);
        return;
    }
    if (m_state == State::Active)
        return;
    if (start()) {
        setState(State::Active);
        qInfo("xpanel-daemon gyro: reading %s (%s) by %s at %g Hz", qPrintable(m_node), qPrintable(m_name),
              qPrintable(method()), m_freq);
        return;
    }
    // busy: another program has the sensor; otherwise it cannot be read (or is gone)
    setState(m_busyNow ? State::Busy : State::NoDevice);
    m_retry.start(m_busyNow ? kBusyRetryMs : kAbsentRetryMs);
}

// ---------------------------------------------------------------- device

QString Imu::attr(const QString &name) const
{
    return readText(m_dir + "/" + name);
}

bool Imu::find()
{
    if (!m_dir.isEmpty() && QFileInfo::exists(m_dir + "/name"))
        return true;
    m_dir.clear();
    const QDir root(m_root);
    QStringList nodes = root.entryList({"iio:device*"}, QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    std::sort(nodes.begin(), nodes.end(), [](const QString &a, const QString &b) {
        return a.mid(10).toInt() < b.mid(10).toInt(); // "iio:device" + N
    });
    QString pick, fallback;
    for (const QString &n : nodes) {
        const QString dir = root.absoluteFilePath(n);
        const bool gyro = QFileInfo::exists(dir + "/in_anglvel_x_raw") || QFileInfo::exists(dir + "/scan_elements/in_anglvel_x_en");
        if (!gyro)
            continue;
        if (readText(dir + "/name") == "bmi323-imu") { pick = n; break; }
        if (fallback.isEmpty())
            fallback = n;
    }
    if (pick.isEmpty())
        pick = fallback;
    if (pick.isEmpty())
        return false;
    m_node = pick;
    m_dir = root.absoluteFilePath(pick);
    m_name = attr("name");
    bool ok = false;
    m_scale = attr("in_anglvel_scale").toDouble(&ok);
    if (!ok)
        m_scale = attr("in_anglvel_x_scale").toDouble(&ok);
    if (!ok || m_scale <= 0) {
        qWarning("xpanel-daemon gyro: %s has no in_anglvel_scale, assuming rad/s per LSB = 1", qPrintable(m_node));
        m_scale = 1.0;
    }
    m_mount = Gyro::Mat3();
    for (const char *f : {"in_anglvel_mount_matrix", "in_mount_matrix", "mount_matrix"}) {
        const QString t = attr(f);
        if (!t.isEmpty() && Gyro::parseMountMatrix(t, &m_mount))
            break;
    }
    qInfo("xpanel-daemon gyro: found %s (%s)", qPrintable(m_node), qPrintable(m_name));
    return true;
}

int Imu::writeAttr(const QString &path, const QByteArray &value, bool remember)
{
    QByteArray old = readText(path).toUtf8();
    if (old == value)
        return 0;
    const int fd = ::open(path.toLocal8Bit().constData(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0)
        return errno;
    const QByteArray out = value.isEmpty() ? QByteArray("\n") : value; // "" = none (current_trigger)
    const int err = ::write(fd, out.constData(), size_t(out.size())) < 0 ? errno : 0;
    ::close(fd);
    if (err == 0 && remember
        && std::none_of(m_restore.cbegin(), m_restore.cend(), [&path](const QPair<QString, QByteArray> &r) { return r.first == path; }))
        m_restore.append({path, old});
    return err;
}

// ---------------------------------------------------------------- start / stop

bool Imu::start()
{
    m_busyNow = false;
    if (!find())
        return false;
    // ~200 Hz: per-type attribute on the BMI323, shared on other drivers
    m_freq = 0;
    for (const QString &f : {QString("in_anglvel_sampling_frequency"), QString("sampling_frequency")}) {
        if (!QFileInfo::exists(m_dir + "/" + f))
            continue;
        const double want = Gyro::chooseFrequency(attr(f + "_available"), kWantHz);
        if (want > 0 && writeAttr(m_dir + "/" + f, QByteArray::number(want)) != 0)
            qInfo("xpanel-daemon gyro: cannot set %s", qPrintable(f));
        m_freq = attr(f).toDouble();
        break;
    }
    if (startBuffer())
        return true;
    if (m_busyNow) {
        stop();
        return false;
    }
    if (startPolling())
        return true;
    stop();
    return false;
}

bool Imu::startBuffer()
{
    const int mark = m_restore.size();
    auto fail = [this, mark](const char *why, bool busy = false) {
        if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
        // undo what this step changed (the sampling frequency stays for polling)
        while (m_restore.size() > mark) {
            const auto r = m_restore.takeLast();
            writeAttr(r.first, r.second, false);
        }
        qInfo("xpanel-daemon gyro: IIO buffer not used (%s)%s", why, busy ? "" : ", polling instead");
        m_busyNow = m_busyNow || busy;
        return false;
    };
    const QString bufDir = m_dir + (QFileInfo::exists(m_dir + "/buffer/enable") ? "/buffer" : "/buffer0");
    if (!QFileInfo::exists(bufDir + "/enable") || !QFileInfo::exists(m_dir + "/scan_elements"))
        return fail("no buffer");

    // The character device is exclusive: EBUSY = another program (hhd, InputPlumber) reads it.
    m_fd = ::open((m_devRoot + "/" + m_node).toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return fail(strerror(errno), errno == EBUSY);
    // Nobody else has it open, so an enabled buffer is stale (e.g. left by a crash): reset it.
    if (readText(bufDir + "/enable") == "1" && writeAttr(bufDir + "/enable", "0", false) != 0)
        return fail("buffer enabled by someone else", true);

    // Trigger: keep one that is set, else the device's own data-ready trigger: named after the
    // device ("bmi323-imu-trig-N"), or registered by the same driver instance (same sysfs parent;
    // never a whole flat directory of siblings).
    if (attr("trigger/current_trigger").isEmpty()) {
        const QDir root(m_root);
        const QString myParent = QFileInfo(QFileInfo(m_dir).canonicalFilePath()).path();
        QString trig, sameParent;
        for (const QString &t : root.entryList({"trigger*"}, QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot)) {
            const QString dir = root.absoluteFilePath(t);
            const QString name = readText(dir + "/name");
            if (name.isEmpty())
                continue;
            if (!m_name.isEmpty() && name.startsWith(m_name)) { trig = name; break; }
            if (sameParent.isEmpty() && QFileInfo(QFileInfo(dir).canonicalFilePath()).path() == myParent
                && myParent != root.canonicalPath())
                sameParent = name;
        }
        if (trig.isEmpty())
            trig = sameParent;
        if (trig.isEmpty())
            return fail("no trigger");
        if (writeAttr(m_dir + "/trigger/current_trigger", trig.toUtf8()) != 0)
            return fail("cannot set the trigger");
    }

    // Scan elements: angular velocity x / y / z and the timestamp, nothing else.
    const QDir scan(m_dir + "/scan_elements");
    for (const QString &en : scan.entryList({"*_en"}, QDir::Files | QDir::System)) {
        const bool want = en == "in_anglvel_x_en" || en == "in_anglvel_y_en" || en == "in_anglvel_z_en" || en == "in_timestamp_en";
        if (writeAttr(scan.absoluteFilePath(en), want ? "1" : "0") != 0 && want && en != "in_timestamp_en")
            return fail("cannot enable the scan elements");
    }
    m_channels.clear();
    auto addChannel = [&](const QString &base) {
        Gyro::ScanChannel c;
        bool ok = false;
        c.index = readText(scan.absoluteFilePath(base + "_index")).toInt(&ok);
        c.type = Gyro::parseScanType(readText(scan.absoluteFilePath(base + "_type")));
        if (!ok || !c.type.valid)
            return -1;
        m_channels.append(c);
        return c.index;
    };
    int index[3], tsIndex = -1;
    for (int i = 0; i < 3; ++i)
        if ((index[i] = addChannel(QString("in_anglvel_%1").arg(kAxes[i]))) < 0)
            return fail("bad scan element");
    if (readText(scan.absoluteFilePath("in_timestamp_en")) == "1")
        tsIndex = addChannel("in_timestamp");
    m_record = Gyro::layoutScan(m_channels);
    if (m_record <= 0)
        return fail("bad scan layout");
    m_tsAt = -1;
    for (int c = 0; c < m_channels.size(); ++c) {
        for (int i = 0; i < 3; ++i)
            if (m_channels[c].index == index[i]) m_axisAt[i] = c;
        if (tsIndex >= 0 && m_channels[c].index == tsIndex) m_tsAt = c;
    }

    if (QFileInfo::exists(bufDir + "/length") && readText(bufDir + "/length").toInt() < 64)
        writeAttr(bufDir + "/length", "64");
    const int err = writeAttr(bufDir + "/enable", "1");
    if (err != 0)
        return fail(strerror(err), err == EBUSY);

    m_pending.clear();
    m_lastTs = 0;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Imu::readBuffer);
    if (m_freq <= 0)
        m_freq = kWantHz;
    return true;
}

bool Imu::startPolling()
{
    for (int i = 0; i < 3; ++i) {
        const QString f = m_dir + QString("/in_anglvel_%1_raw").arg(kAxes[i]);
        m_rawFd[i] = ::open(f.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
        if (m_rawFd[i] < 0) {
            qWarning("xpanel-daemon gyro: %s: %s", qPrintable(f), strerror(errno));
            return false;
        }
        char b[32];
        if (::pread(m_rawFd[i], b, sizeof(b) - 1, 0) < 0) {
            if (errno == EBUSY) { // the buffer is enabled by another program
                qInfo("xpanel-daemon gyro: %s is busy (used by another program?)", qPrintable(m_node));
                m_busyNow = true;
            }
            return false;
        }
    }
    m_clock.start();
    m_poll.start();
    m_freq = 1000.0 / kPollMs;
    return true;
}

void Imu::stop()
{
    m_poll.stop();
    for (int &fd : m_rawFd)
        if (fd >= 0) { ::close(fd); fd = -1; }
    delete m_notifier;
    m_notifier = nullptr;
    // put back what we changed, newest first (buffer/enable = 0 comes first)
    while (!m_restore.isEmpty()) {
        const auto r = m_restore.takeLast();
        if (const int err = writeAttr(r.first, r.second, false))
            qWarning("xpanel-daemon gyro: cannot restore %s: %s", qPrintable(r.first), strerror(err));
    }
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
    m_pending.clear();
}

// ---------------------------------------------------------------- reading

void Imu::readBuffer()
{
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n > 0) {
            m_pending.append(buf, int(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        // n == 0 (writer gone, tests) or an error (device removed): start over later
        qWarning("xpanel-daemon gyro: reading %s stopped: %s", qPrintable(m_node), n < 0 ? strerror(errno) : "end of data");
        stop();
        m_dir.clear();
        setState(State::NoDevice);
        if (m_wanted)
            m_retry.start(kBusyRetryMs);
        return;
    }
    int at = 0;
    const double period = 1.0 / m_freq;
    while (m_pending.size() - at >= m_record) {
        const auto *rec = reinterpret_cast<const unsigned char *>(m_pending.constData() + at);
        at += m_record;
        Gyro::Vec3 v;
        double *axes[3] = {&v.x, &v.y, &v.z};
        for (int i = 0; i < 3; ++i) {
            const Gyro::ScanChannel &c = m_channels[m_axisAt[i]];
            *axes[i] = double(Gyro::decode(rec + c.offset, c.type)) * m_scale;
        }
        double dt = period;
        if (m_tsAt >= 0) {
            const Gyro::ScanChannel &c = m_channels[m_tsAt];
            const qint64 ts = Gyro::decode(rec + c.offset, c.type);
            if (m_lastTs != 0 && ts > m_lastTs)
                dt = double(ts - m_lastTs) / 1e9;
            m_lastTs = ts;
        }
        const Gyro::Vec3 w = Gyro::apply(m_mount, v);
        emit sample(w.x, w.y, w.z, std::clamp(dt, 0.0, 0.05));
    }
    m_pending.remove(0, at);
}

void Imu::poll()
{
    Gyro::Vec3 v;
    double *axes[3] = {&v.x, &v.y, &v.z};
    for (int i = 0; i < 3; ++i) {
        char b[32];
        const ssize_t n = ::pread(m_rawFd[i], b, sizeof(b) - 1, 0);
        if (n <= 0) {
            const bool busy = n < 0 && errno == EBUSY;
            qWarning("xpanel-daemon gyro: reading %s: %s", qPrintable(m_node), n < 0 ? strerror(errno) : "empty");
            stop();
            setState(busy ? State::Busy : State::NoDevice);
            if (!busy)
                m_dir.clear();
            if (m_wanted)
                m_retry.start(kBusyRetryMs);
            return;
        }
        b[n] = 0;
        *axes[i] = std::strtol(b, nullptr, 10) * m_scale;
    }
    const double dt = m_clock.nsecsElapsed() / 1e9;
    m_clock.restart();
    const Gyro::Vec3 w = Gyro::apply(m_mount, v);
    emit sample(w.x, w.y, w.z, std::clamp(dt, 0.0, 0.05));
}
