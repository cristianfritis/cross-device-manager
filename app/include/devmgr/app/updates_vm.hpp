#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Toolkit-agnostic Updates view model (spec §8.3): row/detail/banner
// formatting single-source here (V3, byte-frozen parity T11/T12), durable
// DeviceRequest banner (spec §9), coalesced refresh/rebuild through the
// dispatcher, ModulesVM alive-token teardown contract verbatim.
class UpdatesVM {
   public:
    UpdatesVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher);
    // UI-thread destroy; alive-token handshake (ModulesVM contract verbatim).
    ~UpdatesVM();

    // Holds references + Subscriptions: neither copyable nor movable.
    UpdatesVM(const UpdatesVM&) = delete;
    UpdatesVM& operator=(const UpdatesVM&) = delete;
    UpdatesVM(UpdatesVM&&) = delete;
    UpdatesVM& operator=(UpdatesVM&&) = delete;

    std::vector<std::string>& rowsRef() { return rows_; }
    int& selectedRef() { return selected_; }
    void rebuild();                     // UI thread: snapshot → rows
    std::string banner() const;         // availability + version + reboot marker + Secure Boot
    std::string requestBanner() const;  // "" when none; DURABLE until dismiss (spec §9)
    void dismissRequest();
    std::vector<std::string> detailLines() const;  // selected candidate: facts + releases
    struct InstallArgs {
        std::string providerId, candidateId;
        core::ReleaseRef release;
        std::string confirmText;  // version delta + needs-reboot warn + duration (spec §9)
    };
    std::optional<InstallArgs> selectedInstall() const;  // nullopt ⇔ verb disabled (V1 gate)
    void setRebuildHooks(std::function<void()> before, std::function<void()> after);
    std::string installProgressText() const;  // "" when idle

   private:
    void queueRebuild();  // UpdatesRefreshedEvent → coalesced dispatcher post
    void queueRefresh();  // UpdatesChangedEvent → coalesced facade_.refreshUpdates()
    void postWake();      // no-op post: wakes the UI loop to repaint
    // EventBus handlers (run on publisher threads; mutate under textMutex_).
    void onRequest(const core::UpdateRequestEvent& e);
    void onProgress(const core::TaskProgressEvent& e);
    void onCompleted(const core::TaskCompletedEvent& e);
    // Selected row's candidate, nullptr for the placeholder row; and its
    // stable identity for selection restore across rebuilds.
    const core::UpdateCandidate* selectedCandidate() const;
    std::optional<std::pair<std::string, std::string>> selectedKey() const;

    ApplicationFacade& facade_;
    runtime::EventBus& bus_;
    IUiDispatcher& dispatcher_;
    std::vector<std::string> rows_;
    // Row → (provider idx, candidate idx) into snapshot_; nullopt == placeholder.
    std::vector<std::optional<std::pair<std::size_t, std::size_t>>> rowRefs_;
    std::vector<core::UpdateProviderState> snapshot_;
    std::vector<core::PendingAction> pending_;
    int selected_ = 0;
    std::atomic<bool> rebuildQueued_{false};
    std::atomic<bool> refreshQueued_{false};
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    // Future custody (ApplicationFacade::refreshUpdates contract): the worker
    // captures the facade, so every handle must be waited before the facade
    // dies. queueRefresh() waits the previous handle before starting the next
    // (coalescing bounds that to one in flight) and the dtor waits the last.
    std::future<void> lastRefresh_;
    // EventBus handlers run on publisher (facade worker) threads; the UI
    // thread reads these — guard both strings.
    mutable std::mutex textMutex_;
    std::string requestBanner_;
    std::string progressText_;
    // Last named progress stage of the in-flight install. fwupd sends
    // Percentage and Status in separate PropertiesChanged frames; a
    // percent-only frame decodes as stage "unknown" (fwupd::statusName(0)),
    // so the VM retains the last named stage instead of flashing "unknown"
    // (design "Risks": Phase 6 cosmetic carry-over). Guarded by textMutex_.
    std::string lastNamedStage_;
    runtime::Subscription subRefreshed_;
    runtime::Subscription subChanged_;
    runtime::Subscription subRequest_;
    runtime::Subscription subProgress_;
    runtime::Subscription subCompleted_;
    // Posted closures capture this token by value and no-op once false; the
    // destructor clears it first (ModulesVM i-2 contract: dtor and post()
    // execution serialize on the UI thread).
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace devmgr::app
