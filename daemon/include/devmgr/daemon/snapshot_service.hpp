#pragma once
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/daemon/snapshot_store.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

// Snapshot lifecycle owner inside devmgrd (backup-rollback-engine design
// decision 1). Gathers the payload from everything devmgr owns — the
// StateStore entry list plus devmgr-written modprobe.d files (devmgr-*.conf
// inside `modprobeDir`) — and persists it through the ISnapshotStore seam.
//
// Locking contract: create(), restore() and remove() MUST be called with the
// daemon apply mutex held (RequestProcessor's existing discipline) so captures
// are consistent point-in-time cuts and restore's write-back + convergence
// run without interleaved mutations. Because the caller holds the mutex,
// convergence talks to the controller/guard primitives directly (the same
// machinery EnforcementService::maybeReapply uses) instead of re-entering the
// sweep, which takes the apply mutex itself. list() needs no apply mutex —
// the store's internal lock is enough for a read-only listing.
class SnapshotService {
   public:
    SnapshotService(ISnapshotStore& store, StateStore& state, pal::IDeviceEnumerator& enumerator,
                    pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                    std::string modprobeDir);

    // Captures current state and persists it; returns the snapshot id
    // (existing id when the state is unchanged — store-level hash-dedupe).
    core::Result<std::string> create(core::SnapshotTrigger trigger,
                                     const core::SnapshotReason& reason);
    core::Result<std::vector<core::SnapshotMeta>> list();
    // Deletes a snapshot; the store moves HEAD to the parent when the head
    // snapshot is deleted. Corrupt/unsupported files are refused by the store.
    core::Result<void> remove(const std::string& id);

    // Integrity-check -> own auto safety snapshot -> atomic write-back
    // (StateStore replace-all + modprobe.d rewrite) -> hardware convergence
    // (re-apply restored disables with per-device guard re-check; re-enable
    // devices whose entries the restore removed). Guard refusals and
    // per-device failures are ITEMS in the returned outcome, never verb
    // errors; only integrity/safety-snapshot/write-back failures abort.
    core::Result<core::RestoreOutcome> restore(const std::string& id);

    // ApiVersion 4 read verb. What-changed between two stored snapshots, or —
    // when `targetId` is empty — between a snapshot and the live system, which
    // is what a restore preview shows. Read-only: nothing is written, no
    // safety snapshot is taken. Integrity failures propagate from the store
    // unchanged (corrupt -> Io, unknown id -> NotFound), so a diff can never
    // present the contents of a snapshot a restore would refuse.
    // Call with the apply mutex held when `targetId` is empty, so the live
    // capture is a consistent cut.
    core::Result<core::SnapshotDiff> diff(const std::string& baseId, const std::string& targetId);

    // Current devmgr-owned state as a snapshot payload (entries + devmgr-*
    // modprobe.d files). Exposed for the restore path's pre/post diffing.
    [[nodiscard]] core::SnapshotPayload capturePayload() const;

   private:
    void writeBackModprobe(const core::SnapshotPayload& pre, const core::SnapshotPayload& target,
                           std::vector<core::RestoreItemOutcome>& items);
    void reEnableRemoved(const core::SnapshotPayload& pre, const core::SnapshotPayload& target,
                         std::vector<core::RestoreItemOutcome>& items);
    void reApplyRestored(std::vector<core::RestoreItemOutcome>& items);
    core::RestoreItemOutcome reApplyOne(const core::DisabledDeviceEntry& entry,
                                        const core::Device& device);

    ISnapshotStore& store_;
    StateStore& state_;
    pal::IDeviceEnumerator& enumerator_;
    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    std::string modprobeDir_;
};

}  // namespace devmgr::daemon
