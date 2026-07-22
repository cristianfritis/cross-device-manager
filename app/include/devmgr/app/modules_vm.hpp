#pragma once
#include <atomic>  // CONTROLLER AMENDMENT (D-4): rebuildQueued_ is std::atomic<bool>
#include <functional>
#include <future>
#include <map>
#include <memory>  // FIX ROUND 1 (i-2): alive_ token is a std::shared_ptr<std::atomic<bool>>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

namespace devmgr::app {

// Per-row module signature state for TUI semantic colouring (design decision
// 1a): the state ModulesVM already computes into its signature column.
// Undetermined covers both "?" (could not classify) and the async-pending "…"
// cell. Blacklist is a modprobe.d fact fetched only for the detail pane
// (facade_.modprobeDetail), not held per row, so it stays a detail signal.
enum class ModuleSignature { Signed, Unsigned, Undetermined };

// Toolkit-agnostic Modules view model: filtered rows over listModules(), a
// selected module, an async-filled signature column (spec §7.1 perf note),
// and a Secure Boot/lockdown banner. Subscribes ModulesChangedEvent and
// rebuilds via the dispatcher (UI thread only, like DeviceListVM).
class ModulesVM {
   public:
    ModulesVM(ApplicationFacade& facade, runtime::EventBus& bus, runtime::TaskScheduler& scheduler,
              IUiDispatcher& dispatcher);
    // Waits on the in-flight signature fill (future custody). Contract:
    // destroy on the UI thread — the same thread that drains dispatcher
    // posts; the dtor's unlocked alive_-token handshake relies on that
    // serialization (see the dtor note in the .cpp).
    ~ModulesVM();

    std::vector<std::string>& rowsRef() { return rows_; }
    int& selectedRef() { return selected_; }
    void setFilter(std::string filter);
    std::optional<std::string> selectedModule() const;
    // Per-row signature state for TUI colouring (read-only; no wording change).
    // nullopt for the placeholder and out-of-range rows. Reads the same
    // signature cell the row already shows, so colour and text never disagree.
    std::optional<ModuleSignature> signedForRow(int row) const;
    std::vector<std::string> detailLines() const;  // selected module deep info
    std::string banner() const;
    void setRebuildHooks(std::function<void()> before, std::function<void()> after);
    void rebuild();  // UI thread: snapshot + rows
    // Async: fills the signature cache for names not yet cached, then posts a
    // rebuild. Returns a waitable handle (tests); the dtor also waits on it.
    std::shared_future<void> fillSignatures();

   private:
    void onModulesChanged();  // EventBus handler → coalesced dispatcher post

    ApplicationFacade& facade_;
    runtime::EventBus& bus_;
    runtime::TaskScheduler& scheduler_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<std::string>> rowNames_;  // nullopt == placeholder row
    std::vector<core::LoadedModule> snapshot_;
    std::map<std::string, std::string> signatureCell_;  // name -> "yes (…)" | "NO" | "…"
    std::atomic<bool> rebuildQueued_{false};
    int selected_ = 0;
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    std::shared_future<void> sigFill_;
    runtime::Subscription subModules_;
    // FIX ROUND 1 (i-2): posted closures capture this token by value and no-op
    // if it reads false. The destructor clears it before waiting, so a
    // dispatcher post that outlives destruction (queuing dispatchers in
    // T11/T12) never touches a dead VM. Kept alive by the closures' own
    // shared_ptr copies even after this object is destroyed.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace devmgr::app
