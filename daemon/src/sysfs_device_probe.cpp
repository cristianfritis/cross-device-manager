#include "devmgr/daemon/sysfs_device_probe.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace devmgr::daemon {
namespace fs = std::filesystem;

namespace {
core::BusType busTypeFromPath(const fs::path& dir, std::error_code& ec) {
    const std::string bus = fs::weakly_canonical(dir / "subsystem", ec).filename().string();
    return bus == "usb"        ? core::BusType::Usb
           : bus == "pci"      ? core::BusType::Pci
           : bus == "platform" ? core::BusType::Platform
           : bus == "virtio"   ? core::BusType::Virtio
                               : core::BusType::Other;
}
}  // namespace

core::Device deviceFromSysfs(const std::string& canonical) {
    const fs::path dir(canonical);
    auto attr = [&](const char* name) -> std::string {
        std::ifstream in(dir / name);
        std::string v;
        std::getline(in, v);
        if (v.starts_with("0x")) v = v.substr(2);
        return v;
    };
    core::Device d;
    d.sysfsPath = canonical;
    std::error_code ec;
    d.bus = busTypeFromPath(dir, ec);
    const std::string vendor = attr("idVendor");
    d.vendorId = vendor.empty() ? attr("vendor") : vendor;
    const std::string product = attr("idProduct");
    d.productId = product.empty() ? attr("device") : product;
    d.serial = attr("serial");
    d.status = (d.bus == core::BusType::Usb && attr("authorized") == "0")
                   ? core::DeviceStatus::Disabled
                   : core::DeviceStatus::Active;
    const fs::path driverLink = dir / "driver";
    if (fs::exists(driverLink, ec)) {
        const std::string driver = fs::weakly_canonical(driverLink, ec).filename().string();
        if (!ec && !driver.empty()) d.boundDriver = driver;
    }
    return d;
}

}  // namespace devmgr::daemon
