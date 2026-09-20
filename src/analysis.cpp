#include "vcb.hpp"

namespace vcb {

size_t typeSize(Ty t) {
    switch (t) {
        case Ty::I8:  return 1;
        case Ty::I16: return 2;
        case Ty::I32:
        case Ty::F32: return 4;
        case Ty::I64:
        case Ty::F64:
        case Ty::Ptr: return 8;
        default:      return 0;
    }
}

const char* typeName(Ty t) {
    switch (t) {
        case Ty::Void: return "void";
        case Ty::I8:   return "i8";
        case Ty::I16:  return "i16";
        case Ty::I32:  return "i32";
        case Ty::I64:  return "i64";
        case Ty::F32:  return "f32";
        case Ty::F64:  return "f64";
        case Ty::Ptr:  return "ptr";
    }
    return "?";
}

bool isFloat(Ty t) { return t == Ty::F32 || t == Ty::F64; }
bool isInt  (Ty t) { return t == Ty::I8  || t == Ty::I16 || t == Ty::I32 ||
                            t == Ty::I64 || t == Ty::Ptr; }

// ------------------------------------------------------------
// Backward dataflow liveness over SSA values.
// ------------------------------------------------------------
Liveness computeLiveness(Function& f) {
    Liveness lv;
    std::unordered_map<std::string, std::unordered_set<ValueId>> use, def;
    std::unordered_map<std::string, std::vector<std::string>>     succs;

    for (auto& b : f.blocks) {
        std::unordered_set<ValueId> u, d;
        for (auto& in : b.instrs) {
            auto touch = [&](const Operand& o) {
                if (o.isImm || o.val == NOVAL) return;
                if (!d.count(o.val)) u.insert(o.val);
            };
            if (in.op != Op::Phi) { touch(in.a); touch(in.b); }
            for (auto& pa : in.phiArgs)
                if (!d.count(pa.first)) u.insert(pa.first);
            if (in.dst != NOVAL) d.insert(in.dst);
        }
        use[b.name] = std::move(u);
        def[b.name] = std::move(d);

        if (!b.instrs.empty()) {
            auto& last = b.instrs.back();
            if      (last.op == Op::Jmp) succs[b.name].push_back(last.label);
            else if (last.op == Op::Jnz) {
                succs[b.name].push_back(last.label);
                succs[b.name].push_back(last.label2);
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = f.blocks.rbegin(); it != f.blocks.rend(); ++it) {
            auto& b = *it;
            std::unordered_set<ValueId> newOut;
            for (auto& s : succs[b.name]) {
                auto sit = lv.in.find(s);
                if (sit == lv.in.end()) continue;
                for (auto v : sit->second) newOut.insert(v);
            }
            std::unordered_set<ValueId> newIn = use[b.name];
            for (auto v : newOut)
                if (!def[b.name].count(v)) newIn.insert(v);

            if (newIn != lv.in[b.name] || newOut != lv.out[b.name]) {
                lv.in[b.name]  = std::move(newIn);
                lv.out[b.name] = std::move(newOut);
                changed = true;
            }
        }
    }
    return lv;
}

} // namespace vcb