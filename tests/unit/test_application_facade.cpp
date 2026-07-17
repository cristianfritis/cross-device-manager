#include <mutex>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_privileged_channel.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.status = core::DeviceStatus::Active;
    return d;
}

// Enumerator that always fails — exercises the ErrorEvent path.
class FailingEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override {
        return core::makeError(core::Error::Code::Io, "boom");
    }
};
}  // namespace

TEST(ApplicationFacadeTest, RefreshPopulatesModelAndEmitsAdded) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(dev("a"));
    pal.seedDevice(dev("b"));
    app::DeviceService svc(bus);
    std::atomic<int> added{0};
    auto sub = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(added.load(), 2);
    EXPECT_EQ(facade.devices().size(), 2u);
    EXPECT_TRUE(facade.findById(core::DeviceId{"a"}).has_value());
}

TEST(ApplicationFacadeTest, RefreshErrorEmitsErrorEventAndLeavesModelIntact) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    FailingEnumerator pal;
    app::DeviceService svc(bus);
    std::atomic<int> errors{0};
    auto sub = bus.subscribe<core::ErrorEvent>([&](const auto&) { ++errors; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(errors.load(), 1);
    EXPECT_EQ(facade.devices().size(), 0u);
}

namespace {
core::Device devAt(std::string id, std::string sysfsPath, std::string name) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.sysfsPath = std::move(sysfsPath);
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(ApplicationFacadeTest, SetDeviceEnabledCallsChannelAndPublishesOneCompletion) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    test::FakeCriticalityProber prober;
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, &prober);
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    ASSERT_EQ(channel.calls.size(), 1u);
    EXPECT_EQ(channel.calls[0].sysfsPath, "/sys/devices/usb1/1-4");
    EXPECT_FALSE(channel.calls[0].enabled);
    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].taskId, "set-enabled:u1");
    EXPECT_TRUE(events[0].ok);
    EXPECT_EQ(events[0].message, "Disabled Webcam");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledFailureCarriesChannelErrorMessage) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.next = core::makeError(core::Error::Code::Permission, "authorization denied");
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr);
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "authorization denied");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledWithoutChannelIsUnsupported) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);  // 4-arg form still compiles
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "built without privileged-helper support");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledUnknownIdReportsGoneWithoutChannelCall) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr);
    facade.setDeviceEnabled(core::DeviceId{"ghost"}, false).wait();

    EXPECT_TRUE(channel.calls.empty());
    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "device no longer present");
}

TEST(ApplicationFacadeTest, CanDisableConsultsGuardThroughProber) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-3", "Keyboard"));
    app::DeviceService svc(bus);
    test::FakeCriticalityProber prober;
    pal::CriticalityFacts facts;
    facts.keyboardPaths = {"/sys/devices/usb1/1-3/1-3:1.0/input/input5"};
    prober.next = facts;

    app::ApplicationFacade facade(pal, scheduler, bus, svc, nullptr, &prober);
    facade.refresh().wait();

    const auto verdict = facade.canDisable(core::DeviceId{"u1"});
    EXPECT_FALSE(verdict.allowed);
    EXPECT_EQ(verdict.reason, "would disable the only keyboard");
}

TEST(ApplicationFacadeTest, CanDisableIsAllowedWithoutProberOrOnProbeError) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-3", "Keyboard"));
    app::DeviceService svc(bus);

    app::ApplicationFacade noProber(pal, scheduler, bus, svc);
    noProber.refresh().wait();
    EXPECT_TRUE(noProber.canDisable(core::DeviceId{"u1"}).allowed);

    test::FakeCriticalityProber failing;
    failing.next = core::makeError(core::Error::Code::Io, "mounts unreadable");
    app::ApplicationFacade withFailing(pal, scheduler, bus, svc, nullptr, &failing);
    withFailing.refresh().wait();
    EXPECT_TRUE(withFailing.canDisable(core::DeviceId{"u1"}).allowed);
}

TEST(ApplicationFacadeTest, LoadModulePublishesCompletionAndModulesChanged) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    int modulesChanged = 0;
    auto sub = bus.subscribe<core::ModulesChangedEvent>(
        [&](const core::ModulesChangedEvent&) { ++modulesChanged; });
    std::optional<core::TaskCompletedEvent> done;
    auto sub2 = bus.subscribe<core::TaskCompletedEvent>(
        [&](const core::TaskCompletedEvent& e) { done = e; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr, &pal, &pal);
    facade.loadModule("dummy").wait();

    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(done->taskId, "load-module:dummy");
    EXPECT_TRUE(done->ok);
    EXPECT_EQ(modulesChanged, 1);
    ASSERT_EQ(channel.moduleCalls.size(), 1U);
    EXPECT_EQ(channel.moduleCalls[0], "load:dummy");
}

TEST(ApplicationFacadeTest, UnbindDriverResolvesDeviceAndCallsChannel) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr, &pal, &pal);
    facade.refresh().wait();
    facade.unbindDriver(core::DeviceId{"u1"}).wait();

    ASSERT_EQ(channel.moduleCalls.size(), 1U);
    EXPECT_EQ(channel.moduleCalls[0], "unbind:/sys/devices/usb1/1-4");
}

TEST(ApplicationFacadeTest, RefreshMergesDisabledOverlayFromChannel) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    core::DisabledDeviceEntry e;
    e.lastSysfsPath = "/sys/devices/usb1/1-4";  // empty key => lastSysfsPath fallback matches
    channel.disabledEntries = std::vector<core::DisabledDeviceEntry>{e};

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr, &pal, &pal);
    facade.refresh().wait();

    const auto devices = facade.devices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices[0].status, core::DeviceStatus::Disabled);
}

TEST(ApplicationFacadeTest, CanUnloadModuleAdvisesInUse) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::LoadedModule m;
    m.name = "usbcore";
    m.holders = {"usbhid"};
    pal.seedLoadedModule(m);
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    test::FakeCriticalityProber prober;  // REQUIRED: null prober => advisory-unavailable => allowed

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, &prober, &pal, &pal);
    const auto verdict = facade.canUnloadModule("usbcore");

    EXPECT_FALSE(verdict.allowed);
    EXPECT_EQ(verdict.reason, "in use by usbhid");
}

// ---- Phase 7: snapshots ----

namespace {
core::SnapshotMeta meta(char fill) {
    core::SnapshotMeta m;
    m.id = std::string(64, fill);
    return m;
}
}  // namespace

TEST(ApplicationFacadeTest, RefreshSnapshotsReplacesListAndPublishesRefreshed) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.snapshotMetas = std::vector<core::SnapshotMeta>{meta('a'), meta('b')};
    int refreshed = 0;
    auto sub = bus.subscribe<core::SnapshotsRefreshedEvent>(
        [&](const core::SnapshotsRefreshedEvent&) { ++refreshed; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel);
    facade.refreshSnapshots().wait();

    EXPECT_EQ(refreshed, 1);
    EXPECT_EQ(facade.snapshots().size(), 2U);
}

TEST(ApplicationFacadeTest, RefreshSnapshotsErrorKeepsLastListAndEmitsError) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.snapshotMetas = std::vector<core::SnapshotMeta>{meta('a')};
    int refreshed = 0;
    auto sub = bus.subscribe<core::SnapshotsRefreshedEvent>(
        [&](const core::SnapshotsRefreshedEvent&) { ++refreshed; });
    int errors = 0;
    auto sub2 = bus.subscribe<core::ErrorEvent>([&](const core::ErrorEvent&) { ++errors; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel);
    facade.refreshSnapshots().wait();
    channel.snapshotMetas = core::makeError(core::Error::Code::Io, "daemon gone");
    facade.refreshSnapshots().wait();

    EXPECT_EQ(refreshed, 1);  // failed refresh publishes no refreshed event
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(facade.snapshots().size(), 1U);  // last good list intact
}

TEST(ApplicationFacadeTest, RefreshSnapshotsWithoutChannelPublishesEmptyList) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    int refreshed = 0;
    auto sub = bus.subscribe<core::SnapshotsRefreshedEvent>(
        [&](const core::SnapshotsRefreshedEvent&) { ++refreshed; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refreshSnapshots().wait();

    EXPECT_EQ(refreshed, 1);
    EXPECT_TRUE(facade.snapshots().empty());
}

TEST(ApplicationFacadeTest, CreateSnapshotPublishesCompletionAndChanged) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.nextCreate = std::string(64, 'c');
    int changed = 0;
    auto sub = bus.subscribe<core::SnapshotsChangedEvent>(
        [&](const core::SnapshotsChangedEvent&) { ++changed; });
    std::optional<core::TaskCompletedEvent> done;
    auto sub2 = bus.subscribe<core::TaskCompletedEvent>(
        [&](const core::TaskCompletedEvent& e) { done = e; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel);
    facade.createSnapshot("pre-upgrade").wait();

    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(done->taskId, "snapshot-create:pre-upgrade");
    EXPECT_TRUE(done->ok);
    EXPECT_EQ(done->message, "Created snapshot cccccccccccc");
    EXPECT_EQ(changed, 1);
    ASSERT_EQ(channel.snapshotCalls.size(), 1U);
    EXPECT_EQ(channel.snapshotCalls[0], "create:pre-upgrade");
}

TEST(ApplicationFacadeTest, RestoreSnapshotReportsPartialConvergenceSummary) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    core::RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'a');
    outcome.safetySnapshotId = std::string(64, 'b');
    outcome.items = {
        {.subject = "/sys/devices/usb1/1-4", .action = "re-enable", .status = "ok", .detail = ""},
        {.subject = "/sys/devices/pci0/gpu",
         .action = "re-apply-disable",
         .status = "guard-refused",
         .detail = "critical device"}};
    channel.nextRestore = outcome;
    int changed = 0;
    auto sub = bus.subscribe<core::SnapshotsChangedEvent>(
        [&](const core::SnapshotsChangedEvent&) { ++changed; });
    std::optional<core::TaskCompletedEvent> done;
    auto sub2 = bus.subscribe<core::TaskCompletedEvent>(
        [&](const core::TaskCompletedEvent& e) { done = e; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel);
    facade.restoreSnapshot(outcome.snapshotId).wait();

    ASSERT_TRUE(done.has_value());
    EXPECT_TRUE(done->ok);  // guard refusals are items, never a failed task
    EXPECT_EQ(done->message,
              "Restored aaaaaaaaaaaa: 1 ok, 1 guard-refused; safety snapshot bbbbbbbbbbbb");
    EXPECT_EQ(changed, 1);
}

TEST(ApplicationFacadeTest, DeleteSnapshotFailureCarriesErrorWithoutChangedEvent) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.next = core::makeError(core::Error::Code::NotFound, "no such snapshot");
    int changed = 0;
    auto sub = bus.subscribe<core::SnapshotsChangedEvent>(
        [&](const core::SnapshotsChangedEvent&) { ++changed; });
    std::optional<core::TaskCompletedEvent> done;
    auto sub2 = bus.subscribe<core::TaskCompletedEvent>(
        [&](const core::TaskCompletedEvent& e) { done = e; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel);
    facade.deleteSnapshot(std::string(64, 'a')).wait();

    ASSERT_TRUE(done.has_value());
    EXPECT_FALSE(done->ok);
    EXPECT_EQ(done->message, "no such snapshot");
    EXPECT_EQ(changed, 0);
}

TEST(ApplicationFacadeTest, SnapshotMutationWithoutChannelIsUnsupported) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    int changed = 0;
    auto sub = bus.subscribe<core::SnapshotsChangedEvent>(
        [&](const core::SnapshotsChangedEvent&) { ++changed; });
    std::optional<core::TaskCompletedEvent> done;
    auto sub2 = bus.subscribe<core::TaskCompletedEvent>(
        [&](const core::TaskCompletedEvent& e) { done = e; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.createSnapshot("x").wait();

    ASSERT_TRUE(done.has_value());
    EXPECT_FALSE(done->ok);
    EXPECT_EQ(done->message, "built without privileged-helper support");
    EXPECT_EQ(changed, 0);
}
