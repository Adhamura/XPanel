#include "BrightnessWin.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <windows.h>
#include <oleauto.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <wbemidl.h>
#include <vector>

namespace BrightnessWin {
namespace {

struct ComInit {
    bool ok = false;
    bool uninit = false;
    ComInit()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            ok = uninit = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            ok = true; // this thread already runs COM in another mode: still usable
        }
    }
    ~ComInit()
    {
        if (uninit)
            CoUninitialize();
    }
};

void blanket(IUnknown *p)
{
    CoSetProxyBlanket(p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
}

IWbemServices *connectWmi()
{
    IWbemLocator *loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void **>(&loc))))
        return nullptr;
    IWbemServices *svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    const HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr))
        return nullptr;
    blanket(svc);
    return svc;
}

IEnumWbemClassObject *wql(IWbemServices *svc, const wchar_t *query)
{
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(query);
    IEnumWbemClassObject *en = nullptr;
    const HRESULT hr = svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(lang);
    SysFreeString(q);
    if (FAILED(hr) || !en)
        return nullptr;
    blanket(en);
    return en;
}

struct WmiPanel {
    QString instance;
    int pct = -1;
};

std::vector<WmiPanel> readWmiPanels(IWbemServices *svc)
{
    std::vector<WmiPanel> out;
    IEnumWbemClassObject *en = wql(svc, L"SELECT InstanceName, CurrentBrightness, Active FROM WmiMonitorBrightness");
    if (!en)
        return out;
    for (;;) {
        IWbemClassObject *obj = nullptr;
        ULONG n = 0;
        if (FAILED(en->Next(3000, 1, &obj, &n)) || n == 0)
            break;
        WmiPanel p;
        bool active = true;
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(obj->Get(L"InstanceName", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
            p.instance = QString::fromWCharArray(v.bstrVal);
        VariantClear(&v);
        if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) {
            if (v.vt == VT_UI1)
                p.pct = v.bVal;
            else if (v.vt == VT_I4 || v.vt == VT_UI4)
                p.pct = int(v.lVal);
            else if (v.vt == VT_I2 || v.vt == VT_UI2)
                p.pct = int(v.iVal);
        }
        VariantClear(&v);
        if (SUCCEEDED(obj->Get(L"Active", 0, &v, nullptr, nullptr)) && v.vt == VT_BOOL)
            active = v.boolVal != VARIANT_FALSE;
        VariantClear(&v);
        obj->Release();
        if (active && !p.instance.isEmpty())
            out.push_back(p);
    }
    en->Release();
    return out;
}

bool wmiSet(const QString &instance, int pct)
{
    IWbemServices *svc = connectWmi();
    if (!svc)
        return false;
    bool ok = false;
    QString esc = instance;
    esc.replace("\\", "\\\\");
    const QString query = QStringLiteral("SELECT * FROM WmiMonitorBrightnessMethods WHERE InstanceName='%1'").arg(esc);
    IEnumWbemClassObject *en = wql(svc, reinterpret_cast<const wchar_t *>(query.utf16()));
    if (en) {
        IWbemClassObject *obj = nullptr;
        ULONG n = 0;
        if (SUCCEEDED(en->Next(3000, 1, &obj, &n)) && n == 1) {
            VARIANT path;
            VariantInit(&path);
            IWbemClassObject *cls = nullptr, *inSig = nullptr, *inst = nullptr;
            BSTR clsName = SysAllocString(L"WmiMonitorBrightnessMethods");
            if (SUCCEEDED(obj->Get(L"__PATH", 0, &path, nullptr, nullptr)) && path.vt == VT_BSTR
                && SUCCEEDED(svc->GetObject(clsName, 0, nullptr, &cls, nullptr)) && cls
                && SUCCEEDED(cls->GetMethod(L"WmiSetBrightness", 0, &inSig, nullptr)) && inSig
                && SUCCEEDED(inSig->SpawnInstance(0, &inst)) && inst) {
                VARIANT t, b;
                VariantInit(&t);
                VariantInit(&b);
                t.vt = VT_I4;
                t.lVal = 0;
                b.vt = VT_I4;
                b.lVal = pct;
                inst->Put(L"Timeout", 0, &t, 0);
                inst->Put(L"Brightness", 0, &b, 0);
                BSTR method = SysAllocString(L"WmiSetBrightness");
                ok = SUCCEEDED(svc->ExecMethod(path.bstrVal, method, 0, nullptr, inst, nullptr, nullptr));
                SysFreeString(method);
            }
            SysFreeString(clsName);
            VariantClear(&path);
            if (inst)
                inst->Release();
            if (inSig)
                inSig->Release();
            if (cls)
                cls->Release();
            obj->Release();
        }
        en->Release();
    }
    svc->Release();
    return ok;
}

// ---- display config ----
struct Path {
    QString gdi, friendly, devicePath;
    bool internal = false;
};

std::vector<Path> queryPaths()
{
    std::vector<Path> out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    UINT32 np = 0, nm = 0;
    LONG rc = ERROR_INSUFFICIENT_BUFFER;
    for (int tries = 0; tries < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++tries) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS)
            return out;
        paths.resize(np);
        modes.resize(nm);
        rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr);
    }
    if (rc != ERROR_SUCCESS)
        return out;
    paths.resize(np);
    for (const DISPLAYCONFIG_PATH_INFO &pi : paths) {
        Path p;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
        sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sn.header.size = sizeof(sn);
        sn.header.adapterId = pi.sourceInfo.adapterId;
        sn.header.id = pi.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS)
            p.gdi = QString::fromWCharArray(sn.viewGdiDeviceName);
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn = {};
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = pi.targetInfo.adapterId;
        tn.header.id = pi.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) {
            p.friendly = QString::fromWCharArray(tn.monitorFriendlyDeviceName).trimmed();
            p.devicePath = QString::fromWCharArray(tn.monitorDevicePath);
            const auto t = tn.outputTechnology;
            p.internal = t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL
                || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED
                || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        }
        if (!p.gdi.isEmpty())
            out.push_back(p);
    }
    return out;
}

// "\\?\DISPLAY#TMX0002#5&1f28af72&0&UID256#{guid}" -> "DISPLAY\TMX0002\5&1f28af72&0&UID256"
QString devicePathToInstance(QString p)
{
    if (p.startsWith(QStringLiteral("\\\\?\\")))
        p = p.mid(4);
    const int brace = p.indexOf(QStringLiteral("#{"));
    if (brace > 0)
        p = p.left(brace);
    p.replace('#', '\\');
    return p;
}

struct Phys {
    HANDLE h;
    QString desc;
};

struct MonInfo {
    HMONITOR hm;
    QString gdi;
};

BOOL CALLBACK enumMon(HMONITOR hm, HDC, LPRECT, LPARAM lp)
{
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hm, &mi))
        reinterpret_cast<std::vector<MonInfo> *>(lp)->push_back({hm, QString::fromWCharArray(mi.szDevice)});
    return TRUE;
}

std::vector<Phys> physicalMonitors(const QString &gdi)
{
    std::vector<Phys> out;
    std::vector<MonInfo> mons;
    EnumDisplayMonitors(nullptr, nullptr, enumMon, reinterpret_cast<LPARAM>(&mons));
    for (const MonInfo &m : mons) {
        if (m.gdi.compare(gdi, Qt::CaseInsensitive) != 0)
            continue;
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m.hm, &n) || n == 0)
            break;
        std::vector<PHYSICAL_MONITOR> pm(n);
        if (!GetPhysicalMonitorsFromHMONITOR(m.hm, n, pm.data()))
            break;
        for (DWORD i = 0; i < n; ++i)
            out.push_back({pm[i].hPhysicalMonitor, QString::fromWCharArray(pm[i].szPhysicalMonitorDescription)});
        break;
    }
    return out;
}

void destroyAll(std::vector<Phys> &v)
{
    for (Phys &p : v)
        DestroyPhysicalMonitor(p.h);
    v.clear();
}

int pickPhys(const std::vector<Phys> &v, const Monitor &m)
{
    if (v.empty())
        return -1;
    if (!m.friendly.isEmpty())
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].desc.compare(m.friendly, Qt::CaseInsensitive) == 0)
                return int(i);
    return qMin(m.ddcIndex, int(v.size()) - 1);
}

// min/cur/max; false when the monitor does not talk DDC/CI.
bool ddcRead(HANDLE h, DWORD &mn, DWORD &cur, DWORD &mx)
{
    if (GetMonitorBrightness(h, &mn, &cur, &mx) && mx > mn)
        return true;
    DWORD c = 0, m = 0;
    if (GetVCPFeatureAndVCPFeatureReply(h, 0x10, nullptr, &c, &m) && m > 0) {
        mn = 0;
        cur = c;
        mx = m;
        return true;
    }
    return false;
}


// ---- software dimming (external monitor that does not talk DDC/CI on Windows) ----
// SetDeviceGammaRamp with identity * f. GDI rejects ramps too far from identity, so the lowest
// accepted f is probed once per monitor (the registry limit is left alone).
struct SoftState {
    int pct = 100;
    double floor = -1; // lowest accepted f, -1 = not probed
};
QMutex g_softMx;
QHash<QString, SoftState> g_soft; // key: lower-case GDI name

bool applyRamp(const QString &gdi, double f)
{
    HDC dc = CreateDCW(L"DISPLAY", reinterpret_cast<const wchar_t *>(gdi.utf16()), nullptr, nullptr);
    if (!dc)
        return false;
    WORD ramp[3][256];
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            ramp[c][i] = WORD(qBound(0.0, i * 257.0 * f + 0.5, 65535.0));
    const bool ok = SetDeviceGammaRamp(dc, ramp) != FALSE;
    DeleteDC(dc);
    return ok;
}

void restoreAllSoft()
{
    QMutexLocker l(&g_softMx);
    for (auto it = g_soft.constBegin(); it != g_soft.constEnd(); ++it)
        applyRamp(it.key(), 1.0);
}

// caller holds g_softMx
double probeFloor(const QString &gdi, SoftState &st)
{
    if (st.floor >= 0)
        return st.floor;
    double best = 1.0;
    for (int k = 95; k >= 20; k -= 5) {
        if (!applyRamp(gdi, k / 100.0))
            break;
        best = k / 100.0;
    }
    applyRamp(gdi, 1.0);
    st.floor = best;
    return best;
}

bool softApply(const QString &gdi, int pct, bool store)
{
    static bool hooked = false;
    const QString key = gdi.toLower();
    QMutexLocker l(&g_softMx);
    if (!hooked) {
        hooked = true;
        qAddPostRoutine(restoreAllSoft);
    }
    SoftState &st = g_soft[key];
    if (store)
        st.pct = pct;
    const double fl = probeFloor(gdi, st);
    return applyRamp(gdi, fl + (1.0 - fl) * st.pct / 100.0);
}

void markSoft(Monitor &m)
{
    m.soft = true;
    QMutexLocker l(&g_softMx);
    m.pct = g_soft.value(m.gdi.toLower()).pct;
    if (!g_soft.contains(m.gdi.toLower()))
        g_soft.insert(m.gdi.toLower(), SoftState());
}

} // namespace

QVector<Monitor> enumerate(bool readDdc)
{
    QVector<Monitor> out;
    ComInit com;
    const std::vector<Path> paths = queryPaths();
    std::vector<WmiPanel> panels;
    bool wmiTried = false;
    QHash<QString, int> perGdi;
    int extN = 0;
    for (const Path &p : paths) {
        Monitor m;
        m.gdi = p.gdi;
        m.friendly = p.friendly;
        m.internal = p.internal;
        if (p.internal) {
            if (!wmiTried && com.ok) {
                wmiTried = true;
                if (IWbemServices *svc = connectWmi()) {
                    panels = readWmiPanels(svc);
                    svc->Release();
                }
            }
            const QString inst = devicePathToInstance(p.devicePath);
            const WmiPanel *hit = nullptr;
            for (const WmiPanel &w : panels)
                if (!inst.isEmpty() && w.instance.startsWith(inst, Qt::CaseInsensitive))
                    hit = &w;
            if (!hit && panels.size() == 1)
                hit = &panels.front();
            if (!hit)
                continue; // no software brightness for this panel
            m.id = QStringLiteral("winint");
            m.label = QStringLiteral("Built-in");
            m.wmiInstance = hit->instance;
            m.pct = qBound(0, hit->pct, 100);
        } else {
            m.ddcIndex = perGdi[p.gdi]++;
            m.id = QStringLiteral("winext%1").arg(extN++);
            m.label = p.friendly; // empty: the caller names it
            bool softKnown;
            {
                QMutexLocker l(&g_softMx);
                softKnown = g_soft.contains(p.gdi.toLower());
            }
            if (readDdc) {
                std::vector<Phys> ph = physicalMonitors(p.gdi);
                const int i = pickPhys(ph, m);
                DWORD mn = 0, cur = 0, mx = 0;
                const bool ok = i >= 0 && ddcRead(ph[size_t(i)].h, mn, cur, mx);
                destroyAll(ph);
                if (ok) {
                    m.pct = qBound(0, int((cur - mn) * 100.0 / (mx - mn) + 0.5), 100);
                    QMutexLocker l(&g_softMx);
                    g_soft.remove(p.gdi.toLower());
                } else {
                    markSoft(m); // no DDC/CI: software dimming
                }
            } else if (softKnown) {
                markSoft(m);
            }
        }
        out.push_back(m);
    }
    reapplySoft();
    return out;
}

void reapplySoft()
{
    QStringList todo;
    {
        QMutexLocker l(&g_softMx);
        for (auto it = g_soft.constBegin(); it != g_soft.constEnd(); ++it)
            if (it->pct != 100)
                todo << it.key();
    }
    for (const QString &g : todo)
        softApply(g, 0, false);
}

bool setPercent(const Monitor &m, int percent)
{
    percent = qBound(0, percent, 100);
    ComInit com;
    if (m.internal)
        return com.ok && wmiSet(m.wmiInstance, percent);
    if (m.soft)
        return softApply(m.gdi, percent, true);
    std::vector<Phys> ph = physicalMonitors(m.gdi);
    const int i = pickPhys(ph, m);
    bool ok = false;
    DWORD mn = 0, cur = 0, mx = 0;
    if (i >= 0 && ddcRead(ph[size_t(i)].h, mn, cur, mx)) {
        const DWORD raw = mn + DWORD((mx - mn) * percent / 100.0 + 0.5);
        ok = SetMonitorBrightness(ph[size_t(i)].h, raw) || SetVCPFeature(ph[size_t(i)].h, 0x10, raw);
    }
    destroyAll(ph);
    return ok;
}

} // namespace BrightnessWin
