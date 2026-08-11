#include "devmgr/platform/windows/cfgmgr_device_enumerator.hpp"

// clang-format off
#include <windows.h>
#include <initguid.h>   // instantiates the DEVPROPKEY constants below
#include <cfgmgr32.h>
#include <devpkey.h>
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "devmgr/platform/windows/windows_device_mapper.hpp"
#include "windows_text.hpp"

// THE ONLY FILE THAT KNOWS WINDOWS PROPERTY KEYS. Every DEVPKEY_* this backend
// reads is named here and nowhere else; what the values MEAN is decided in
// windows_device_mapper.cpp, which is platform-neutral and unit-tested from
// captured fixtures on any host.
namespace devmgr::platform_windows {
namespace {

// The neutral mapper mirrors these so it needs no Windows header. If the SDK
// ever disagrees with the mirrored copy, this build stops rather than
// mis-reporting every device's state.
static_assert(kDnHasProblem == DN_HAS_PROBLEM);
static_assert(kDnStarted == DN_STARTED);
static_assert(kProblemDisabled == CM_PROB_DISABLED);
static_assert(kProblemHardwareDisabled == CM_PROB_HARDWARE_DISABLED);

// Raw property bytes, or empty when the device does not report the property.
// An absent property is the normal case, not an error: the spec's rule is that
// what Windows does not report stays unset.
std::vector<std::byte> queryProperty(DEVINST devInst, const DEVPROPKEY& key, DEVPROPTYPE& type) {
    ULONG size = 0;
    type = DEVPROP_TYPE_EMPTY;
    CONFIGRET cr = ::CM_Get_DevNode_PropertyW(devInst, &key, &type, nullptr, &size, 0);
    if (cr != CR_BUFFER_SMALL || size == 0) return {};
    std::vector<std::byte> buffer(size);
    cr = ::CM_Get_DevNode_PropertyW(devInst, &key, &type, reinterpret_cast<PBYTE>(buffer.data()),
                                    &size, 0);
    if (cr != CR_SUCCESS) return {};
    buffer.resize(size);
    return buffer;
}

// A DEVPROP_TYPE_STRING value. Trailing NUL characters are dropped: the
// property store counts them in the byte length and a string carrying one would
// compare unequal to the same text everywhere else.
std::string stringProperty(DEVINST devInst, const DEVPROPKEY& key) {
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    const std::vector<std::byte> raw = queryProperty(devInst, key, type);
    if (raw.empty() || type != DEVPROP_TYPE_STRING) return {};
    const auto* text = reinterpret_cast<const wchar_t*>(raw.data());
    std::size_t chars = raw.size() / sizeof(wchar_t);
    while (chars > 0 && text[chars - 1] == L'\0') --chars;
    return toUtf8(text, static_cast<int>(chars));
}

// A DEVPROP_TYPE_STRING_LIST value: NUL-separated, double-NUL terminated. The
// reported ORDER is preserved — the first entry is the most specific hardware
// identifier, and the detail row shows the list as Windows ordered it.
std::vector<std::string> stringListProperty(DEVINST devInst, const DEVPROPKEY& key) {
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    const std::vector<std::byte> raw = queryProperty(devInst, key, type);
    if (raw.empty() || type != DEVPROP_TYPE_STRING_LIST) return {};
    const auto* cursor = reinterpret_cast<const wchar_t*>(raw.data());
    const std::size_t chars = raw.size() / sizeof(wchar_t);

    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < chars; ++i) {
        if (cursor[i] != L'\0') continue;
        if (i > start) out.push_back(toUtf8(cursor + start, static_cast<int>(i - start)));
        start = i + 1;
    }
    return out;
}

// A DEVPROP_TYPE_FILETIME value, rendered as an ISO date. A date is a
// product-facing fact; the encoding it arrived in is not, so it never leaves
// this file in native form.
std::string dateProperty(DEVINST devInst, const DEVPROPKEY& key) {
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    const std::vector<std::byte> raw = queryProperty(devInst, key, type);
    if (type != DEVPROP_TYPE_FILETIME || raw.size() < sizeof(FILETIME)) return {};
    FILETIME fileTime{};
    std::memcpy(&fileTime, raw.data(), sizeof fileTime);
    SYSTEMTIME systemTime{};
    if (::FileTimeToSystemTime(&fileTime, &systemTime) == 0) return {};
    std::array<char, 11> buf{};
    std::snprintf(buf.data(), buf.size(), "%04u-%02u-%02u", systemTime.wYear, systemTime.wMonth,
                  systemTime.wDay);
    return std::string(buf.data());
}

// THE MAPPING TABLE. Native property key on the left, neutral fact on the
// right, and nothing else in this backend names a key. What each fact then
// means for the shared model — which becomes the display name, which becomes
// the bus, which detail row it lands in — is windows_device_mapper.cpp's
// decision, not this file's.
DevnodeFacts factsFor(DEVINST devInst, std::string instanceId) {
    DevnodeFacts facts;
    facts.instanceId = std::move(instanceId);
    facts.friendlyName = stringProperty(devInst, DEVPKEY_Device_FriendlyName);
    facts.deviceDesc = stringProperty(devInst, DEVPKEY_Device_DeviceDesc);
    facts.manufacturer = stringProperty(devInst, DEVPKEY_Device_Manufacturer);
    facts.deviceClass = stringProperty(devInst, DEVPKEY_Device_Class);
    facts.hardwareIds = stringListProperty(devInst, DEVPKEY_Device_HardwareIds);
    facts.service = stringProperty(devInst, DEVPKEY_Device_Service);
    facts.driverKey = stringProperty(devInst, DEVPKEY_Device_Driver);
    facts.driverVersion = stringProperty(devInst, DEVPKEY_Device_DriverVersion);
    facts.driverProvider = stringProperty(devInst, DEVPKEY_Device_DriverProvider);
    facts.driverDate = dateProperty(devInst, DEVPKEY_Device_DriverDate);
    facts.locationInfo = stringProperty(devInst, DEVPKEY_Device_LocationInfo);

    ULONG status = 0;
    ULONG problem = 0;
    if (::CM_Get_DevNode_Status(&status, &problem, devInst, 0) == CR_SUCCESS) {
        facts.status = static_cast<std::uint32_t>(status);
        facts.problem = static_cast<std::uint32_t>(problem);
        facts.statusRead = true;
    }
    return facts;
}

// Locates a PRESENT device node. Absent is not an error here — see the header.
bool locate(const std::wstring& instanceId, DEVINST& devInst) {
    // The const_cast is the API's shape, not ours: DEVINSTID_W is a non-const
    // pointer even though CM_Locate_DevNodeW only reads it.
    return ::CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(instanceId.c_str()),
                                CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

}  // namespace

std::optional<DevnodeFacts> readDevnodeFacts(const std::string& instanceId) {
    const std::wstring wide = toWide(instanceId);
    if (wide.empty()) return std::nullopt;
    DEVINST devInst = 0;
    if (!locate(wide, devInst)) return std::nullopt;
    return factsFor(devInst, instanceId);
}

core::Result<std::vector<core::Device>> CfgMgrDeviceEnumerator::enumerate() {
    // CM_GETIDLIST_FILTER_PRESENT: present devices only. A device node the
    // machine remembers but that is not attached is not something a user can
    // act on, and listing it would make the Devices tab disagree with reality.
    constexpr ULONG kFlags = CM_GETIDLIST_FILTER_PRESENT;

    // The list can grow between sizing and fetching (a device arriving mid-call
    // is ordinary), and CM_Get_Device_ID_ListW then reports CR_BUFFER_SMALL.
    // Retry a bounded number of times rather than failing an enumeration for a
    // race that resolves itself.
    constexpr int kMaxAttempts = 4;
    std::vector<wchar_t> buffer;
    CONFIGRET cr = CR_SUCCESS;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        ULONG chars = 0;
        cr = ::CM_Get_Device_ID_List_SizeW(&chars, nullptr, kFlags);
        if (cr != CR_SUCCESS)
            return core::makeError(core::Error::Code::Io, "CM_Get_Device_ID_List_SizeW failed");
        buffer.assign(chars, L'\0');
        cr = ::CM_Get_Device_ID_ListW(nullptr, buffer.data(), chars, kFlags);
        if (cr == CR_SUCCESS) break;
        if (cr != CR_BUFFER_SMALL)
            return core::makeError(core::Error::Code::Io, "CM_Get_Device_ID_ListW failed");
    }
    if (cr != CR_SUCCESS)
        return core::makeError(core::Error::Code::Io,
                               "CM_Get_Device_ID_ListW kept growing; device list is unstable");

    std::vector<core::Device> out;
    for (const wchar_t* id = buffer.data(); *id != L'\0'; id += std::wcslen(id) + 1) {
        const std::wstring wide(id);
        const std::string instanceId = toUtf8(wide);
        if (instanceId.empty()) continue;

        DEVINST devInst = 0;
        if (!locate(wide, devInst)) {
            // FAULT ISOLATION, as on Linux: one device that vanished between
            // the list and the lookup never aborts the scan.
            core::Device bad = mapKnownOnlyByInstanceId(instanceId);
            bad.status = core::DeviceStatus::Error;
            bad.errorNote = "device node could not be located";
            out.push_back(std::move(bad));
            continue;
        }
        out.push_back(mapDevice(factsFor(devInst, instanceId)));
    }
    return out;
}

}  // namespace devmgr::platform_windows
