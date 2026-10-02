#pragma once

#include "model.h"

namespace rg {

// Structural checks on the elaborated graph, before any Verilog exists:
// endpoints and directions, one driver per input, widths, clock/reset use,
// legal port names, and combinational loops (through the hierarchy).
void run_checks(Netlist& nl);

bool is_verilog_keyword(const std::string& s);
bool is_identifier(const std::string& s);

}  // namespace rg
