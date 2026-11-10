#include "AmdAdlxWin.h"
#ifdef _WIN32

#include "ADLXHelper.h"
#include "ISystem.h"
#include "I3DSettings.h"
#include "I3DSettings1.h"

#include <mutex>
#include <windows.h>

adlx_handle ADLX_CDECL_CALL adlx_load_library(const TCHAR *filename)
{
    return reinterpret_cast<adlx_handle>(LoadLibrary(filename));
}

int ADLX_CDECL_CALL adlx_free_library(adlx_handle module)
{
    return FreeLibrary(reinterpret_cast<HMODULE>(module)) ? 1 : 0;
}

void *ADLX_CDECL_CALL adlx_get_proc_address(adlx_handle module, const char *proc_name)
{
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(module), proc_name));
}

namespace AmdAdlx {
namespace {

std::mutex g_mutex;
bool g_initialized = false;
bool g_supported = false;

bool ensureInit()
{
    if (g_initialized)
        return g_supported;

    g_initialized = true;
    ADLX_RESULT res = g_ADLX.Initialize();
    if (res != ADLX_OK && res != ADLX_ALREADY_INITIALIZED) {
        res = g_ADLX.InitializeWithIncompatibleDriver();
    }
    if (res == ADLX_OK || res == ADLX_ALREADY_INITIALIZED) {
        adlx::IADLXSystem* sys = g_ADLX.GetSystemServices();
        if (sys) {
            adlx::IADLXGPUListPtr gpus;
            if (sys->GetGPUs(&gpus) == ADLX_OK && gpus) {
                if (gpus->Size() > 0) {
                    g_supported = true;
                    return true;
                }
            }
        }
    }
    g_supported = false;
    return false;
}

bool getPrimary(adlx::IADLXGPUPtr &gpu, adlx::IADLX3DSettingsServicesPtr &d3d)
{
    if (!ensureInit())
        return false;

    adlx::IADLXSystem* sys = g_ADLX.GetSystemServices();
    if (!sys)
        return false;

    adlx::IADLXGPUListPtr gpus;
    if (sys->GetGPUs(&gpus) != ADLX_OK || !gpus)
        return false;

    if (gpus->At(0, &gpu) != ADLX_OK || !gpu)
        return false;

    if (sys->Get3DSettingsServices(&d3d) != ADLX_OK || !d3d)
        return false;

    return true;
}

} // namespace

GraphicsState query()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    GraphicsState s;
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return s;

    s.supported = true;

    // 1. Anti-Lag
    adlx::IADLX3DAntiLagPtr antiLag;
    if (d3d->GetAntiLag(gpu, &antiLag) == ADLX_OK && antiLag) {
        adlx_bool sup = false, en = false;
        antiLag->IsSupported(&sup);
        antiLag->IsEnabled(&en);
        s.antiLagSupported = sup;
        s.antiLagEnabled = en;
    }

    // 2. Chill
    adlx::IADLX3DChillPtr chill;
    if (d3d->GetChill(gpu, &chill) == ADLX_OK && chill) {
        adlx_bool sup = false, en = false;
        chill->IsSupported(&sup);
        chill->IsEnabled(&en);
        s.chillSupported = sup;
        s.chillEnabled = en;
    }

    // 3. Boost
    adlx::IADLX3DBoostPtr boost;
    if (d3d->GetBoost(gpu, &boost) == ADLX_OK && boost) {
        adlx_bool sup = false, en = false;
        boost->IsSupported(&sup);
        boost->IsEnabled(&en);
        s.boostSupported = sup;
        s.boostEnabled = en;
    }

    // 4. RSR
    adlx::IADLX3DRadeonSuperResolutionPtr rsr;
    if (d3d->GetRadeonSuperResolution(&rsr) == ADLX_OK && rsr) {
        adlx_bool sup = false, en = false;
        rsr->IsSupported(&sup);
        rsr->IsEnabled(&en);
        s.rsrSupported = sup;
        s.rsrEnabled = en;
    }

    // 5. AFMF
    adlx::IADLX3DSettingsServices1Ptr d3d1;
    if (d3d->QueryInterface(adlx::IADLX3DSettingsServices1::IID(), reinterpret_cast<void**>(&d3d1)) == ADLX_OK && d3d1) {
        adlx::IADLX3DAMDFluidMotionFramesPtr afmf;
        if (d3d1->GetAMDFluidMotionFrames(&afmf) == ADLX_OK && afmf) {
            adlx_bool sup = false, en = false;
            afmf->IsSupported(&sup);
            afmf->IsEnabled(&en);
            s.afmfSupported = sup;
            s.afmfEnabled = en;
        }
    }

    return s;
}

bool setRsr(bool enable)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return false;

    adlx::IADLX3DRadeonSuperResolutionPtr rsr;
    if (d3d->GetRadeonSuperResolution(&rsr) == ADLX_OK && rsr) {
        return rsr->SetEnabled(enable) == ADLX_OK;
    }
    return false;
}

bool setAfmf(bool enable)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return false;

    adlx::IADLX3DSettingsServices1Ptr d3d1;
    if (d3d->QueryInterface(adlx::IADLX3DSettingsServices1::IID(), reinterpret_cast<void**>(&d3d1)) == ADLX_OK && d3d1) {
        adlx::IADLX3DAMDFluidMotionFramesPtr afmf;
        if (d3d1->GetAMDFluidMotionFrames(&afmf) == ADLX_OK && afmf) {
            return afmf->SetEnabled(enable) == ADLX_OK;
        }
    }
    return false;
}

bool setAntiLag(bool enable)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return false;

    adlx::IADLX3DAntiLagPtr antiLag;
    if (d3d->GetAntiLag(gpu, &antiLag) == ADLX_OK && antiLag) {
        return antiLag->SetEnabled(enable) == ADLX_OK;
    }
    return false;
}

bool setBoost(bool enable)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return false;

    adlx::IADLX3DBoostPtr boost;
    if (d3d->GetBoost(gpu, &boost) == ADLX_OK && boost) {
        return boost->SetEnabled(enable) == ADLX_OK;
    }
    return false;
}

bool setChill(bool enable)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    adlx::IADLXGPUPtr gpu;
    adlx::IADLX3DSettingsServicesPtr d3d;
    if (!getPrimary(gpu, d3d))
        return false;

    adlx::IADLX3DChillPtr chill;
    if (d3d->GetChill(gpu, &chill) == ADLX_OK && chill) {
        return chill->SetEnabled(enable) == ADLX_OK;
    }
    return false;
}

bool optimizeAll()
{
    setAntiLag(true);
    setBoost(true);
    setChill(false);
    return true;
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        g_ADLX.Terminate();
        g_initialized = false;
        g_supported = false;
    }
}

} // namespace AmdAdlx
#endif
