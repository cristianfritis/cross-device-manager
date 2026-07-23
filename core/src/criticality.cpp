#include "devmgr/core/criticality.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "devmgr/services/critical_device_guard.hpp"

namespace devmgr::core {
namespace {

// Curated policy (design.md Decision 8, owner sign-off 2026-07-23). Unloading
// one of these may leave the machine unusable even when nothing holds it right
// now, which is precisely what a refcount cannot tell you. Kept as a sorted,
// exact-match list: a substring or prefix rule would sweep in unrelated modules
// (an "md-mod" prefix also matches "md-mod-helper-of-nothing"), and a marker
// that over-warns is a marker users learn to ignore.
constexpr std::array kEssentialModules = {
    // storage / filesystem — unloading unmounts the world
    std::string_view("ahci"),
    std::string_view("btrfs"),
    std::string_view("dm-crypt"),
    std::string_view("dm-mod"),
    std::string_view("ext4"),
    std::string_view("f2fs"),
    std::string_view("libata"),
    std::string_view("md-mod"),
    std::string_view("nvme"),
    std::string_view("scsi_mod"),
    std::string_view("sd_mod"),
    std::string_view("xfs"),
    // input — unloading takes the keyboard and pointer with it
    std::string_view("atkbd"),
    std::string_view("evdev"),
    std::string_view("hid"),
    std::string_view("i8042"),
    std::string_view("usbhid"),
    // display / DRM — unloading blanks the session
    std::string_view("amdgpu"),
    std::string_view("drm"),
    std::string_view("drm_kms_helper"),
    std::string_view("i915"),
    std::string_view("nouveau"),
    // USB host controllers and the PCI root — the buses everything else hangs off
    std::string_view("ehci_hcd"),
    std::string_view("ohci_hcd"),
    std::string_view("pci"),
    std::string_view("pcieport"),
    std::string_view("usbcore"),
    std::string_view("xhci_hcd"),
};

// Security enforcement. Unloading these does not break the machine, it lowers
// its defences — worth a marker, but never the same one as "this breaks boot".
constexpr std::array kSecurityModules = {
    std::string_view("apparmor"), std::string_view("ima"),     std::string_view("integrity"),
    std::string_view("lockdown"), std::string_view("selinux"),
};

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& names, const std::string& name) {
    return std::ranges::find(names, name) != names.end();
}

}  // namespace

const char* displayCriticality(Criticality level) {
    switch (level) {
        case Criticality::Essential:
            return "essential";
        case Criticality::Important:
            return "important";
        case Criticality::Ordinary:
            return "";
    }
    return "";
}

Criticality classifyDevice(const pal::CriticalityFacts& facts, const std::string& sysfsPath) {
    return services::evaluateDisable(facts, sysfsPath).allowed ? Criticality::Ordinary
                                                               : Criticality::Essential;
}

Criticality classifyModule(const std::string& name, long refCount,
                           const std::vector<std::string>& holders) {
    if (listed(kEssentialModules, name)) return Criticality::Essential;
    if (!holders.empty() || refCount > 0) return Criticality::Important;
    if (listed(kSecurityModules, name)) return Criticality::Important;
    return Criticality::Ordinary;
}

}  // namespace devmgr::core
