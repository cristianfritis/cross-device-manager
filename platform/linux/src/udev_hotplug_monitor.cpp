#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"

#include <libudev.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "devmgr/platform/linux/udev_field_mapping.hpp"
#include "udev_device_mapper.hpp"

namespace devmgr::platform_linux {
namespace {
constexpr int kReceiveBufferBytes = 8 * 1024 * 1024;  // absorb enumeration bursts
}  // namespace

UdevHotplugMonitor::~UdevHotplugMonitor() {
    stop();
}

core::Result<void> UdevHotplugMonitor::start(Callback callback) {
    {
        std::scoped_lock lock(lifecycleMutex_);
        if (started_) {
            return core::makeError(core::Error::Code::Io, "hotplug monitor already started");
        }
    }
    callback_ = std::move(callback);

    udev_ = udev_new();
    if (udev_ == nullptr) {
        freeResources();
        return core::makeError(core::Error::Code::Io, "udev_new failed");
    }

    monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (monitor_ == nullptr) {
        freeResources();
        return core::makeError(core::Error::Code::Io, "udev_monitor_new_from_netlink failed");
    }

    for (std::string_view sub : kSubsystems) {
        udev_monitor_filter_add_match_subsystem_devtype(monitor_, std::string(sub).c_str(),
                                                        nullptr);
    }
    udev_monitor_set_receive_buffer_size(monitor_, kReceiveBufferBytes);

    if (udev_monitor_enable_receiving(monitor_) < 0) {  // installs filters + binds socket
        freeResources();
        return core::makeError(core::Error::Code::Io, "udev_monitor_enable_receiving failed");
    }

    wakeFd_ = eventfd(0, EFD_CLOEXEC);
    if (wakeFd_ < 0) {
        freeResources();
        return core::makeError(core::Error::Code::Io, "eventfd failed");
    }

    {
        std::scoped_lock lock(lifecycleMutex_);
        started_ = true;
        cleanupClaimed_ = false;
    }
    stopRequested_.store(false);
    reader_ = std::thread([this] {
        readerThreadId_.store(std::this_thread::get_id());  // first action: fixes "self" identity
        readLoop();
    });
    return {};
}

void UdevHotplugMonitor::stop() {
    const bool selfCall = (std::this_thread::get_id() == readerThreadId_.load());

    bool shouldCleanup = false;
    {
        std::scoped_lock lock(lifecycleMutex_);
        if (!started_) return;  // never started, or a prior stop() already finished cleanup
        if (!selfCall && !cleanupClaimed_) {
            cleanupClaimed_ = true;
            shouldCleanup = true;
        }
    }
    // Tell readLoop()/drainEvents() to unwind without touching libudev again. Set
    // unconditionally (self or external) — cheap, and lets a self-call's own
    // drainEvents() loop notice immediately rather than delivering more events.
    stopRequested_.store(true);

    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t w = ::write(wakeFd_, &one, sizeof one);  // wake poll()

    if (!shouldCleanup)
        return;  // reentrant self-call, or another external caller already owns cleanup

    if (reader_.joinable())
        reader_.join();  // reader has fully unwound; safe to release its state now
    freeResources();

    std::scoped_lock lock(lifecycleMutex_);
    started_ = false;  // fully stopped; start() may run again
}

void UdevHotplugMonitor::freeResources() {
    if (monitor_ != nullptr) monitor_ = udev_monitor_unref(monitor_);
    if (udev_ != nullptr) udev_ = udev_unref(udev_);
    if (wakeFd_ >= 0) {
        ::close(wakeFd_);
        wakeFd_ = -1;
    }
}

void UdevHotplugMonitor::readLoop() {
    std::array<struct pollfd, 2> fds{};
    fds[0] = {.fd = udev_monitor_get_fd(monitor_), .events = POLLIN, .revents = 0};
    fds[1] = {.fd = wakeFd_, .events = POLLIN, .revents = 0};

    while (!stopRequested_.load()) {
        const int n = ::poll(fds.data(), fds.size(), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;  // unexpected poll error
        }
        if (fds[1].revents & POLLIN) {  // stop requested
            std::uint64_t drain = 0;
            [[maybe_unused]] const ssize_t r = ::read(wakeFd_, &drain, sizeof drain);
            break;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;  // socket dead/overrun
        if ((fds[0].revents & POLLIN) == 0) continue;

        drainEvents();
    }
}

void UdevHotplugMonitor::drainEvents() {
    // The fd is non-blocking, so receive returns nullptr at end of queue. Also
    // bail as soon as a stop is requested (e.g. reentrantly, from inside
    // callback_ itself calling stop()) so we never touch monitor_ again once
    // we've signalled that the reader thread is done with it.
    for (udev_device* dev = nullptr;
         !stopRequested_.load() && (dev = udev_monitor_receive_device(monitor_)) != nullptr;) {
        const char* subsystem = udev_device_get_subsystem(dev);
        const auto action = actionFromString(udev_device_get_action(dev));
        const bool modeled =
            subsystem != nullptr &&
            std::ranges::any_of(kSubsystems, [&](std::string_view s) { return s == subsystem; });
        if (modeled && action.has_value()) {
            callback_(pal::HotplugEvent{.action = *action, .device = mapDevice(dev)});
        }
        udev_device_unref(dev);
    }
}

}  // namespace devmgr::platform_linux
