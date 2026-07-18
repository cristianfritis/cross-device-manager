#include "gui/src/qt_ui_dispatcher.hpp"

#include <utility>

#include <QMetaObject>

namespace devmgr::gui {

void QtUiDispatcher::post(std::function<void()> fn) {
    // clang-analyzer sees the QFunctorSlotObject allocation inside invokeMethod but not Qt's
    // ownership transfer to the event loop, so it reports a leak. False positive.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    QMetaObject::invokeMethod(this, [this, fn = std::move(fn)] {
        fn();
        emit taskExecuted();
    });
}

}  // namespace devmgr::gui
