# rtlgen

Generates synthesizable Verilog from a structured design description (JSON):
module hierarchy, ports, connectivity, inserted logic (pipeline stages, width
adapters, inverters, tie-offs, clock/reset wiring), and the Verilog source.
Every description goes through graph-based checks before any Verilog is
written.

```bash
cmake -S . -B build && cmake --build build
./build/rtlgen examples/dual_lane.json -o dual_lane.v
```
