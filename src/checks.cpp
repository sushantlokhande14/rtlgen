#include "checks.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

#include "prims.h"

namespace rg {

bool is_verilog_keyword(const std::string& s) {
  static const std::set<std::string> kw = {
      "always", "and", "assign", "automatic", "begin", "buf", "case", "casex", "casez", "cell", "config",
      "deassign", "default", "defparam", "design", "disable", "edge", "else", "end", "endcase", "endconfig",
      "endfunction", "endgenerate", "endmodule", "endprimitive", "endspecify", "endtable", "endtask", "event",
      "for", "force", "forever", "fork", "function", "generate", "genvar", "if", "ifnone", "initial", "inout",
      "input", "instance", "integer", "join", "large", "liblist", "library", "localparam", "macromodule",
      "medium", "module", "nand", "negedge", "nmos", "nor", "not", "or", "output", "parameter", "pmos",
      "posedge", "primitive", "real", "realtime", "reg", "release", "repeat", "scalared", "signed", "small",
      "specify", "specparam", "strong0", "strong1", "supply0", "supply1", "table", "task", "time", "tran",
      "tri", "tri0", "tri1", "triand", "trior", "trireg", "unsigned", "use", "vectored", "wait", "wand",
      "weak0", "weak1", "while", "wire", "wor", "xnor", "xor", "logic", "bit", "byte", "int", "always_ff",
      "always_comb", "always_latch", "interface", "package", "class", "typedef", "enum", "struct"};
  return kw.count(s) > 0;
}

bool is_identifier(const std::string& s) {
  if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
  for (char c : s)
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '$')) return false;
  return true;
}

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

class Checker {
 public:
  explicit Checker(Netlist& nl) : nl_(nl) {}

  void run() {
    for (const auto& m : nl_.mods) check_module(m);
  }

 private:
  void err(const Module& m, const std::string& code, const std::string& where, const std::string& msg) {
    nl_.diags.push_back({code, true, m.name, where, msg});
  }
  void warn(const Module& m, const std::string& code, const std::string& where, const std::string& msg) {
    nl_.diags.push_back({code, false, m.name, where, msg});
  }

  static const Port* port_of(const Module& m, const Ep& e) {
    if (e.inst == -1) return m.port(e.port);
    if (e.inst < 0) return nullptr;
    for (const auto& p : m.insts[e.inst].ports)
      if (p.name == e.port) return &p;
    return nullptr;
  }

  static std::string key(const Module& m, int inst, const std::string& port) {
    return (inst >= 0 ? m.insts[inst].name : std::string()) + "." + port;
  }

  static std::string show(const Module& m, int inst, const std::string& port) {
    return inst >= 0 ? m.insts[inst].name + "." + port : port;
  }

  // false if the endpoint is unusable (already reported)
  bool resolve(const Module& m, const Ep& e, const std::string& where) {
    if (e.inst == -2) {
      err(m, "E_REF", where, "'" + e.text + "': no instance named '" + e.text.substr(0, e.text.find('.')) + "'");
      return false;
    }
    if (e.inst >= 0 && !m.insts[e.inst].known) return false;  // unknown type, reported at elaboration
    if (!port_of(m, e)) {
      err(m, "E_REF", where,
          e.inst >= 0 ? "'" + m.insts[e.inst].name + "' (" + m.insts[e.inst].type + ") has no port '" + e.port + "'"
                      : "module has no port '" + e.port + "'");
      return false;
    }
    return true;
  }

  void check_module(const Module& m) {
    // port names are the interface: they can't be renamed behind the user's back
    for (const auto& p : m.ports)
      if (!is_identifier(p.name) || is_verilog_keyword(p.name))
        err(m, "E_NAME", "port " + p.name, "'" + p.name + "' is not a legal Verilog port name");

    std::map<std::string, std::vector<std::string>> drivers;  // load key -> who drives it
    std::set<std::string> used_outputs;
    bool need_clock = false;

    for (const auto& c : m.conns) {
      bool fok = resolve(m, c.from, c.where);
      bool tok = resolve(m, c.to, c.where);
      if (!fok || !tok) continue;
      const Port* fp = port_of(m, c.from);
      const Port* tp = port_of(m, c.to);
      bool from_ok = (c.from.inst == -1) ? !fp->out : fp->out;
      bool to_ok = (c.to.inst == -1) ? tp->out : !tp->out;
      if (!from_ok) {
        err(m, "E_DIRECTION", c.where, "'" + c.from.text + "' is " + (fp->out ? "an output" : "an input") +
                                           " here and can't drive anything");
        continue;
      }
      if (!to_ok) {
        err(m, "E_DIRECTION", c.where, "'" + c.to.text + "' can't be driven (it's " +
                                           (c.to.inst == -1 ? "a module input" : "an instance output") + ")");
        continue;
      }
      used_outputs.insert(key(m, c.from.inst, c.from.port));
      drivers[key(m, c.to.inst, c.to.port)].push_back(c.from.text);
      if (c.to.sliced()) err(m, "E_SLICE", c.where, "slices are only supported on the driving side");

      long dw = fp->width, lw = tp->width;
      if (c.from.sliced()) {
        if (c.from.hi == -2 || c.from.lo < 0 || c.from.hi < c.from.lo || (fp->width > 0 && c.from.hi >= fp->width)) {
          err(m, "E_WIDTH", c.where, "slice of '" + c.from.text + "' is outside 0.." + std::to_string(fp->width - 1));
          continue;
        }
        dw = c.from.hi - c.from.lo + 1;
      }
      if (dw > 0 && lw > 0) {
        std::string sizes = std::to_string(dw) + "-bit '" + c.from.text + "' into " + std::to_string(lw) + "-bit '" +
                            c.to.text + "'";
        if (c.adapt == Adapt::None && dw != lw)
          err(m, "E_WIDTH", c.where,
              sizes + (dw < lw ? "; add \"adapt\": \"zext\" or \"sext\", or fix the widths"
                               : "; add \"adapt\": \"trunc\", or fix the widths"));
        if ((c.adapt == Adapt::Zext || c.adapt == Adapt::Sext) && dw >= lw)
          err(m, "E_WIDTH", c.where, sizes + ": extension needs a narrower source");
        if (c.adapt == Adapt::Trunc && dw <= lw) err(m, "E_WIDTH", c.where, sizes + ": truncation needs a wider source");
      }
      if (c.pipeline < 0) err(m, "E_PARAM", c.where, "pipeline stages can't be negative");
      if (c.pipeline > 0) need_clock = true;
      if (fp->kind == Kind::Data && tp->kind != Kind::Data && !c.automatic)
        warn(m, "W_KIND", c.where, "data signal '" + c.from.text + "' drives " + kind_name(tp->kind) + " pin '" +
                                       c.to.text + "'");
      if (fp->kind != Kind::Data && tp->kind == Kind::Data)
        warn(m, "W_KIND", c.where, std::string(kind_name(fp->kind)) + " '" + c.from.text + "' used as data at '" +
                                       c.to.text + "'");
    }

    for (const auto& t : m.ties) {
      if (!resolve(m, t.to, t.where)) continue;
      const Port* tp = port_of(m, t.to);
      if (t.to.inst == -1 ? !tp->out : tp->out) {
        err(m, "E_DIRECTION", t.where, "'" + t.to.text + "' can't be tied (it's a driver)");
        continue;
      }
      drivers[key(m, t.to.inst, t.to.port)].push_back("constant " + std::to_string(t.value));
      if (tp->width > 0 && (t.value < 0 || (tp->width < 63 && t.value >= (1L << tp->width))))
        err(m, "E_WIDTH", t.where, "value " + std::to_string(t.value) + " doesn't fit in " +
                                       std::to_string(tp->width) + " bits");
    }

    // exactly one driver for every instance input and module output
    auto expect_one = [&](int inst, const Port& p) {
      auto it = drivers.find(key(m, inst, p.name));
      std::string who = show(m, inst, p.name);
      if (it == drivers.end() || it->second.empty()) {
        err(m, "E_UNDRIVEN", inst >= 0 ? "instance " + m.insts[inst].name : "port " + p.name,
            "'" + who + "' has no driver; connect or tie it");
      } else if (it->second.size() > 1) {
        std::string list;
        for (const auto& s : it->second) list += (list.empty() ? "" : ", ") + s;
        err(m, "E_MULTI_DRIVER", inst >= 0 ? "instance " + m.insts[inst].name : "port " + p.name,
            "'" + who + "' is driven by " + std::to_string(it->second.size()) + " sources: " + list);
      }
    };
    for (size_t i = 0; i < m.insts.size(); i++)
      for (const auto& p : m.insts[i].ports)
        if (!p.out) expect_one((int)i, p);
    for (const auto& p : m.ports)
      if (p.out) expect_one(-1, p);

    for (size_t i = 0; i < m.insts.size(); i++)
      for (const auto& p : m.insts[i].ports)
        if (p.out && !used_outputs.count(key(m, (int)i, p.name)))
          warn(m, "W_UNUSED", "instance " + m.insts[i].name, "output '" + m.insts[i].name + "." + p.name +
                                                                  "' is not used");

    if (need_clock) {
      int clocks = 0, resets = 0;
      for (const auto& p : m.ports) {
        clocks += !p.out && p.kind == Kind::Clock;
        resets += !p.out && p.kind == Kind::Reset;
      }
      if (clocks != 1 || resets != 1)
        err(m, "E_NO_CLOCK", "connect", "pipeline insertion needs exactly one clock and one reset input on the module");
    }

    check_loops(m);
  }

  // Combinational loop detection on port-level nodes. Edges: every
  // connection (unless pipelined) and every input->output combinational path
  // through an instance. Submodules contribute their own summary, so a loop
  // that goes through two levels of hierarchy is still found.
  void check_loops(const Module& m) {
    std::map<std::string, int> id;
    std::vector<std::string> names;
    std::vector<std::vector<int>> adj;
    auto node = [&](const std::string& k) {
      auto it = id.find(k);
      if (it != id.end()) return it->second;
      id[k] = (int)names.size();
      names.push_back(k);
      adj.emplace_back();
      return (int)names.size() - 1;
    };
    for (const auto& c : m.conns) {
      if (c.pipeline > 0 || c.from.inst == -2 || c.to.inst == -2) continue;
      if (!port_of(m, c.from) || !port_of(m, c.to)) continue;
      // two statements on purpose: node() can grow adj, so don't hold adj[u] across it
      int u = node(key(m, c.from.inst, c.from.port));
      int v = node(key(m, c.to.inst, c.to.port));
      adj[u].push_back(v);
    }
    for (size_t i = 0; i < m.insts.size(); i++) {
      const Inst& in = m.insts[i];
      const Pairs* pairs = nullptr;
      Pairs prim_pairs;
      if (in.prim) {
        if (const Prim* p = find_prim(in.type)) prim_pairs = p->comb;
        pairs = &prim_pairs;
      } else if (summary_.count(in.sub)) {
        pairs = &summary_[in.sub];
      }
      if (!pairs) continue;
      for (const auto& [a, b] : *pairs) {
        int u = node(key(m, (int)i, a));
        int v = node(key(m, (int)i, b));
        adj[u].push_back(v);
      }
    }

    // Tarjan's strongly connected components
    int n = (int)names.size(), counter = 0;
    std::vector<int> index(n, -1), low(n, 0), stack;
    std::vector<char> on(n, 0);
    std::vector<std::vector<int>> sccs;
    std::function<void(int)> strong = [&](int v) {
      index[v] = low[v] = counter++;
      stack.push_back(v);
      on[v] = 1;
      for (int w : adj[v]) {
        if (index[w] < 0) {
          strong(w);
          low[v] = std::min(low[v], low[w]);
        } else if (on[w]) {
          low[v] = std::min(low[v], index[w]);
        }
      }
      if (low[v] == index[v]) {
        std::vector<int> comp;
        int w;
        do {
          w = stack.back();
          stack.pop_back();
          on[w] = 0;
          comp.push_back(w);
        } while (w != v);
        sccs.push_back(comp);
      }
    };
    for (int v = 0; v < n; v++)
      if (index[v] < 0) strong(v);

    for (const auto& comp : sccs) {
      bool self = comp.size() == 1 && std::count(adj[comp[0]].begin(), adj[comp[0]].end(), comp[0]);
      if (comp.size() < 2 && !self) continue;
      // shortest cycle through the alphabetically first node (BFS), so the
      // message is stable and every step in it is a real edge
      std::set<int> in_comp(comp.begin(), comp.end());
      int start = *std::min_element(comp.begin(), comp.end(), [&](int a, int b) { return names[a] < names[b]; });
      std::map<int, int> parent;
      std::vector<int> q = {start};
      int last = -1;
      for (size_t h = 0; h < q.size() && last < 0; h++)
        for (int w : adj[q[h]]) {
          if (!in_comp.count(w)) continue;
          if (w == start) {
            last = q[h];
            break;
          }
          if (!parent.count(w)) {
            parent[w] = q[h];
            q.push_back(w);
          }
        }
      std::vector<int> path;
      for (int v = last; v >= 0 && v != start; v = parent.count(v) ? parent[v] : -1) path.push_back(v);
      path.push_back(start);
      std::reverse(path.begin(), path.end());
      std::string text;
      for (int v : path) text += (text.empty() ? "" : " -> ") + pretty(names[v]);
      text += " -> " + pretty(names[start]);
      err(m, "E_COMB_LOOP", "module", "combinational loop: " + text + "; break it with a register or \"pipeline\"");
    }

    // summary: which inputs reach which outputs without a register
    Pairs sum;
    for (const auto& p : m.ports) {
      if (p.out) continue;
      auto it = id.find("." + p.name);
      if (it == id.end()) continue;
      std::vector<char> vis(n, 0);
      std::vector<int> st = {it->second};
      while (!st.empty()) {
        int v = st.back();
        st.pop_back();
        if (vis[v]) continue;
        vis[v] = 1;
        for (int w : adj[v]) st.push_back(w);
      }
      for (const auto& q : m.ports)
        if (q.out) {
          auto jt = id.find("." + q.name);
          if (jt != id.end() && vis[jt->second]) sum.push_back({p.name, q.name});
        }
    }
    summary_[m.name] = sum;
  }

  static std::string pretty(const std::string& k) { return k[0] == '.' ? k.substr(1) : k; }

  Netlist& nl_;
  std::map<std::string, Pairs> summary_;
};

}  // namespace

void run_checks(Netlist& nl) { Checker(nl).run(); }

}  // namespace rg
