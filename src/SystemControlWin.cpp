// Windows side of SystemControl: volume, Wi-Fi / Bluetooth, power menu, keyboard, Show Desktop,
// refresh rate, display mode and sensors. No Qt GUI types here, only QString / QVector.
#include <QtGlobal>
#ifdef Q_OS_WIN

#define QT_NO_KEYWORDS
#include "SystemControlWin.h"
#include "AmdAdlWin.h"

#include <QByteArray>
#include <QtEndian>

#include <windows.h>
#include <unknwn.h>
#include <winioctl.h>
#include <batclass.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shldisp.h>
#include <powrprof.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace SysWin {

namespace {

// COM for the current thread: S_OK / S_FALSE need a matching CoUninitialize, RPC_E_CHANGED_MODE does not
// (the thread already has COM in another mode, which is fine for what we call here).
struct ComScope {
    bool inited = false;
    explicit ComScope(DWORD mode) { inited = SUCCEEDED(CoInitializeEx(nullptr, mode)); }
    ~ComScope() { if (inited) CoUninitialize(); }
};

} // namespace

// =====================================================================================
// Volume
// =====================================================================================
namespace {

// events caused by our own writes carry this context and are ignored
const GUID kOurContext = {0x7b0a1c52, 0x3e54, 0x4c1f, {0x9a, 0x61, 0x58, 0x50, 0x61, 0x6e, 0x65, 0x6c}};

std::mutex g_audioMutex;
std::function<void(bool)> g_onChange;

void notifyChange(bool deviceChanged)
{
    std::function<void(bool)> f;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        f = g_onChange;
    }
    if (f)
        f(deviceChanged);
}

class VolumeCallback : public IAudioEndpointVolumeCallback {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&m_ref)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = ULONG(InterlockedDecrement(&m_ref));
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override
    {
        if (data && data->guidEventContext == kOurContext)
            return S_OK;
        notifyChange(false);
        return S_OK;
    }

private:
    LONG m_ref = 1;
};

class DeviceCallback : public IMMNotificationClient {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&m_ref)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = ULONG(InterlockedDecrement(&m_ref));
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole)
            notifyChange(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    LONG m_ref = 1;
};

// all of this is touched on the GUI thread only
IMMDeviceEnumerator *g_enum = nullptr;
IAudioEndpointVolume *g_vol = nullptr;
VolumeCallback *g_volCb = nullptr;
DeviceCallback *g_devCb = nullptr;
bool g_audioInited = false;

void unbindEndpoint()
{
    if (g_vol) {
        if (g_volCb)
            g_vol->UnregisterControlChangeNotify(g_volCb);
        g_vol->Release();
        g_vol = nullptr;
    }
}

} // namespace

void audioRebind()
{
    if (!g_enum)
        return;
    unbindEndpoint();
    IMMDevice *dev = nullptr;
    if (FAILED(g_enum->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev)
        return;
    IAudioEndpointVolume *vol = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(&vol))) && vol) {
        g_vol = vol;
        if (!g_volCb)
            g_volCb = new VolumeCallback;
        g_vol->RegisterControlChangeNotify(g_volCb);
    }
    dev->Release();
}

void audioInit(const std::function<void(bool)> &onChange)
{
    if (g_audioInited)
        return;
    g_audioInited = true;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_onChange = onChange;
    }
    // the Qt GUI thread is an STA already; RPC_E_CHANGED_MODE (another mode) is fine too
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Q_UNUSED(hr);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&g_enum))) || !g_enum)
        return;
    g_devCb = new DeviceCallback;
    g_enum->RegisterEndpointNotificationCallback(g_devCb);
    audioRebind();
}

Volume audioGet()
{
    Volume v;
    if (!g_vol)
        return v;
    float level = 0;
    BOOL mute = FALSE;
    if (FAILED(g_vol->GetMasterVolumeLevelScalar(&level)))
        return v;
    g_vol->GetMute(&mute);
    v.ok = true;
    v.percent = std::clamp(int(std::lround(level * 100.0f)), 0, 100);
    v.muted = mute != FALSE;
    return v;
}

void audioSetVolume(int percent)
{
    if (!g_vol)
        return;
    g_vol->SetMasterVolumeLevelScalar(std::clamp(percent, 0, 100) / 100.0f, &kOurContext);
}

void audioSetMute(bool muted)
{
    if (!g_vol)
        return;
    g_vol->SetMute(muted ? TRUE : FALSE, &kOurContext);
}

void audioShutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_onChange = nullptr;
    }
    unbindEndpoint();
    if (g_volCb) {
        g_volCb->Release();
        g_volCb = nullptr;
    }
    if (g_enum) {
        if (g_devCb)
            g_enum->UnregisterEndpointNotificationCallback(g_devCb);
        g_enum->Release();
        g_enum = nullptr;
    }
    if (g_devCb) {
        g_devCb->Release();
        g_devCb = nullptr;
    }
    g_audioInited = false;
}

// =====================================================================================
// Radios (C++/WinRT, Windows.Devices.Radios)
// =====================================================================================
Radios radiosRead()
{
    Radios r;
    ComScope com(COINIT_MULTITHREADED);
    try {
        using namespace winrt::Windows::Devices::Radios;
        Radio::RequestAccessAsync().get(); // the answer is not needed to read the state
        const auto radios = Radio::GetRadiosAsync().get();
        for (const Radio &rd : radios) {
            if (rd.Kind() == RadioKind::WiFi) {
                r.hasWifi = true;
                r.wifi = r.wifi || rd.State() == RadioState::On;
            } else if (rd.Kind() == RadioKind::Bluetooth) {
                r.hasBluetooth = true;
                r.bluetooth = r.bluetooth || rd.State() == RadioState::On;
            }
        }
        r.ok = true;
    } catch (const winrt::hresult_error &) {
    } catch (...) {
    }
    return r;
}

bool radioSet(bool wifi, bool on)
{
    ComScope com(COINIT_MULTITHREADED);
    bool any = false, allOk = true;
    try {
        using namespace winrt::Windows::Devices::Radios;
        if (Radio::RequestAccessAsync().get() != RadioAccessStatus::Allowed)
            return false;
        const auto radios = Radio::GetRadiosAsync().get();
        for (const Radio &rd : radios) {
            if (rd.Kind() != (wifi ? RadioKind::WiFi : RadioKind::Bluetooth))
                continue;
            any = true;
            const bool isOn = rd.State() == RadioState::On;
            if (isOn == on)
                continue;
            if (rd.SetStateAsync(on ? RadioState::On : RadioState::Off).get() != RadioAccessStatus::Allowed)
                allOk = false;
        }
    } catch (...) {
        return false;
    }
    return any && allOk;
}

// =====================================================================================
// Power menu
// =====================================================================================
namespace {

bool enableShutdownPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid)
              && AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

bool runHidden(const wchar_t *cmdline)
{
    std::wstring cmd = cmdline;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

bool power(const QString &what)
{
    enableShutdownPrivilege();
    if (what == "suspend")
        return SetSuspendState(FALSE, FALSE, FALSE) != 0; // sleep (S3 / modern standby); blocks until resume
    if (what == "reboot") {
        if (ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_FLAG_PLANNED))
            return true;
        return runHidden(L"shutdown.exe /r /t 0");
    }
    if (what == "poweroff") {
        if (ExitWindowsEx(EWX_POWEROFF, SHTDN_REASON_FLAG_PLANNED))
            return true;
        return runHidden(L"shutdown.exe /s /t 0");
    }
    if (what == "logout")
        return ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_FLAG_PLANNED) != 0;
    return false;
}

// =====================================================================================
// Touch keyboard, Show Desktop
// =====================================================================================
namespace {

struct __declspec(uuid("37c994e7-432b-4834-a2f7-dce1f13b834b")) ITipInvocation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};
const CLSID kClsidUIHostNoLaunch = {0x4ce576fa, 0x83dc, 0x4f88, {0x95, 0x1c, 0x9d, 0x07, 0x82, 0xb4, 0xe3, 0x76}};

bool processRunning(const wchar_t *exe)
{
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exe) == 0) {
            found = true;
            break;
        }
    CloseHandle(snap);
    return found;
}

bool keyboardVisible()
{
    const HWND w = FindWindowW(L"IPTip_Main_Window", nullptr);
    return w && IsWindowVisible(w);
}

bool toggleTip()
{
    ITipInvocation *tip = nullptr;
    if (FAILED(CoCreateInstance(kClsidUIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                __uuidof(ITipInvocation), reinterpret_cast<void **>(&tip))) || !tip)
        return false;
    const HRESULT hr = tip->Toggle(GetDesktopWindow());
    tip->Release();
    return SUCCEEDED(hr);
}

} // namespace

void openKeyboard()
{
    ComScope com(COINIT_APARTMENTTHREADED);
    if (!processRunning(L"TabTip.exe")) {
        wchar_t base[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"CommonProgramFiles", base, MAX_PATH)) {
            std::wstring exe = std::wstring(base) + L"\\microsoft shared\\ink\\TabTip.exe";
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            for (int i = 0; i < 30 && !processRunning(L"TabTip.exe"); ++i)
                Sleep(100);
            Sleep(600);
            if (keyboardVisible())
                return; // starting TabTip already showed the keyboard
        }
    }
    if (toggleTip())
        return;
    ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

void showDesktop()
{
    ComScope com(COINIT_APARTMENTTHREADED);
    IShellDispatch4 *sh = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(Shell), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellDispatch4),
                                   reinterpret_cast<void **>(&sh))) && sh) {
        const HRESULT hr = sh->ToggleDesktop();
        sh->Release();
        if (SUCCEEDED(hr))
            return;
    }
    // fallback: Win + D
    INPUT in[4] = {};
    in[0].type = in[1].type = in[2].type = in[3].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_LWIN;
    in[1].ki.wVk = 'D';
    in[2].ki.wVk = 'D';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_LWIN;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// =====================================================================================
// Displays
// =====================================================================================
namespace {

struct Topology {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

bool queryConfig(UINT32 flags, Topology &t)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(flags, &np, &nm) != ERROR_SUCCESS)
            return false;
        t.paths.assign(np, {});
        t.modes.assign(nm, {});
        const LONG r = QueryDisplayConfig(flags, &np, t.paths.data(), &nm, t.modes.data(), nullptr);
        if (r == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (r != ERROR_SUCCESS)
            return false;
        t.paths.resize(np);
        t.modes.resize(nm);
        return true;
    }
    return false;
}

bool isInternalTech(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech)
{
    const UINT32 v = UINT32(tech);
    return v == 0x80000000u /* INTERNAL */ || v == UINT32(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED)
           || v == UINT32(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED);
}

QString sourceGdiName(const DISPLAYCONFIG_PATH_INFO &p)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME n = {};
    n.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    n.header.size = sizeof(n);
    n.header.adapterId = p.sourceInfo.adapterId;
    n.header.id = p.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&n.header) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(n.viewGdiDeviceName);
}

bool currentMode(const wchar_t *name, DEVMODEW &dm)
{
    dm = {};
    dm.dmSize = sizeof(dm);
    return EnumDisplaySettingsExW(name, ENUM_CURRENT_SETTINGS, &dm, 0) != FALSE;
}

} // namespace

DisplayState displayRead()
{
    DisplayState s;
    Topology act;
    if (!queryConfig(QDC_ONLY_ACTIVE_PATHS, act))
        return s;
    int intIdx = -1;
    bool activeExt = false, mirrored = false;
    for (size_t i = 0; i < act.paths.size(); ++i) {
        const auto &p = act.paths[i];
        if (isInternalTech(p.targetInfo.outputTechnology)) {
            if (intIdx < 0)
                intIdx = int(i);
        } else {
            activeExt = true;
        }
    }
    if (intIdx < 0 && act.paths.size() == 1 && !activeExt)
        intIdx = 0; // unusual driver: a lone screen is the built-in one
    if (intIdx >= 0) {
        const auto &ip = act.paths[size_t(intIdx)];
        for (size_t i = 0; i < act.paths.size(); ++i) {
            if (int(i) == intIdx)
                continue;
            const auto &p = act.paths[i];
            if (p.sourceInfo.id == ip.sourceInfo.id && p.sourceInfo.adapterId.LowPart == ip.sourceInfo.adapterId.LowPart
                && p.sourceInfo.adapterId.HighPart == ip.sourceInfo.adapterId.HighPart)
                mirrored = true;
        }
        s.internalName = sourceGdiName(ip);
    }
    if (intIdx < 0)
        s.mode = activeExt ? "external" : "handheld";
    else if (!activeExt)
        s.mode = "handheld";
    else
        s.mode = mirrored ? "duplicate" : "extend";

    Topology all;
    if (queryConfig(QDC_ALL_PATHS, all))
        for (const auto &p : all.paths)
            if (p.targetInfo.targetAvailable && !isInternalTech(p.targetInfo.outputTechnology)) {
                s.hasExternal = true;
                break;
            }
    if (activeExt)
        s.hasExternal = true;

    if (!s.internalName.isEmpty()) {
        DEVMODEW cur;
        const std::wstring name = s.internalName.toStdWString();
        if (currentMode(name.c_str(), cur)) {
            s.refresh = int(cur.dmDisplayFrequency);
            DEVMODEW dm = {};
            dm.dmSize = sizeof(dm);
            for (DWORD i = 0; EnumDisplaySettingsExW(name.c_str(), i, &dm, 0); ++i) {
                if (dm.dmPelsWidth != cur.dmPelsWidth || dm.dmPelsHeight != cur.dmPelsHeight
                    || dm.dmBitsPerPel != cur.dmBitsPerPel || dm.dmDisplayOrientation != cur.dmDisplayOrientation
                    || dm.dmDisplayFrequency <= 1)
                    continue;
                if (!s.rates.contains(int(dm.dmDisplayFrequency)))
                    s.rates.push_back(int(dm.dmDisplayFrequency));
            }
            std::sort(s.rates.begin(), s.rates.end());
        }
    }
    s.ok = true;
    return s;
}

bool displaySetRefresh(int hz)
{
    const DisplayState s = displayRead();
    if (s.internalName.isEmpty())
        return false;
    const std::wstring name = s.internalName.toStdWString();
    DEVMODEW dm;
    if (!currentMode(name.c_str(), dm))
        return false;
    dm.dmDisplayFrequency = DWORD(hz);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_DISPLAYORIENTATION
                  | DM_POSITION;
    return ChangeDisplaySettingsExW(name.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

bool displaySetMode(const QString &mode)
{
    UINT32 topo = 0;
    if (mode == "handheld")
        topo = SDC_TOPOLOGY_INTERNAL;
    else if (mode == "external")
        topo = SDC_TOPOLOGY_EXTERNAL;
    else if (mode == "duplicate")
        topo = SDC_TOPOLOGY_CLONE;
    else if (mode == "extend")
        topo = SDC_TOPOLOGY_EXTEND;
    else
        return false;
    return SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | topo) == ERROR_SUCCESS;
}

// =====================================================================================
// Sensors
// =====================================================================================
namespace {

quint64 ft(const FILETIME &f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

// ATKACPI (ASUS System Control Interface): DSTS reads a device. Same call layout as the daemon.
HANDLE atkAcpi()
{
    static HANDLE h = INVALID_HANDLE_VALUE;
    static bool tried = false;
    if (!tried) {
        tried = true;
        h = CreateFileW(L"\\\\.\\ATKACPI", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    return h;
}

// value with bit 16 set = device exists; -1 = not available
int atkDsts(quint32 device)
{
    const HANDLE h = atkAcpi();
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    quint32 in[4] = {0x53545344u /* DSTS */, 8u, device, 0u};
    quint32 out[4] = {};
    DWORD got = 0;
    if (!DeviceIoControl(h, 0x0022240C, in, sizeof(in), out, sizeof(out), &got, nullptr) || got < 4)
        return -1;
    return (out[0] & 0x10000) ? int(out[0] & 0xFFFF) : -1;
}

// Battery charge / discharge rate (mW) from the battery class driver; the handle stays open.
const GUID kBatteryInterface = {0x72631e54, 0x78A4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
HANDLE g_batHandle = INVALID_HANDLE_VALUE;
ULONG g_batTag = 0;
int g_batFails = 0;

void closeBattery()
{
    if (g_batHandle != INVALID_HANDLE_VALUE)
        CloseHandle(g_batHandle);
    g_batHandle = INVALID_HANDLE_VALUE;
    g_batTag = 0;
}

bool openBattery()
{
    HDEVINFO set = SetupDiGetClassDevsW(&kBatteryInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kBatteryInterface, i, &did); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
        if (need == 0)
            continue;
        QByteArray buf(int(need), 0);
        auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &did, det, need, nullptr, nullptr))
            continue;
        HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        DWORD wait = 0, tag = 0, got = 0;
        if (DeviceIoControl(h, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &got, nullptr)
            && tag != 0) {
            g_batHandle = h;
            g_batTag = tag;
            SetupDiDestroyDeviceInfoList(set);
            return true;
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return false;
}

// watts, always >= 0 (the panel shows the sign from "charging")
double batteryWatts()
{
    if (g_batHandle == INVALID_HANDLE_VALUE) {
        if (g_batFails > 3 && (g_batFails++ % 20) != 0)
            return 0; // no battery interface: do not enumerate every second
        if (!openBattery()) {
            ++g_batFails;
            return 0;
        }
        g_batFails = 0;
    }
    BATTERY_WAIT_STATUS bws = {};
    bws.BatteryTag = g_batTag;
    BATTERY_STATUS bs = {};
    DWORD got = 0;
    if (!DeviceIoControl(g_batHandle, IOCTL_BATTERY_QUERY_STATUS, &bws, sizeof(bws), &bs, sizeof(bs), &got, nullptr)) {
        closeBattery(); // tag changed or device gone: reopen next time
        return 0;
    }
    if (bs.Rate == LONG(BATTERY_UNKNOWN_RATE))
        return 0;
    return std::fabs(double(bs.Rate)) / 1000.0;
}

// GPU 3D engine load: sum of all "engtype_3D" engine instances (as Task Manager does), capped at 100.
PDH_HQUERY g_pdhQuery = nullptr;
PDH_HCOUNTER g_pdhCounter = nullptr;
bool g_pdhFailed = false;

int gpuLoad()
{
    if (g_pdhFailed)
        return -1;
    if (!g_pdhQuery) {
        if (PdhOpenQueryW(nullptr, 0, &g_pdhQuery) != ERROR_SUCCESS
            || PdhAddEnglishCounterW(g_pdhQuery, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0,
                                     &g_pdhCounter) != ERROR_SUCCESS) {
            g_pdhFailed = true;
            if (g_pdhQuery)
                PdhCloseQuery(g_pdhQuery);
            g_pdhQuery = nullptr;
            return -1;
        }
        PdhCollectQueryData(g_pdhQuery);
        return -1; // a rate needs two samples
    }
    if (PdhCollectQueryData(g_pdhQuery) != ERROR_SUCCESS)
        return -1;
    DWORD bytes = 0, count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(g_pdhCounter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
    if (st != PDH_MORE_DATA || bytes == 0)
        return st == ERROR_SUCCESS || st == PDH_NO_DATA ? 0 : -1;
    std::vector<char> buf(bytes);
    auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buf.data());
    if (PdhGetFormattedCounterArrayW(g_pdhCounter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS)
        return -1;
    double sum = 0;
    for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            sum += items[i].FmtValue.doubleValue;
    return std::clamp(int(std::lround(sum)), 0, 100);
}

} // namespace

Stats statsRead()
{
    static quint64 lastIdle = 0, lastTotal = 0;
    Stats s;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 i = ft(idle), total = ft(kernel) + ft(user); // kernel time includes idle
        if (lastTotal && total > lastTotal)
            s.cpuLoad = std::clamp(int(100 - (i - lastIdle) * 100 / (total - lastTotal)), 0, 100);
        lastIdle = i;
        lastTotal = total;
    }
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        s.ramUsedMb = int((ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024));
    s.batteryWatts = batteryWatts();
    s.gpuLoad = gpuLoad();
    {
        const AmdAdl::Sample g = AmdAdl::read();
        if (g.ok) {
            s.gpuTemp = g.tempC;
            s.socWatts = g.asicWatts > 0 ? g.asicWatts : g.socWatts;
        }
    }
    const int t = atkDsts(0x00120094);
    s.cpuTemp = (t > 0 && t < 150) ? t : 0;
    int fan = atkDsts(0x00110013);
    if (fan <= 0)
        fan = atkDsts(0x00110014);
    s.fanRpm = fan > 0 ? fan * 100 : 0;
    return s;
}

} // namespace SysWin

#endif
