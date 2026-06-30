#pragma once
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "devmgr/core/models.hpp"

namespace devmgr::platform_linux {

inline std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
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
