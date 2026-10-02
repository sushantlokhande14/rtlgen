# rtlgen

Generates synthesizable Verilog from a structured design description (JSON):
module hierarchy, ports, connectivity, inserted logic (pipeline stages, width
adapters, inverters, tie-offs, clock/reset wiring), and the Verilog source.
Every description goes through graph-based checks before any Verilog is
written, so mistakes come back as a diagnostic instead of as RTL that breaks
(or quietly misbehaves) three tools later.

```
$ rtlgen examples/bad/E_WIDTH__carry.json
error[E_WIDTH] adder: connect[2] (add.y -> s): 8-bit 'add.y' into 9-bit 's'; add "adapt": "zext" or "sext", or fix the widths
rtlgen: 1 error(s), 0 warning(s); no Verilog written

$ rtlgen examples/bad/E_COMB_LOOP__ring.json
error[E_COMB_LOOP] ring: module: combinational loop: x0.b -> x0.y -> x1.b -> x1.y -> x0.b; break it with a register or "pipeline"
```

## Build

Needs a C++17 compiler, CMake and nlohmann/json (fetched if not installed).
Tests and the regression scripts also want Yosys, Icarus Verilog and Python 3.

```bash
cmake -S . -B build && cmake --build build
./build/rtlgen examples/dual_lane.json -o dual_lane.v
ctest --test-dir build --output-on-failure
```

```
rtlgen [-o FILE] [--no-checks] [--Werror] [--diags FILE] [-q] design.json
```

`--no-checks` turns the checker and the name fixing off and emits whatever the
description says. It exists so the checked flow has something to be compared
against (see Results).

## A description

```json
{
  "top": "dual_lane",
  "modules": {
    "lane": {
      "params": {"W": 8},
      "ports": {"clk": {"dir": "in", "kind": "clock"}, "rst_n": {"dir": "in", "kind": "reset"},
                "a": {"dir": "in", "width": "W"}, "b": {"dir": "in", "width": "W"},
                "sel": {"dir": "in"}, "y": {"dir": "out", "width": "W+1"}},
      "instances": {"sum": {"type": "rg_add", "params": {"WIDTH": "W+1"}},
                    "diff": {"type": "rg_xor", "params": {"WIDTH": "W+1"}},
                    "pick": {"type": "rg_mux2", "params": {"WIDTH": "W+1"}},
                    "out_q": {"type": "rg_reg", "params": {"WIDTH": "W+1"}}},
      "connect": [{"from": "a", "to": ["sum.a", "diff.a"], "adapt": "zext"},
                  {"from": "b", "to": ["sum.b", "diff.b"], "adapt": "zext"},
                  {"from": "sel", "to": "pick.sel"},
                  {"from": "sum.y", "to": "pick.a"}, {"from": "diff.y", "to": "pick.b"},
                  {"from": "pick.y", "to": "out_q.d"},
                  {"from": "out_q.q", "to": "y", "pipeline": 1}],
      "tie": {"out_q.en": 1}
    },
    "dual_lane": { "...": "two lanes, W=8 and W=16" }
  }
}
```

rtlgen specializes `lane` once per parameter set (`lane__W8`, `lane__W16`),
zero-extends `a` and `b`, inserts a pipeline register on `y`, ties the enable,
and wires every `clk`/`rst_n` it can find to the module's clock and reset. The
full example is [examples/dual_lane.json](examples/dual_lane.json);
[docs/format.md](docs/format.md) has the whole format.

## Results

All numbers come from `tools/` and are checked in under [results/](results/).
How they were measured, and what they do and don't show, is in
[docs/results.md](docs/results.md).

**1,200 generated configurations** (five families, 240 each, all distinct):
1,200 emitted, 1,200 synthesized by Yosys, 1,200 byte-identical on a second
run. 4 to 10,084 cells after `synth`, 178,827 lines of Verilog, 49 s on 12
jobs.

**1,000 faulty descriptions** (ten mistake classes, 100 each), run through
`--no-checks` and through the checked flow:

|                                   | `--no-checks` | checked |
|-----------------------------------|--------------:|--------:|
| invalid RTL (Yosys rejects it)    | 766           | 0       |
| faulty RTL Yosys accepts silently | 234           | 200     |
| blocked with a diagnostic         | 0             | 600     |
| fixed by renaming, valid RTL      | 0             | 200     |

The 200 the checked flow still lets through are swapped mux inputs and flipped
tie-off constants. They are structurally fine; no connectivity check can see
them, and they are in the benchmark on purpose to show that limit.

## Layout

```
src/        elaboration, checks, emitter, primitive library (~1.5k lines of C++)
tests/      unit tests (one per check) and a simulation testbench for the primitives
examples/   valid descriptions, plus examples/bad/<CODE>__*.json that must fail with CODE
tools/      sweep.py (config generator), mutate.py (fault injection), regress.py (Yosys judge)
results/    sweep.json and faults.json from the runs above
docs/       format.md, checks.md, design.md, results.md
```
