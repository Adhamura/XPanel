// Windows backend of NetworkControl: Wi-Fi through the WLAN API (wlanapi), Bluetooth through
// C++/WinRT (Windows.Devices.Enumeration / Windows.Devices.Bluetooth).
// WinRT calls block (.get()), so they run only on worker threads; results go back to the GUI
// thread with QMetaObject::invokeMethod.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>

#include <windows.h>
#include <wlanapi.h>

#include "NetworkControl.h"

#include <QDesktopServices>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace wdev = winrt::Windows::Devices;
using winrt::Windows::Devices::Enumeration::DeviceInformation;
using winrt::Windows::Devices::Enumeration::DeviceInformationKind;

// ---------------------------------------------------------------------------------------
// Wi-Fi (WLAN API)
// ---------------------------------------------------------------------------------------
namespace {

QString ssidToString(const DOT11_SSID &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.ucSSID), int(s.uSSIDLength));
}

QString securityName(const WLAN_AVAILABLE_NETWORK &n)
{
    if (!n.bSecurityEnabled)
        return QString();
    switch (n.dot11DefaultAuthAlgorithm) {
    case DOT11_AUTH_ALGO_80211_SHARED_KEY: return "WEP";
    case DOT11_AUTH_ALGO_WPA: case DOT11_AUTH_ALGO_WPA_PSK: return "WPA";
    case DOT11_AUTH_ALGO_RSNA: case DOT11_AUTH_ALGO_RSNA_PSK: return "WPA2";
    case DOT11_AUTH_ALGO_WPA3_ENT_192: case DOT11_AUTH_ALGO_WPA3_SAE: return "WPA3";
    default: return "WPA";
    }
}

// All visible networks of all Wi-Fi adapters (one entry per SSID).
QVariantList readNetworks(HANDLE h, bool scan)
{
    QVariantList out;
    if (!h)
        return out;
    PWLAN_INTERFACE_INFO_LIST ifs = nullptr;
    if (WlanEnumInterfaces(h, nullptr, &ifs) != ERROR_SUCCESS || !ifs)
        return out;
    std::map<QString, QVariantMap> best;
    for (DWORD i = 0; i < ifs->dwNumberOfItems; ++i) {
        const GUID &guid = ifs->InterfaceInfo[i].InterfaceGuid;
        if (scan)
            WlanScan(h, &guid, nullptr, nullptr, nullptr);
        PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
        if (WlanGetAvailableNetworkList(h, &guid, 0, nullptr, &list) != ERROR_SUCCESS || !list)
            continue;
        for (DWORD k = 0; k < list->dwNumberOfItems; ++k) {
            const WLAN_AVAILABLE_NETWORK &n = list->Network[k];
            const QString ssid = ssidToString(n.dot11Ssid);
            if (ssid.isEmpty())
                continue;
            const bool active = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
            const int sig = int(n.wlanSignalQuality);
            const QString sec = securityName(n);
            QVariantMap m{{"active", active}, {"ssid", ssid}, {"signal", sig}, {"secure", n.bSecurityEnabled != 0},
                          {"security", sec}, {"known", (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0}};
            auto it = best.find(ssid);
            if (it == best.end())
                best.emplace(ssid, m);
            else if (!it->second["active"].toBool() && (active || sig > it->second["signal"].toInt()))
                it->second = m;
        }
        WlanFreeMemory(list);
    }
    WlanFreeMemory(ifs);
    for (auto &kv : best)
        out << kv.second;
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x["active"].toBool() != y["active"].toBool())
            return x["active"].toBool();
        return x["signal"].toInt() > y["signal"].toInt();
    });
    return out;
}

enum class ConnectResult { Started, Failed, Unsupported };

QString hexOf(const QByteArray &b) { return QString::fromLatin1(b.toHex().toUpper()); }

// Connect to ssid. Uses the saved profile when there is one and no password was given;
// otherwise writes a profile (open, WPA2-Personal or WPA3-Personal) first.
ConnectResult doConnect(HANDLE h, const QString &ssid, const QString &password)
{
    PWLAN_INTERFACE_INFO_LIST ifs = nullptr;
    if (!h || WlanEnumInterfaces(h, nullptr, &ifs) != ERROR_SUCCESS || !ifs)
        return ConnectResult::Failed;
    ConnectResult result = ConnectResult::Failed;
    const QByteArray ssidBytes = ssid.toUtf8();
    for (DWORD i = 0; i < ifs->dwNumberOfItems && result != ConnectResult::Started; ++i) {
        const GUID guid = ifs->InterfaceInfo[i].InterfaceGuid;
        PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
        if (WlanGetAvailableNetworkList(h, &guid, 0, nullptr, &list) != ERROR_SUCCESS || !list)
            continue;
        for (DWORD k = 0; k < list->dwNumberOfItems; ++k) {
            const WLAN_AVAILABLE_NETWORK &n = list->Network[k];
            if (QByteArray(reinterpret_cast<const char *>(n.dot11Ssid.ucSSID), int(n.dot11Ssid.uSSIDLength)) != ssidBytes)
                continue;
            std::wstring profile;
            const bool hasProfile = (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
            if (hasProfile && password.isEmpty()) {
                profile = n.strProfileName;
            } else {
                QString auth, enc = "AES";
                if (!n.bSecurityEnabled) {
                    auth = "open"; enc = "none";
                } else if (n.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE) {
                    auth = "WPA3SAE";
                } else if (n.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK) {
                    auth = "WPA2PSK";
                } else if (n.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_WPA_PSK) {
                    auth = "WPAPSK"; enc = "TKIP";
                } else if (hasProfile) {
                    profile = n.strProfileName; // enterprise / WEP with an existing profile: use it
                }
                if (profile.empty()) {
                    if (auth.isEmpty() || (n.bSecurityEnabled && password.isEmpty())) {
                        result = auth.isEmpty() ? ConnectResult::Unsupported : ConnectResult::Failed;
                        break;
                    }
                    const QString name = ssid.toHtmlEscaped();
                    QString xml = QString(
                        "<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                        "<name>%1</name><SSIDConfig><SSID><hex>%2</hex><name>%1</name></SSID></SSIDConfig>"
                        "<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
                        "<MSM><security><authEncryption><authentication>%3</authentication><encryption>%4</encryption>"
                        "<useOneX>false</useOneX></authEncryption>")
                        .arg(name, hexOf(ssidBytes), auth, enc);
                    if (n.bSecurityEnabled)
                        xml += QString("<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
                                       "<keyMaterial>%1</keyMaterial></sharedKey>").arg(password.toHtmlEscaped());
                    xml += "</security></MSM></WLANProfile>";
                    DWORD reason = 0;
                    const std::wstring wxml = xml.toStdWString();
                    if (WlanSetProfile(h, &guid, 0, wxml.c_str(), nullptr, TRUE, nullptr, &reason) != ERROR_SUCCESS)
                        break;
                    profile = ssid.toStdWString();
                }
            }
            WLAN_CONNECTION_PARAMETERS p{};
            p.wlanConnectionMode = wlan_connection_mode_profile;
            p.strProfile = profile.c_str();
            p.dot11BssType = n.dot11BssType;
            p.dwFlags = 0;
            if (WlanConnect(h, &guid, &p, nullptr) == ERROR_SUCCESS)
                result = ConnectResult::Started;
            break;
        }
        WlanFreeMemory(list);
    }
    WlanFreeMemory(ifs);
    return result;
}

void CALLBACK wlanCallback(PWLAN_NOTIFICATION_DATA d, PVOID ctx)
{
    if (!d || d->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM)
        return;
    auto *self = static_cast<NetworkControl *>(ctx);
    const int code = int(d->NotificationCode);
    QString ssid;
    bool ok = false;
    if ((code == wlan_notification_acm_connection_complete || code == wlan_notification_acm_connection_attempt_fail)
        && d->pData && d->dwDataSize >= sizeof(WLAN_CONNECTION_NOTIFICATION_DATA)) {
        const auto *c = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA *>(d->pData);
        ssid = ssidToString(c->dot11Ssid);
        ok = code == wlan_notification_acm_connection_complete && c->wlanReasonCode == WLAN_REASON_CODE_SUCCESS;
    }
    QMetaObject::invokeMethod(self, [self, code, ssid, ok] { self->onWlanEvent(code, ssid, ok); }, Qt::QueuedConnection);
}

} // namespace

NetworkControl::~NetworkControl()
{
    if (m_wlan) {
        WlanRegisterNotification(m_wlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr);
        WlanCloseHandle(m_wlan, nullptr);
    }
}

void NetworkControl::winInit()
{
    m_wifiTimer = new QTimer(this);
    m_wifiTimer->setSingleShot(true);
    m_wifiTimer->setInterval(400);
    connect(m_wifiTimer, &QTimer::timeout, this, [this] { wifiRefresh(false); });

    DWORD ver = 0;
    HANDLE h = nullptr;
    if (WlanOpenHandle(2, nullptr, &ver, &h) == ERROR_SUCCESS) {
        m_wlan = h;
        WlanRegisterNotification(h, WLAN_NOTIFICATION_SOURCE_ACM, FALSE, wlanCallback, this, nullptr, nullptr);
    }
}

void NetworkControl::onWlanEvent(int code, const QString &ssid, bool ok)
{
    if (code == wlan_notification_acm_connection_complete || code == wlan_notification_acm_connection_attempt_fail) {
        if (!ssid.isEmpty())
            setMessage(ok ? tr("Connected to %1").arg(ssid) : tr("Could not connect to %1").arg(ssid));
    }
    switch (code) {
    case wlan_notification_acm_scan_complete:
    case wlan_notification_acm_connection_complete:
    case wlan_notification_acm_connection_attempt_fail:
    case wlan_notification_acm_disconnected:
    case wlan_notification_acm_network_available:
    case wlan_notification_acm_network_not_available:
        m_wifiTimer->start();
        break;
    default:
        break;
    }
}

// Read the list on a worker thread (no WlanScan unless scan is true).
void NetworkControl::wifiRefresh(bool scan)
{
    HANDLE h = m_wlan;
    QPointer<NetworkControl> self(this);
    std::thread([h, scan, self] {
        const QVariantList list = readNetworks(h, scan);
        QMetaObject::invokeMethod(self.data(), [self, list] {
            if (!self) return;
            self->m_networks = list;
            emit self->networksChanged();
        }, Qt::QueuedConnection);
    }).detach();
}

void NetworkControl::scanWifi()
{
    if (!m_wlan) {
        setMessage(tr("No Wi-Fi adapter found"));
        return;
    }
    begin();
    HANDLE h = m_wlan;
    QPointer<NetworkControl> self(this);
    std::thread([h, self] {
        const QVariantList list = readNetworks(h, true);
        QMetaObject::invokeMethod(self.data(), [self, list] {
            if (!self) return;
            self->m_networks = list;
            emit self->networksChanged();
            self->end();
        }, Qt::QueuedConnection);
    }).detach();
}

void NetworkControl::connectWifi(const QString &ssid, const QString &password)
{
    if (!m_wlan)
        return;
    begin();
    setMessage(tr("Connecting to %1…").arg(ssid));
    HANDLE h = m_wlan;
    QPointer<NetworkControl> self(this);
    std::thread([h, self, ssid, password] {
        const ConnectResult r = doConnect(h, ssid, password);
        QMetaObject::invokeMethod(self.data(), [self, r, ssid] {
            if (!self) return;
            // success is reported later by the WLAN notification
            if (r == ConnectResult::Failed)
                self->setMessage(tr("Could not connect to %1").arg(ssid));
            else if (r == ConnectResult::Unsupported)
                self->setMessage(tr("This network type is not supported here. Use Windows Wi-Fi settings."));
            self->end();
        }, Qt::QueuedConnection);
    }).detach();
}

void NetworkControl::disconnectWifi()
{
    if (!m_wlan)
        return;
    begin();
    HANDLE h = m_wlan;
    QPointer<NetworkControl> self(this);
    std::thread([h, self] {
        PWLAN_INTERFACE_INFO_LIST ifs = nullptr;
        if (WlanEnumInterfaces(h, nullptr, &ifs) == ERROR_SUCCESS && ifs) {
            for (DWORD i = 0; i < ifs->dwNumberOfItems; ++i)
                WlanDisconnect(h, &ifs->InterfaceInfo[i].InterfaceGuid, nullptr);
            WlanFreeMemory(ifs);
        }
        QMetaObject::invokeMethod(self.data(), [self] { if (self) { self->end(); self->wifiRefresh(false); } }, Qt::QueuedConnection);
    }).detach();
}

// ---------------------------------------------------------------------------------------
// Bluetooth (C++/WinRT)
// ---------------------------------------------------------------------------------------
namespace {

using winrt::hstring;

winrt::Windows::Foundation::Collections::IVector<hstring> btProps()
{
    return winrt::single_threaded_vector<hstring>({L"System.Devices.Aep.DeviceAddress", L"System.Devices.Aep.IsConnected",
                                                   L"System.Devices.Aep.IsPaired", L"System.Devices.Aep.CanPair"});
}

QVariantMap btEntry(const DeviceInformation &info, bool paired)
{
    auto props = info.Properties();
    QString mac = QString::fromStdWString(std::wstring(winrt::unbox_value_or<hstring>(props.TryLookup(L"System.Devices.Aep.DeviceAddress"), L"")))
                      .toUpper();
    const bool connected = winrt::unbox_value_or<bool>(props.TryLookup(L"System.Devices.Aep.IsConnected"), false);
    return QVariantMap{{"mac", mac}, {"name", QString::fromStdWString(std::wstring(info.Name()))},
                       {"connected", connected}, {"paired", paired},
                       {"id", QString::fromStdWString(std::wstring(info.Id()))}};
}

void btAddAll(QVariantList &list, const hstring &aqs, bool paired)
{
    try {
        auto found = DeviceInformation::FindAllAsync(aqs, btProps(), DeviceInformationKind::AssociationEndpoint).get();
        for (auto const &info : found) {
            QVariantMap e = btEntry(info, paired);
            if (e["mac"].toString().isEmpty())
                continue;
            bool dup = false;
            for (QVariant &v : list) {
                QVariantMap o = v.toMap();
                if (o["mac"] == e["mac"]) {
                    o["connected"] = o["connected"].toBool() || e["connected"].toBool();
                    v = o;
                    dup = true;
                    break;
                }
            }
            if (!dup)
                list << e;
        }
    } catch (winrt::hresult_error const &) {
    }
}

QVariantList btPairedList()
{
    QVariantList list;
    btAddAll(list, wdev::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), true);
    btAddAll(list, wdev::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), true);
    return list;
}

struct ScanState {
    std::mutex mu;
    std::map<std::wstring, DeviceInformation> found;
};

// Watch for unpaired devices for a few seconds.
QVariantList btScanUnpaired(int ms)
{
    auto st = std::make_shared<ScanState>();
    std::vector<wdev::Enumeration::DeviceWatcher> watchers;
    std::vector<wdev::Enumeration::DeviceWatcher::Added_revoker> r1;
    std::vector<wdev::Enumeration::DeviceWatcher::Updated_revoker> r2;
    const hstring sels[2] = {wdev::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(false),
                             wdev::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(false)};
    try {
        for (const hstring &sel : sels) {
            auto w = DeviceInformation::CreateWatcher(sel, btProps(), DeviceInformationKind::AssociationEndpoint);
            r1.push_back(w.Added(winrt::auto_revoke, [st](auto &&, DeviceInformation const &info) {
                std::lock_guard<std::mutex> g(st->mu);
                st->found.insert_or_assign(std::wstring(info.Id()), info);
            }));
            r2.push_back(w.Updated(winrt::auto_revoke, [st](auto &&, wdev::Enumeration::DeviceInformationUpdate const &u) {
                std::lock_guard<std::mutex> g(st->mu);
                auto it = st->found.find(std::wstring(u.Id()));
                if (it != st->found.end())
                    it->second.Update(u);
            }));
            w.Start();
            watchers.push_back(w);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        for (auto &w : watchers)
            if (w.Status() == wdev::Enumeration::DeviceWatcherStatus::Started
                || w.Status() == wdev::Enumeration::DeviceWatcherStatus::EnumerationCompleted)
                w.Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    } catch (winrt::hresult_error const &) {
    }
    QVariantList out;
    std::lock_guard<std::mutex> g(st->mu);
    for (auto &kv : st->found) {
        QVariantMap e = btEntry(kv.second, false);
        if (e["mac"].toString().isEmpty() || e["name"].toString().isEmpty())
            continue; // skip devices that did not report a name
        bool dup = false;
        for (const QVariant &v : out)
            dup = dup || v.toMap()["mac"] == e["mac"];
        if (!dup)
            out << e;
    }
    return out;
}

template <class F> void runWinrt(F f)
{
    std::thread([f] {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}
        f();
        try { winrt::uninit_apartment(); } catch (...) {}
    }).detach();
}

QVariantList btMerge(QVariantList paired, const QVariantList &found)
{
    for (const QVariant &f : found) {
        bool dup = false;
        for (const QVariant &p : paired)
            dup = dup || p.toMap()["mac"] == f.toMap()["mac"];
        if (!dup)
            paired << f;
    }
    std::sort(paired.begin(), paired.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x["connected"].toBool() != y["connected"].toBool())
            return x["connected"].toBool();
        return x["paired"].toBool() > y["paired"].toBool();
    });
    return paired;
}

DeviceInformation btInfoFromId(const QString &id)
{
    return DeviceInformation::CreateFromIdAsync(hstring(id.toStdWString()), btProps(), DeviceInformationKind::AssociationEndpoint).get();
}

QString btIdOf(const QVariantList &devices, const QString &mac, bool *paired = nullptr)
{
    for (const QVariant &v : devices) {
        const QVariantMap m = v.toMap();
        if (m["mac"].toString().compare(mac, Qt::CaseInsensitive) == 0) {
            if (paired) *paired = m["paired"].toBool();
            return m["id"].toString();
        }
    }
    return QString();
}

} // namespace

void NetworkControl::listBluetooth()
{
    QPointer<NetworkControl> self(this);
    const QVariantList found = m_btFound;
    runWinrt([self, found] {
        const QVariantList list = btMerge(btPairedList(), found);
        QMetaObject::invokeMethod(self.data(), [self, list] {
            if (!self) return;
            self->m_devices = list;
            emit self->devicesChanged();
        }, Qt::QueuedConnection);
    });
}

void NetworkControl::scanBluetooth()
{
    begin();
    QPointer<NetworkControl> self(this);
    runWinrt([self] {
        const QVariantList paired = btPairedList();
        QMetaObject::invokeMethod(self.data(), [self, paired] {
            if (!self) return;
            self->m_devices = btMerge(paired, self->m_btFound);
            emit self->devicesChanged();
        }, Qt::QueuedConnection);
        const QVariantList found = btScanUnpaired(8000);
        const QVariantList paired2 = btPairedList();
        QMetaObject::invokeMethod(self.data(), [self, found, paired2] {
            if (!self) return;
            self->m_btFound = found;
            self->m_devices = btMerge(paired2, found);
            emit self->devicesChanged();
            self->end();
        }, Qt::QueuedConnection);
    });
}

void NetworkControl::connectBluetooth(const QString &mac)
{
    bool paired = false;
    const QString id = btIdOf(m_devices, mac, &paired);
    if (id.isEmpty())
        return;
    if (paired) {
        // Windows has no public API to connect an already paired audio device
        setMessage(tr("Windows cannot connect a paired device from here. Opened the Bluetooth settings."));
        QDesktopServices::openUrl(QUrl("ms-settings:bluetooth"));
        return;
    }
    begin();
    setMessage(tr("Pairing…"));
    QPointer<NetworkControl> self(this);
    runWinrt([self, id] {
        using namespace winrt::Windows::Devices::Enumeration;
        bool ok = false;
        try {
            DeviceInformation info = btInfoFromId(id);
            auto custom = info.Pairing().Custom();
            auto tok = custom.PairingRequested([self](DeviceInformationCustomPairing const &, DevicePairingRequestedEventArgs const &a) {
                switch (a.PairingKind()) {
                case DevicePairingKinds::DisplayPin:
                case DevicePairingKinds::ConfirmPinMatch: {
                    const QString pin = QString::fromStdWString(std::wstring(a.Pin()));
                    QMetaObject::invokeMethod(self.data(), [self, pin] {
                        if (self) self->setMessage(tr("Pairing code: %1. Confirm it on the device.").arg(pin));
                    }, Qt::QueuedConnection);
                    a.Accept();
                    break;
                }
                case DevicePairingKinds::ProvidePin:
                    a.Accept(L"0000");
                    break;
                default:
                    a.Accept();
                    break;
                }
            });
            auto res = custom.PairAsync(DevicePairingKinds::ConfirmOnly | DevicePairingKinds::DisplayPin
                                        | DevicePairingKinds::ConfirmPinMatch | DevicePairingKinds::ProvidePin,
                                        DevicePairingProtectionLevel::Default).get();
            custom.PairingRequested(tok);
            ok = res.Status() == DevicePairingResultStatus::Paired || res.Status() == DevicePairingResultStatus::AlreadyPaired;
        } catch (winrt::hresult_error const &) {
        }
        QMetaObject::invokeMethod(self.data(), [self, ok, id] {
            if (!self) return;
            self->setMessage(ok ? tr("Paired. If it does not connect, use the Bluetooth settings of Windows.")
                                : tr("Could not pair"));
            if (ok) {
                QVariantList rest;
                for (const QVariant &v : std::as_const(self->m_btFound))
                    if (v.toMap()["id"].toString() != id) rest << v;
                self->m_btFound = rest;
            }
            self->listBluetooth();
            self->end();
        }, Qt::QueuedConnection);
    });
}

void NetworkControl::disconnectBluetooth(const QString &)
{
    // no public API to disconnect a paired device without removing it
    setMessage(tr("Windows cannot disconnect a device from here. Opened the Bluetooth settings."));
    QDesktopServices::openUrl(QUrl("ms-settings:bluetooth"));
}

void NetworkControl::removeBluetooth(const QString &mac)
{
    const QString id = btIdOf(m_devices, mac);
    if (id.isEmpty())
        return;
    begin();
    QPointer<NetworkControl> self(this);
    runWinrt([self, id] {
        bool ok = false;
        try {
            ok = btInfoFromId(id).Pairing().UnpairAsync().get().Status()
                 == winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus::Unpaired;
        } catch (winrt::hresult_error const &) {
        }
        QMetaObject::invokeMethod(self.data(), [self, ok] {
            if (!self) return;
            if (!ok) self->setMessage(tr("Could not remove the device"));
            self->listBluetooth();
            self->end();
        }, Qt::QueuedConnection);
    });
}
