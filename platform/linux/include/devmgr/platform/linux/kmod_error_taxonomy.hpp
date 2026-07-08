#pragma once
#include <cerrno>
#include <string>
#include <system_error>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

struct DepState {
    std::string name;
    bool loaded = false;
};

// Maps a failed insert (positive errno `err`) to the user-facing error.
// `deps` lets dependency failures be named: the first unloaded dependency is
// reported as the culprit (spec: dependency failures bubble up named).
inline core::Error describeLoadFailure(int err, const std::string& module,
                                       const std::vector<DepState>& deps,
                                       const std::string& lockdownMode) {
    const auto culprit = [&]() -> std::string {
        for (const auto& d : deps)
            if (!d.loaded) return d.name;
        return {};
    }();
    const std::string subject =
        culprit.empty() ? "module '" + module + "'" : "dependency '" + culprit + "'";
    if (err == EKEYREJECTED || err == ENOKEY || err == EPERM)
        return {
            core::Error::Code::Permission,
            subject + " rejected: unsigned module (Secure Boot / lockdown: " + lockdownMode + ")"};
    if (err == ENOENT) return {core::Error::Code::NotFound, subject + " not found for this kernel"};
    if (err == EBUSY) return {core::Error::Code::Busy, subject + " is busy"};
    return {core::Error::Code::Io,
            "loading " + subject + " failed: " + std::generic_category().message(err)};
}

inline core::Error describeUnloadFailure(int err, const std::string& module,
                                         const std::vector<std::string>& holders) {
    if (err == EBUSY) {
        std::string names;
        for (const auto& h : holders) {
            if (!names.empty()) names += ", ";
            names += h;
        }
        return {core::Error::Code::Busy,
                "module '" + module + "' is in use" + (names.empty() ? "" : " by " + names)};
    }
    if (err == EPERM)
        return {core::Error::Code::Permission,
                "unload of '" + module + "' rejected by the kernel (lockdown?)"};
    if (err == ENOENT) return {core::Error::Code::NotFound, "module '" + module + "' not loaded"};
    return {core::Error::Code::Io,
            "unloading '" + module + "' failed: " + std::generic_category().message(err)};
}

}  // namespace devmgr::platform_linux
