#include "cfgmgr_notification_source.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include "devmgr/platform/windows/windows_device_mapper.hpp"
#include "windows_text.hpp"

namespace devmgr::platform_windows {

CfgMgrNotificationSource::~CfgMgrNotificationSource() {
    endDelivery();
}

core::Result<void> CfgMgrNotificationSource::beginDelivery(Handler handler) {
    if (notification_ != nullptr)
        return core::makeError(core::Error::Code::Io, "notification source already registered");
    handler_ = std::move(handler);

    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof filter;
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    // Every interface class, so the monitor sees the same device population the
    // enumerator lists rather than one hand-picked category.
    filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;

    const CONFIGRET cr = ::CM_Register_Notification(&filter, this, &trampoline, &notification_);
    if (cr != CR_SUCCESS) {
        notification_ = nullptr;
        handler_ = {};
        return core::makeError(core::Error::Code::Io, "CM_Register_Notification failed");
    }
    return {};
}

void CfgMgrNotificationSource::endDelivery() {
    if (notification_ == nullptr) return;
    // Blocks until no callback is executing and guarantees none fires
    // afterwards — the join half of the monitor's shutdown contract. Deadlocks
    // if called from inside a callback, which WindowsHotplugMonitor::stop()
    // detects and refuses rather than trusting callers not to.
    ::CM_Unregister_Notification(notification_);
    notification_ = nullptr;
    // Only safe once the call above has returned: until then a callback could
    // still be reading it.
    handler_ = {};
}

DWORD CALLBACK CfgMgrNotificationSource::trampoline(HCMNOTIFICATION notify, PVOID context,
                                                    CM_NOTIFY_ACTION action,
                                                    PCM_NOTIFY_EVENT_DATA data, DWORD dataSize) {
    (void)notify;
    auto* self = static_cast<CfgMgrNotificationSource*>(context);
    if (self == nullptr || data == nullptr || !self->handler_) return ERROR_SUCCESS;

    NativeNotification::Action mapped{};
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) {
        mapped = NativeNotification::Action::Arrived;
    } else if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
        mapped = NativeNotification::Action::Removed;
    } else {
        return ERROR_SUCCESS;  // not an arrival or a removal — nothing to report
    }

    // SymbolicLink is a NUL-terminated trailing array; its length comes from the
    // event size rather than from trust in the terminator.
    constexpr std::size_t kLinkOffset =
        offsetof(CM_NOTIFY_EVENT_DATA, u.DeviceInterface.SymbolicLink);
    if (dataSize <= kLinkOffset) return ERROR_SUCCESS;
    const std::size_t maxChars = (dataSize - kLinkOffset) / sizeof(wchar_t);
    const wchar_t* link = data->u.DeviceInterface.SymbolicLink;
    std::size_t chars = 0;
    while (chars < maxChars && link[chars] != L'\0') ++chars;

    const std::string instanceId =
        instanceIdFromSymbolicLink(toUtf8(link, static_cast<int>(chars)));
    if (instanceId.empty()) return ERROR_SUCCESS;

    self->handler_(NativeNotification{.action = mapped, .instanceId = instanceId});
    return ERROR_SUCCESS;
}

}  // namespace devmgr::platform_windows
