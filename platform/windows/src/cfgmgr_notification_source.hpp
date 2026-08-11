#pragma once
// clang-format off
#include <windows.h>
#include <cfgmgr32.h>
// clang-format on

#include "devmgr/platform/windows/windows_hotplug_monitor.hpp"

// Internal to platform/windows — includes <windows.h>, so it is not part of the
// public devmgr/ surface.
namespace devmgr::platform_windows {

// The operating-system half of hotplug: CM_Register_Notification over all
// device-interface classes (design D8). No window and no message pump, so the
// console CLI and the Qt GUI behave identically and hotplug is not coupled to
// any event loop.
//
// Callbacks arrive on system thread-pool threads. CM_Unregister_Notification
// blocks until in-flight callbacks return, which is what satisfies the join
// half of pal::IHotplugMonitor's shutdown contract — and is also why it must
// never be called from inside a callback. WindowsHotplugMonitor enforces that.
class CfgMgrNotificationSource final : public INotificationSource {
   public:
    CfgMgrNotificationSource() = default;
    ~CfgMgrNotificationSource() override;
    CfgMgrNotificationSource(const CfgMgrNotificationSource&) = delete;
    CfgMgrNotificationSource& operator=(const CfgMgrNotificationSource&) = delete;
    CfgMgrNotificationSource(CfgMgrNotificationSource&&) = delete;
    CfgMgrNotificationSource& operator=(CfgMgrNotificationSource&&) = delete;

    core::Result<void> beginDelivery(Handler handler) override;
    void endDelivery() override;

   private:
    static DWORD CALLBACK trampoline(HCMNOTIFICATION notify, PVOID context, CM_NOTIFY_ACTION action,
                                     PCM_NOTIFY_EVENT_DATA data, DWORD dataSize);

    Handler handler_;
    HCMNOTIFICATION notification_ = nullptr;
};

}  // namespace devmgr::platform_windows
