#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Direct hardware access for the ROG Ally / Ally X (runs as root).
//   TDP:       /sys/firmware/acpi/platform_profile + asus-armoury ppt_pl1/2/3
//   Fan:       hwmon "asus_custom_fan_curve"
//   Charge:    /sys/class/power_supply/BAT0/charge_control_end_threshold
//   Lighting:  ASUS Aura HID output reports (report id 0x5A) on the N-KEY hidraw
namespace Hardware {

struct Tdp {
    int spl = 0;   // sustained, W
    int sppt = 0;  // slow boost, W
    int fppt = 0;  // fast boost, W
    QString profile; // low-power | balanced | performance
};

bool onAc();
QJsonObject limits();              // {tdpMin, tdpMax}
Tdp readTdp();
bool applyTdp(const Tdp &t);

bool applyFan(const QString &preset); // auto | quiet | max
bool applyChargeLimit(int percent);   // 100 = no limit
int readChargeLimit();

// effect: off | static | breathing | colorcycle | rainbow ; color "#rrggbb" ;
// brightness 0..3 ; speed: low | medium | high
bool applyLighting(const QString &effect, const QString &color, int brightness, const QString &speed);

// CPU energy preference + boost (amd-pstate-epp). eco = save power.
bool applyCpu(bool eco, const QString &opmode);
// GPU max clock in MHz, 0 = automatic.
bool applyGpuClock(int mhz);
QJsonObject gpuRange();                 // {gpuMin, gpuMax} MHz
// asus-armoury enumeration attributes (boot_sound, mcu_powersave)
int readAttr(const QString &name);
bool writeAttr(const QString &name, int value);
QJsonObject deviceInfo();               // product, bios, serial (root only)

} // namespace Hardware
