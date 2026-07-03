#pragma once
#include <functional>

#include <QObject>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::gui {

// IUiDispatcher over Qt: post() marshals the closure onto the thread this
// object was created on (the GUI thread) via the auto-connection functor
// overload of QMetaObject::invokeMethod — queued from other threads, direct
// when already on the GUI thread. taskExecuted() fires on the GUI thread
// after each closure runs; MainWindow uses it the way the TUI uses
// Event::Custom (re-read cheap derived state such as StatusLineVM::text()).
//
// Qt-floor note: do NOT switch to the functor + Qt::ConnectionType overload
// of invokeMethod — it requires Qt >= 6.7 and the CI floor is Ubuntu 24.04's
// Qt 6.4 (see the Phase 3 spec's "Minimum Qt" decision).
//
// Teardown contract: once this QObject is destroyed, queued-but-undelivered
// posts are dropped silently (never run). The composition root must stop all
// producers (hotplug monitor, DelayedScheduler, in-flight refreshes) before
// this object and the VMs posting into it unwind — devmgr-gui mirrors
// tui/src/tui_app.cpp's teardown ordering, which exists for the same reason.
class QtUiDispatcher final : public QObject, public app::IUiDispatcher {
    Q_OBJECT
   public:
    void post(std::function<void()> fn) override;

   signals:
    void taskExecuted();  // on the GUI thread, after a posted closure ran
};

}  // namespace devmgr::gui
