#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::daemon {

// Pruning policy (snapshot-store spec): at most this many auto-trigger
// snapshots are kept; manual snapshots are exempt and live until deleted.
inline constexpr std::size_t kKeepAutoSnapshots = 20;

// Storage seam for the backup-rollback engine (design decision 5): v1 is one
// JSON file per snapshot + a HEAD pointer file; the post-beta 0.6
// content-addressed backend replaces the implementation behind this same
// interface. Ids are content hashes (SHA-256 of the canonical payload
// serialization), parents link snapshots into a linear history.
class ISnapshotStore {
   public:
    virtual ~ISnapshotStore() = default;

    // Persists a snapshot of `payload` chained onto HEAD; returns its id.
    virtual core::Result<std::string> put(const core::SnapshotPayload& payload,
                                          core::SnapshotTrigger trigger,
                                          const core::SnapshotReason& reason,
                                          std::int64_t createdAtUtc) = 0;
    // Metadata of every snapshot in the store, newest first — including
    // quarantined (.bad, health=Corrupt) and future-format (health=
    // Unsupported) files, which are surfaced but never restorable.
    virtual core::Result<std::vector<core::SnapshotMeta>> list() = 0;
    // Full document, integrity-verified: hash mismatch quarantines the file
    // and returns Io; unknown id returns NotFound; future format returns
    // Unsupported.
    virtual core::Result<core::Snapshot> read(const std::string& id) = 0;
    // Deletes the named snapshot. Deleting the snapshot HEAD names moves HEAD
    // to its parent. Quarantined and future-format files are refused.
    virtual core::Result<void> remove(const std::string& id) = 0;
    // Id HEAD currently names, nullopt when the store is empty/fresh.
    virtual core::Result<std::optional<std::string>> head() = 0;
};

// v1 on-disk layout under `dir` (production: /var/lib/devmgrd/snapshots):
//   <id>.json   one pretty-printed snapshot document, id = payload SHA-256
//   HEAD        the most recent snapshot id
// All writes use the shared tmp+fsync+rename idiom; corrupt files are
// quarantined to <name>.bad-<timestamp>, never deleted (evidence rule).
// Thread-safe (internal mutex) — callers still serialize mutating ACTIONS via
// the daemon apply mutex; this lock only guards store files, mirroring
// StateStore's discipline.
class JsonSnapshotStore final : public ISnapshotStore {
   public:
    explicit JsonSnapshotStore(std::string dirPath);

    core::Result<std::string> put(const core::SnapshotPayload& payload,
                                  core::SnapshotTrigger trigger, const core::SnapshotReason& reason,
                                  std::int64_t createdAtUtc) override;
    core::Result<std::vector<core::SnapshotMeta>> list() override;
    core::Result<core::Snapshot> read(const std::string& id) override;
    core::Result<void> remove(const std::string& id) override;
    core::Result<std::optional<std::string>> head() override;

   private:
    core::Result<core::Snapshot> readLocked(const std::string& id);
    [[nodiscard]] core::Result<std::optional<std::string>> headLocked() const;
    std::vector<core::SnapshotMeta> scanLocked();
    core::SnapshotMeta scanOneLocked(const std::string& id);
    void pruneLocked();

    std::string dir_;
    std::mutex mutex_;
};

}  // namespace devmgr::daemon
