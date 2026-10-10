#pragma once

// The supported handhelds and what differs between them. Shared by the Linux (Hardware.cpp)
// and Windows (HardwareWin.cpp) backends: Qt Core only, no platform headers.
//
// A new model is one more entry in models(): its DMI board name prefix, the controller's
// USB product id, the Operating Mode wattages and the power limit ranges.

#include <QString>
#include <QtGlobal>

namespace Model {

struct Watts {
    int spl, sppt, fppt; // W: sustained, slow boost, fast boost
};

struct Range {
    int min, max; // W
};

// Allowed power limits for one power source (used when the firmware does not report them).
struct Ranges {
    Range spl, sppt, fppt;
};

struct Info {
    const char *id;          // "ally", "allyx", "unknown"
    const char *name;        // shown to the user
    const char *boardPrefix; // DMI board_name / BaseBoardProduct starts with this ("" = never)
    quint16 controllerPid;   // USB 0b05:<pid> of the built-in controller (0 = none)
    Watts silent, performance, turboAc, turboBattery;
    Ranges ac, battery;
};

constexpr quint16 kAsusVendor = 0x0b05;

// ASUS published values. RC71 and RC72 have the same firmware power ranges (asus-armoury).
inline const Info *models(int *count)
{
    static const Info table[] = {
        {"ally", "ROG Ally", "RC71", 0x1abe,
         {10, 13, 16}, {15, 20, 25}, {30, 35, 43}, {25, 30, 35},
         {{7, 30}, {15, 43}, {15, 53}}, {{7, 25}, {15, 30}, {15, 35}}},
        {"allyx", "ROG Ally X", "RC72", 0x1b4c,
         {13, 15, 20}, {17, 22, 30}, {30, 35, 43}, {25, 30, 35},
         {{7, 30}, {15, 43}, {15, 53}}, {{7, 25}, {15, 30}, {15, 35}}},
    };
    *count = int(sizeof(table) / sizeof(table[0]));
    return table;
}

// Not a known model: the Ally X numbers (clamped to the firmware's own limits by the caller).
inline const Info &unknown()
{
    static const Info u = [] {
        int n = 0;
        Info i = models(&n)[1];
        i.id = "unknown";
        i.name = "ASUS handheld";
        i.boardPrefix = "";
        i.controllerPid = 0;
        return i;
    }();
    return u;
}

inline bool isController(quint16 vendor, quint16 product)
{
    int n = 0;
    const Info *t = models(&n);
    for (int i = 0; i < n; ++i)
        if (vendor == kAsusVendor && product == t[i].controllerPid)
            return true;
    return false;
}

// By board name first ("RC71L", "RC72LA"), then by the controller found (0 = none).
inline Info detect(const QString &board, quint16 controllerPid)
{
    int n = 0;
    const Info *t = models(&n);
    const QString b = board.trimmed();
    for (int i = 0; i < n; ++i)
        if (!b.isEmpty() && b.startsWith(QLatin1String(t[i].boardPrefix), Qt::CaseInsensitive))
            return t[i];
    for (int i = 0; i < n; ++i)
        if (controllerPid != 0 && controllerPid == t[i].controllerPid)
            return t[i];
    return unknown();
}

inline Watts modeWatts(const Info &m, const QString &mode, bool ac)
{
    if (mode == QLatin1String("silent")) return m.silent;
    if (mode == QLatin1String("turbo")) return ac ? m.turboAc : m.turboBattery;
    return m.performance;
}

} // namespace Model
