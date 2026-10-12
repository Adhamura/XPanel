#pragma once
#include <QtGlobal>
// Windows implementation of the Quick Panel system features (volume, radios, power, keyboard,
// display, screenshot helpers, sensors). Only built on Windows (src/SystemControlWin.cpp).
// Functions that say "worker thread" block: never call them on the GUI thread.
#ifdef Q_OS_WIN

#include <QString>
#include <QVector>
#include <functional>

namespace SysWin {

// ---- volume (Core Audio, default render endpoint) ----
struct Volume {
    bool ok = false;
    int percent = 0;
    bool muted = false;
};
// GUI thread. onChange(deviceChanged) is called from an audio thread when the volume or mute state
// changed outside XPanel, or when the default device changed (deviceChanged = true).
void audioInit(const std::function<void(bool deviceChanged)> &onChange);
void audioRebind();            // GUI thread: pick the default device again
Volume audioGet();
void audioSetVolume(int percent);
void audioSetMute(bool muted);
void audioShutdown();

// ---- radios (WinRT Windows.Devices.Radios) ----
struct Radios {
    bool ok = false;
    bool wifi = false, bluetooth = false;
    bool hasWifi = false, hasBluetooth = false;
};
Radios radiosRead();                       // worker thread
bool radioSet(bool wifi, bool on);         // worker thread; false = Windows refused

// ---- power menu ----
bool power(const QString &what);           // suspend | reboot | poweroff | logout (may block: worker thread)

// ---- keyboard / show desktop ----
void openKeyboard();                       // worker thread
void showDesktop();                        // GUI thread

// ---- displays ----
struct DisplayState {
    bool ok = false;
    int refresh = 0;            // internal panel, Hz
    QVector<int> rates;         // rates the panel offers at its current resolution
    bool hasExternal = false;
    QString mode = "handheld";  // handheld | external | duplicate | extend
    QString internalName;       // \\.\DISPLAYn
};
DisplayState displayRead();                // cheap, GUI thread is fine
bool displaySetRefresh(int hz);            // worker thread
bool displaySetMode(const QString &mode);  // worker thread

// ---- sensors ----
struct Stats {
    int cpuLoad = -1;       // percent, -1 = no value yet
    int cpuTemp = 0;        // deg C, 0 = unknown
    int gpuLoad = -1;
    int gpuTemp = 0;        // deg C, 0 = unknown (AMD ADL)
    double socWatts = 0;    // APU package power, W, 0 = unknown (AMD ADL)
    int fanRpm = 0;
    double batteryWatts = 0;
    int ramUsedMb = 0;
};
Stats statsRead();                         // GUI thread (keeps state between calls)

} // namespace SysWin

#endif
