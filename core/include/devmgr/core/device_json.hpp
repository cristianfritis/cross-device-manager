#pragma once
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"

namespace devmgr::core {

// Structured output for the CLI inventory verbs (cli-inventory spec).
//
// Device data only: no diagnostics, no timing, no host facts, nothing about the
// run that produced it. A caller parsing this gets the same record the GUI and
// TUI render, under the same product-facing names — the display name and bus
// come from the shared presentation helpers, and the detail rows from the
// shared detail-field vocabulary, so no platform-native key or label can appear.
//
// Deterministic by construction: object keys are emitted in a fixed order and
// detail fields in kDetailFieldOrder, so two runs over an unchanged device set
// produce byte-identical bytes.

// One device as a JSON object.
std::string deviceToJson(const Device& device);

// A JSON array of device objects, in the order given. An empty list is `[]` —
// a successful query that found nothing, which is a different fact from a
// failed one and is why enumeration failure never reaches this function.
std::string deviceListToJson(const std::vector<Device>& devices);

}  // namespace devmgr::core
