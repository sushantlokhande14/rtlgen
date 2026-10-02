#pragma once

#include <string>

#include "model.h"

namespace rg {

// Writes one Verilog file: the primitives that are used, then every
// specialized module, leaves first. With `fix_names`, identifiers are made
// legal and unique deterministically (and every rename is reported);
// without it, names go out exactly as written.
std::string emit_verilog(Netlist& nl, bool fix_names, const std::string& source);

}  // namespace rg
