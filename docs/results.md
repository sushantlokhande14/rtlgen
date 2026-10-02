# Results

Measured on an Intel Core Ultra 9 185H, Ubuntu 24.04 in Docker, Yosys 0.33,
Icarus Verilog 12, `-j 12`. Raw per-design rows are in `results/sweep.json`
and `results/faults.json`.

## The judge

`tools/regress.py` decides whether a Verilog file is valid. Yosys must:

1. read it (`read_verilog -sv`),
2. resolve the hierarchy with no missing modules or ports (`hierarchy -check`),
3. pass `check -assert` after `proc; flatten; opt_clean`: no undriven nets in
   use, no conflicting drivers, no logic loops,
4. synthesize it (`synth`),
5. not print `Resizing cell port`, which is how Yosys reports a port
   connection whose widths don't match. Verilog allows those, so without this
   rule a width bug would count as valid.

The same judge runs on the examples in CI, on the sweep and on the faults.

## 1,200 configurations

```bash
python3 tools/sweep.py --count 1200 --seed 1 --out bench/sweep
python3 tools/regress.py --rtlgen build/rtlgen --sweep bench/sweep --jobs 12 --out results/sweep.json
```

`sweep.py` draws random parameters for five design families and keeps only
distinct descriptions. Every configuration is emitted twice and the two files
compared byte for byte.

| family   | what                                                     | configs | cells min / median / max | Verilog lines |
|----------|----------------------------------------------------------|--------:|-------------------------:|--------------:|
| lanes    | 1-4 datapath lanes, per-lane width and operator, pipelines | 240   | 4 / 305 / 1,405          | 34,858        |
| arb_hub  | 2-8 FIFOs behind a round-robin arbiter and a mux chain   | 240     | 51 / 626 / 8,868         | 29,516        |
| counters | 1-8 counters with compare-match, OR-reduced              | 240     | 10 / 376 / 1,744         | 24,910        |
| accum    | accumulators, optional input synchronizer and pipeline   | 240     | 6 / 333 / 726            | 16,603        |
| hier     | a top instantiating 2-4 of the above as submodules       | 240     | 154 / 1,441 / 10,084     | 72,940        |
| **all**  |                                                          | **1,200** | **4 / 471 / 10,084**   | **178,827**   |

1,200 of 1,200 emitted, synthesized and were deterministic. Wall time 49.1 s.

## 1,000 faulty descriptions

```bash
python3 tools/regress.py --rtlgen build/rtlgen --faults bench/sweep --per-class 100 --jobs 12 --out results/faults.json
```

`mutate.py` takes a valid configuration from the sweep and makes one mistake
in it. The ten classes were written down before any of these numbers existed
(they're listed at the top of `tools/mutate.py`). Each faulty description goes
through `rtlgen --no-checks` (emit whatever the description says) and through
the normal checked flow, and both outputs go to the judge.

| class              | `--no-checks`: invalid | `--no-checks`: Yosys accepts, still wrong | checked: blocked | checked: fixed, valid | checked: emitted, still wrong |
|--------------------|----:|----:|----:|----:|----:|
| width_mismatch     | 100 |   0 | 100 |   0 |   0 |
| missing_connection |  99 |   1 | 100 |   0 |   0 |
| extra_driver       | 100 |   0 | 100 |   0 |   0 |
| comb_loop          | 100 |   0 | 100 |   0 |   0 |
| keyword_name       | 100 |   0 |   0 | 100 |   0 |
| name_collision     |  94 |   6 |   0 | 100 |   0 |
| bad_param          |  89 |  11 | 100 |   0 |   0 |
| port_typo          |  84 |  16 | 100 |   0 |   0 |
| swapped_inputs     |   0 | 100 |   0 |   0 | 100 |
| wrong_tie_value    |   0 | 100 |   0 |   0 | 100 |
| **total**          | **766** | **234** | **600** | **200** | **200** |

- **Invalid RTL written:** 766 with `--no-checks`, 0 with checks.
- **Faulty RTL written at all** (invalid, or valid but wrong): 1,000 with
  `--no-checks`, 200 with checks, an 80% reduction.
- Every blocked design was blocked with the diagnostic for its class
  (`E_WIDTH`, `E_UNDRIVEN`, `E_MULTI_DRIVER`, `E_COMB_LOOP`, `E_PARAM`, `E_REF`),
  sometimes with follow-on warnings.

The middle column is the interesting one: Yosys accepted 234 files generated
from broken descriptions. Some examples from the run:

- `port_typo`: `arb.grantx[0]` drives a FIFO's `pop`. Yosys warns that
  `arb_grantx` is implicitly declared (even under `default_nettype none`),
  turns the out-of-range bit into `x`, and synthesizes a FIFO that never pops.
- `bad_param`: an `rg_fifo` with `DEPTH` 6. The Verilog is legal, but the
  pointers address 8 entries of a 6-entry memory.
- `name_collision`: a top port renamed to `r_q`, the wire name for `r.q`. The
  naive output declares both, which Yosys merges into one net, and writes
  `assign r_q = r_q;`. In these 6 cases the merged net happens to match what
  the description meant, so counting them as "still wrong" is generous to the
  checker; with checks the wire becomes `r_q_2` and the output is clean.

The semantic classes account for the other 200, and the checker lets those
through too.

## What this does and doesn't show

- The fault classes are mine, and so is the checker. 0 invalid out of 766
  shows the checks cover the mistakes I thought to inject; it says nothing
  about mistakes I didn't think of. The semantic classes are in the set to
  keep that honest: structural checks cannot tell `a` from `b` on a mux, and
  the table says so.
- `--no-checks` is a deliberately naive baseline, not another tool. A real
  flow would run lint after generation and catch some of the 766 there, later
  and with messages about generated wire names rather than the description.
- Cell counts are generic gates after `synth`, not a technology mapping.
