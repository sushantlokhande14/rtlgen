#include "expr.h"

#include <cctype>
#include <stdexcept>

namespace rg {

namespace {

struct Parser {
  const std::string& s;
  const std::map<std::string, long>& vars;
  size_t i = 0;

  void ws() {
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
  }
  bool eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) {
      i++;
      return true;
    }
    return false;
  }

  long expr() {
    long v = term();
    for (;;) {
      if (eat('+')) v += term();
      else if (eat('-')) v -= term();
      else return v;
    }
  }

  long term() {
    long v = unary();
    for (;;) {
      if (eat('*')) v *= unary();
      else if (eat('/')) {
        long d = unary();
        if (d == 0) throw std::runtime_error("division by zero");
        v /= d;
      } else if (eat('%')) {
        long d = unary();
        if (d == 0) throw std::runtime_error("modulo by zero");
        v %= d;
      } else return v;
    }
  }

  long unary() {
    if (eat('-')) return -unary();
    return atom();
  }

  long atom() {
    ws();
    if (eat('(')) {
      long v = expr();
      if (!eat(')')) throw std::runtime_error("missing ')'");
      return v;
    }
    if (i < s.size() && std::isdigit((unsigned char)s[i])) {
      long v = 0;
      while (i < s.size() && std::isdigit((unsigned char)s[i])) v = v * 10 + (s[i++] - '0');
      return v;
    }
    if (i < s.size() && (std::isalpha((unsigned char)s[i]) || s[i] == '_')) {
      size_t b = i;
      while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
      std::string id = s.substr(b, i - b);
      if (id == "clog2") {
        if (!eat('(')) throw std::runtime_error("clog2 needs '('");
        long x = expr();
        if (!eat(')')) throw std::runtime_error("missing ')'");
        long r = 0;
        while ((1L << r) < x) r++;
        return r;
      }
      auto it = vars.find(id);
      if (it == vars.end()) throw std::runtime_error("unknown parameter '" + id + "'");
      return it->second;
    }
    throw std::runtime_error("unexpected '" + (i < s.size() ? std::string(1, s[i]) : std::string("end")) + "'");
  }
};

}  // namespace

bool eval_expr(const std::string& text, const std::map<std::string, long>& vars, long* out, std::string* err) {
  try {
    Parser p{text, vars};
    long v = p.expr();
    p.ws();
    if (p.i != text.size()) throw std::runtime_error("trailing text in '" + text + "'");
    *out = v;
    return true;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

}  // namespace rg
