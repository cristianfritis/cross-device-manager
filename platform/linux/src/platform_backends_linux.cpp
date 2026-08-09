#include <memory>

#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/pal/refusing_backends.hpp"
#include "devmgr/platform/linux/dkms_status_provider.hpp"
#include "devmgr/platform/linux/kmod_driver_manager.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/linux_system_info.hpp"
#include "devmgr/platform/linux/sysfs_device_controller.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#include "devmgr/platform/linux/fwupd_update_provider.hpp"
#endif

namespace devmgr::pal {
namespace {

#ifdef DEVMGR_HAS_SDBUS
platform_linux::DbusPrivilegedChannel::Bus busFor(BackendOptions::PrivilegedEndpoint endpoint) {
    // The private endpoint is the session bus the integration rig stands up, so
    // a test run can never reach the machine's real devmgrd.
    return endpoint == BackendOptions::PrivilegedEndpoint::Private
               ? platform_linux::DbusPrivilegedChannel::Bus::Session
               : platform_linux::DbusPrivilegedChannel::Bus::System;
}
#endif

// The Linux definition of the one factory declared in core. Linking this file
// into a binary IS the platform selection; nothing above the PAL names a type
// from platform/linux any more.
class LinuxPlatformBackends final : public PlatformBackends {
   public:
    explicit LinuxPlatformBackends(const BackendOptions& options)
        :
#ifdef DEVMGR_HAS_SDBUS
          channel_(busFor(options.privilegedEndpoint)),
          fwupd_(*options.eventBus),
#endif
          set_{.enumerator = enumerator_,
               .hotplug = monitor_,
               .controller = controller_,
               .drivers = kmod_,
#ifdef DEVMGR_HAS_SDBUS
               .privileged = channel_,
#else
               .privileged = refusingPrivilegedChannel(),
#endif
               .systemInfo = sysinfo_,
               .criticality = prober_,
               .updateProviders = {}} {
#ifndef DEVMGR_HAS_SDBUS
        (void)options;
#endif
        // Provider order is the order the frontends pushed them in before this
        // change; UpdatesVM renders providers in list order.
#ifdef DEVMGR_HAS_SDBUS
        set_.updateProviders.push_back(&fwupd_);
#endif
        set_.updateProviders.push_back(&dkms_);
    }

    const BackendSet& backends() const override { return set_; }

    PlatformCapabilities capabilities() const override {
        return PlatformCapabilities{.deviceEnumeration = true,
                                    .hotplug = true,
                                    .deviceControl = true,
                                    .driverManagement = true,
#ifdef DEVMGR_HAS_SDBUS
                                    .privilegedChannel = true,
#else
                                    // No transport compiled in, so this build
                                    // genuinely has no privileged channel.
                                    .privilegedChannel = false,
#endif
                                    // Whether a given provider can answer right
                                    // now is its own runtime ProviderAvailability;
                                    // this only says Linux has providers at all.
                                    .updateProviders = true,
                                    .criticalityProbing = true,
                                    .systemInfo = true};
    }

   private:
    // DECLARATION ORDER IS THE TEARDOWN CONTRACT. Members are constructed in
    // declaration order and destroyed in reverse, and this order mirrors what
    // gui_app.cpp and tui_app.cpp declared before the seam existed. The Phase 2
    // hotplug use-after-free fix depends on the monitor outliving every
    // consumer of its callbacks: the entry point creates HotplugService AFTER
    // this object and destroys it BEFORE, so the monitor is still alive when
    // HotplugService::stop() joins the reader. Do not reorder these members,
    // and do not move ownership out of the entry point's scope.
    platform_linux::UdevDeviceEnumerator enumerator_;
    platform_linux::UdevHotplugMonitor monitor_;
    platform_linux::LinuxCriticalityProber prober_;  // advisory guard facts
    platform_linux::KmodDriverManager kmod_;         // system defaults: /sys, real modules
    platform_linux::LinuxSystemInfo sysinfo_;
    // Not constructed by the frontends before this change: they mutate through
    // devmgrd, never sysfs directly. It is wired here because Linux does
    // implement device control — a capability is a property of the platform,
    // not of which surface happens to use it. Constructing it touches nothing;
    // it only remembers the sysfs root.
    platform_linux::SysfsDeviceController controller_;
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::DbusPrivilegedChannel channel_;  // system bus → devmgrd
    platform_linux::FwupdUpdateProvider fwupd_;
#endif
    platform_linux::DkmsStatusProvider dkms_;
    BackendSet set_;
};

}  // namespace

core::Result<std::unique_ptr<PlatformBackends>> PlatformBackends::create(
    const BackendOptions& options) {
#ifdef DEVMGR_HAS_SDBUS
    // The fwupd provider publishes progress on the bus, so a Linux set cannot
    // be wired without one. Refusing here beats handing back a half-wired set.
    if (options.eventBus == nullptr)
        return core::makeError(core::Error::Code::InvalidArgs,
                               "platform backends need an event bus");
#endif
    return std::unique_ptr<PlatformBackends>(new LinuxPlatformBackends(options));
}

}  // namespace devmgr::pal
