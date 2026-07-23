#include "devmgr/core/device_presentation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace devmgr::core {
namespace {

// udev property keys. These arrive through Device::properties, which the Linux
// enumerator fills from udev; other backends simply do not set them.
constexpr std::string_view kVendorFromDatabase = "ID_VENDOR_FROM_DATABASE";
constexpr std::string_view kModelFromDatabase = "ID_MODEL_FROM_DATABASE";
constexpr std::string_view kSelfReportedModel = "ID_MODEL";
constexpr std::string_view kPciSubclassFromDatabase = "ID_PCI_SUBCLASS_FROM_DATABASE";
constexpr std::string_view kPciClassFromDatabase = "ID_PCI_CLASS_FROM_DATABASE";

constexpr std::size_t kPciDomainDigits = 4;
constexpr std::size_t kPciBusDigits = 2;
constexpr std::size_t kPciSlotDigits = 2;
constexpr std::size_t kPciFunctionDigits = 1;
// A four-hex-digit "name" is a bare product id, not a product name.
constexpr std::size_t kProductIdDigits = 4;

std::string property(const Device& device, std::string_view key) {
    const auto it = device.properties.find(std::string(key));
    return it == device.properties.end() ? std::string{} : it->second;
}

std::string trimmed(std::string value) {
    const auto isBlank = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isBlank(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto start = value.find_first_not_of(" \t\n\v\f\r");
    return start == std::string::npos ? std::string{} : value.substr(start);
}

std::string lastSegment(const std::string& path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

bool isHexRun(std::string_view value, std::size_t digits) {
    return value.size() == digits &&
           std::ranges::all_of(value, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool isDigitRun(std::string_view value) {
    return !value.empty() &&
           std::ranges::all_of(value, [](unsigned char c) { return std::isdigit(c) != 0; });
}

// "0000:c5:00.4" — the kernel's positional name for a PCI function.
bool looksLikePciAddress(std::string_view value) {
    const auto firstColon = value.find(':');
    if (firstColon == std::string_view::npos) return false;
    const auto secondColon = value.find(':', firstColon + 1);
    if (secondColon == std::string_view::npos) return false;
    const auto dot = value.find('.', secondColon + 1);
    if (dot == std::string_view::npos) return false;
    return isHexRun(value.substr(0, firstColon), kPciDomainDigits) &&
           isHexRun(value.substr(firstColon + 1, secondColon - firstColon - 1), kPciBusDigits) &&
           isHexRun(value.substr(secondColon + 1, dot - secondColon - 1), kPciSlotDigits) &&
           isHexRun(value.substr(dot + 1), kPciFunctionDigits);
}

// "3-1", "2-1.4.2" (port chain) or "usb3" (root hub).
bool looksLikeUsbPosition(std::string_view value) {
    if (value.starts_with("usb")) return isDigitRun(value.substr(3));
    const auto dash = value.find('-');
    if (dash == std::string_view::npos || dash == 0) return false;
    if (!isDigitRun(value.substr(0, dash))) return false;
    std::string_view ports = value.substr(dash + 1);
    while (!ports.empty()) {
        const auto dot = ports.find('.');
        const std::string_view port = ports.substr(0, dot);
        if (!isDigitRun(port)) return false;
        if (dot == std::string_view::npos) break;
        ports = ports.substr(dot + 1);
    }
    return !ports.empty();
}

// True when Device::name is the mapper's last-resort sysname fallback rather
// than a name a human wrote. Compared against the sysfs path first because that
// is how the rest of the codebase defines "positional" (services::positionFor);
// the shape checks then cover devices whose path is unknown to us.
bool nameIsPositional(const Device& device) {
    if (device.name.empty()) return true;
    const std::string position = lastSegment(device.sysfsPath);
    if (!position.empty() && device.name == position) return true;
    return looksLikePciAddress(device.name) || looksLikeUsbPosition(device.name);
}

constexpr std::array kLegalSuffixes = {
    std::string_view("Incorporated"), std::string_view("Inc."),  std::string_view("Inc"),
    std::string_view("Corporation"),  std::string_view("Corp."), std::string_view("Corp"),
    std::string_view("Company"),      std::string_view("Co."),   std::string_view("Co"),
    std::string_view("Limited"),      std::string_view("Ltd."),  std::string_view("Ltd"),
    std::string_view("GmbH"),         std::string_view("LLC"),   std::string_view("AG"),
    std::string_view("S.A."),         std::string_view("B.V."),
};

// "Advanced Micro Devices, Inc. [AMD]" -> "AMD". The bracketed alias is the
// short form pci.ids itself publishes, so nothing beats it. Empty when absent.
std::string bracketedAlias(const std::string& vendor) {
    if (vendor.size() <= 2 || vendor.back() != ']') return {};
    const auto open = vendor.rfind('[');
    if (open == std::string::npos || open + 1 >= vendor.size() - 1) return {};
    return vendor.substr(open + 1, vendor.size() - open - 2);
}

// Drops trailing punctuation plus at most one legal-form token; false when the
// vendor does not end in one, which ends the caller's loop.
bool dropLegalSuffix(std::string& vendor) {
    while (!vendor.empty() &&
           (vendor.back() == ',' || std::isspace(static_cast<unsigned char>(vendor.back())) != 0))
        vendor.pop_back();
    const auto* hit = std::ranges::find_if(kLegalSuffixes, [&vendor](std::string_view suffix) {
        if (vendor.size() <= suffix.size()) return false;  // never strip to nothing
        const char boundary = vendor[vendor.size() - suffix.size() - 1];
        return (boundary == ' ' || boundary == ',') &&
               equalsIgnoringCase(std::string_view(vendor).substr(vendor.size() - suffix.size()),
                                  suffix);
    });
    if (hit == kLegalSuffixes.end()) return false;
    vendor.resize(vendor.size() - hit->size());
    return true;
}

// "Chicony Electronics Co., Ltd" -> "Chicony Electronics". Vendor strings run to
// 34 characters unshortened, which alone would overrun the 80-column row budget
// these labels have to fit.
std::string shortVendor(std::string vendor) {
    vendor = trimmed(std::move(vendor));
    if (std::string alias = bracketedAlias(vendor); !alias.empty()) return alias;
    // "Co., Ltd" needs two passes ("Ltd", then "Co."); the bound stops a
    // pathological name from looping and keeps this linear.
    constexpr int kMaxPasses = 4;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        if (!dropLegalSuffix(vendor)) break;
    }
    return trimmed(std::move(vendor));
}

// udev reports self-declared product strings with underscores for spaces
// ("HP_9MP_Camera"). Devices that declare nothing useful report their own hex
// product id instead, which is noise dressed as a name.
std::string cleanSelfReportedModel(std::string model, const std::string& productId) {
    std::ranges::replace(model, '_', ' ');
    model = trimmed(std::move(model));
    if (model.empty()) return {};
    if (equalsIgnoringCase(model, productId)) return {};
    if (isHexRun(model, kProductIdDigits)) return {};
    return model;
}

std::string withVendor(const std::string& vendor, const std::string& rest) {
    if (vendor.empty()) return rest;
    if (equalsIgnoringCase(std::string_view(rest).substr(0, std::min(vendor.size(), rest.size())),
                           vendor))
        return rest;  // catalogue already leads with the vendor
    return vendor + " " + rest;
}

}  // namespace

std::string displayDeviceName(const Device& device) {
    // Tier 1 — the enumerator already resolved a real name.
    if (!nameIsPositional(device)) return device.name;
    if (const std::string catalogued = trimmed(property(device, kModelFromDatabase));
        !catalogued.empty())
        return catalogued;

    // Tier 2 — no catalogue entry, but the device names itself.
    if (const std::string self =
            cleanSelfReportedModel(property(device, kSelfReportedModel), device.productId);
        !self.empty())
        return self;

    // Tiers 3-5 — only a category is left, which identifies nothing without the
    // vendor attached.
    const std::string vendor = shortVendor(property(device, kVendorFromDatabase));
    for (const std::string_view key : {kPciSubclassFromDatabase, kPciClassFromDatabase}) {
        if (const std::string category = trimmed(property(device, key)); !category.empty())
            return withVendor(vendor, category);
    }
    if (!vendor.empty()) return vendor + " device";

    // Tier 6 — nothing but ids. Say what it is generically and let the identity
    // line carry the address; a bare address as the label is the defect.
    if (!displayDeviceIdentity(device).empty())
        return std::string(displayBus(device.bus)) + " device";
    return device.name.empty() ? std::string("(unknown device)") : device.name;
}

std::string displayDeviceIdentity(const Device& device) {
    std::string ids;
    if (!device.vendorId.empty() || !device.productId.empty())
        ids = device.vendorId + ":" + device.productId;
    std::string position = lastSegment(device.sysfsPath);
    if (ids.empty()) return position;
    if (position.empty()) return ids;
    return ids + " · " + position;
}

}  // namespace devmgr::core
