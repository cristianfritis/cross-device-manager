#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"

namespace devmgr::daemon {

// /var/lib/devmgrd/state.json owner (spec §5.2). Only non-default state is
// stored; enable deletes the entry. Atomic writes (tmp+fsync+rename); corrupt
// file moved to state.json.bad and started empty. Thread-safe (internal mutex)
// — callers still serialize ACTIONS via the apply mutex; this lock only guards
// the entry list + file.
class StateStore {
   public:
    explicit StateStore(std::string dirPath);
    core::Result<void> load();  // missing file => empty store, success
    core::Result<void> upsert(const core::DisabledDeviceEntry& entry);  // keyed by entry.key
    core::Result<void> remove(const core::DeviceKey& key);
    core::Result<void> setGuardSuspended(const core::DeviceKey& key, bool suspended);
    core::Result<void> setLastSysfsPath(const core::DeviceKey& key, const std::string& path);
    std::vector<core::DisabledDeviceEntry> entries() const;
    std::optional<core::DisabledDeviceEntry> findFor(const core::Device& device) const;

   private:
    core::Result<void> save();  // callers hold mutex_
    std::string dir_;
    std::vector<core::DisabledDeviceEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace devmgr::daemon
