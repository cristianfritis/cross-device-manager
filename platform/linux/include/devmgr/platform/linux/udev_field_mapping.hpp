#pragma once
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include <array>
#include <optional>

#include "devmgr/core/models.hpp"
#include "devmgr/pal/hotplug_event.hpp"

namespace devmgr::platform_linux {

inline std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// The subsystems this app models. Shared by the enumerator (scan filter) and the
// hotplug monitor (netlink match + post-receive re-validation) so they stay in sync.
inline constexpr std::array<std::string_view, 4> kSubsystems{"pci", "usb", "platform", "virtio"};

// Maps a udev action string (from udev_device_get_action) to a domain action.
// add->Added, remove->Removed, everything else that mutates an existing device
// (change/bind/unbind/move/online/offline)->Changed; unknown/null -> nullopt (ignore).
inline std::optional<pal::HotplugEvent::Action> actionFromString(const char* action) {
    if (action == nullptr) return std::nullopt;
    const std::string_view a(action);
    if (a == "add") return pal::HotplugEvent::Action::Added;
    if (a == "remove") return pal::HotplugEvent::Action::Removed;
    if (a == "change" || a == "bind" || a == "unbind" || a == "move" || a == "online" ||
        a == "offline") {
        return pal::HotplugEvent::Action::Changed;
    }
    return std::nullopt;
}

// Deterministic, process-stable device identity (unlike std::hash<string>).
inline std::string stableId(std::string_view subsystem, std::string_view syspath,
                            std::string_view vendor, std::string_view product,
                            std::string_view serial) {
    std::string key;
    key.reserve(subsystem.size() + syspath.size() + vendor.size() + product.size() + serial.size() +
                4);
    key.append(subsystem).push_back('\x1f');
    key.append(syspath).push_back('\x1f');
    key.append(vendor).push_back(':');
    key.append(product).push_back('\x1f');
    key.append(serial);
    char buf[21];
    std::snprintf(buf, sizeof buf, "dev-%016llx", static_cast<unsigned long long>(fnv1a64(key)));
    return std::string(buf);
}

inline core::BusType busFor(std::string_view subsystem) {
    if (subsystem == "pci") return core::BusType::Pci;
    if (subsystem == "usb") return core::BusType::Usb;
    if (subsystem == "platform") return core::BusType::Platform;
    if (subsystem == "virtio") return core::BusType::Virtio;
    return core::BusType::Other;
}

inline std::string strip0x(std::string_view v) {
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v.remove_prefix(2);
    return std::string(v);
}

inline std::string firstNonEmpty(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates) {
        if (c != nullptr && c[0] != '\0') return std::string(c);
    }
    return std::string();
}

}  // namespace devmgr::platform_linux
