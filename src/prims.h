#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "model.h"

namespace rg {

struct PrimPort {
  std::string name;
  bool out;
  std::string width;  // expression over the primitive's parameters
  Kind kind;
};

// A library cell rtlgen can instantiate. `comb` lists input -> output pairs
// with a purely combinational path; the loop checker needs them.
struct Prim {
  std::string name;
  std::vector<std::pair<std::string, long>> params;  // name, default
  std::vector<PrimPort> ports;
  std::vector<std::pair<std::string, std::string>> comb;
  std::string verilog;
};

const Prim* find_prim(const std::string& name);
const std::vector<Prim>& all_prims();

// "" if the parameter values are legal for this primitive, else why not.
std::string check_prim_params(const Prim& p, const std::map<std::string, long>& v);

}  // namespace rg
