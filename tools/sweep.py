#!/usr/bin/env python3
"""Generate a sweep of design descriptions for rtlgen.

    python3 tools/sweep.py --count 1200 --seed 1 --out bench/sweep

Five families, each with randomized parameters (widths, depths, channel
counts, pipeline stages, operators):

  lanes       K datapath lanes (add/xor/and/or + mux + register), per-lane widths
  arb_hub     N FIFOs behind a round-robin arbiter and a mux tree
  counters    a bank of counters with compare-match outputs
  accum       accumulators (adder + register feedback) with input synchronizers
  hier        a top that instantiates the other families as submodules

Every description is valid by construction: rtlgen must accept all of them
and Yosys must synthesize all of them.
"""
import argparse
import json
import os
import random

CLK = {"dir": "in", "kind": "clock"}
RST = {"dir": "in", "kind": "reset"}


def inp(w=1):
    return {"dir": "in", "width": w}


def outp(w=1):
    return {"dir": "out", "width": w}


def lane_module(op):
    return {
        "params": {"W": 8},
        "ports": {"clk": CLK, "rst_n": RST, "a": inp("W"), "b": inp("W"), "sel": inp(), "y": outp("W+1")},
        "instances": {
            "core": {"type": "rg_" + op, "params": {"WIDTH": "W+1"}},
            "sum": {"type": "rg_add", "params": {"WIDTH": "W+1"}},
            "pick": {"type": "rg_mux2", "params": {"WIDTH": "W+1"}},
            "q": {"type": "rg_reg", "params": {"WIDTH": "W+1"}},
        },
        "connect": [
            {"from": "a", "to": ["core.a", "sum.a"], "adapt": "zext"},
            {"from": "b", "to": ["core.b", "sum.b"], "adapt": "zext"},
            {"from": "sel", "to": "pick.sel"},
            {"from": "sum.y", "to": "pick.a"},
            {"from": "core.y", "to": "pick.b"},
            {"from": "pick.y", "to": "q.d"},
            {"from": "q.q", "to": "y"},
        ],
        "tie": {"q.en": 1},
    }


def lanes(rnd, name="lanes"):
    k = rnd.randint(1, 4)
    ops = [rnd.choice(["xor", "and", "or", "add"]) for _ in range(k)]
    widths = [rnd.choice([1, 2, 4, 8, 12, 16, 24, 32]) for _ in range(k)]
    stages = [rnd.choice([0, 0, 1, 2, 3]) for _ in range(k)]
    mods = {}
    ports = {"clk": CLK, "rst_n": RST, "sel": inp(k)}
    insts, conns = {}, []
    for i in range(k):
        mname = "lane_" + ops[i]
        mods[mname] = lane_module(ops[i])
        ports[f"a{i}"] = inp(widths[i])
        ports[f"b{i}"] = inp(widths[i])
        ports[f"y{i}"] = outp(widths[i] + 1)
        insts[f"l{i}"] = {"type": mname, "params": {"W": widths[i]}}
        conns += [
            {"from": f"a{i}", "to": f"l{i}.a"},
            {"from": f"b{i}", "to": f"l{i}.b"},
            {"from": f"sel[{i}]", "to": f"l{i}.sel"},
        ]
        c = {"from": f"l{i}.y", "to": f"y{i}"}
        if stages[i]:
            c["pipeline"] = stages[i]
        conns.append(c)
    mods[name] = {"ports": ports, "instances": insts, "connect": conns}
    return name, mods


def arb_hub(rnd, name="arb_hub"):
    n = rnd.randint(2, 8)
    w = rnd.choice([1, 4, 8, 16, 32])
    depth = rnd.choice([2, 4, 8, 16])
    ports = {"clk": CLK, "rst_n": RST, "push": inp(n), "req": inp(n), "grant": outp(n), "dout": outp(w)}
    insts = {"arb": {"type": "rg_rr_arb", "params": {"N": n}}}
    conns = [{"from": "req", "to": "arb.req"}, {"from": "arb.grant", "to": "grant"}]
    for i in range(n):
        ports[f"din{i}"] = inp(w)
        insts[f"f{i}"] = {"type": "rg_fifo", "params": {"WIDTH": w, "DEPTH": depth}}
        conns += [
            {"from": f"push[{i}]", "to": f"f{i}.push"},
            {"from": f"din{i}", "to": f"f{i}.din"},
            {"from": f"arb.grant[{i}]", "to": f"f{i}.pop"},
        ]
    # mux chain: later requesters win when their grant bit is set
    prev = "f0.dout"
    for i in range(1, n):
        insts[f"m{i}"] = {"type": "rg_mux2", "params": {"WIDTH": w}}
        conns += [
            {"from": f"arb.grant[{i}]", "to": f"m{i}.sel"},
            {"from": prev, "to": f"m{i}.a"},
            {"from": f"f{i}.dout", "to": f"m{i}.b"},
        ]
        prev = f"m{i}.y"
    out = {"from": prev, "to": "dout"}
    if rnd.random() < 0.5:
        out["pipeline"] = rnd.randint(1, 2)
    conns.append(out)
    return name, {name: {"ports": ports, "instances": insts, "connect": conns}}


def counters(rnd, name="counters"):
    n = rnd.randint(1, 8)
    w = rnd.randint(2, 32)
    ports = {"clk": CLK, "rst_n": RST, "en": inp(n), "clear": inp(), "limit": inp(w), "hit": outp()}
    insts, conns, ties = {}, [], {}
    for i in range(n):
        insts[f"c{i}"] = {"type": "rg_counter", "params": {"WIDTH": w}}
        insts[f"e{i}"] = {"type": "rg_eq", "params": {"WIDTH": w}}
        insts[f"h{i}"] = {"type": "rg_reg", "params": {"WIDTH": 1}}
        conns += [
            {"from": f"en[{i}]", "to": f"c{i}.en"},
            {"from": "clear", "to": f"c{i}.clear"},
            {"from": f"c{i}.count", "to": f"e{i}.a"},
            {"from": "limit", "to": f"e{i}.b"},
            {"from": f"e{i}.y", "to": f"h{i}.d"},
        ]
        ties[f"h{i}.en"] = 1
    # hit = any counter matched (OR chain)
    prev = "h0.q"
    for i in range(1, n):
        insts[f"o{i}"] = {"type": "rg_or", "params": {"WIDTH": 1}}
        conns += [{"from": prev, "to": f"o{i}.a"}, {"from": f"h{i}.q", "to": f"o{i}.b"}]
        prev = f"o{i}.y"
    hit = {"from": prev, "to": "hit"}
    if rnd.random() < 0.5:
        hit["pipeline"] = rnd.randint(1, 2)
    conns.append(hit)
    return name, {name: {"ports": ports, "instances": insts, "connect": conns, "tie": ties}}


def accum(rnd, name="accum"):
    w = rnd.randint(2, 64)
    sync = rnd.random() < 0.5
    ports = {"clk": CLK, "rst_n": RST, "x": inp(w), "en": inp(), "acc": outp(w)}
    insts = {
        "add": {"type": "rg_add", "params": {"WIDTH": w}},
        "r": {"type": "rg_reg", "params": {"WIDTH": w}},
    }
    conns = [
        {"from": "r.q", "to": ["add.a", "acc"]},
        {"from": "add.y", "to": "r.d"},
    ]
    if sync:
        insts["s"] = {"type": "rg_sync2", "params": {"WIDTH": 1}}
        conns += [{"from": "en", "to": "s.d"}, {"from": "s.q", "to": "r.en"}]
    else:
        conns.append({"from": "en", "to": "r.en"})
    stages = rnd.randint(0, 3)
    c = {"from": "x", "to": "add.b"}
    if stages:
        c["pipeline"] = stages
    conns.append(c)
    return name, {name: {"ports": ports, "instances": insts, "connect": conns}}


def hier(rnd, name="hier_top"):
    mods = {}
    ports = {"clk": CLK, "rst_n": RST}
    insts, conns = {}, []
    for i, fam in enumerate(rnd.sample([lanes, arb_hub, counters, accum], rnd.randint(2, 4))):
        sub, submods = fam(rnd, name=f"{fam.__name__}_{i}")
        mods.update(submods)
        insts[f"u{i}"] = {"type": sub}
        for pn, pj in submods[sub]["ports"].items():
            if pj.get("kind") in ("clock", "reset"):
                continue
            top_pn = f"u{i}_{pn}"
            ports[top_pn] = dict(pj)
            if pj["dir"] == "in":
                conns.append({"from": top_pn, "to": f"u{i}.{pn}"})
            else:
                conns.append({"from": f"u{i}.{pn}", "to": top_pn})
    mods[name] = {"ports": ports, "instances": insts, "connect": conns}
    return name, mods


FAMILIES = [lanes, arb_hub, counters, accum, hier]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=1200)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    os.makedirs(a.out, exist_ok=True)
    seen, made, dupes = set(), 0, 0
    while made < a.count:
        fam = FAMILIES[made % len(FAMILIES)]
        top, mods = fam(rnd)
        desc = {"top": top, "modules": mods}
        text = json.dumps(desc, sort_keys=True)
        if text in seen:  # identical configuration already generated
            dupes += 1
            if dupes > 10000:
                raise SystemExit(f"{fam.__name__}: parameter space exhausted after {made} configurations")
            continue
        seen.add(text)
        dupes = 0
        with open(os.path.join(a.out, f"cfg{made:04d}_{fam.__name__}.json"), "w") as f:
            json.dump(desc, f, indent=1)
        made += 1
    print(f"{made} distinct configurations in {a.out}")


if __name__ == "__main__":
    main()
