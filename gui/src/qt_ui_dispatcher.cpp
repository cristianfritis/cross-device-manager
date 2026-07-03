#include "gui/src/qt_ui_dispatcher.hpp"

#include <utility>

#include <QMetaObject>

namespace devmgr::gui {

void QtUiDispatcher::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [this, fn = std::move(fn)] {
        fn();
        emit taskExecuted();
    });
}

}  // namespace devmgr::gui
