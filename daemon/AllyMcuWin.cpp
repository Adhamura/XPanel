#include "AllyMcuWin.h"

#include <QByteArray>

#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}

bool AllyMcuWin::open()
{
    if (m_h)
        return true;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; !m_h && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0)
            continue;
        QByteArray buf(int(size), '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        const HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof(attr);
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        const bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == 0x0B05
                           && HidD_GetPreparsedData(h, &pp) && HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS
                           && caps.UsagePage == 0xFF31 && caps.Usage == 0x0076 && caps.FeatureReportByteLength == 64;
        if (pp)
            HidD_FreePreparsedData(pp);
        if (match)
            m_h = h;
        else
            CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return m_h != nullptr;
}

void AllyMcuWin::close()
{
    if (m_h)
        CloseHandle(m_h);
    m_h = nullptr;
}

bool AllyMcuWin::send(const AllyMcu::Report &r)
{
    if (!m_h || !AllyMcu::allowed(r))
        return false;
    AllyMcu::Report copy = r;
    const bool ok = HidD_SetFeature(m_h, copy.data(), ULONG(copy.size())) != FALSE;
    Sleep(8); // the MCU takes the reports one by one
    return ok;
}

bool AllyMcuWin::sendAll(const std::vector<AllyMcu::Report> &rs)
{
    bool ok = true;
    for (const AllyMcu::Report &r : rs)
        if (!send(r))
            ok = false;
    return ok;
}

bool AllyMcuWin::selfHeal()
{
    return sendAll({AllyMcu::controllerOn(), AllyMcu::gameMode()});
}

bool AllyMcuWin::apply(const std::map<std::string, AllyMcu::Target> &remap)
{
    return sendAll(AllyMcu::buildMapping(remap));
}
