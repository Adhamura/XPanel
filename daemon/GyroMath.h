#pragma once

// Pure gyro math for the input layer (no hardware, no Qt event loop), so tests/tst_gyro.cpp can
// check it: IIO sysfs parsing (mount matrix, scan element types and layout, sampling
// frequency), the device-frame -> aim mapping, and gyro -> mouse / right stick.

#include <QList>
#include <QString>
#include <QtGlobal>

namespace Gyro {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// Row-major 3x3 matrix, as the IIO "mount_matrix" attribute prints it.
struct Mat3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

// "x1, y1, z1; x2, y2, z2; x3, y3, z3" (the kernel's iio_show_mount_matrix format).
// Returns false (and leaves *out unchanged) when the text is not a 3x3 matrix.
bool parseMountMatrix(const QString &text, Mat3 *out);
// The vector in the device's own frame: out = M * v.
Vec3 apply(const Mat3 &m, const Vec3 &v);

// One IIO scan element type, e.g. "le:s16/16>>0" or "be:u12/16>>4" (repeat "X" form is not used
// by gyros and is rejected).
struct ScanType {
    bool valid = false;
    bool bigEndian = false;
    bool isSigned = false;
    int realBits = 0;
    int storageBits = 0;
    int shift = 0;
};
ScanType parseScanType(const QString &text);
// The value of one element stored at `data` (storageBits / 8 bytes), sign-extended.
qint64 decode(const unsigned char *data, const ScanType &t);

// Byte offsets of the enabled scan elements in one buffer record. The kernel puts the elements
// in index order, each aligned to its own size, and pads the record to the largest element.
struct ScanChannel {
    int index = 0;
    ScanType type;
    int offset = 0; // filled by layoutScan()
};
// Sorts `channels` by index, fills each offset; returns the record size in bytes (0 on error).
int layoutScan(QList<ScanChannel> &channels);

// The frequency to request from a "sampling_frequency_available" list ("12.5 25 50 100 200
// 400 ..."): the lowest one >= want, else the highest one; 0 when the list is empty.
double chooseFrequency(const QString &available, double want = 200);

// ---- aim ----

enum class TurnAxis { Yaw, Roll };

struct Settings {
    double sensX = 100, sensY = 100; // percent (10..300)
    TurnAxis axis = TurnAxis::Yaw;
    bool invertX = false, invertY = false;
};

struct Aim {
    double h = 0; // deg/s, positive = aim right
    double v = 0; // deg/s, positive = aim down (screen / REL_Y / ABS_RY direction)
};

// Angular velocity below this is treated as sensor noise and ignored; between the two values
// it is faded in, so slow aiming does not jump.
constexpr double kNoiseDegPerSec = 0.5;
constexpr double kFullDegPerSec = 1.0;

// Angular velocity in the device frame (rad/s, mount matrix applied, bias not yet removed)
// -> aim rate in deg/s, with the bias removed and the noise filter applied.
//
// THE AXIS MAPPING, in one place. The IIO mount matrix rotates the sensor into the device frame
// the kernel uses for handhelds and phones: x to the right along the screen, y up along the
// screen, z out of the screen towards the player (right-handed; positive rotation is
// counter-clockwise when looking down the axis towards the origin).
//   - Yaw = rotation about y (device held upright in landscape). Turning the device to the left
//     is counter-clockwise seen from above: +y rotation -> aim left -> h = -wy.
//   - Roll = rotation about z (turning it like a steering wheel). Turning the top to the left
//     is counter-clockwise seen by the player: +z rotation -> aim left -> h = -wz.
//   - Pitch = rotation about x. Tilting the top away from the player raises the aim
//     (counter-clockwise seen from the right): +x rotation -> aim up -> v = -wx.
// If a device's mount matrix is wrong, "Invert X / Invert Y" in Control Mode fix the direction.
Aim aimRate(const Vec3 &omega, const Vec3 &bias, const Settings &s);

// Mouse: pixels per degree of rotation at 100 % sensitivity.
constexpr double kPixelsPerDegree = 20.0;

// Gyro -> relative mouse motion with sub-pixel accumulation.
class MouseAccumulator {
public:
    // Adds `dt` seconds at `aim` and returns the whole pixels to send now; the remainder is kept.
    void add(const Aim &aim, double dt, const Settings &s, int *dx, int *dy);
    void reset() { m_remX = m_remY = 0; }

private:
    double m_remX = 0, m_remY = 0;
};

// Right stick: angular velocity at which the stick is fully deflected at 100 % sensitivity.
constexpr double kStickFullDegPerSec = 120.0;

struct Stick {
    double x = 0, y = 0; // -1..1, Xbox convention (y positive = down)
};
// Gyro aim rate -> stick deflection (proportional, sensitivity-scaled, clamped to the circle).
Stick toStick(const Aim &aim, const Settings &s);
// The physical stick plus the gyro, clamped to the unit circle.
Stick combine(const Stick &physical, const Stick &gyro);

} // namespace Gyro
