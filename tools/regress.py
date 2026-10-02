#!/usr/bin/env python3
"""Run rtlgen's output through Yosys.

    regress.py --rtlgen build/rtlgen --examples examples
        every examples/*.json must emit, synthesize, and emit byte-identically twice;
        every examples/bad/<CODE>__*.json must be rejected with diagnostic CODE

    regress.py --rtlgen build/rtlgen --sweep bench/sweep --out results/sweep.json
        all configurations: emit + synthesize, pass rate, cell counts, determinism

    regress.py --rtlgen build/rtlgen --faults bench/sweep --per-class 100 --out results/faults.json
        inject faults (tools/mutate.py), then compare `--no-checks` against the checked flow

The judge is the same everywhere: Yosys must read the file, pass
`hierarchy -check`, `check -assert` (no undriven nets, conflicting drivers or
logic loops) and `synth`, and must not resize any cell port (a width mismatch
Verilog would otherwise accept silently).
"""
import argparse
import glob
import hashlib
import json
import os
import random
import re
import statistics
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutate  # noqa: E402


def run_rtlgen(rtlgen, desc_path, out_v, checks=True):
    cmd = [rtlgen, "-q", "-o", out_v, "--diags", out_v + ".diags.json", desc_path]
    if not checks:
        cmd.insert(1, "--no-checks")
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    diags = []
    if os.path.exists(out_v + ".diags.json"):
        with open(out_v + ".diags.json") as f:
            diags = json.load(f)
    return r.returncode == 0 and os.path.exists(out_v), diags


def top_of(vfile):
    with open(vfile) as f:
        for line in f:
            m = re.match(r"// top: ([^,]+),", line)
            if m:
                return m.group(1)
    return None


def yosys(vfile):
    top = top_of(vfile)
    script = (f"read_verilog -sv {vfile}; hierarchy -check -top {top}; proc; flatten; opt_clean; "
              f"check -assert; synth -top {top}; stat")
    r = subprocess.run(["yosys", "-p", script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       timeout=900)
    log = r.stdout
    problems = []
    if r.returncode < 0:
        problems.append(f"yosys killed by signal {-r.returncode} (out of memory?)")
    elif r.returncode != 0:
        warn = [l for l in log.splitlines() if l.startswith("Warning:")]
        err = [l for l in log.splitlines() if "ERROR" in l]
        problems.append((warn[-1] if warn else "") + " " + (err[0] if err else "yosys failed"))
    if "Resizing cell port" in log:
        problems.append("cell port resized (width mismatch)")
    cells = re.findall(r"Number of cells:\s+(\d+)", log)
    return not problems, problems, int(cells[-1]) if cells else 0


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def examples(a):
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for desc in sorted(glob.glob(os.path.join(a.examples, "*.json"))):
            name = os.path.basename(desc)
            v1, v2 = os.path.join(tmp, name + ".v"), os.path.join(tmp, name + ".2.v")
            ok, diags = run_rtlgen(a.rtlgen, desc, v1)
            if not ok:
                print(f"FAIL {name}: rejected: {[d['code'] for d in diags if d['error']]}")
                failures += 1
                continue
            run_rtlgen(a.rtlgen, desc, v2)
            good, problems, cells = yosys(v1)
            same = sha(v1) == sha(v2)
            status = "ok  " if good and same else "FAIL"
            failures += status == "FAIL"
            print(f"{status} {name}: {cells} cells{'' if same else ', NOT deterministic'} {' '.join(problems)}")
        for desc in sorted(glob.glob(os.path.join(a.examples, "bad", "*.json"))):
            name = os.path.basename(desc)
            want = name.split("__")[0]
            ok, diags = run_rtlgen(a.rtlgen, desc, os.path.join(tmp, name + ".v"))
            codes = sorted({d["code"] for d in diags})
            caught = (not ok) or want.startswith("W_")
            good = caught and want in codes
            failures += not good
            print(f"{'ok  ' if good else 'FAIL'} bad/{name}: expected {want}, got {codes}")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


def sweep(a):
    descs = sorted(glob.glob(os.path.join(a.sweep, "*.json")))
    t0 = time.time()

    def one(desc):
        with tempfile.TemporaryDirectory() as tmp:
            v1, v2 = os.path.join(tmp, "a.v"), os.path.join(tmp, "b.v")
            ok, diags = run_rtlgen(a.rtlgen, desc, v1)
            if not ok:
                return {"desc": os.path.basename(desc), "emitted": False,
                        "errors": [d["code"] for d in diags if d["error"]]}
            run_rtlgen(a.rtlgen, desc, v2)
            good, problems, cells = yosys(v1)
            with open(v1) as f:
                lines = sum(1 for _ in f)
            return {"desc": os.path.basename(desc), "emitted": True, "synth_ok": good, "problems": problems,
                    "cells": cells, "lines": lines, "deterministic": sha(v1) == sha(v2)}

    with ThreadPoolExecutor(a.jobs) as ex:
        rows = list(ex.map(one, descs))
    elapsed = time.time() - t0
    emitted = [r for r in rows if r["emitted"]]
    good = [r for r in emitted if r["synth_ok"]]
    det = [r for r in emitted if r["deterministic"]]
    fams = {}
    for r in rows:
        fam = r["desc"].split("_", 1)[1].rsplit(".", 1)[0]
        f = fams.setdefault(fam, [0, 0])
        f[0] += 1
        f[1] += r.get("synth_ok", False)
    cells = [r["cells"] for r in good]
    summary = {
        "configs": len(rows), "emitted": len(emitted), "synthesized": len(good), "deterministic": len(det),
        "cells_min": min(cells) if cells else 0, "cells_median": statistics.median(cells) if cells else 0,
        "cells_max": max(cells) if cells else 0, "verilog_lines_total": sum(r["lines"] for r in emitted),
        "families": {k: {"configs": v[0], "synthesized": v[1]} for k, v in sorted(fams.items())},
        "seconds": round(elapsed, 1),
    }
    print(json.dumps(summary, indent=2))
    for r in rows:
        if not r.get("synth_ok"):
            print("FAILED:", r)
    if a.out:
        with open(a.out, "w") as f:
            json.dump({"summary": summary, "rows": rows}, f, indent=1)
    return 0 if len(good) == len(rows) and len(det) == len(emitted) else 1


def faults(a):
    descs = sorted(glob.glob(os.path.join(a.faults, "*.json")))
    loaded = []
    for p in descs:
        with open(p) as f:
            loaded.append((os.path.basename(p), json.load(f)))
    work = []
    for ci, cls in enumerate(mutate.CLASSES):
        rnd = random.Random(a.seed * 1000 + ci)
        order = list(range(len(loaded)))
        rnd.shuffle(order)
        got = 0
        for k in order:
            if got >= a.per_class:
                break
            m = mutate.mutate(loaded[k][1], cls, rnd)
            if m is not None:
                work.append((cls, loaded[k][0], m))
                got += 1

    def one(item):
        cls, src, desc = item
        with tempfile.TemporaryDirectory() as tmp:
            dpath = os.path.join(tmp, "d.json")
            with open(dpath, "w") as f:
                json.dump(desc, f)
            naive_v, checked_v = os.path.join(tmp, "naive.v"), os.path.join(tmp, "checked.v")
            n_ok, _ = run_rtlgen(a.rtlgen, dpath, naive_v, checks=False)
            n_valid, n_prob, _ = yosys(naive_v) if n_ok else (False, ["naive emitter produced nothing"], 0)
            c_ok, c_diags = run_rtlgen(a.rtlgen, dpath, checked_v, checks=True)
            res = {"class": cls, "source": src, "naive_valid": n_valid, "naive_problems": n_prob,
                   "checked_emitted": c_ok, "checked_codes": sorted({d["code"] for d in c_diags})}
            if c_ok:
                c_valid, c_prob, _ = yosys(checked_v)
                res["checked_valid"] = c_valid
                res["checked_problems"] = c_prob
            return res

    t0 = time.time()
    with ThreadPoolExecutor(a.jobs) as ex:
        rows = list(ex.map(one, work))

    table = {}
    for r in rows:
        t = table.setdefault(r["class"], {"n": 0, "naive_invalid": 0, "naive_silent": 0, "blocked": 0,
                                          "fixed_valid": 0, "emitted_invalid": 0, "emitted_silent": 0})
        t["n"] += 1
        if r["naive_valid"]:
            t["naive_silent"] += 1  # Yosys accepted RTL that still carries the fault
        else:
            t["naive_invalid"] += 1
        if not r["checked_emitted"]:
            t["blocked"] += 1
        elif not r["checked_valid"]:
            t["emitted_invalid"] += 1
        elif r["class"] in mutate.SEMANTIC:
            t["emitted_silent"] += 1
        else:
            t["fixed_valid"] += 1  # renamed into valid RTL with the same behaviour

    tot = {k: sum(t[k] for t in table.values()) for k in next(iter(table.values()))}
    naive_bad = tot["naive_invalid"] + tot["naive_silent"]
    checked_bad = tot["emitted_invalid"] + tot["emitted_silent"]
    summary = {
        "faulty_designs": tot["n"],
        "invalid_rtl": {"naive": tot["naive_invalid"], "checked": tot["emitted_invalid"],
                        "reduction": round(1 - tot["emitted_invalid"] / tot["naive_invalid"], 4)
                        if tot["naive_invalid"] else None},
        "faulty_rtl_emitted": {"naive": naive_bad, "checked": checked_bad,
                               "reduction": round(1 - checked_bad / naive_bad, 4) if naive_bad else None},
        "per_class": table,
        "seconds": round(time.time() - t0, 1),
    }
    print(json.dumps(summary, indent=2))
    if a.out:
        with open(a.out, "w") as f:
            json.dump({"summary": summary, "rows": rows}, f, indent=1)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rtlgen", required=True)
    ap.add_argument("--examples")
    ap.add_argument("--sweep")
    ap.add_argument("--faults")
    ap.add_argument("--per-class", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.examples:
        sys.exit(examples(a))
    if a.sweep:
        sys.exit(sweep(a))
    if a.faults:
        sys.exit(faults(a))
    ap.error("pick --examples, --sweep or --faults")


if __name__ == "__main__":
    main()
