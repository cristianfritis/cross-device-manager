#include <atomic>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QThread>

#include "gui/src/qt_ui_dispatcher.hpp"

using devmgr::gui::QtUiDispatcher;

namespace {
// Deliver queued cross-thread posts to this (the GUI) thread.
void pumpUntil(const std::atomic<bool>& done) {
    for (int i = 0; i < 100 && !done.load(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
}  // namespace

TEST(QtUiDispatcherTest, PostFromWorkerThreadRunsOnGuiThread) {
    QtUiDispatcher dispatcher;
    std::atomic<bool> ran{false};
    QThread* runThread = nullptr;
    std::thread worker([&] {
        dispatcher.post([&] {
            runThread = QThread::currentThread();
            ran.store(true);
        });
    });
    worker.join();
    pumpUntil(ran);
    ASSERT_TRUE(ran.load());
    EXPECT_EQ(runThread, QCoreApplication::instance()->thread());
}

TEST(QtUiDispatcherTest, PostOnGuiThreadRunsImmediately) {
    QtUiDispatcher dispatcher;
    bool ran = false;
    dispatcher.post([&] { ran = true; });
    // Auto-connection on the owning thread == direct call — this synchronicity
    // is what lets same-thread test drivers (Tasks 3–4) skip event pumping.
    EXPECT_TRUE(ran);
}

TEST(QtUiDispatcherTest, TaskExecutedFiresAfterEachPost) {
    QtUiDispatcher dispatcher;
    QSignalSpy spy(&dispatcher, &QtUiDispatcher::taskExecuted);
    dispatcher.post([] {});
    dispatcher.post([] {});
    EXPECT_EQ(spy.count(), 2);
}

TEST(QtUiDispatcherTest, PostsQueuedAtDestructionAreDropped) {
    auto dispatcher = std::make_unique<QtUiDispatcher>();
    std::atomic<bool> ran{false};
    std::thread worker([&] { dispatcher->post([&] { ran.store(true); }); });
    worker.join();       // the closure is now queued for the GUI thread
    dispatcher.reset();  // destroy the context before delivery
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    EXPECT_FALSE(ran.load());  // dropped, never delivered — documented contract
}
