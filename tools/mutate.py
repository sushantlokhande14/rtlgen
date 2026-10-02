"""Fault injection for design descriptions.

Each mutation is a realistic mistake someone makes when writing (or
generating) a description. Ten classes, fixed before any results existed:

  width_mismatch     an instance's WIDTH bumped by one
  missing_connection one connection (or one fan-out target) deleted
  extra_driver       a second driver added to an instance input
  comb_loop          a combinational cell's output fed back to its own input
  keyword_name       an instance renamed to a Verilog keyword
  name_collision     a top port renamed to the wire name rtlgen derives for an instance output
  bad_param          FIFO depth 6, width 0, or a 1-input arbiter
  port_typo          a misspelled port in a connection
  swapped_inputs     a mux's a/b drivers swapped (semantic: no structural check can see it)
  wrong_tie_value    a tie-off constant flipped (semantic as well)

mutate(desc, cls, rnd) returns a new description, or None if the class
doesn't apply to this design.
"""
import copy
import re

CLASSES = ["width_mismatch", "missing_connection", "extra_driver", "comb_loop", "keyword_name",
           "name_collision", "bad_param", "port_typo", "swapped_inputs", "wrong_tie_value"]
SEMANTIC = {"swapped_inputs", "wrong_tie_value"}

COMB = {"rg_add", "rg_xor", "rg_and", "rg_or", "rg_mux2"}
PRIM_OUTPUTS = {"rg_reg": ["q"], "rg_pipe": ["q"], "rg_fifo": ["dout", "full", "empty"], "rg_add": ["y"],
                "rg_mux2": ["y"], "rg_and": ["y"], "rg_or": ["y"], "rg_xor": ["y"], "rg_not": ["y"],
                "rg_eq": ["y"], "rg_counter": ["count"], "rg_rr_arb": ["grant"], "rg_sync2": ["q"]}
KEYWORDS = ["reg", "wire", "begin", "output", "assign", "module", "input", "always"]


def targets(c):
    return c["to"] if isinstance(c["to"], list) else [c["to"]]


def set_targets(c, ts):
    c["to"] = ts if len(ts) != 1 else ts[0]


def instances(desc):
    for mname, m in desc["modules"].items():
        for iname, inst in m.get("instances", {}).items():
            yield mname, iname, inst


def rename_refs(m, old, new):
    pat = re.compile(r"^" + re.escape(old) + r"(?=[.\[]|$)")
    for c in m.get("connect", []):
        c["from"] = pat.sub(new, c["from"])
        set_targets(c, [pat.sub(new, t) for t in targets(c)])
    if "tie" in m:
        m["tie"] = {pat.sub(new, k): v for k, v in m["tie"].items()}


def mutate(desc, cls, rnd):
    d = copy.deepcopy(desc)
    mods = d["modules"]

    if cls == "width_mismatch":
        cands = [(mn, i) for mn, i, inst in instances(d) if "WIDTH" in inst.get("params", {})]
        if not cands:
            return None
        mn, i = rnd.choice(cands)
        p = mods[mn]["instances"][i]["params"]
        p["WIDTH"] = f"({p['WIDTH']})+1"
        return d

    if cls == "missing_connection":
        cands = [(mn, k) for mn, m in mods.items() for k in range(len(m.get("connect", [])))]
        if not cands:
            return None
        mn, k = rnd.choice(cands)
        c = mods[mn]["connect"][k]
        ts = targets(c)
        if len(ts) > 1:
            ts.pop(rnd.randrange(len(ts)))
            set_targets(c, ts)
        else:
            mods[mn]["connect"].pop(k)
        return d

    if cls == "extra_driver":
        cands = []
        for mn, m in mods.items():
            cs = m.get("connect", [])
            for a in range(len(cs)):
                for b in range(len(cs)):
                    if a != b and cs[a]["from"] != cs[b]["from"]:
                        for t in targets(cs[b]):
                            if "." in t:
                                cands.append((mn, cs[a]["from"], t))
        if not cands:
            return None
        mn, src, t = rnd.choice(cands)
        mods[mn]["connect"].append({"from": src, "to": t})
        return d

    if cls == "comb_loop":
        cands = []
        for mn, m in mods.items():
            for iname, inst in m.get("instances", {}).items():
                if inst["type"] not in COMB:
                    continue
                for k, c in enumerate(m.get("connect", [])):
                    for t in targets(c):
                        if t in (f"{iname}.a", f"{iname}.b"):
                            cands.append((mn, iname, k, t))
        if not cands:
            return None
        mn, iname, k, t = rnd.choice(cands)
        m = mods[mn]
        ts = [x for x in targets(m["connect"][k]) if x != t]
        if ts:
            set_targets(m["connect"][k], ts)
        else:
            m["connect"].pop(k)
        m["connect"].append({"from": f"{iname}.y", "to": t})
        return d

    if cls == "keyword_name":
        cands = list(instances(d))
        if not cands:
            return None
        mn, iname, _ = rnd.choice(cands)
        new = rnd.choice(KEYWORDS)
        m = mods[mn]
        if new in m["instances"]:
            return None
        m["instances"] = {(new if k == iname else k): v for k, v in m["instances"].items()}
        rename_refs(m, iname, new)
        return d

    if cls == "name_collision":
        top = mods[d["top"]]
        outs = []
        for iname, inst in top.get("instances", {}).items():
            names = PRIM_OUTPUTS.get(inst["type"])
            if names is None and inst["type"] in mods:
                names = [p for p, pj in mods[inst["type"]]["ports"].items() if pj["dir"] == "out"]
            outs += [f"{iname}_{o}" for o in names or []]
        ports = [p for p, pj in top["ports"].items() if pj.get("kind", "data") == "data"]
        cands = [(p, w) for p in ports for w in outs if w not in top["ports"]]
        if not cands:
            return None
        old, new = rnd.choice(cands)
        top["ports"] = {(new if k == old else k): v for k, v in top["ports"].items()}
        rename_refs(top, old, new)
        return d

    if cls == "bad_param":
        cands = list(instances(d))
        rnd.shuffle(cands)
        for mn, iname, inst in cands:
            p = mods[mn]["instances"][iname].setdefault("params", {})
            if inst["type"] == "rg_fifo":
                p["DEPTH"] = 6
                return d
            if inst["type"] == "rg_rr_arb":
                p["N"] = 1
                return d
            if "WIDTH" in p:
                p["WIDTH"] = 0
                return d
        return None

    if cls == "port_typo":
        cands = [(mn, k) for mn, m in mods.items() for k, c in enumerate(m.get("connect", [])) if "." in c["from"]]
        if not cands:
            return None
        mn, k = rnd.choice(cands)
        c = mods[mn]["connect"][k]
        inst, rest = c["from"].split(".", 1)
        port = re.match(r"[A-Za-z_0-9]+", rest).group(0)
        c["from"] = f"{inst}.{port}x{rest[len(port):]}"
        return d

    if cls == "swapped_inputs":
        cands = []
        for mn, m in mods.items():
            for iname, inst in m.get("instances", {}).items():
                if inst["type"] != "rg_mux2":
                    continue
                src = {}
                for k, c in enumerate(m.get("connect", [])):
                    for t in targets(c):
                        if t in (f"{iname}.a", f"{iname}.b"):
                            src[t[-1]] = k
                if len(src) == 2 and m["connect"][src["a"]]["from"] != m["connect"][src["b"]]["from"]:
                    cands.append((mn, iname, src["a"], src["b"]))
        if not cands:
            return None
        mn, iname, ka, kb = rnd.choice(cands)
        cs = mods[mn]["connect"]
        if len(targets(cs[ka])) == 1 and len(targets(cs[kb])) == 1:
            cs[ka]["from"], cs[kb]["from"] = cs[kb]["from"], cs[ka]["from"]
            return d
        return None

    if cls == "wrong_tie_value":
        cands = [(mn, k) for mn, m in mods.items() for k in m.get("tie", {})]
        if not cands:
            return None
        mn, k = rnd.choice(cands)
        v = mods[mn]["tie"][k]
        mods[mn]["tie"][k] = 0 if v else 1
        return d

    raise ValueError(cls)
