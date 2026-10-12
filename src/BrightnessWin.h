#pragma once
// Windows brightness backends (no Qt GUI, no windows.h in this header).
//  - internal panel: WMI root\wmi WmiMonitorBrightness / WmiMonitorBrightnessMethods
//  - external monitors: DDC/CI through dxva2 (slow: call from a worker thread)
//  - which screen is internal and its label: QueryDisplayConfig + DisplayConfigGetDeviceInfo
// All functions are thread-safe (each call initialises COM for its own thread).
#include <QString>
#include <QVector>

namespace BrightnessWin {

struct Monitor {
    QString id;           // "winint", "winext0_DISPLAY2"
    QString gdi;          // "\\.\DISPLAY1" (matches QScreen::name())
    QString label;
    QString wmiInstance;  // internal panel: WMI InstanceName
    QString friendly;     // monitorFriendlyDeviceName, used to pick the physical monitor
    bool internal = false;
    bool soft = false;    // external, no DDC/CI: dimmed with a gamma ramp (SetDeviceGammaRamp)
    int ddcIndex = 0;     // external: index among the physical monitors of this HMONITOR
    int pct = -1;         // 0-100, -1 = not read
};

// Active display paths with their brightness. readDdc = false skips the slow DDC/CI reads
// (external entries then have pct = -1). Entries without brightness control are left out
// (internal panel without WMI brightness; with readDdc, external monitor without DDC/CI).
QVector<Monitor> enumerate(bool readDdc);

// Re-apply the stored gamma ramps of soft-dimmed monitors (Windows resets them on mode change/sleep).
void reapplySoft();

// Set 0-100. Slow for DDC/CI: never call on the GUI thread.
bool setPercent(const Monitor &m, int percent);

} // namespace BrightnessWin
