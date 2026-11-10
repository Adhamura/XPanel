#include "RyzenSmuWin.h"

#include <QCoreApplication>
#include <QFile>
#include <QMutex>

#include <windows.h>

namespace RyzenSmu {
namespace {

using OpenFn = HRESULT(WINAPI *)(PHANDLE);
using LoadFn = HRESULT(WINAPI *)(HANDLE, const UCHAR *, SIZE_T);
using ExecFn = HRESULT(WINAPI *)(HANDLE, PCSTR, const ULONG64 *, SIZE_T, PULONG64, SIZE_T, PSIZE_T);

constexpr ULONG64 kForceGfxClk = 0x89; // PSMU (the module's mailbox on Phoenix / Hawk Point)

QMutex g_lock;
bool g_tried = false;
HANDLE g_handle = nullptr;
ExecFn g_exec = nullptr;
int g_code = -1;

// The mutant HWiNFO, LibreHardwareMonitor, G-Helper and others take around PCI config access.
struct PciLock {
    HANDLE m = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
    bool held = m && WaitForSingleObject(m, 2000) != WAIT_TIMEOUT;
    ~PciLock() { if (m) { if (held) ReleaseMutex(m); CloseHandle(m); } }
};

QString pawnLib()
{
    wchar_t pf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    return (n ? QString::fromWCharArray(pf, int(n)) : QStringLiteral("C:\\Program Files")) + "\\PawnIO\\PawnIOLib.dll";
}

bool init()
{
    if (g_tried)
        return g_handle != nullptr;
    g_tried = true;

    HMODULE lib = LoadLibraryW(reinterpret_cast<LPCWSTR>(pawnLib().utf16()));
    if (!lib)
        return false; // PawnIO not installed
    auto open = reinterpret_cast<OpenFn>(GetProcAddress(lib, "pawnio_open"));
    auto load = reinterpret_cast<LoadFn>(GetProcAddress(lib, "pawnio_load"));
    g_exec = reinterpret_cast<ExecFn>(GetProcAddress(lib, "pawnio_execute"));
    if (!open || !load || !g_exec)
        return false;

    QFile f(QCoreApplication::applicationDirPath() + "/RyzenSMU.bin");
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray blob = f.readAll();

    HANDLE h = nullptr;
    if (FAILED(open(&h)) || !h)
        return false; // not elevated, or the driver is not running
    if (FAILED(load(h, reinterpret_cast<const UCHAR *>(blob.constData()), SIZE_T(blob.size())))) {
        CloseHandle(h); // the module refuses this CPU
        return false;
    }
    ULONG64 out = 0;
    SIZE_T got = 0;
    if (FAILED(g_exec(h, "ioctl_get_code_name", nullptr, 0, &out, 1, &got)) || got != 1) {
        CloseHandle(h);
        return false;
    }
    g_code = int(out);
    g_handle = h;
    return true;
}

bool supported(int code)
{
    return code == Phoenix || code == Phoenix2 || code == HawkPoint;
}

} // namespace

bool available()
{
    QMutexLocker l(&g_lock);
    return init() && supported(g_code);
}

int codeName()
{
    QMutexLocker l(&g_lock);
    return init() ? g_code : -1;
}

bool forceGfxClock(int mhz)
{
    QMutexLocker l(&g_lock);
    if (mhz <= 0 || !init() || !supported(g_code))
        return false;
    const ULONG64 in[7] = {kForceGfxClk, ULONG64(mhz), 0, 0, 0, 0, 0};
    ULONG64 out[6] = {};
    SIZE_T got = 0;
    PciLock pci;
    return SUCCEEDED(g_exec(g_handle, "ioctl_send_smu_command", in, 7, out, 6, &got)) && out[0] == ULONG64(mhz);
}

} // namespace RyzenSmu
