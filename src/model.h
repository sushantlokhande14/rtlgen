#pragma once

#include <map>
#include <string>
#include <vector>

namespace rg {

enum class Kind { Data, Clock, Reset };
enum class Adapt { None, Zext, Sext, Trunc };

struct Diag {
  std::string code;    // E_WIDTH, W_RENAMED, ...
  bool error = true;
  std::string module;  // specialized module name
  std::string where;   // "connect[3]", "instance fifo0", ...
  std::string msg;
};

struct Port {
  std::string name;
  bool out = false;
  long width = 1;     // -1 if the expression couldn't be evaluated
  std::string wtext;  // the expression as written, for naive emission
  Kind kind = Kind::Data;
};

// One side of a connection: a module port (inst == -1) or an instance port.
// Drivers may take a bit slice: "arb.grant[2]" or "a[7:4]".
struct Ep {
  int inst = -1;
  std::string port;
  long hi = -1, lo = -1;  // slice, -1 = whole port
  std::string text;       // as written in the description
  bool sliced() const { return hi >= 0; }
};

struct Inst {
  std::string name;
  std::string type;   // primitive or user module name
  bool prim = false;
  bool known = true;  // type exists
  std::map<std::string, long> params;
  std::vector<std::pair<std::string, std::string>> param_text;  // unevaluated, for naive emission
  std::vector<Port> ports;
  std::string sub;    // specialized module name for user modules
  bool inserted = false;  // created by logic insertion, not the user
};

struct Conn {
  Ep from, to;
  int pipeline = 0;
  Adapt adapt = Adapt::None;
  bool invert = false;
  std::string where;
  bool automatic = false;  // auto-wired clock/reset
};

struct Tie {
  Ep to;
  long value = 0;
  std::string where;
};

struct Module {
  std::string base;  // name in the description
  std::string name;  // specialized: base__W16_D4
  std::map<std::string, long> params;
  std::vector<Port> ports;
  std::vector<Inst> insts;
  std::vector<Conn> conns;
  std::vector<Tie> ties;

  const Port* port(const std::string& n) const {
    for (const auto& p : ports)
      if (p.name == n) return &p;
    return nullptr;
  }
};

struct Netlist {
  std::string top;
  std::vector<Module> mods;  // leaves first, top last
  std::vector<Diag> diags;

  const Module* find(const std::string& name) const {
    for (const auto& m : mods)
      if (m.name == name) return &m;
    return nullptr;
  }
  bool has_errors() const {
    for (const auto& d : diags)
      if (d.error) return true;
    return false;
  }
};

const char* kind_name(Kind k);

}  // namespace rg
