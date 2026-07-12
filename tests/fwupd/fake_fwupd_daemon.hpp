#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/fwupd_contract.hpp"

namespace devmgr::test {

// Scriptable in-process org.freedesktop.fwupd double on the SESSION bus
// (suite runs under dbus-run-session, tests/ipc pattern). Registers the
// exact bus name / object path / interface FwupdUpdateProvider (T6) uses —
// see devmgr::platform_linux::fwupd::{kBusName,kObjectPath,kInterface} — so
// the real provider can be pointed at this fake for read-side integration
// tests (test_fwupd_provider_ipc.cpp). T8 extends this same double for the
// install lifecycle (scriptInstall/setResults).
class FakeFwupdDaemon {
   public:
    using Dict = devmgr::platform_linux::fwupd::Dict;
    using InstallHook = std::function<void(const std::string& deviceId, int cabFd,
                                           const std::map<std::string, sdbus::Variant>& options)>;

    FakeFwupdDaemon();  // claims name, registers vtable, starts loop
    ~FakeFwupdDaemon();

    FakeFwupdDaemon(const FakeFwupdDaemon&) = delete;
    FakeFwupdDaemon& operator=(const FakeFwupdDaemon&) = delete;
    FakeFwupdDaemon(FakeFwupdDaemon&&) = delete;
    FakeFwupdDaemon& operator=(FakeFwupdDaemon&&) = delete;

    void setDevices(std::vector<Dict> devices);
    // Distinguish "unset" (device absent ⇒ GetUpgrades returns {}, no throw)
    // from "explicitly empty" (⇒ GetUpgrades throws NothingToDo) — mirrors
    // the real daemon's behavior that the provider (T6) already relies on.
    void setUpgrades(const std::string& deviceId, std::vector<Dict> releases);
    void setUpgradesError(const std::string& deviceId, const std::string& errName,
                          const std::string& errMsg);
    void setRemotes(std::vector<Dict> remotes);
    void setHistory(std::vector<Dict> entries);
    void setResults(const std::string& deviceId, Dict results);
    void scriptInstall(InstallHook hook);  // throw sdbus::Error inside to fail

    void emitDeviceAdded(const Dict& device);
    void emitProgress(std::uint32_t status, std::uint32_t percentage);  // PropertiesChanged
    void emitDeviceRequest(const Dict& request);

    void dropName();  // daemon-restart scenario
    void reacquireName();

    // Public by design (task-7 brief interface): tests overwrite it directly.
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes,cppcoreguidelines-non-private-member-variables-in-classes)
    std::string daemonVersion_ = "2.0.20-fake";

   private:
    struct UpgradeScript {
        std::optional<std::vector<Dict>> releases;                 // nullopt ⇒ unset
        std::optional<std::pair<std::string, std::string>> error;  // name, message
    };

    void registerVTable();

    std::vector<Dict> getDevices() const;
    std::vector<Dict> getRemotes() const;
    std::vector<Dict> getHistory() const;
    std::vector<Dict> getUpgrades(const std::string& deviceId) const;
    Dict getResults(const std::string& deviceId) const;
    void install(const std::string& deviceId, const sdbus::UnixFd& fd,
                 const std::map<std::string, sdbus::Variant>& options);

    mutable std::mutex mutex_;
    std::vector<Dict> devices_;
    std::vector<Dict> remotes_;
    std::vector<Dict> history_;
    std::map<std::string, UpgradeScript> upgrades_;
    std::map<std::string, Dict> results_;
    InstallHook installHook_;
    std::atomic<std::uint32_t> status_{0};
    std::atomic<std::uint32_t> percentage_{0};

    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IObject> object_;
};

}  // namespace devmgr::test
