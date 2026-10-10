#pragma once

// Custom fan curve: validation and conversion. Qt Core only, shared by the daemon (Linux and
// Windows) and the unit tests (tests/tst_fancurve.cpp).
//
// Wire format (daemon key "fanCurve"):
//   {"cpu": [[temp, percent] x 8], "gpu": [[temp, percent] x 8]}
// "gpu" is optional: without it the CPU curve drives both fans. The hardware takes 8 points
// per fan; the curve is a staircase of (temperature in °C, fan duty in %).

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <array>

namespace FanCurve {

constexpr int kPoints = 8;
constexpr int kMinTemp = 20;  // °C
constexpr int kMaxTemp = 105; // °C

struct Point {
    int temp;    // °C
    int percent; // 0..100
};
using Curve = std::array<Point, kPoints>;

// Whole numbers only: 40 and 40.0 pass, 40.5 and "40" do not.
inline bool wholeNumber(const QJsonValue &v, int *out)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (d < -1e6 || d > 1e6 || d != double(int(d)))
        return false;
    *out = int(d);
    return true;
}

// One curve: exactly 8 points [temp, percent]; temperatures kMinTemp..kMaxTemp strictly
// increasing, duty 0..100 never decreasing (the firmware rejects a curve that gets quieter
// as it gets hotter).
inline bool parseCurve(const QJsonValue &v, Curve *out = nullptr)
{
    if (!v.isArray())
        return false;
    const QJsonArray a = v.toArray();
    if (a.size() != kPoints)
        return false;
    Curve c{};
    for (int i = 0; i < kPoints; ++i) {
        if (!a[i].isArray())
            return false;
        const QJsonArray p = a[i].toArray();
        if (p.size() != 2 || !wholeNumber(p[0], &c[i].temp) || !wholeNumber(p[1], &c[i].percent))
            return false;
        if (c[i].temp < kMinTemp || c[i].temp > kMaxTemp || c[i].percent < 0 || c[i].percent > 100)
            return false;
        if (i > 0 && (c[i].temp <= c[i - 1].temp || c[i].percent < c[i - 1].percent))
            return false;
    }
    if (out)
        *out = c;
    return true;
}

// The value of the daemon key "fanCurve": an object with "cpu" and optionally "gpu", nothing else.
inline bool valid(const QJsonValue &v)
{
    if (!v.isObject())
        return false;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != QLatin1String("cpu") && it.key() != QLatin1String("gpu"))
            return false;
    return o.contains("cpu") && parseCurve(o["cpu"]) && (!o.contains("gpu") || parseCurve(o["gpu"]));
}

// Used when "custom" is chosen before any curve was saved: between Quiet and Max.
inline Curve balanced()
{
    return {{{40, 10}, {50, 25}, {60, 40}, {70, 55}, {78, 70}, {85, 82}, {90, 92}, {95, 100}}};
}

// Curves of a valid "fanCurve" value; the GPU fan follows the CPU curve when none is given.
// An invalid or missing value gives balanced().
inline Curve cpu(const QJsonObject &fanCurve)
{
    Curve c;
    return parseCurve(fanCurve["cpu"], &c) ? c : balanced();
}
inline Curve gpu(const QJsonObject &fanCurve)
{
    Curve c;
    return parseCurve(fanCurve["gpu"], &c) ? c : cpu(fanCurve);
}

// Duty in % -> the hardware's 0..255 (hwmon pwm), rounded.
inline int toPwm(int percent)
{
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    return (percent * 255 + 50) / 100;
}

} // namespace FanCurve
