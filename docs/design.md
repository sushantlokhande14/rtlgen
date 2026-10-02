# Design

```
design.json --> elaborate --> check --> emit --> design.v
                (elab.cpp)   (checks.cpp) (emit.cpp)
```

## Elaborate

`elaborate()` walks the description from `top` down and builds a `Netlist`
(`model.h`): one `Module` per specialized module, leaves first, each with its
ports, instances and connections resolved to indices.

- **Parameters.** A module's defaults are evaluated in order, then the
  instantiating module's overrides are evaluated against the *parent's*
  parameters. `expr.cpp` is a small recursive-descent evaluator for
  `+ - * / % ( ) clog2()`.
- **Specialization.** A module used with `W=8` and `W=16` becomes `lane__W8`
  and `lane__W16`. The name depends only on the base name and the values, so
  two instances with the same values share one module and the output is
  stable.
- **Recursion.** A stack of modules being elaborated; meeting one again is
  `E_RECURSION`.
- **Endpoints.** `"inst.port[hi:lo]"` is parsed once into instance index,
  port, and slice. Unknown instances are kept as `-2` so the checker can
  report them with context instead of the parser throwing.
- **Auto-wiring.** Unconnected clock/reset inputs get a connection from the
  module's clock/reset port, flagged `automatic` so diagnostics and unit tests
  can tell them apart.

Elaboration never stops at the first problem. It records diagnostics and
keeps going, so one run reports everything.

## Check

`run_checks()` goes module by module, leaves first. Per module:

1. resolve every endpoint (`E_REF`), check direction, width, slices and
   pipelining needs per connection;
2. count drivers per load: exactly one for every instance input and module
   output (`E_UNDRIVEN`, `E_MULTI_DRIVER`);
3. names: module ports must already be legal identifiers (`E_NAME`);
4. loops: Tarjan SCC on a port-level graph, using each submodule's
   input-to-output summary (see [checks.md](checks.md));
5. store this module's summary for its parents.

Leaves first is what makes step 4 work across hierarchy without flattening:
by the time a parent is checked, every child already has a summary.

## Emit

`emit_verilog()` writes:

1. a header (`// top: NAME, N module(s)`, which the regression reads);
2. the primitives the design uses, and only those;
3. `` `default_nettype none ``, so a misspelled net in generated code is an
   error rather than a new wire;
4. the modules, leaves first: ports, one wire per instance output, inserted
   `rg_pipe` instances, instances with named port connections, then `assign`s
   for module outputs;
5. `` `default_nettype wire `` so the file doesn't change the default for
   whatever is read after it.

Width adapters are expressions, not cells: `{{4{1'b0}}, a}` for `zext`,
`{{4{a[3]}}, a}` for `sext`, `a[3:0]` for `trunc`. `invert` is `~`. Ties are
sized literals (`1'd1`).

### Deterministic naming

Every identifier goes through one `Names` object per module that hands out
names in the order the description lists things. Illegal characters become
`_`, keywords get `_r`, collisions get `_2`, `_3`. The same description gives
the same bytes every time; the sweep checks this for all 1,200 configs.

## Naive mode

`--no-checks` skips `run_checks()` and turns name fixing off. Unknown ports
become wire names, multiple drivers become multiple `assign`s to one wire,
and so on: the output is what you'd get from a template that trusts its input.
It exists only as the baseline for `tools/regress.py --faults`.

## Things that went wrong

- **The arbiter blew up Yosys.** The first `rg_rr_arb` picked the next
  requester with `(ptr + i) % N` on 32-bit integers. Yosys built a divider
  tree for every `%`. Two of the first 50 sweep configs got OOM-killed (about
  1.4 GB each), and because the regression only looked at exit codes, they
  just looked "failed". The rewrite uses a mask and lowest-set-bit isolation
  (`x & (~x + 1)`). The 50-config sweep went from 129 s to 2.3 s and the
  largest design from 8,222 to 2,996 cells. `regress.py` now reports a signal
  kill as such, and a crashing rtlgen raises instead of counting as "rejected".
- **Use-after-free in the loop checker.** `adj[node(a)].push_back(node(b))`:
  `node(b)` can grow `adj`, which invalidates the reference from `adj[...]`
  that C++17 evaluates first. AddressSanitizer found it on
  the hierarchical unit test. Fixed by computing both indices first. CI now
  runs the tests under ASan and UBSan.
- **Cycle messages that weren't cycles.** The first version reconstructed the
  loop by walking greedily inside the SCC, which can wander and print a path
  that doesn't close. It's now a BFS for the shortest cycle.
- **`default_nettype none` broke the primitives**, whose ports are declared
  without a net type. The directive now comes after them.
- **The sweep generator looped forever** at config 182: the `counters` family
  only had 36 distinct parameter combinations. The ranges are wider now, and
  the generator gives up loudly if it can't find a new config.
