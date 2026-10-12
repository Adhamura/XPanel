// Windows backend of AudioControl: Core Audio (MMDevice, IAudioEndpointVolume) and the
// undocumented IPolicyConfig for the default device (same method as EarTrumpet and AudioDeviceCmdlets).
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <propsys.h>
#include <propidl.h>

#include "AudioControl.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <atomic>

namespace {

// IPolicyConfig (Windows 7 and later variant). Only SetDefaultEndpoint is used.
struct DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfigX : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

// PKEY_Device_FriendlyName (a45c254e-df1c-4efd-8020-67d146a850e0, 14)
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

template <class T> void release(T *&p) { if (p) { p->Release(); p = nullptr; } }

IMMDeviceEnumerator *createEnumerator()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // S_FALSE or RPC_E_CHANGED_MODE are fine
    IMMDeviceEnumerator *e = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&e))))
        return nullptr;
    return e;
}

QString idOf(IMMDevice *d)
{
    LPWSTR id = nullptr;
    QString s;
    if (SUCCEEDED(d->GetId(&id)) && id) {
        s = QString::fromWCharArray(id);
        CoTaskMemFree(id);
    }
    return s;
}

IAudioEndpointVolume *volumeOf(IMMDevice *d)
{
    IAudioEndpointVolume *v = nullptr;
    if (FAILED(d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&v))))
        return nullptr;
    return v;
}

QVariantList listEndpoints(IMMDeviceEnumerator *en, EDataFlow flow, QString *defaultId)
{
    QVariantList out;
    if (IMMDevice *def = nullptr; SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &def)) && def) {
        *defaultId = idOf(def);
        def->Release();
    }
    IMMDeviceCollection *col = nullptr;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) || !col)
        return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        IMMDevice *d = nullptr;
        if (FAILED(col->Item(i, &d)) || !d)
            continue;
        QString label;
        IPropertyStore *ps = nullptr;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
                label = QString::fromWCharArray(pv.pwszVal);
            PropVariantClear(&pv);
            ps->Release();
        }
        int vol = 0;
        bool muted = false;
        if (IAudioEndpointVolume *v = volumeOf(d)) {
            float f = 0;
            BOOL m = FALSE;
            if (SUCCEEDED(v->GetMasterVolumeLevelScalar(&f)))
                vol = qRound(f * 100);
            v->GetMute(&m);
            muted = m;
            v->Release();
        }
        out << QVariantMap{{"name", idOf(d)}, {"label", label}, {"volume", vol}, {"muted", muted}};
        d->Release();
    }
    col->Release();
    return out;
}

bool setDefaultEndpoint(const QString &id)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IPolicyConfigX *pc = nullptr;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfigX), reinterpret_cast<void **>(&pc))) || !pc)
        return false;
    const std::wstring w = id.toStdWString();
    bool ok = true;
    for (ERole r : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(pc->SetDefaultEndpoint(w.c_str(), r)) && ok;
    pc->Release();
    return ok;
}

// Device-change notifications arrive on arbitrary threads; they only ask the GUI thread to refresh.
class Notifier : public IMMNotificationClient {
public:
    explicit Notifier(AudioControl *c) : m_ctl(c) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { poke(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { poke(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { poke(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { poke(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
    void detach() { m_ctl = QPointer<AudioControl>(); }

private:
    void poke()
    {
        if (m_pending.exchange(true))
            return;
        QPointer<AudioControl> c = m_ctl;
        if (!c)
            return;
        QMetaObject::invokeMethod(c.data(), [this, c] {
            QTimer::singleShot(250, c.data(), [this, c] {
                m_pending = false;
                if (c) c->refresh();
            });
        }, Qt::QueuedConnection);
    }
    std::atomic<ULONG> m_ref{1};
    std::atomic<bool> m_pending{false};
    QPointer<AudioControl> m_ctl;
};

} // namespace

AudioControl::~AudioControl()
{
    if (auto *n = static_cast<Notifier *>(m_mm)) {
        n->detach();
        if (IMMDeviceEnumerator *e = createEnumerator()) {
            e->UnregisterEndpointNotificationCallback(n);
            e->Release();
        }
        n->Release();
    }
}

void AudioControl::winInit()
{
    if (IMMDeviceEnumerator *e = createEnumerator()) {
        auto *n = new Notifier(this);
        if (SUCCEEDED(e->RegisterEndpointNotificationCallback(n)))
            m_mm = n;
        else
            n->Release();
        e->Release();
    }
}

bool AudioControl::noiseCancelAvailable() const { return false; }

void AudioControl::refresh()
{
    IMMDeviceEnumerator *e = createEnumerator();
    if (!e)
        return;
    QString defOut, defIn;
    m_outputs = listEndpoints(e, eRender, &defOut);
    m_inputs = listEndpoints(e, eCapture, &defIn);
    m_defOut = defOut;
    m_defIn = defIn;
    e->Release();
    emit changed();
}

void AudioControl::setDefaultOutput(const QString &name)
{
    setDefaultEndpoint(name);
    refresh();
}

void AudioControl::setDefaultInput(const QString &name)
{
    setDefaultEndpoint(name);
    refresh();
}

static IMMDevice *deviceById(IMMDeviceEnumerator *e, const QString &id)
{
    IMMDevice *d = nullptr;
    if (FAILED(e->GetDevice(id.toStdWString().c_str(), &d)))
        return nullptr;
    return d;
}

void AudioControl::setInputVolume(const QString &name, int percent)
{
    IMMDeviceEnumerator *e = createEnumerator();
    if (!e)
        return;
    if (IMMDevice *d = deviceById(e, name)) {
        if (IAudioEndpointVolume *v = volumeOf(d)) {
            v->SetMasterVolumeLevelScalar(float(qBound(0, percent, 100)) / 100.0f, nullptr);
            v->Release();
        }
        d->Release();
    }
    e->Release();
}

void AudioControl::setInputMute(const QString &name, bool mute)
{
    IMMDeviceEnumerator *e = createEnumerator();
    if (!e)
        return;
    if (IMMDevice *d = deviceById(e, name)) {
        if (IAudioEndpointVolume *v = volumeOf(d)) {
            v->SetMute(mute, nullptr);
            v->Release();
        }
        d->Release();
    }
    e->Release();
    refresh();
}
