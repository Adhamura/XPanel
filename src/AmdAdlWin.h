#pragma once
// AMD GPU temperature and power through ADL (atiadlxx.dll, loaded at run time, never linked).
// Thread-safe. Skips silently when the DLL or the sensors are missing.
#ifdef _WIN32
namespace AmdAdl {

struct Sample {
    bool ok = false;
    int tempC = 0;       // GPU (GFX) temperature, deg C; 0 = unknown
    double asicWatts = 0; // whole ASIC (APU) power, W
    double socWatts = 0;  // SoC rail power, W
    double gfxWatts = 0;  // graphics rail power, W
};
Sample read();      // first call loads ADL and creates the context
void shutdown();    // destroys the context (call on exit, optional)

} // namespace AmdAdl
#endif
