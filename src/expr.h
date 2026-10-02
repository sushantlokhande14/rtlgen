#pragma once

#include <map>
#include <string>

namespace rg {

// Integer expressions for widths and parameters: + - * / %, parentheses,
// parameter names and clog2(x). Returns false (with a message) on anything else.
bool eval_expr(const std::string& text, const std::map<std::string, long>& vars, long* out, std::string* err);

}  // namespace rg
