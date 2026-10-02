#include "elab.h"

#include <cctype>
#include <set>

#include "expr.h"
#include "prims.h"

namespace rg {

namespace {

std::string as_text(const json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_number_integer()) return std::to_string(v.get<long>());
  return v.dump();
}

class Elaborator {
 public:
  explicit Elaborator(const json& d) : desc_(d) {}

  Netlist run() {
    nl_.top = desc_.value("top", "");
    if (!desc_.contains("modules") || !desc_["modules"].is_object()) {
      diag("E_REF", "", "design", "description has no \"modules\" object");
      return std::move(nl_);
    }
    if (nl_.top.empty() || !desc_["modules"].contains(nl_.top)) {
      diag("E_REF", "", "design", "top module '" + nl_.top + "' is not defined");
      return std::move(nl_);
    }
    nl_.top = specialize(nl_.top, {}, "top");
    return std::move(nl_);
  }

 private:
  void diag(const std::string& code, const std::string& mod, const std::string& where, const std::string& msg,
            bool error = true) {
    nl_.diags.push_back({code, error, mod, where, msg});
  }

  // Specialized names are stable: same base + same parameter values, same name.
  static std::string spec_name(const std::string& base, const std::map<std::string, long>& params,
                               const json& md) {
    if (!md.contains("params") || md["params"].empty()) return base;
    std::string n = base + "_";
    for (const auto& [k, v] : params) n += "_" + k + std::to_string(v);
    for (char& c : n)
      if (c == '-') c = 'm';
    return n;
  }

  std::string specialize(const std::string& base, const std::map<std::string, std::string>& overrides,
                         const std::string& context) {
    const json& md = desc_["modules"][base];

    // parameter values: defaults, then the instantiating module's overrides
    std::map<std::string, long> params;
    if (md.contains("params"))
      for (const auto& [k, v] : md["params"].items()) {
        long x = 0;
        std::string err;
        if (!eval_expr(as_text(v), params, &x, &err)) diag("E_PARAM", base, "params." + k, err);
        params[k] = x;
      }
    for (const auto& [k, txt] : overrides) {
      if (!params.count(k)) {
        diag("E_PARAM", base, context, "module '" + base + "' has no parameter '" + k + "'");
        continue;
      }
      long x = 0;
      std::string err;
      if (eval_expr(txt, parent_params_, &x, &err)) params[k] = x;
      else diag("E_PARAM", base, context, "parameter " + k + ": " + err);
    }

    std::string name = spec_name(base, params, md);
    if (done_.count(name)) return name;
    for (const auto& s : stack_)
      if (s == name) {
        diag("E_RECURSION", name, context, "module instantiates itself (directly or indirectly)");
        return name;
      }
    stack_.push_back(name);

    Module m;
    m.base = base;
    m.name = name;
    m.params = params;

    if (md.contains("ports"))
      for (const auto& [pn, pj] : md["ports"].items()) {
        Port p;
        p.name = pn;
        p.out = pj.value("dir", "in") == "out";
        p.wtext = pj.contains("width") ? as_text(pj["width"]) : "1";
        std::string err;
        if (!eval_expr(p.wtext, params, &p.width, &err)) {
          diag("E_PARAM", name, "port " + pn, "width: " + err);
          p.width = -1;
        } else if (p.width < 1) {
          diag("E_PARAM", name, "port " + pn, "width evaluates to " + std::to_string(p.width));
        }
        std::string k = pj.value("kind", "data");
        p.kind = k == "clock" ? Kind::Clock : k == "reset" ? Kind::Reset : Kind::Data;
        m.ports.push_back(p);
      }

    if (md.contains("instances"))
      for (const auto& [in, ij] : md["instances"].items()) {
        Inst inst;
        inst.name = in;
        inst.type = ij.value("type", "");
        std::map<std::string, std::string> ptext;
        if (ij.contains("params"))
          for (const auto& [k, v] : ij["params"].items()) {
            ptext[k] = as_text(v);
            inst.param_text.push_back({k, as_text(v)});
          }
        std::string where = "instance " + in;
        if (const Prim* pr = find_prim(inst.type)) {
          inst.prim = true;
          for (const auto& [k, def] : pr->params) inst.params[k] = def;
          for (const auto& [k, txt] : ptext) {
            long x = 0;
            std::string err;
            if (eval_expr(txt, params, &x, &err)) inst.params[k] = x;
            else diag("E_PARAM", name, where, k + ": " + err);
          }
          std::string why = check_prim_params(*pr, inst.params);
          if (!why.empty()) diag("E_PARAM", name, where, inst.type + ": " + why);
          for (const auto& pp : pr->ports) {
            Port p;
            p.name = pp.name;
            p.out = pp.out;
            p.kind = pp.kind;
            p.wtext = pp.width;
            std::string err;
            if (!eval_expr(pp.width, inst.params, &p.width, &err)) p.width = -1;
            inst.ports.push_back(p);
          }
        } else if (desc_["modules"].contains(inst.type)) {
          auto saved = parent_params_;
          parent_params_ = params;
          inst.sub = specialize(inst.type, ptext, where);
          parent_params_ = saved;
          if (const Module* sm = nl_.find(inst.sub)) inst.ports = sm->ports;
        } else {
          inst.known = false;
          diag("E_REF", name, where, "unknown module or primitive '" + inst.type + "'");
        }
        m.insts.push_back(inst);
      }

    if (md.contains("connect"))
      for (size_t i = 0; i < md["connect"].size(); i++) {
        const json& cj = md["connect"][i];
        std::string where = "connect[" + std::to_string(i) + "]";
        if (!cj.contains("from") || !cj.contains("to")) {
          diag("E_REF", name, where, "connection needs \"from\" and \"to\"");
          continue;
        }
        Conn base_c;
        base_c.from = parse_ep(m, as_text(cj["from"]));
        base_c.pipeline = cj.value("pipeline", 0);
        base_c.invert = cj.value("invert", false);
        std::string ad = cj.value("adapt", "");
        base_c.adapt = ad == "zext" ? Adapt::Zext : ad == "sext" ? Adapt::Sext : ad == "trunc" ? Adapt::Trunc : Adapt::None;
        if (!ad.empty() && base_c.adapt == Adapt::None) diag("E_REF", name, where, "unknown adapt '" + ad + "'");
        std::vector<std::string> tos;
        if (cj["to"].is_array())
          for (const auto& t : cj["to"]) tos.push_back(as_text(t));
        else
          tos.push_back(as_text(cj["to"]));
        for (const auto& t : tos) {
          Conn c = base_c;
          c.to = parse_ep(m, t);
          c.where = where + " (" + c.from.text + " -> " + c.to.text + ")";
          m.conns.push_back(c);
        }
      }

    if (md.contains("tie"))
      for (const auto& [ep, v] : md["tie"].items()) {
        Tie t;
        t.to = parse_ep(m, ep);
        t.value = v.is_number_integer() ? v.get<long>() : 0;
        t.where = "tie " + ep;
        m.ties.push_back(t);
      }

    auto_wire(m);

    stack_.pop_back();
    done_.insert(name);
    nl_.mods.push_back(std::move(m));
    return name;
  }

  // "inst.port", "port", optionally with [hi:lo] or [bit]
  static Ep parse_ep(const Module& m, const std::string& text) {
    Ep e;
    e.text = text;
    std::string s = text;
    auto lb = s.find('[');
    if (lb != std::string::npos && s.back() == ']') {
      std::string sl = s.substr(lb + 1, s.size() - lb - 2);
      s = s.substr(0, lb);
      auto colon = sl.find(':');
      try {
        e.hi = std::stol(sl.substr(0, colon));
        e.lo = colon == std::string::npos ? e.hi : std::stol(sl.substr(colon + 1));
      } catch (...) {
        e.hi = e.lo = -2;  // malformed; the checker reports it
      }
    }
    auto dot = s.find('.');
    if (dot == std::string::npos) {
      e.port = s;
      return e;
    }
    std::string in = s.substr(0, dot);
    e.port = s.substr(dot + 1);
    e.inst = -2;  // unknown instance until found
    for (size_t i = 0; i < m.insts.size(); i++)
      if (m.insts[i].name == in) e.inst = (int)i;
    return e;
  }

  // Unconnected clock/reset inputs go to the module's only clock/reset port.
  void auto_wire(Module& m) {
    for (size_t i = 0; i < m.insts.size(); i++) {
      for (const auto& p : m.insts[i].ports) {
        if (p.out || p.kind == Kind::Data) continue;
        bool driven = false;
        for (const auto& c : m.conns) driven |= c.to.inst == (int)i && c.to.port == p.name;
        for (const auto& t : m.ties) driven |= t.to.inst == (int)i && t.to.port == p.name;
        if (driven) continue;
        std::vector<const Port*> cands;
        for (const auto& mp : m.ports)
          if (!mp.out && mp.kind == p.kind) cands.push_back(&mp);
        if (cands.empty()) continue;  // stays undriven; the checker says so
        if (cands.size() > 1)
          diag("E_AMBIGUOUS", m.name, "instance " + m.insts[i].name,
               std::string(kind_name(p.kind)) + " port " + p.name + " could be any of " +
                   std::to_string(cands.size()) + " module " + kind_name(p.kind) + "s; connect it explicitly");
        Conn c;
        c.from.port = cands[0]->name;
        c.from.text = cands[0]->name;
        c.to.inst = (int)i;
        c.to.port = p.name;
        c.to.text = m.insts[i].name + "." + p.name;
        c.automatic = true;
        c.where = "auto-wired " + c.to.text;
        m.conns.push_back(c);
      }
    }
  }

  const json& desc_;
  Netlist nl_;
  std::set<std::string> done_;
  std::vector<std::string> stack_;
  std::map<std::string, long> parent_params_;
};

}  // namespace

Netlist elaborate(const json& desc) { return Elaborator(desc).run(); }

}  // namespace rg
