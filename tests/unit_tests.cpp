// Unit tests: one per check, plus naming, determinism and the expression evaluator.

#include <cstdio>
#include <string>

#include "checks.h"
#include "elab.h"
#include "emit.h"
#include "expr.h"

using namespace rg;

static int failures = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
      failures++;                                                        \
    }                                                                    \
  } while (0)

static Netlist check(const std::string& text) {
  Netlist nl = elaborate(json::parse(text));
  run_checks(nl);
  return nl;
}

static bool has(const Netlist& nl, const std::string& code) {
  for (const auto& d : nl.diags)
    if (d.code == code) return true;
  return false;
}

static std::string diag_text(const Netlist& nl, const std::string& code) {
  for (const auto& d : nl.diags)
    if (d.code == code) return d.msg;
  return "";
}

// a small valid design the tests break in different ways
static std::string base(const std::string& extra_connect = "", const std::string& add_y = "\"add.y\"",
                        const std::string& reg_w = "8") {
  return R"({"top": "t", "modules": {"t": {
    "ports": {"clk": {"dir": "in", "kind": "clock"}, "rst_n": {"dir": "in", "kind": "reset"},
              "a": {"dir": "in", "width": 8}, "b": {"dir": "in", "width": 8}, "y": {"dir": "out", "width": 8}},
    "instances": {"add": {"type": "rg_add", "params": {"WIDTH": 8}},
                  "r": {"type": "rg_reg", "params": {"WIDTH": )" +
         reg_w + R"(}}},
    "connect": [{"from": "a", "to": "add.a"}, {"from": "b", "to": "add.b"},
                {"from": )" +
         add_y + R"(, "to": "r.d"}, {"from": "r.q", "to": "y"})" + extra_connect + R"(],
    "tie": {"r.en": 1}}}})";
}

int main() {
  // expressions
  long v = 0;
  CHECK(eval_expr("W*2+1", {{"W", 8}}, &v, nullptr) && v == 17);
  CHECK(eval_expr("clog2(DEPTH)", {{"DEPTH", 16}}, &v, nullptr) && v == 4);
  CHECK(eval_expr("(W-1)/2", {{"W", 9}}, &v, nullptr) && v == 4);
  std::string err;
  CHECK(!eval_expr("W+X", {{"W", 8}}, &v, &err) && err.find("'X'") != std::string::npos);

  // the valid base design: no errors, clock and reset auto-wired
  Netlist ok = check(base());
  CHECK(!ok.has_errors());
  int auto_wired = 0;
  for (const auto& c : ok.mods[0].conns) auto_wired += c.automatic;
  CHECK(auto_wired == 2);

  CHECK(has(check(base("", "\"add.y\"", "9")), "E_WIDTH"));
  CHECK(diag_text(check(base("", "\"add.y\"", "9")), "E_WIDTH").find("8-bit 'add.y' into 9-bit 'r.d'") !=
        std::string::npos);
  CHECK(has(check(base(R"(, {"from": "b", "to": "r.d"})")), "E_MULTI_DRIVER"));
  CHECK(has(check(base(R"(, {"from": "y", "to": "add.a"})")), "E_DIRECTION"));
  CHECK(has(check(base(R"(, {"from": "add.zz", "to": "y"})")), "E_REF"));
  CHECK(has(check(base(R"(, {"from": "nope.q", "to": "y"})")), "E_REF"));
  CHECK(has(check(base("", "\"add.y[3:0]\"")), "E_WIDTH"));     // slice is 4 bits
  CHECK(has(check(base("", "\"add.y[9:0]\"")), "E_WIDTH"));     // slice out of range
  CHECK(has(check(base("", "\"add.y\"", "0")), "E_PARAM"));     // WIDTH 0

  // nothing drives add.b any more
  std::string undriven = base();
  undriven.replace(undriven.find(R"({"from": "b", "to": "add.b"},)"), 30, "");
  CHECK(has(check(undriven), "E_UNDRIVEN"));

  // a combinational loop: add.y feeds its own input
  std::string loop = base();
  loop.replace(loop.find(R"({"from": "b", "to": "add.b"})"), 28, R"({"from": "add.y", "to": "add.b"})");
  Netlist nl = check(loop);
  CHECK(has(nl, "E_COMB_LOOP"));
  CHECK(diag_text(nl, "E_COMB_LOOP").find("add.b -> add.y -> add.b") != std::string::npos);

  // the same loop with a pipeline stage on it is fine
  std::string piped = loop;
  piped.replace(piped.find(R"({"from": "add.y", "to": "add.b"})"), 32,
                R"({"from": "add.y", "to": "add.b", "pipeline": 1})");
  CHECK(!has(check(piped), "E_COMB_LOOP"));

  // a loop that only exists through two levels of hierarchy
  Netlist hier = check(R"({"top": "top", "modules": {
    "pass": {"ports": {"i": {"dir": "in", "width": 4}, "o": {"dir": "out", "width": 4}},
             "instances": {"n": {"type": "rg_not", "params": {"WIDTH": 4}}},
             "connect": [{"from": "i", "to": "n.a"}, {"from": "n.y", "to": "o"}]},
    "top": {"ports": {"y": {"dir": "out", "width": 4}},
            "instances": {"u0": {"type": "pass"}, "u1": {"type": "pass"}},
            "connect": [{"from": "u0.o", "to": ["u1.i", "y"]}, {"from": "u1.o", "to": "u0.i"}]}}})");
  CHECK(has(hier, "E_COMB_LOOP"));

  // pipelining needs a clock
  CHECK(has(check(R"({"top": "t", "modules": {"t": {
    "ports": {"a": {"dir": "in", "width": 2}, "y": {"dir": "out", "width": 2}},
    "connect": [{"from": "a", "to": "y", "pipeline": 1}]}}})"),
            "E_NO_CLOCK"));

  // keyword port names are an error; keyword instance names get renamed
  CHECK(has(check(R"({"top": "t", "modules": {"t": {"ports": {"input": {"dir": "in"}, "y": {"dir": "out"}},
    "connect": [{"from": "input", "to": "y"}]}}})"),
            "E_NAME"));
  Netlist kw = check(R"({"top": "t", "modules": {"t": {
    "ports": {"a": {"dir": "in", "width": 2}, "y": {"dir": "out", "width": 2}, "reg_q": {"dir": "out", "width": 2}},
    "instances": {"reg": {"type": "rg_not", "params": {"WIDTH": 2}}},
    "connect": [{"from": "a", "to": "reg.a"}, {"from": "reg.y", "to": ["y", "reg_q"]}]}}})");
  CHECK(!kw.has_errors());
  std::string v1 = emit_verilog(kw, true, "kw.json");
  CHECK(has(kw, "W_RENAMED"));
  CHECK(v1.find("rg_not #(.WIDTH(2)) reg_r (") != std::string::npos);
  CHECK(v1.find("wire [1:0] reg_y;") != std::string::npos);

  // deterministic: same input, same bytes
  Netlist again = check(base());
  Netlist again2 = check(base());
  CHECK(emit_verilog(again, true, "x") == emit_verilog(again2, true, "x"));

  // unused outputs and data-as-clock are warnings, not errors
  Netlist w = check(R"({"top": "t", "modules": {"t": {
    "ports": {"clk": {"dir": "in", "kind": "clock"}, "rst_n": {"dir": "in", "kind": "reset"},
              "d": {"dir": "in"}, "y": {"dir": "out"}},
    "instances": {"f": {"type": "rg_fifo", "params": {"WIDTH": 1, "DEPTH": 2}},
                  "r": {"type": "rg_reg"}},
    "connect": [{"from": "d", "to": ["f.din", "f.push", "f.pop", "r.d", "r.clk"]}, {"from": "f.dout", "to": "y"}],
    "tie": {"r.en": 1}}}})");
  CHECK(!w.has_errors());
  CHECK(has(w, "W_UNUSED"));
  CHECK(has(w, "W_KIND"));

  // FIFO depth must be a power of two
  CHECK(diag_text(check(R"({"top": "t", "modules": {"t": {"instances": {"f": {"type": "rg_fifo",
    "params": {"WIDTH": 8, "DEPTH": 6}}}}}})"),
                  "E_PARAM")
            .find("power of two") != std::string::npos);

  std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
