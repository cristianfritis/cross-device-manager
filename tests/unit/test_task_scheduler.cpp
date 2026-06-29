#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include "devmgr/runtime/cancellation.hpp"
#include "devmgr/runtime/progress.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

using devmgr::runtime::CancellationSource;
using devmgr::runtime::ProgressReporter;
using devmgr::runtime::ProgressUpdate;
using devmgr::runtime::TaskScheduler;

TEST(TaskScheduler, RunsSubmittedWorkAndReturnsValue) {
    TaskScheduler pool{2};
    auto future = pool.submit([] { return 6 * 7; });
    EXPECT_EQ(future.get(), 42);
}

TEST(TaskScheduler, RunsManyTasks) {
    TaskScheduler pool{4};
    std::atomic<int> sum{0};
    std::vector<std::future<void>> futures;
    for (int i = 1; i <= 100; ++i) futures.push_back(pool.submit([&sum, i] { sum += i; }));
    for (auto& f : futures) f.get();
    EXPECT_EQ(sum.load(), 5050);
}

TEST(Cancellation, TokenObservesRequest) {
    CancellationSource source;
    auto token = source.token();
    EXPECT_FALSE(token.isCancellationRequested());
    source.cancel();
    EXPECT_TRUE(token.isCancellationRequested());
}

TEST(Progress, ReporterForwardsUpdates) {
    ProgressUpdate seen{};
    ProgressReporter reporter = [&](const ProgressUpdate& u) { seen = u; };
    reporter(ProgressUpdate{50, "downloading"});
    EXPECT_EQ(seen.percent, 50);
    EXPECT_EQ(seen.stage, "downloading");
}
