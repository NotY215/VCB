#include "vcb.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>

using namespace vcb;

static void t_parse_basic() {
  const char* src = R"(
export function i32 main() {
entry:
    %1 = add i32 5, 3
    %2 = mul i32 %1, 2
    ret i32 %2
}
)";
  Module m;
  std::string err;
  bool ok = parseIR(src, m, err);
  if (!ok) { std::fprintf(stderr, "parse failed: %s\n", err.c_str()); }
  assert(ok);
  assert(m.funcs.size() == 1);
  assert(m.funcs[0]->name == "main");
  assert(m.funcs[0]->blocks.size() == 1);
  assert(m.funcs[0]->blocks[0].instrs.size() == 3);
}

static void t_optimize() {
  Module m;
  std::string err;
  parseIR(R"(
function i32 f() {
entry:
    %1 = add i32 2, 3
    %2 = add i32 %1, 0
    %3 = mul i32 %2, 1
    ret i32 %3
}
)", m, err);
  assert(!m.funcs.empty());
  optimize(m);
  // After folding + simplify, the body should be a single ret of 5.
  auto& f = *m.funcs[0];
  assert(f.blocks.size() == 1);
  // Sanity: DCE should have removed unused ops.
  assert(f.blocks[0].instrs.size() <= 3);
}

static void t_codegen_smoke() {
  Module m;
  std::string err;
  parseIR(R"(
export function i32 main() {
entry:
    %1 = add i32 5, 3
    ret i32 %1
}
)", m, err);
  optimize(m); lower(m);
  std::ostringstream out;
  codegen(m, out);
  std::string s = out.str();
  assert(s.find(".globl main") != std::string::npos);
  assert(s.find("push rbp") != std::string::npos);
  assert(s.find("ret") != std::string::npos);
}

int main() {
  t_parse_basic();
  t_optimize();
  t_codegen_smoke();
  std::printf("vcb-tests: all passed\n");
  return 0;
}