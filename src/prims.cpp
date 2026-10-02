#include "prims.h"

namespace rg {

namespace {

const Kind D = Kind::Data, C = Kind::Clock, R = Kind::Reset;

std::vector<Prim> make_library() {
  std::vector<Prim> lib;

  lib.push_back({"rg_reg", {{"WIDTH", 1}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"en", false, "1", D},
                  {"d", false, "WIDTH", D}, {"q", true, "WIDTH", D}},
                 {},
                 R"V(module rg_reg #(parameter WIDTH = 1) (
  input clk,
  input rst_n,
  input en,
  input [WIDTH-1:0] d,
  output reg [WIDTH-1:0] q
);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) q <= {WIDTH{1'b0}};
    else if (en) q <= d;
endmodule
)V"});

  lib.push_back({"rg_pipe", {{"WIDTH", 1}, {"STAGES", 1}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"d", false, "WIDTH", D},
                  {"q", true, "WIDTH", D}},
                 {},
                 R"V(module rg_pipe #(parameter WIDTH = 1, parameter STAGES = 1) (
  input clk,
  input rst_n,
  input [WIDTH-1:0] d,
  output [WIDTH-1:0] q
);
  reg [WIDTH*STAGES-1:0] sr;
  generate
    if (STAGES == 1) begin : one
      always @(posedge clk or negedge rst_n)
        if (!rst_n) sr <= {WIDTH{1'b0}};
        else sr <= d;
    end else begin : many
      always @(posedge clk or negedge rst_n)
        if (!rst_n) sr <= {(WIDTH*STAGES){1'b0}};
        else sr <= {sr[WIDTH*(STAGES-1)-1:0], d};
    end
  endgenerate
  assign q = sr[WIDTH*STAGES-1 -: WIDTH];
endmodule
)V"});

  lib.push_back({"rg_fifo", {{"WIDTH", 8}, {"DEPTH", 4}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"push", false, "1", D},
                  {"din", false, "WIDTH", D}, {"pop", false, "1", D}, {"dout", true, "WIDTH", D},
                  {"full", true, "1", D}, {"empty", true, "1", D}},
                 {},
                 R"V(module rg_fifo #(parameter WIDTH = 8, parameter DEPTH = 4) (
  input clk,
  input rst_n,
  input push,
  input [WIDTH-1:0] din,
  input pop,
  output [WIDTH-1:0] dout,
  output full,
  output empty
);
  localparam AW = $clog2(DEPTH);
  reg [WIDTH-1:0] mem [0:DEPTH-1];
  reg [AW:0] wp, rp;
  wire do_push = push && !full;
  wire do_pop = pop && !empty;
  always @(posedge clk)
    if (do_push) mem[wp[AW-1:0]] <= din;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      wp <= {(AW+1){1'b0}};
      rp <= {(AW+1){1'b0}};
    end else begin
      if (do_push) wp <= wp + 1'b1;
      if (do_pop) rp <= rp + 1'b1;
    end
  assign empty = (wp == rp);
  assign full = (wp[AW-1:0] == rp[AW-1:0]) && (wp[AW] != rp[AW]);
  assign dout = mem[rp[AW-1:0]];
endmodule
)V"});

  lib.push_back({"rg_add", {{"WIDTH", 8}},
                 {{"a", false, "WIDTH", D}, {"b", false, "WIDTH", D}, {"y", true, "WIDTH", D}},
                 {{"a", "y"}, {"b", "y"}},
                 R"V(module rg_add #(parameter WIDTH = 8) (
  input [WIDTH-1:0] a,
  input [WIDTH-1:0] b,
  output [WIDTH-1:0] y
);
  assign y = a + b;
endmodule
)V"});

  lib.push_back({"rg_mux2", {{"WIDTH", 8}},
                 {{"sel", false, "1", D}, {"a", false, "WIDTH", D}, {"b", false, "WIDTH", D},
                  {"y", true, "WIDTH", D}},
                 {{"sel", "y"}, {"a", "y"}, {"b", "y"}},
                 R"V(module rg_mux2 #(parameter WIDTH = 8) (
  input sel,
  input [WIDTH-1:0] a,
  input [WIDTH-1:0] b,
  output [WIDTH-1:0] y
);
  assign y = sel ? b : a;
endmodule
)V"});

  for (const char* op : {"and", "or", "xor"}) {
    std::string n = std::string("rg_") + op;
    std::string sym = op[0] == 'a' ? "&" : op[0] == 'o' ? "|" : "^";
    lib.push_back({n, {{"WIDTH", 8}},
                   {{"a", false, "WIDTH", D}, {"b", false, "WIDTH", D}, {"y", true, "WIDTH", D}},
                   {{"a", "y"}, {"b", "y"}},
                   "module " + n +
                       " #(parameter WIDTH = 8) (\n  input [WIDTH-1:0] a,\n  input [WIDTH-1:0] b,\n"
                       "  output [WIDTH-1:0] y\n);\n  assign y = a " +
                       sym + " b;\nendmodule\n"});
  }

  lib.push_back({"rg_not", {{"WIDTH", 8}},
                 {{"a", false, "WIDTH", D}, {"y", true, "WIDTH", D}},
                 {{"a", "y"}},
                 R"V(module rg_not #(parameter WIDTH = 8) (
  input [WIDTH-1:0] a,
  output [WIDTH-1:0] y
);
  assign y = ~a;
endmodule
)V"});

  lib.push_back({"rg_eq", {{"WIDTH", 8}},
                 {{"a", false, "WIDTH", D}, {"b", false, "WIDTH", D}, {"y", true, "1", D}},
                 {{"a", "y"}, {"b", "y"}},
                 R"V(module rg_eq #(parameter WIDTH = 8) (
  input [WIDTH-1:0] a,
  input [WIDTH-1:0] b,
  output y
);
  assign y = (a == b);
endmodule
)V"});

  lib.push_back({"rg_counter", {{"WIDTH", 8}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"en", false, "1", D},
                  {"clear", false, "1", D}, {"count", true, "WIDTH", D}},
                 {},
                 R"V(module rg_counter #(parameter WIDTH = 8) (
  input clk,
  input rst_n,
  input en,
  input clear,
  output reg [WIDTH-1:0] count
);
  always @(posedge clk or negedge rst_n)
    if (!rst_n) count <= {WIDTH{1'b0}};
    else if (clear) count <= {WIDTH{1'b0}};
    else if (en) count <= count + 1'b1;
endmodule
)V"});

  lib.push_back({"rg_rr_arb", {{"N", 2}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"req", false, "N", D},
                  {"grant", true, "N", D}},
                 {{"req", "grant"}},
                 // Round robin without a modulo: x & -x isolates the lowest set bit.
                 // Prefer requesters above the last grant (mask), else wrap around.
                 // (The first version looped with (ptr + i) % N on 32-bit integers;
                 // Yosys built divider trees for it and needed 1.4 GB per design.)
                 R"V(module rg_rr_arb #(parameter N = 2) (
  input clk,
  input rst_n,
  input [N-1:0] req,
  output [N-1:0] grant
);
  reg [N-1:0] mask;
  wire [N-1:0] masked = req & mask;
  wire [N-1:0] first_masked = masked & (~masked + 1'b1);
  wire [N-1:0] first_any = req & (~req + 1'b1);
  assign grant = (|masked) ? first_masked : first_any;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) mask <= {N{1'b1}};
    else if (|grant) mask <= ~((grant << 1) - 1'b1);
endmodule
)V"});

  lib.push_back({"rg_sync2", {{"WIDTH", 1}},
                 {{"clk", false, "1", C}, {"rst_n", false, "1", R}, {"d", false, "WIDTH", D},
                  {"q", true, "WIDTH", D}},
                 {},
                 R"V(module rg_sync2 #(parameter WIDTH = 1) (
  input clk,
  input rst_n,
  input [WIDTH-1:0] d,
  output [WIDTH-1:0] q
);
  reg [WIDTH-1:0] s1, s2;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      s1 <= {WIDTH{1'b0}};
      s2 <= {WIDTH{1'b0}};
    end else begin
      s1 <= d;
      s2 <= s1;
    end
  assign q = s2;
endmodule
)V"});

  return lib;
}

}  // namespace

const std::vector<Prim>& all_prims() {
  static const std::vector<Prim> lib = make_library();
  return lib;
}

const Prim* find_prim(const std::string& name) {
  for (const auto& p : all_prims())
    if (p.name == name) return &p;
  return nullptr;
}

std::string check_prim_params(const Prim& p, const std::map<std::string, long>& v) {
  auto get = [&](const char* k) {
    auto it = v.find(k);
    return it == v.end() ? 0L : it->second;
  };
  for (const auto& [name, def] : p.params) {
    (void)def;
    if (!v.count(name)) return "missing parameter " + name;
  }
  if (v.count("WIDTH") && (get("WIDTH") < 1 || get("WIDTH") > 4096))
    return "WIDTH must be 1..4096, got " + std::to_string(get("WIDTH"));
  if (p.name == "rg_pipe" && get("STAGES") < 1) return "STAGES must be at least 1";
  if (p.name == "rg_fifo") {
    long d = get("DEPTH");
    if (d < 2 || (d & (d - 1)) != 0) return "DEPTH must be a power of two >= 2, got " + std::to_string(d);
  }
  if (p.name == "rg_rr_arb" && (get("N") < 2 || get("N") > 64)) return "N must be 2..64";
  for (const auto& [name, val] : v) {
    bool known = false;
    for (const auto& [pn, def] : p.params) known |= pn == name;
    (void)val;
    if (!known) return "unknown parameter " + name;
  }
  return "";
}

const char* kind_name(Kind k) {
  return k == Kind::Clock ? "clock" : k == Kind::Reset ? "reset" : "data";
}

}  // namespace rg
