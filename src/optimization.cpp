#include "vcb.hpp"
#include <algorithm>

namespace vcb {

// ------------------------------------------------------------
// Constant folding.
// ------------------------------------------------------------
static bool foldInstr(Instr& in) {
    if (!(in.a.isImm && in.b.isImm)) return false;

    int64_t x = in.a.imm, y = in.b.imm, r = 0;
    bool ok = true;
    switch (in.op) {
        case Op::Add: r = x + y; break;
        case Op::Sub: r = x - y; break;
        case Op::Mul: r = x * y; break;
        case Op::Div: if (y == 0) { ok = false; break; } r = x / y; break;
        case Op::Rem: if (y == 0) { ok = false; break; } r = x % y; break;
        case Op::And: r = x & y; break;
        case Op::Or:  r = x | y; break;
        case Op::Xor: r = x ^ y; break;
        case Op::Shl: r = x << (y & 63); break;
        case Op::Shr: r = (int64_t)((uint64_t)x >> (y & 63)); break;
        case Op::Sar: r = x >> (y & 63); break;
        case Op::Ceq: r = (x == y); break;
        case Op::Cne: r = (x != y); break;
        case Op::Clt: r = (x <  y); break;
        case Op::Cle: r = (x <= y); break;
        case Op::Cgt: r = (x >  y); break;
        case Op::Cge: r = (x >= y); break;
        default: ok = false; break;
    }
    if (!ok) return false;

    in.op    = Op::Copy;
    in.a     = Operand::I(r);
    in.b     = Operand{};
    return true;
}

void constantFold(Function& f) {
    // Iterate until no more changes
    bool changed = true;
    while (changed) {
        changed = false;

        // Map value -> last constant seen (Copy-from-imm)
        std::unordered_map<ValueId, int64_t> constants;
        for (auto& b : f.blocks) {
            for (auto& in : b.instrs) {
                if (in.op == Op::Copy && in.a.isImm && in.dst != NOVAL)
                    constants[in.dst] = in.a.imm;

                // Rewrite operands to immediates when possible
                auto rewrite = [&](Operand& o) {
                    if (o.isImm || o.val == NOVAL) return;
                    auto it = constants.find(o.val);
                    if (it != constants.end()) {
                        o = Operand::I(it->second);
                        changed = true;
                    }
                };
                if (in.op != Op::Phi) { rewrite(in.a); rewrite(in.b); }

                if (foldInstr(in)) changed = true;
            }
        }
    }
}

// ------------------------------------------------------------
// Dead code elimination.
// ------------------------------------------------------------
void deadCodeElim(Function& f) {
    auto lv = computeLiveness(f);

    // Collect side-effecting dsts
    auto hasSideEffect = [](Op op) {
        return op == Op::Store || op == Op::Call ||
               op == Op::Ret   || op == Op::Jmp  || op == Op::Jnz;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        // Collect all values used anywhere
        std::unordered_set<ValueId> used;
        for (auto& b : f.blocks) {
            for (auto& in : b.instrs) {
                if (hasSideEffect(in.op)) {
                    // side effect: keep all its operands alive
                }
                auto touch = [&](const Operand& o) {
                    if (!o.isImm && o.val != NOVAL) used.insert(o.val);
                };
                if (in.op != Op::Phi) { touch(in.a); touch(in.b); }
                for (auto& pa : in.phiArgs) used.insert(pa.first);
                // Phi values used by other phis in same block already captured
            }
        }
        for (auto& b : f.blocks) {
            std::vector<Instr> keep;
            keep.reserve(b.instrs.size());
            for (auto& in : b.instrs) {
                if (in.dst != NOVAL && !hasSideEffect(in.op) &&
                    !used.count(in.dst)) {
                    changed = true;
                    continue;  // drop
                }
                keep.push_back(std::move(in));
            }
            b.instrs = std::move(keep);
        }
    }
}

// ------------------------------------------------------------
// Local CSE within a basic block.
// ------------------------------------------------------------
struct ExprKey {
    Op      op;
    Ty      ty;
    bool    aImm; int64_t aImmV; ValueId aVal;
    bool    bImm; int64_t bImmV; ValueId bVal;

    bool operator==(const ExprKey& o) const {
        return op == o.op && ty == o.ty &&
               aImm == o.aImm && aImmV == o.aImmV && aVal == o.aVal &&
               bImm == o.bImm && bImmV == o.bImmV && bVal == o.bVal;
    }
};

struct ExprHash {
    size_t operator()(const ExprKey& k) const {
        size_t h = (size_t)k.op * 131 + (size_t)k.ty * 17;
        h = h * 31 + k.aImmV; h = h * 31 + k.aVal;
        h = h * 31 + k.bImmV; h = h * 31 + k.bVal;
        h ^= (size_t)k.aImm << 3;
        h ^= (size_t)k.bImm << 5;
        return h;
    }
};

static bool pureOp(Op op) {
    switch (op) {
        case Op::Add: case Op::Sub: case Op::Mul:
        case Op::And: case Op::Or:  case Op::Xor:
        case Op::Shl: case Op::Shr: case Op::Sar:
        case Op::Ceq: case Op::Cne: case Op::Clt:
        case Op::Cle: case Op::Cgt: case Op::Cge:
        case Op::Copy:
            return true;
        default: return false;
    }
}

void commonSubexprElim(Function& f) {
    for (auto& b : f.blocks) {
        std::unordered_map<ExprKey, ValueId, ExprHash> table;
        std::vector<Instr> out;
        out.reserve(b.instrs.size());
        for (auto& in : b.instrs) {
            if (pureOp(in.op)) {
                ExprKey k{};
                k.op = in.op; k.ty = in.ty;
                k.aImm = in.a.isImm; k.aImmV = in.a.imm; k.aVal = in.a.val;
                k.bImm = in.b.isImm; k.bImmV = in.b.imm; k.bVal = in.b.val;
                auto it = table.find(k);
                if (it != table.end()) {
                    // Replace with a copy
                    Instr c;
                    c.op  = Op::Copy;
                    c.ty  = in.ty;
                    c.dst = in.dst;
                    c.a   = Operand::V(it->second);
                    out.push_back(std::move(c));
                    continue;
                }
                table[k] = in.dst;
            } else {
                // Barrier: keep table for loads? we drop loads from table.
                if (in.op == Op::Load || in.op == Op::Call || in.op == Op::Store)
                    table.clear();
            }
            out.push_back(std::move(in));
        }
        b.instrs = std::move(out);
    }
}

// ------------------------------------------------------------
// Algebraic simplify: x+0, x*1, x*0, x-0, x|x, x&x.
// ------------------------------------------------------------
void simplify(Function& f) {
    for (auto& b : f.blocks) {
        for (auto& in : b.instrs) {
            if (!in.a.isImm && in.b.isImm) {
                int64_t c = in.b.imm;
                switch (in.op) {
                    case Op::Add: if (c == 0) { in.op = Op::Copy; in.b = {}; } break;
                    case Op::Sub: if (c == 0) { in.op = Op::Copy; in.b = {}; } break;
                    case Op::Mul:
                        if (c == 1) { in.op = Op::Copy; in.b = {}; }
                        else if (c == 0) { in.a = Operand::I(0); in.b = {}; }
                        break;
                    case Op::Or:
                    case Op::Xor: if (c == 0) { in.op = Op::Copy; in.b = {}; } break;
                    case Op::And:
                        if (c == -1) { in.op = Op::Copy; in.b = {}; }
                        else if (c == 0) { in.a = Operand::I(0); in.b = {}; }
                        break;
                    case Op::Shl:
                    case Op::Shr:
                    case Op::Sar: if (c == 0) { in.op = Op::Copy; in.b = {}; } break;
                    default: break;
                }
            }
        }
    }
}

// ------------------------------------------------------------
// Copy coalescing / propagation.
//
// For every `%d = copy %a` where %a has no other use anywhere,
// replace all uses of %d with %a and delete the copy. This is
// the IR-level equivalent of register coalescing: the emitter
// then sees one value where it saw two.
//
// Iterates to fixpoint. O(n) per sweep; typically 2-3 sweeps
// for functions under a few hundred instructions.
// ------------------------------------------------------------
static void coalesceCopiesFn(Function& f) {
    auto countUses = [&](ValueId v) {
        int n = 0;
        for (auto& b : f.blocks)
            for (auto& in : b.instrs) {
                if (!in.a.isImm && in.a.val == v) ++n;
                if (!in.b.isImm && in.b.val == v) ++n;
                for (auto& pa : in.phiArgs) if (pa.first == v) ++n;
            }
        return n;
        };

    auto renameUses = [&](ValueId from, ValueId to) {
        for (auto& b : f.blocks)
            for (auto& in : b.instrs) {
                if (!in.a.isImm && in.a.val == from) in.a.val = to;
                if (!in.b.isImm && in.b.val == from) in.b.val = to;
                for (auto& pa : in.phiArgs)
                    if (pa.first == from) pa.first = to;
            }
        };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& b : f.blocks) {
            for (auto& in : b.instrs) {
                if (in.op != Op::Copy)  continue;
                if (in.a.isImm)         continue;
                if (in.dst == NOVAL)    continue;

                ValueId src = in.a.val;
                ValueId dst = in.dst;
                if (src == dst) { in.op = Op::Nop; changed = true; break; }

                // Only safe if src has exactly this one use.
                if (countUses(src) != 1) continue;

                renameUses(dst, src);
                in.op = Op::Nop;
                in.dst = NOVAL;
                changed = true;
                break;
            }
            if (changed) break;
        }
    }

    // Sweep Nops out of every block.
    for (auto& b : f.blocks) {
        std::vector<Instr> keep;
        keep.reserve(b.instrs.size());
        for (auto& in : b.instrs)
            if (in.op != Op::Nop) keep.push_back(std::move(in));
        b.instrs = std::move(keep);
    }
}

// ------------------------------------------------------------
// Driver.
// ------------------------------------------------------------
void optimize(Module& m) {
    for (auto& fp : m.funcs) {
        Function& f = *fp;
        for (int round = 0; round < 4; ++round) {
            simplify(f);
            constantFold(f);
            commonSubexprElim(f);
            coalesceCopiesFn(f);
            deadCodeElim(f);
        }
    }
}
void coalesceCopies(Module& m) {
    for (auto& fp : m.funcs)
        coalesceCopiesFn(*fp);
}

} // namespace vcb