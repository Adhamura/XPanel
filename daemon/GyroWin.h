#pragma once

#include <functional>

// Windows gyro: the built-in gyrometer through Windows.Devices.Sensors (C++/WinRT, GyroWin.cpp).
// The callback gets the angular velocity in rad/s (x right, y up the screen, z out of the screen,
// the frame GyroMath.h expects) and the seconds since the previous sample. It runs on a
// thread-pool thread.
class WinGyro {
public:
    using Callback = std::function<void(double x, double y, double z, double dt)>;
    WinGyro();
    ~WinGyro();
    bool available() const;     // a gyrometer exists
    bool start(const Callback &cb);
    void stop();
    bool running() const;
    // Windows' own report interval in ms (0 when no sensor); for the log
    unsigned minIntervalMs() const;

private:
    struct P;
    P *p;
};
