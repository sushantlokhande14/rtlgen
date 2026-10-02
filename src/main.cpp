// rtlgen: structured design description (JSON) in, synthesizable Verilog out.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "checks.h"
#include "elab.h"
#include "emit.h"
#include "prims.h"

using namespace rg;

static const char* kUsage = R"(usage: rtlgen [options] design.json

  -o FILE          write Verilog here (default: stdout)
  --no-checks      skip validation and name fixing; emit whatever the description says
  --Werror         treat warnings as errors
  --diags FILE     also write diagnostics as JSON
  -q               only print errors
)";

int main(int argc, char** argv) {
  std::string in, out, diag_file;
  bool checks = true, werror = false, quiet = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "--diags" && i + 1 < argc) diag_file = argv[++i];
    else if (a == "--no-checks") checks = false;
    else if (a == "--Werror") werror = true;
    else if (a == "-q") quiet = true;
    else if (a == "--prims") {  // the primitive library on its own, for simulation tests
      for (const auto& p : all_prims()) std::cout << p.verilog << "\n";
      return 0;
    }
    else if (a == "-h" || a == "--help") {
      std::cout << kUsage;
      return 0;
    } else if (a[0] != '-' && in.empty()) in = a;
    else {
      std::cerr << kUsage;
      return 2;
    }
  }
  if (in.empty()) {
    std::cerr << kUsage;
    return 2;
  }

  json desc;
  try {
    std::ifstream f(in);
    if (!f) throw std::runtime_error("cannot open " + in);
    desc = json::parse(f);
  } catch (const std::exception& e) {
    std::cerr << "rtlgen: " << e.what() << "\n";
    return 1;
  }

  Netlist nl = elaborate(desc);
  if (checks) run_checks(nl);

  std::string source = in.substr(in.find_last_of("/\\") + 1);
  bool blocked = checks && (nl.has_errors() || (werror && !nl.diags.empty()));
  std::string verilog;
  if (!blocked) verilog = emit_verilog(nl, checks, source);  // naming fixes add W_RENAMED diags

  int errors = 0, warnings = 0;
  for (const auto& d : nl.diags) {
    (d.error ? errors : warnings)++;
    if (!checks || (quiet && !d.error)) continue;
    std::cerr << (d.error ? "error" : "warning") << "[" << d.code << "] " << d.module << ": " << d.where << ": "
              << d.msg << "\n";
  }

  if (!diag_file.empty()) {
    json arr = json::array();
    for (const auto& d : nl.diags)
      arr.push_back({{"code", d.code}, {"error", d.error}, {"module", d.module}, {"where", d.where}, {"msg", d.msg}});
    std::ofstream(diag_file) << arr.dump(2) << "\n";
  }

  if (blocked) {
    std::cerr << "rtlgen: " << errors << " error(s), " << warnings << " warning(s); no Verilog written\n";
    return 1;
  }
  if (out.empty()) std::cout << verilog;
  else std::ofstream(out) << verilog;
  if (!quiet)
    std::cerr << "rtlgen: wrote " << (out.empty() ? "stdout" : out) << " (" << nl.mods.size() << " module(s), "
              << warnings << " warning(s))\n";
  return 0;
}
