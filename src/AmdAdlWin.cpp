#include "AmdAdlWin.h"
#ifdef _WIN32

#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "adl_sdk.h"

namespace AmdAdl {
namespace {

using Ctx = void *;
using CreateFn = int (*)(ADL_MAIN_MALLOC_CALLBACK, int, Ctx *);
using DestroyFn = int (*)(Ctx);
using NumFn = int (*)(Ctx, int *);
using InfoFn = int (*)(Ctx, LPAdapterInfo, int);
using ActiveFn = int (*)(Ctx, int, int *);
using PmLogFn = int (*)(Ctx, int, ADLPMLogDataOutput *);

std::mutex g_mutex;
bool g_tried = false;
HMODULE g_dll = nullptr;
Ctx g_ctx = nullptr;
int g_adapter = -1;
DestroyFn g_destroy = nullptr;
PmLogFn g_pmlog = nullptr;
ADLPMLogDataOutput g_out; // large (about 2 KB), kept static

void *__stdcall adlAlloc(int size) { return std::malloc(size > 0 ? size : 1); }

void init()
{
    g_tried = true;
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    std::wstring path = std::wstring(dir) + L"\atiadlxx.dll";
    g_dll = LoadLibraryW(path.c_str());
    if (!g_dll)
        return;
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(g_dll, "ADL2_Main_Control_Create"));
    g_destroy = reinterpret_cast<DestroyFn>(GetProcAddress(g_dll, "ADL2_Main_Control_Destroy"));
    auto num = reinterpret_cast<NumFn>(GetProcAddress(g_dll, "ADL2_Adapter_NumberOfAdapters_Get"));
    auto info = reinterpret_cast<InfoFn>(GetProcAddress(g_dll, "ADL2_Adapter_AdapterInfo_Get"));
    auto active = reinterpret_cast<ActiveFn>(GetProcAddress(g_dll, "ADL2_Adapter_Active_Get"));
    g_pmlog = reinterpret_cast<PmLogFn>(GetProcAddress(g_dll, "ADL2_New_QueryPMLogData_Get"));
    if (!create || !g_destroy || !num || !info || !active || !g_pmlog || create(adlAlloc, 1, &g_ctx) != ADL_OK) {
        g_ctx = nullptr;
        g_pmlog = nullptr;
        return;
    }
    int count = 0;
    if (num(g_ctx, &count) == ADL_OK && count > 0) {
        std::vector<AdapterInfo> ai(count);
        std::memset(ai.data(), 0, sizeof(AdapterInfo) * count);
        if (info(g_ctx, ai.data(), int(sizeof(AdapterInfo) * count)) == ADL_OK) {
            for (int i = 0; i < count && g_adapter < 0; ++i) {
                int act = 0;
                if (active(g_ctx, ai[i].iAdapterIndex, &act) == ADL_OK && act)
                    g_adapter = ai[i].iAdapterIndex;
            }
        }
    }
    if (g_adapter < 0) {
        g_destroy(g_ctx);
        g_ctx = nullptr;
    }
}

bool sensor(int id, int &v)
{
    if (id < 0 || id >= ADL_PMLOG_MAX_SENSORS || !g_out.sensors[id].supported)
        return false;
    v = g_out.sensors[id].value;
    return true;
}

} // namespace

Sample read()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Sample s;
    if (!g_tried)
        init();
    if (!g_ctx || !g_pmlog)
        return s;
    std::memset(&g_out, 0, sizeof(g_out));
    if (g_pmlog(g_ctx, g_adapter, &g_out) != ADL_OK)
        return s;
    int v = 0;
    if (sensor(ADL_PMLOG_TEMPERATURE_GFX, v) || sensor(ADL_PMLOG_TEMPERATURE_EDGE, v)
        || sensor(ADL_PMLOG_TEMPERATURE_HOTSPOT, v) || sensor(ADL_PMLOG_TEMPERATURE_SOC, v))
        s.tempC = (v > 0 && v < 150) ? v : 0;
    if (sensor(ADL_PMLOG_ASIC_POWER, v))
        s.asicWatts = v;
    if (sensor(ADL_PMLOG_SOC_POWER, v))
        s.socWatts = v;
    if (sensor(ADL_PMLOG_GFX_POWER, v))
        s.gfxWatts = v;
    s.ok = true;
    return s;
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ctx && g_destroy)
        g_destroy(g_ctx);
    g_ctx = nullptr;
    g_pmlog = nullptr;
}

} // namespace AmdAdl
#endif
