#include "devmgr/daemon/entry_json.hpp"

namespace devmgr::daemon {
using nlohmann::json;

json entryToJson(const core::DisabledDeviceEntry& e) {
    return json{{"bus", e.key.bus},
                {"vendor_id", e.key.vendorId},
                {"product_id", e.key.productId},
                {"serial", e.key.serial},
                {"position", e.key.position},
                {"mechanism", e.mechanism},
                {"last_driver", e.lastDriver},
                {"last_sysfs_path", e.lastSysfsPath},
                {"disabled_at_utc", e.disabledAtUtc},
                {"guard_suspended", e.guardSuspended}};
}

core::DisabledDeviceEntry entryFromJson(const json& j) {
    core::DisabledDeviceEntry e;
    e.key = core::DeviceKey{.bus = j.at("bus"),
                            .vendorId = j.at("vendor_id"),
                            .productId = j.at("product_id"),
                            .serial = j.at("serial"),
                            .position = j.at("position")};
    e.mechanism = j.at("mechanism");
    e.lastDriver = j.at("last_driver");
    e.lastSysfsPath = j.at("last_sysfs_path");
    e.disabledAtUtc = j.at("disabled_at_utc");
    e.guardSuspended = j.at("guard_suspended");
    return e;
}

}  // namespace devmgr::daemon
