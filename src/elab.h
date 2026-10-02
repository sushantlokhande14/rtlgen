#pragma once

#include <nlohmann/json.hpp>

#include "model.h"

namespace rg {

using json = nlohmann::ordered_json;

// Turns a design description into specialized modules (one per distinct
// parameter set), leaves first. Problems are recorded as diagnostics, never
// thrown, so --no-checks can still emit something for any input.
Netlist elaborate(const json& desc);

}  // namespace rg
