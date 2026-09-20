// VCB — Vayu Compiler Backend
// regalloc.cpp — linear-scan register allocator
//
// Pools:
//   Int   : r10 r11 rbx r12 r13 r14 r15  (indices 0..6)
//           indices 0..1  = caller-saved
//           indices 2..6  = callee-saved
//   Float : xmm1..xmm7                    (indices 0..6, all caller-saved)
//
// Policy:
//   * Values spanning a call get callee-saved ints; floats spill.
//   * Values not spanning a call may use any pool register.
//   * Spills go to stack slots, offset assigned by codegen.

#include "vcb.hpp"
#include <algorithm>
#include <vector>

namespace vcb {

    namespace {

        constexpr int INT_POOL_SIZE = 7;
        constexpr int INT_CALLEE_START = 2;
        constexpr int FLOAT_POOL_SIZE = 7;

        struct Interval {
            ValueId v = NOVAL;
            int     start = 0;
            int     end = 0;
            bool    isFloat = false;
            bool    spansCall = false;
            int     reg = -1;   // pool index or -1
            int     slot = -1;   // spill index or -1
        };

    } // anon

    RegAllocResult regalloc(Function& f) {
        RegAllocResult res;

        // --------------------------------------------------------
        // 1) Linear positions per instruction.
        //    Params live from -1.
        // --------------------------------------------------------
        std::unordered_map<ValueId, int> defPos;
        std::unordered_map<ValueId, int> lastUse;
        std::unordered_map<ValueId, bool> isFlt;

        for (auto& p : f.params) {
            defPos[p.id] = -1;
            lastUse[p.id] = -1;
            isFlt[p.id] = isFloat(p.ty);
        }

        int pos = 0;
        std::vector<int> callPos;

        for (auto& b : f.blocks) {
            for (auto& in : b.instrs) {
                if (in.op == Op::Call) callPos.push_back(pos);

                if (in.op != Op::Phi) {
                    if (!in.a.isImm && in.a.val != NOVAL) lastUse[in.a.val] = pos;
                    if (!in.b.isImm && in.b.val != NOVAL) lastUse[in.b.val] = pos;
                }
                for (auto& pa : in.phiArgs) lastUse[pa.first] = pos;

                if (in.dst != NOVAL) {
                    if (!defPos.count(in.dst)) defPos[in.dst] = pos;
                    if (!lastUse.count(in.dst)) lastUse[in.dst] = pos;
                    isFlt[in.dst] = isFloat(in.ty);
                }
                ++pos;
            }
        }

        // --------------------------------------------------------
        // 2) Build intervals.
        // --------------------------------------------------------
        std::vector<Interval> ivs;
        ivs.reserve(defPos.size());

        for (auto& kv : defPos) {
            ValueId v = kv.first;
            int s = kv.second;
            int e = lastUse.count(v) ? lastUse[v] : s;
            if (e < s) e = s;

            Interval iv;
            iv.v = v;
            iv.start = s;
            iv.end = e;
            iv.isFloat = isFlt.count(v) ? isFlt[v] : false;

            // spansCall?
            for (int cp : callPos) {
                if (cp > s && cp <= e) { iv.spansCall = true; break; }
            }
            ivs.push_back(iv);
        }

        // --------------------------------------------------------
        // 3) Sort by start position.
        // --------------------------------------------------------
        std::sort(ivs.begin(), ivs.end(),
            [](const Interval& a, const Interval& b) { return a.start < b.start; });

        // --------------------------------------------------------
        // 4) Linear scan.
        // --------------------------------------------------------
        std::vector<Interval*> active;
        std::vector<bool> intUsed(INT_POOL_SIZE, false);
        std::vector<bool> fltUsed(FLOAT_POOL_SIZE, false);

        auto freeAll = [&](Interval* i) {
            if (i->reg < 0) return;
            if (i->isFloat) fltUsed[i->reg] = false;
            else            intUsed[i->reg] = false;
            };

        auto expireOld = [&](int point) {
            active.erase(std::remove_if(active.begin(), active.end(),
                [&](Interval* i) {
                    if (i->end < point) { freeAll(i); return true; }
                    return false;
                }), active.end());
            };

        auto pickInt = [&](bool calleeOnly) -> int {
            int lo = calleeOnly ? INT_CALLEE_START : 0;
            for (int i = lo; i < INT_POOL_SIZE; ++i)
                if (!intUsed[i]) return i;
            return -1;
            };
        auto pickFloat = [&]() -> int {
            for (int i = 0; i < FLOAT_POOL_SIZE; ++i)
                if (!fltUsed[i]) return i;
            return -1;
            };

        for (auto& iv : ivs) {
            expireOld(iv.start);

            int r = -1;
            if (iv.isFloat) {
                r = iv.spansCall ? -1 : pickFloat();
            }
            else {
                r = pickInt(iv.spansCall);
            }

            if (r < 0) {
                iv.reg = -1;
                iv.slot = res.spillCount++;
            }
            else {
                iv.reg = r;
                if (iv.isFloat) fltUsed[r] = true;
                else            intUsed[r] = true;
            }
            active.push_back(&iv);
        }

        // --------------------------------------------------------
        // 5) Which callee-saved ints were used?
        // --------------------------------------------------------
        for (int i = INT_CALLEE_START; i < INT_POOL_SIZE; ++i)
            if (intUsed[i]) res.usedCalleeSaved.push_back(i);

        // --------------------------------------------------------
        // 6) Materialize result.
        // --------------------------------------------------------
        for (auto& iv : ivs) {
            Loc l;
            l.isFloat = iv.isFloat;
            if (iv.reg >= 0) { l.kind = Loc::Reg;  l.idx = iv.reg; }
            else { l.kind = Loc::Slot; l.idx = iv.slot; }
            res.loc[iv.v] = l;
        }

        return res;
    }

} // namespace vcb