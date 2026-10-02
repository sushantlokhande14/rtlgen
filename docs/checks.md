# Checks

Errors stop rtlgen before it writes anything. Warnings don't, unless
`--Werror`. Every diagnostic names the module, the place in the description
(`connect[3] (x.y -> r.d)`, `instance f`, `tie r.en`) and what to do about it.
`--diags FILE` writes the same thing as JSON.

| code             | when                                                                  |
|------------------|-----------------------------------------------------------------------|
| `E_REF`          | unknown instance, port, module type or `adapt` value                  |
| `E_DIRECTION`    | driving from a load (module output, instance input) or into a driver  |
| `E_UNDRIVEN`     | an instance input or module output nothing drives                     |
| `E_MULTI_DRIVER` | two connections (or a connection and a tie) on the same load          |
| `E_WIDTH`        | driver and load widths differ with no `adapt`, a select is out of range, or a tie value doesn't fit |
| `E_SLICE`        | a bit or part select on the load side (only drivers can be sliced)    |
| `E_PARAM`        | a parameter that doesn't evaluate, is out of range, or doesn't exist  |
| `E_NO_CLOCK`     | `pipeline` in a module without exactly one clock and one reset input  |
| `E_AMBIGUOUS`    | auto-wiring would have to pick between several clocks or resets       |
| `E_NAME`         | a module port named like a Verilog keyword or an illegal identifier   |
| `E_RECURSION`    | a module that instantiates itself, directly or through others         |
| `E_COMB_LOOP`    | a cycle with no register on it, possibly through several modules      |
| `W_UNUSED`       | an instance output that goes nowhere                                  |
| `W_KIND`         | a data signal into a clock or reset pin, or the other way round       |
| `W_RENAMED`      | an instance or wire renamed so the Verilog is legal or unique         |

## Why each one exists

Each of these is something Verilog either rejects late or, worse, accepts.

- A width mismatch in a port connection is legal Verilog. The tool pads or
  drops bits and prints a warning nobody reads. rtlgen makes it an error and
  makes the fix explicit (`"adapt": "zext"`).
- An undriven input simulates as `x` and synthesizes as whatever the tool
  likes. Two drivers synthesize as a short.
- A typo in a port name under the default `default_nettype wire` creates a
  new 1-bit net silently. The generated files use `default_nettype none`, and
  rtlgen catches the typo before that (`E_REF`).
- Instance and wire names come from the description, so `"begin"` or `"reg"`
  as an instance name would be a syntax error. Those are renamed (`begin_r`)
  with a warning. Module ports are the interface, so those are never renamed:
  `E_NAME`.

## Combinational loops

The loop check builds a graph whose nodes are ports (`inst.port`, plus the
module's own ports) and whose edges are:

- every connection without `pipeline`,
- every input-to-output path through an instance that has no register on it.

For primitives those paths are listed in the library (`rg_add`: a->y, b->y;
`rg_reg`: none). For a submodule they come from its *summary*: which inputs
reach which outputs combinationally, computed when the submodule was checked.
Modules are checked leaves first, so a loop that only exists once two
instances of a module are wired together one level up is still found.

Strongly connected components come from Tarjan's algorithm. Any component with
more than one node (or a node with a self edge) is a loop. To report it,
rtlgen runs a BFS for the shortest cycle through the alphabetically first node
in the component, so the message is short, every step in it is a real edge,
and it reads the same on every run.
