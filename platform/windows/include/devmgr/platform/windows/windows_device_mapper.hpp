#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "devmgr/core/models.hpp"
#include "devmgr/platform/windows/windows_device_facts.hpp"

// PLATFORM-NEUTRAL. This header and its .cpp include no Windows header and
// name no native property key, so everything they decide is unit-tested from
// captured fixtures on any host. The Win32 half lives in cfgmgr_*.cpp.
namespace devmgr::platform_windows {

// Device node status bits and problem codes the mapper reasons about, mirrored
// from the SDK so the neutral half needs no Windows header. Each one is
// static_assert-ed against its SDK macro in cfgmgr_device_enumerator.cpp, so a
// value that ever drifts fails the Windows build rather than mis-reporting a
// device.
inline constexpr std::uint32_t kDnHasProblem = 0x00000400U;     // DN_HAS_PROBLEM
inline constexpr std::uint32_t kDnStarted = 0x00000008U;        // DN_STARTED
inline constexpr std::uint32_t kProblemDisabled = 22U;          // CM_PROB_DISABLED
inline constexpr std::uint32_t kProblemHardwareDisabled = 29U;  // CM_PROB_HARDWARE_DISABLED

// The raw enumerator prefix of a device instance identifier — everything before
// the first separator, uppercase as Windows reports it ("USB", "PCI", "ACPI",
// "ROOT", "HID", "SWD", …). Empty when the identifier carries no separator.
// Preserved in the property map so the detail pane can show what the shared bus
// taxonomy flattened away.
std::string instanceIdPrefix(std::string_view instanceId);

// Bus classification by that prefix, into the EXISTING shared taxonomy — no
// Windows-flavoured enumerator is introduced, because `Platform` already means
// "fixed, on-board, not a removable bus" on every surface and ACPI- and
// root-enumerated devices are exactly that (design D10).
core::BusType busForInstanceId(std::string_view instanceId);

// Deterministic, process-stable identity derived from the device instance
// identifier ALONE.
//
// Identity must be reconstructible from a removal notification, which carries
// nothing but the identifier — DeviceService keys its model on this value, so
// an identity that needed properties would leave removed devices in the list
// forever. The instance identifier is unique per device node and stable while
// the device stays attached in the same location, which is precisely the
// guarantee this needs. Format matches the other platforms' ("dev-" + 16 hex)
// so nothing above the PAL can tell which backend minted an id.
core::DeviceId deviceIdFor(std::string_view instanceId);

// The full model for an enumerated device.
core::Device mapDevice(const DevnodeFacts& facts);

// The model for a device known only by its instance identifier — a removal
// notification, whose device node is gone before it can be queried. Carries the
// same DeviceId and bus an enumeration produced, and nothing it cannot know.
core::Device mapKnownOnlyByInstanceId(std::string_view instanceId);

// The device instance identifier named by a device interface symbolic link
// (`\\?\USB#VID_046D&PID_C52B#5&1234&0&2#{guid}` → `USB\VID_046D&PID_C52B\5&1234&0&2`):
// drop the `\\?\` or `\\.\` prefix, drop the trailing interface-class GUID, and
// restore `\` as the separator. Empty when the link has no recoverable
// identifier, which the caller treats as "ignore this notification" rather than
// inventing one.
std::string instanceIdFromSymbolicLink(std::string_view symbolicLink);

}  // namespace devmgr::platform_windows
