# Description format

A description is one JSON object:

```json
{"top": "name_of_top_module", "modules": {"name": { ...module... }, ...}}
```

Modules can appear in any order. rtlgen elaborates from `top` down, and only
modules reachable from `top` are emitted.

## Module

| key         | what                                                                      |
|-------------|---------------------------------------------------------------------------|
| `params`    | `{"W": 8, "D": "W*2"}`: defaults, evaluated in order, overridable per instance |
| `ports`     | `{"name": {"dir": "in"\|"out", "width": 8 or "W+1", "kind": "data"\|"clock"\|"reset"}}` |
| `instances` | `{"name": {"type": "rg_add" or a module name, "params": {...}}}`         |
| `connect`   | list of connections, below                                                |
| `tie`       | `{"inst.port": 1}`: drive an input with a constant                       |

`width` defaults to 1, `dir` to `in`, `kind` to `data`. Widths and parameter
values are integer expressions: `+ - * / %`, parentheses, parameter names and
`clog2(x)`.

## Connections

```json
{"from": "inst.port", "to": "inst.port"}
{"from": "a", "to": ["x.a", "y.a"]}               // fan-out
{"from": "sel[1]", "to": "m.sel"}                  // bit select
{"from": "bus[7:4]", "to": "n.a"}                  // part select
{"from": "x.y", "to": "r.d", "pipeline": 2}        // two register stages in between
{"from": "a", "to": "add.a", "adapt": "zext"}      // zext, sext or trunc to the load's width
{"from": "busy", "to": "g.en", "invert": true}
```

A bare name (`"a"`) is a port of the module being described; `inst.port` is
a port of an instance. Drivers are module inputs and instance outputs; loads
are module outputs and instance inputs.

`pipeline` inserts an `rg_pipe` on the connection and clocks it from the
module's clock and reset inputs (exactly one of each; `E_NO_CLOCK` otherwise).

## Automatic wiring

Every instance input of kind `clock` or `reset` that nothing connects or ties
is connected to the module's clock or reset input. If the module has two clock
inputs, rtlgen won't guess: `E_AMBIGUOUS`, connect it yourself.

## Specialization

A module instantiated with different parameter values becomes one Verilog
module per value set, named `base__K1V1_K2V2` (`lane__W8`, `lane__W16`).
Parameter-free modules keep their name. Same description, same names, every
run.

## Primitive library

| primitive    | params            | ports                                         |
|--------------|-------------------|-----------------------------------------------|
| `rg_reg`     | WIDTH             | clk, rst_n, en, d -> q                        |
| `rg_pipe`    | WIDTH, STAGES     | clk, rst_n, d -> q (STAGES cycles later)      |
| `rg_fifo`    | WIDTH, DEPTH (2^k)| clk, rst_n, push, din, pop -> dout, full, empty |
| `rg_add`     | WIDTH             | a, b -> y                                     |
| `rg_mux2`    | WIDTH             | sel, a, b -> y (`b` when sel)                 |
| `rg_and` `rg_or` `rg_xor` | WIDTH | a, b -> y                                    |
| `rg_not`     | WIDTH             | a -> y                                        |
| `rg_eq`      | WIDTH             | a, b -> y (1 bit)                             |
| `rg_counter` | WIDTH             | clk, rst_n, en, clear -> count                |
| `rg_rr_arb`  | N (2..64)         | clk, rst_n, req -> grant (one-hot, round robin) |
| `rg_sync2`   | WIDTH             | clk, rst_n, d -> q (two-flop synchronizer)    |

All registers reset asynchronously, active low. Only the primitives a design
uses are written to the output file. `rtlgen --prims` prints the whole library;
`tests/tb_prims.v` simulates it.
