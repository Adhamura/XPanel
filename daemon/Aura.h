#pragma once

// ASUS Aura (N-KEY device) lighting: the packets for an effect, and the effects that follow the
// system (battery level, Operating Mode). Qt Core only, shared by the Linux and Windows backends
// (Hardware.cpp, HardwareWin.cpp) and the unit tests (tests/tst_aura.cpp).
//
// Packets are 64 bytes, report id 0x5A:
//   handshake   5A "ASUS Tech.Inc."
//   brightness  5A BA C5 C4 level            (0 off .. 3 high)
//   mode        5A B3 zone mode R G B speed direction breathingType R2 G2 B2   (zone 0 = all)
//   set         5A B5
//   apply       5A B4
// Modes (asusctl's AuraModeNum): 0 static, 1 breathe, 2 colour cycle, 3 rainbow wave, 0x0a strobe.
// Verified on hardware: off, static, breathing, colour cycle, rainbow (direction 0). Dual
// breathing, the wave directions, strobe, battery level and Follow Operating Mode are not tested
// on hardware yet.

#include <QByteArray>
#include <QList>
#include <QString>

#include <initializer_list>

namespace Aura {

struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Params {
    QString effect = "static"; // off static breathing dualbreathing colorcycle rainbow strobe
                               // (battery and followmode: see resolve())
    QString color = "#00c8ff"; // "#rrggbb"
    QString color2 = "#ff2d55";
    int brightness = 3;        // 0..3
    QString speed = "medium";  // low | medium | high
    QString direction = "right"; // rainbow wave: right | left | up | down
};

inline Rgb parseColor(const QString &hex)
{
    const unsigned v = hex.mid(1).toUInt(nullptr, 16);
    return {int(v >> 16) & 0xFF, int(v >> 8) & 0xFF, int(v) & 0xFF};
}

inline QString toHex(const Rgb &c)
{
    return QString("#%1%2%3").arg(c.r, 2, 16, QLatin1Char('0')).arg(c.g, 2, 16, QLatin1Char('0'))
        .arg(c.b, 2, 16, QLatin1Char('0'));
}

inline QByteArray packet(std::initializer_list<int> bytes)
{
    QByteArray b(64, '\0');
    int i = 0;
    for (int v : bytes)
        b[i++] = char(v);
    return b;
}

inline int speedByte(const QString &speed)
{
    return speed == "low" ? 0xE1 : (speed == "high" ? 0xF5 : 0xEB);
}

// Wave direction byte: right 0, left 1, up 2, down 3 (the Ally's rings may show up / down alike).
inline int directionByte(const QString &d)
{
    return d == "left" ? 1 : (d == "up" ? 2 : (d == "down" ? 3 : 0));
}

// The packets that set an effect; empty for an effect that is not a hardware mode.
inline QList<QByteArray> packets(const Params &p)
{
    const QString &e = p.effect;
    int mode = 0x00;
    if (e == "breathing" || e == "dualbreathing") mode = 0x01;
    else if (e == "colorcycle") mode = 0x02;
    else if (e == "rainbow") mode = 0x03;
    else if (e == "strobe") mode = 0x0a;
    else if (e != "static" && e != "off") return {};

    Rgb c = parseColor(p.color), c2{};
    int type = 0, direction = 0;
    if (mode == 0x03) {
        c = {};
        direction = directionByte(p.direction);
    } else if (e == "dualbreathing") {
        c2 = parseColor(p.color2);
        type = 1; // two colours (breathing type; from asusctl's layout, not tested)
    }
    const int spd = mode == 0x00 ? 0x00 : speedByte(p.speed);
    const int level = e == "off" ? 0 : qBound(0, p.brightness, 3);

    QList<QByteArray> cmds;
    cmds << packet({0x5A, 'A', 'S', 'U', 'S', ' ', 'T', 'e', 'c', 'h', '.', 'I', 'n', 'c', '.'});
    cmds << packet({0x5A, 0xBA, 0xC5, 0xC4, level});
    if (level > 0) {
        cmds << packet({0x5A, 0xB3, 0x00, mode, c.r, c.g, c.b, spd, direction, type, c2.r, c2.g, c2.b});
        cmds << packet({0x5A, 0xB5});
        cmds << packet({0x5A, 0xB4});
    }
    return cmds;
}

// ---------- effects that follow the system ----------

inline bool isDynamic(const QString &effect)
{
    return effect == "battery" || effect == "followmode";
}

// Red (0 %) through yellow (50 %) to green (100 %).
inline Rgb batteryColor(int percent)
{
    const int p = qBound(0, percent, 100);
    return {p < 50 ? 255 : (100 - p) * 255 / 50, p < 50 ? p * 255 / 50 : 255, 0};
}

// Silent blue, Performance white, Turbo red; Manual keeps the chosen colour.
inline Rgb modeColor(const QString &opmode, const Rgb &manual)
{
    if (opmode == "silent") return {0x00, 0x40, 0xFF};
    if (opmode == "performance") return {0xFF, 0xFF, 0xFF};
    if (opmode == "turbo") return {0xFF, 0x00, 0x14};
    return manual;
}

// Turns battery / followmode into a static colour for the current state; other effects pass.
// batteryPercent < 0 = unknown (no battery): the chosen colour is kept.
inline Params resolve(Params p, int batteryPercent, const QString &opmode)
{
    if (p.effect == "battery") {
        p.effect = "static";
        if (batteryPercent >= 0)
            p.color = toHex(batteryColor(batteryPercent));
    } else if (p.effect == "followmode") {
        p.effect = "static";
        p.color = toHex(modeColor(opmode, parseColor(p.color)));
    }
    return p;
}

// Battery effect: apply again when the charge moved by 5 points or more since the last time.
inline bool batteryMoved(int lastApplied, int now)
{
    return lastApplied < 0 || now < 0 ? lastApplied != now : qAbs(now - lastApplied) >= 5;
}

} // namespace Aura
