#include "vcb.hpp"

namespace vcb {

// ------------------------------------------------------------
// Peephole pass:
//   - Copy x, x         -> drop
//   - Copy b, a; use b  -> chain  (single-use copy forwarding)
// ------------------------------------------------------------
void peephole(Function& f) {
  // Count uses
  std::unordered_map<ValueId, int> uses;
  for (auto& b : f.blocks)
    for (auto& in : b.instrs) {
      auto touch = [&](const Operand& o) {
        if (!o.isImm && o.val != NOVAL) uses[o.val]++;
      };
      if (in.op != Op::Phi) { touch(in.a); touch(in.b); }
      for (auto& pa : in.phiArgs) uses[pa.first]++;
    }

  // Remove self-copies and drop trivial Copys from immediates that are
  // used only once (handled by codegen anyway). Simply drop self-copies.
  for (auto& b : f.blocks) {
    std::vector<Instr> keep;
    keep.reserve(b.instrs.size());
    for (auto& in : b.instrs) {
      if (in.op == Op::Copy && !in.a.isImm && in.a.val == in.dst)
        continue;
      keep.push_back(std::move(in));
    }
    b.instrs = std::move(keep);
  }

  // Forward single-use copies from imms where it simplifies downstream.
  // (Conservative: skip for now. Codegen handles Copy uniformly.)
}

void lower(Module& m) {
  for (auto& fp : m.funcs)
    peephole(*fp);
}

} // namespace vcb