#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/models.hpp"

using devmgr::core::DetailField;
using devmgr::core::detailFieldKey;
using devmgr::core::detailFieldLabel;
using devmgr::core::detailFields;
using devmgr::core::Device;
using devmgr::core::kDetailFieldOrder;

namespace {

Device withFields(const std::vector<std::pair<DetailField, std::string>>& values) {
    Device d;
    d.id = devmgr::core::DeviceId{"d1"};
    d.name = "Device";
    for (const auto& [field, value] : values)
        d.properties[std::string(detailFieldKey(field))] = value;
    return d;
}

std::vector<std::string> labelsOf(const std::vector<devmgr::core::DetailFieldValue>& fields) {
    std::vector<std::string> out;
    out.reserve(fields.size());
    for (const auto& f : fields) out.emplace_back(f.label);
    return out;
}

}  // namespace

// The order is the contract: two platforms populating overlapping subsets must
// present them in the same sequence, so a user who learns the pane on one
// machine reads it the same way on another.
TEST(DeviceDetailFields, OrderIsFixedAndProductFacing) {
    EXPECT_EQ(labelsOf(detailFields(withFields({{DetailField::DeviceInstanceId, "USB\\VID_046D"},
                                                {DetailField::Manufacturer, "Logitech"},
                                                {DetailField::Class, "HIDClass"},
                                                {DetailField::DriverVersion, "10.0.1"}}))),
              (std::vector<std::string>{"Manufacturer", "Driver Version", "Class",
                                        "Device Instance ID"}));
}

// A backend that populates a subset renders exactly those rows, in order, with
// no gap and no placeholder standing in for the missing ones.
TEST(DeviceDetailFields, SubsetRendersWithNoGapOrPlaceholder) {
    const auto fields = detailFields(
        withFields({{DetailField::Manufacturer, "Intel"}, {DetailField::Class, "Net"}}));
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_EQ(fields[0].field, DetailField::Manufacturer);
    EXPECT_EQ(fields[1].field, DetailField::Class);
    for (const auto& f : fields) EXPECT_FALSE(f.value.empty());
}

// A backend writing an empty string has reported nothing, and a blank row would
// claim otherwise (ui-accessibility: absent properties are omitted, not blanked).
TEST(DeviceDetailFields, EmptyValueIsTreatedAsUnsupplied) {
    const auto fields = detailFields(
        withFields({{DetailField::Manufacturer, ""}, {DetailField::Driver, "usbhid"}}));
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].field, DetailField::Driver);
}

// Two backends populating the same field render ONE identical label — the whole
// reason the vocabulary exists rather than per-platform label tables.
TEST(DeviceDetailFields, TwoBackendsRenderOneIdenticalLabel) {
    const auto linuxLike = detailFields(withFields({{DetailField::Manufacturer, "Intel Corp."}}));
    const auto windowsLike = detailFields(withFields({{DetailField::Manufacturer, "Intel"}}));
    ASSERT_EQ(linuxLike.size(), 1U);
    ASSERT_EQ(windowsLike.size(), 1U);
    EXPECT_EQ(linuxLike[0].label, windowsLike[0].label);
    EXPECT_EQ(linuxLike[0].label, "Manufacturer");
}

// Closed: a raw platform key in the property map cannot become a row. Only the
// vocabulary's own keys are read, so adding a field is an edit to core and not
// something a backend can do by writing a new string.
TEST(DeviceDetailFields, SetIsClosedAgainstRawKeys) {
    Device d;
    d.id = devmgr::core::DeviceId{"d1"};
    d.properties["DEVPKEY_Device_Manufacturer"] = "Logitech";  // native-key-guard: allow
    d.properties["ID_VENDOR_FROM_DATABASE"] = "Logitech";
    d.properties["Manufacturer"] = "Logitech";  // even the label spelling is not a key
    EXPECT_TRUE(detailFields(d).empty());
}

// Every field has a non-empty, unique label and a non-empty, unique key, and no
// label or key carries a native platform property-key prefix.
TEST(DeviceDetailFields, LabelsAndKeysAreTotalUniqueAndNeutral) {
    std::set<std::string_view> labels;
    std::set<std::string_view> keys;
    for (const auto field : kDetailFieldOrder) {
        const auto label = detailFieldLabel(field);
        const auto key = detailFieldKey(field);
        EXPECT_FALSE(label.empty()) << static_cast<int>(field);
        EXPECT_FALSE(key.empty()) << static_cast<int>(field);
        EXPECT_TRUE(labels.insert(label).second) << "duplicate label: " << label;
        EXPECT_TRUE(keys.insert(key).second) << "duplicate key: " << key;
        for (const auto* native : {"DEVPKEY", "SPDRP_", "ID_", "DEVPROP"}) {
            EXPECT_EQ(label.find(native), std::string_view::npos) << label;
            EXPECT_EQ(key.find(native), std::string_view::npos) << key;
        }
    }
    EXPECT_EQ(labels.size(), kDetailFieldOrder.size());
}
