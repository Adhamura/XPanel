#pragma once
// AMD Advanced Graphics Options through ADLX (amdadlx64.dll, loaded at runtime, never linked).
// Supports RSR, AMD Fluid Motion Frames, Anti-Lag, Radeon Boost, Radeon Chill.
#ifdef _WIN32
namespace AmdAdlx {

struct GraphicsState {
    bool supported = false;
    bool rsrSupported = false;
    bool rsrEnabled = false;
    bool afmfSupported = false;
    bool afmfEnabled = false;
    bool antiLagSupported = false;
    bool antiLagEnabled = false;
    bool boostSupported = false;
    bool boostEnabled = false;
    bool chillSupported = false;
    bool chillEnabled = false;
};

GraphicsState query();
bool setRsr(bool enable);
bool setAfmf(bool enable);
bool setAntiLag(bool enable);
bool setBoost(bool enable);
bool setChill(bool enable);
bool optimizeAll();
void shutdown();

} // namespace AmdAdlx
#endif
