#include "GyroMath.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace Gyro {

bool parseMountMatrix(const QString &text, Mat3 *out)
{
    const QStringList rows = text.trimmed().split(';');
    if (rows.size() != 3)
        return false;
    Mat3 m;
    for (int r = 0; r < 3; ++r) {
        const QStringList cols = rows[r].split(',');
        if (cols.size() != 3)
            return false;
        for (int c = 0; c < 3; ++c) {
            bool ok = false;
            m.m[r][c] = cols[c].trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(m.m[r][c]))
                return false;
        }
    }
    *out = m;
    return true;
}

Vec3 apply(const Mat3 &m, const Vec3 &v)
{
    return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
            m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
            m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

ScanType parseScanType(const QString &text)
{
    static const QRegularExpression re(R"(^(le|be):([su])(\d+)/(\d+)>>(\d+)$)");
    const QRegularExpressionMatch m = re.match(text.trimmed());
    ScanType t;
    if (!m.hasMatch())
        return t;
    t.bigEndian = m.captured(1) == "be";
    t.isSigned = m.captured(2) == "s";
    t.realBits = m.captured(3).toInt();
    t.storageBits = m.captured(4).toInt();
    t.shift = m.captured(5).toInt();
    const bool sizeOk = t.storageBits == 8 || t.storageBits == 16 || t.storageBits == 32 || t.storageBits == 64;
    t.valid = sizeOk && t.realBits > 0 && t.realBits + t.shift <= t.storageBits;
    return t;
}

qint64 decode(const unsigned char *data, const ScanType &t)
{
    const int bytes = t.storageBits / 8;
    quint64 raw = 0;
    for (int i = 0; i < bytes; ++i) {
        const int b = t.bigEndian ? i : bytes - 1 - i;
        raw = (raw << 8) | data[b];
    }
    raw >>= t.shift;
    if (t.realBits < 64) {
        const quint64 mask = (quint64(1) << t.realBits) - 1;
        raw &= mask;
        if (t.isSigned && (raw & (quint64(1) << (t.realBits - 1))))
            raw |= ~mask; // sign-extend
    }
    return qint64(raw);
}

int layoutScan(QList<ScanChannel> &channels)
{
    std::sort(channels.begin(), channels.end(), [](const ScanChannel &a, const ScanChannel &b) { return a.index < b.index; });
    int offset = 0, largest = 1;
    for (ScanChannel &c : channels) {
        if (!c.type.valid)
            return 0;
        const int size = c.type.storageBits / 8;
        offset = (offset + size - 1) / size * size;
        c.offset = offset;
        offset += size;
        largest = std::max(largest, size);
    }
    return (offset + largest - 1) / largest * largest;
}

double chooseFrequency(const QString &available, double want)
{
    double best = 0, highest = 0;
    for (const QString &s : available.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double f = s.toDouble(&ok);
        if (!ok || f <= 0)
            continue;
        highest = std::max(highest, f);
        if (f >= want && (best == 0 || f < best))
            best = f;
    }
    return best > 0 ? best : highest;
}

Aim aimRate(const Vec3 &omega, const Vec3 &bias, const Settings &s)
{
    constexpr double kDeg = 57.29577951308232; // 180 / pi
    const Vec3 w{(omega.x - bias.x) * kDeg, (omega.y - bias.y) * kDeg, (omega.z - bias.z) * kDeg};
    // The mapping (see the comment in GyroMath.h): yaw / roll -> horizontal, pitch -> vertical.
    Aim a;
    a.h = -(s.axis == TurnAxis::Roll ? w.z : w.y);
    a.v = -w.x;
    if (s.invertX) a.h = -a.h;
    if (s.invertY) a.v = -a.v;
    // noise filter: nothing below kNoiseDegPerSec, faded in up to kFullDegPerSec
    const double mag = std::hypot(a.h, a.v);
    const double f = std::clamp((mag - kNoiseDegPerSec) / (kFullDegPerSec - kNoiseDegPerSec), 0.0, 1.0);
    a.h *= f;
    a.v *= f;
    return a;
}

void MouseAccumulator::add(const Aim &aim, double dt, const Settings &s, int *dx, int *dy)
{
    m_remX += aim.h * dt * kPixelsPerDegree * s.sensX / 100.0;
    m_remY += aim.v * dt * kPixelsPerDegree * s.sensY / 100.0;
    *dx = int(m_remX); // toward zero: the remainder keeps its sign
    *dy = int(m_remY);
    m_remX -= *dx;
    m_remY -= *dy;
}

static Stick clampCircle(Stick st)
{
    const double r = std::hypot(st.x, st.y);
    if (r > 1.0) {
        st.x /= r;
        st.y /= r;
    }
    return st;
}

Stick toStick(const Aim &aim, const Settings &s)
{
    return clampCircle({aim.h / kStickFullDegPerSec * s.sensX / 100.0, aim.v / kStickFullDegPerSec * s.sensY / 100.0});
}

Stick combine(const Stick &physical, const Stick &gyro)
{
    return clampCircle({physical.x + gyro.x, physical.y + gyro.y});
}

} // namespace Gyro
