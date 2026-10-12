// Windows gyro through Windows.Devices.Sensors.Gyrometer (see GyroWin.h).
#include "GyroWin.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Sensors.h>

#include <atomic>
#include <mutex>

using namespace winrt::Windows::Devices::Sensors;

struct WinGyro::P {
    Gyrometer sensor{nullptr};
    winrt::event_token token{};
    bool on = false;
    Callback cb;
    std::mutex mu;
    long long lastTicks = 0;
};

WinGyro::WinGyro() : p(new P)
{
    try {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {} // already set by Qt / the app
        p->sensor = Gyrometer::GetDefault();
    } catch (...) {
        p->sensor = nullptr;
    }
}

WinGyro::~WinGyro()
{
    stop();
    p->sensor = nullptr;
    delete p;
}

bool WinGyro::available() const { return p->sensor != nullptr; }

unsigned WinGyro::minIntervalMs() const
{
    try { return p->sensor ? p->sensor.MinimumReportInterval() : 0; } catch (...) { return 0; }
}

bool WinGyro::running() const { return p->on; }

bool WinGyro::start(const Callback &cb)
{
    if (!p->sensor || p->on)
        return p->on;
    try {
        {
            std::lock_guard<std::mutex> l(p->mu);
            p->cb = cb;
            p->lastTicks = 0;
        }
        const uint32_t minMs = p->sensor.MinimumReportInterval();
        p->sensor.ReportInterval(minMs < 8 ? 8 : minMs); // ~100 Hz is plenty for aiming
        P *pp = p;
        p->token = p->sensor.ReadingChanged([pp](Gyrometer const &, GyrometerReadingChangedEventArgs const &a) {
            const auto r = a.Reading();
            const long long ticks = r.Timestamp().time_since_epoch().count(); // 100 ns
            constexpr double kRad = 0.017453292519943295; // pi / 180
            std::lock_guard<std::mutex> l(pp->mu);
            double dt = pp->lastTicks ? double(ticks - pp->lastTicks) / 1e7 : 0.01;
            pp->lastTicks = ticks;
            if (dt < 0.001) dt = 0.001;
            if (dt > 0.1) dt = 0.1;
            if (pp->cb)
                pp->cb(r.AngularVelocityX() * kRad, r.AngularVelocityY() * kRad, r.AngularVelocityZ() * kRad, dt);
        });
        p->on = true;
    } catch (...) {
        p->on = false;
    }
    return p->on;
}

void WinGyro::stop()
{
    if (!p->on)
        return;
    try {
        p->sensor.ReadingChanged(p->token);
        p->sensor.ReportInterval(0);
    } catch (...) {}
    std::lock_guard<std::mutex> l(p->mu);
    p->cb = nullptr;
    p->on = false;
}
