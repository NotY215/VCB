// VCB — Vayu Compiler Backend
// codegen.cpp — x86-64 SysV code generation, register-allocated

#include "vcb.hpp"

#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <cstdint>

namespace vcb {

    namespace {

        // ============================================================
        // Register pool naming
        // ============================================================
        struct IntNames { const char* r64; const char* r32; const char* r16; const char* r8; };
        static const IntNames INT_POOL[] = {
            { "r10","r10d","r10w","r10b" },
            { "r11","r11d","r11w","r11b" },
            { "rbx","ebx", "bx",  "bl"   },
            { "r12","r12d","r12w","r12b" },
            { "r13","r13d","r13w","r13b" },
            { "r14","r14d","r14w","r14b" },
            { "r15","r15d","r15w","r15b" },
        };
        static constexpr int INT_POOL_SIZE = 7;
        static constexpr int INT_CALLEE_START = 2;

        static const char* FLOAT_POOL[] = {
            "xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"
        };
        static constexpr int FLOAT_POOL_SIZE = 7;

        inline const char* poolIntName(int i, Ty t) {
            switch (t) {
            case Ty::I8:  return INT_POOL[i].r8;
            case Ty::I16: return INT_POOL[i].r16;
            case Ty::I32: return INT_POOL[i].r32;
            default:      return INT_POOL[i].r64;
            }
        }
        inline const char* poolFloatName(int i) { return FLOAT_POOL[i]; }

        inline const char* iszOf(Ty t) {
            switch (t) {
            case Ty::I8:  return "byte";
            case Ty::I16: return "word";
            case Ty::I32: return "dword";
            case Ty::F32: return "dword";
            case Ty::I64:
            case Ty::Ptr:
            case Ty::F64: return "qword";
            default:      return "qword";
            }
        }

        inline const char* sseSuf(Ty t) { return t == Ty::F32 ? "ss" : "sd"; }

        // Scratch (never in pool)
        inline const char* rAx(Ty t) {
            switch (t) {
            case Ty::I8:  return "al";
            case Ty::I16: return "ax";
            case Ty::I32: return "eax";
            default:      return "rax";
            }
        }
        inline const char* rCx(Ty t) {
            switch (t) {
            case Ty::I8:  return "cl";
            case Ty::I16: return "cx";
            case Ty::I32: return "ecx";
            default:      return "rcx";
            }
        }

        inline std::string symName(const std::string& user) {
            return user == "main" ? user : ("_vcb_" + user);
        }

        // ============================================================
        // Frame / layout
        // ============================================================
        struct Frame {
            RegAllocResult ra;
            std::unordered_map<ValueId, int> allocaOfs;    // alloca dst -> rbp offset
            std::unordered_map<int, int>     csOfs;        // pool idx -> rbp offset
            int totalSize = 0;
        };

        int slotOffset(const Frame& fr, int spillIdx) {
            int C = (int)fr.ra.usedCalleeSaved.size();
            return -(8 * C) - 8 * (spillIdx + 1);
        }

        // ============================================================
        // Loading / storing values according to location
        // ============================================================
        void loadInt(std::ostream& o, const Frame& fr, const Operand& op,
            Ty t, const char* dst)
        {
            if (op.isImm) {
                o << "    mov " << dst << ", " << op.imm << "\n";
                return;
            }
            auto it = fr.ra.loc.find(op.val);
            if (it == fr.ra.loc.end()) {
                o << "    ; [warn] no loc for %" << op.val << "\n";
                return;
            }
            const Loc& l = it->second;
            if (l.kind == Loc::Reg) {
                const char* src = poolIntName(l.idx, t);
                if (std::string(src) != dst)
                    o << "    mov " << dst << ", " << src << "\n";
            }
            else {
                int off = slotOffset(fr, l.idx);
                o << "    mov " << dst << ", " << iszOf(t)
                    << " ptr [rbp" << off << "]\n";
            }
        }

        void storeInt(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, const char* src)
        {
            auto it = fr.ra.loc.find(v);
            if (it == fr.ra.loc.end()) return;
            const Loc& l = it->second;
            if (l.kind == Loc::Reg) {
                const char* dst = poolIntName(l.idx, t);
                if (std::string(dst) != src)
                    o << "    mov " << dst << ", " << src << "\n";
            }
            else {
                int off = slotOffset(fr, l.idx);
                o << "    mov " << iszOf(t) << " ptr [rbp" << off
                    << "], " << src << "\n";
            }
        }

        // movaps / movapd are the correct wide moves for xmm-to-xmm.
        inline const char* movapMnem(Ty t) {
            return t == Ty::F32 ? "movaps" : "movapd";
        }

        void loadFloat(std::ostream& o, const Frame& fr, const Operand& op,
            Ty t, const char* dst)
        {
            const char* suf = sseSuf(t);
            if (op.isImm) {
                o << "    mov rax, " << op.imm << "\n";
                o << "    cvtsi2" << suf << " " << dst << ", rax\n";
                return;
            }
            auto it = fr.ra.loc.find(op.val);
            if (it == fr.ra.loc.end()) return;
            const Loc& l = it->second;
            if (l.kind == Loc::Reg) {
                const char* src = poolFloatName(l.idx);
                if (std::string(src) != dst)
                    o << "    " << movapMnem(t) << " " << dst << ", " << src << "\n";
            }
            else {
                int off = slotOffset(fr, l.idx);
                o << "    mov" << suf << " " << dst
                    << ", " << iszOf(t) << " ptr [rbp" << off << "]\n";
            }
        }

        void storeFloat(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, const char* src)
        {
            const char* suf = sseSuf(t);
            auto it = fr.ra.loc.find(v);
            if (it == fr.ra.loc.end()) return;
            const Loc& l = it->second;
            if (l.kind == Loc::Reg) {
                const char* dst = poolFloatName(l.idx);
                if (std::string(dst) != src)
                    o << "    " << movapMnem(t) << " " << dst << ", " << src << "\n";
            }
            else {
                int off = slotOffset(fr, l.idx);
                o << "    mov" << suf << " " << iszOf(t) << " ptr [rbp" << off
                    << "], " << src << "\n";
            }
        }

        // ============================================================
        // Extend / truncate
        // ============================================================
        void emitExtend(std::ostream& o, Ty srcTy, Ty dstTy, bool sign) {
            auto bytes = [](Ty t) {
                switch (t) {
                case Ty::I8:  return 1;
                case Ty::I16: return 2;
                case Ty::I32: return 4;
                case Ty::I64:
                case Ty::Ptr: return 8;
                default:      return 4;
                }
                };
            int s = bytes(srcTy), d = bytes(dstTy);
            if (s >= d) return;
            if (sign) {
                if (s == 1) o << "    movsx eax, al\n";
                else if (s == 2) o << "    movsx eax, ax\n";
                else if (s == 4) o << "    movsxd rax, eax\n";
            }
            else {
                if (s == 1) o << "    movzx eax, al\n";
                else if (s == 2) o << "    movzx eax, ax\n";
                else if (s == 4) o << "    mov eax, eax\n";
            }
        }

        // ============================================================
        // Single instruction
        // ============================================================
        void emitOneInstr(std::ostream& o, Instr& in, const Frame& fr,
            const std::string& epilogue, const std::string& fnSym)
        {
            const bool fp = isFloat(in.ty);

            switch (in.op) {

            case Op::Nop: break;

            case Op::Copy: {
                if (fp) {
                    loadFloat(o, fr, in.a, in.ty, "xmm0");
                    storeFloat(o, fr, in.dst, in.ty, "xmm0");
                }
                else {
                    loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                    storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                }
                break;
            }

            case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: {
                if (fp) {
                    loadFloat(o, fr, in.a, in.ty, "xmm0");
                    loadFloat(o, fr, in.b, in.ty, "xmm2");
                    const char* mn =
                        in.op == Op::Add ? "add" :
                        in.op == Op::Sub ? "sub" :
                        in.op == Op::Mul ? "mul" : "div";
                    o << "    " << mn << sseSuf(in.ty)
                        << " xmm0, xmm2\n";
                    storeFloat(o, fr, in.dst, in.ty, "xmm0");
                    break;
                }
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                if (in.op == Op::Mul) {
                    if (in.b.isImm) {
                        o << "    imul " << rAx(in.ty) << ", " << rAx(in.ty)
                            << ", " << in.b.imm << "\n";
                    }
                    else {
                        loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                        o << "    imul " << rAx(in.ty) << ", " << rCx(in.ty) << "\n";
                    }
                    storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                    break;
                }
                if (in.op == Op::Div) {
                    if (in.b.isImm) o << "    mov " << rCx(in.ty) << ", " << in.b.imm << "\n";
                    else            loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                    if (in.ty == Ty::I32) { o << "    cdq\n    idiv ecx\n"; }
                    else { o << "    cqo\n    idiv rcx\n"; }
                    storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                    break;
                }
                const char* mn =
                    in.op == Op::Add ? "add" : "sub";
                if (in.b.isImm) {
                    o << "    " << mn << " " << rAx(in.ty)
                        << ", " << in.b.imm << "\n";
                }
                else {
                    loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                    o << "    " << mn << " " << rAx(in.ty)
                        << ", " << rCx(in.ty) << "\n";
                }
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Rem: {
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                if (in.b.isImm) o << "    mov " << rCx(in.ty) << ", " << in.b.imm << "\n";
                else            loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                if (in.ty == Ty::I32) { o << "    cdq\n    idiv ecx\n    mov eax, edx\n"; }
                else { o << "    cqo\n    idiv rcx\n    mov rax, rdx\n"; }
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::And: case Op::Or: case Op::Xor: {
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                const char* mn =
                    in.op == Op::And ? "and" :
                    in.op == Op::Or ? "or" : "xor";
                if (in.b.isImm) {
                    o << "    " << mn << " " << rAx(in.ty)
                        << ", " << in.b.imm << "\n";
                }
                else {
                    loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                    o << "    " << mn << " " << rAx(in.ty)
                        << ", " << rCx(in.ty) << "\n";
                }
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Shl: case Op::Shr: case Op::Sar: {
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                if (in.b.isImm) o << "    mov cl, " << (in.b.imm & 63) << "\n";
                else            loadInt(o, fr, in.b, Ty::I8, "cl");
                const char* mn =
                    in.op == Op::Shl ? "shl" :
                    in.op == Op::Shr ? "shr" : "sar";
                o << "    " << mn << " " << rAx(in.ty) << ", cl\n";
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Neg: {
                if (fp) {
                    loadFloat(o, fr, in.a, in.ty, "xmm0");
                    if (in.ty == Ty::F32) {
                        o << "    mov eax, 0x80000000\n    movd xmm2, eax\n    xorps xmm0, xmm2\n";
                    }
                    else {
                        o << "    mov rax, 0x8000000000000000\n    movq xmm2, rax\n    xorpd xmm0, xmm2\n";
                    }
                    storeFloat(o, fr, in.dst, in.ty, "xmm0");
                    break;
                }
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                o << "    neg " << rAx(in.ty) << "\n";
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }
            case Op::Not: {
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                o << "    not " << rAx(in.ty) << "\n";
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Ceq: case Op::Cne: case Op::Clt:
            case Op::Cle: case Op::Cgt: case Op::Cge: {
                if (fp) {
                    loadFloat(o, fr, in.a, in.ty, "xmm0");
                    loadFloat(o, fr, in.b, in.ty, "xmm2");
                    o << "    ucomi" << sseSuf(in.ty) << " xmm0, xmm2\n";
                    const char* setcc = nullptr;
                    bool needNp = false;
                    switch (in.op) {
                    case Op::Ceq: setcc = "sete";  needNp = true;  break;
                    case Op::Cne: setcc = "setne"; needNp = true;  break;
                    case Op::Clt: setcc = "setb";  break;
                    case Op::Cle: setcc = "setbe"; break;
                    case Op::Cgt: setcc = "seta";  break;
                    case Op::Cge: setcc = "setae"; break;
                    default:      setcc = "sete";  break;
                    }
                    o << "    " << setcc << " al\n";
                    if (needNp) o << "    setnp cl\n    and al, cl\n";
                    o << "    movzx eax, al\n";
                    storeInt(o, fr, in.dst, Ty::I32, "eax");
                    break;
                }
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                loadInt(o, fr, in.b, in.ty, rCx(in.ty));
                o << "    cmp " << rAx(in.ty) << ", " << rCx(in.ty) << "\n";
                const char* setcc =
                    in.op == Op::Ceq ? "sete" :
                    in.op == Op::Cne ? "setne" :
                    in.op == Op::Clt ? "setl" :
                    in.op == Op::Cle ? "setle" :
                    in.op == Op::Cgt ? "setg" : "setge";
                o << "    " << setcc << " al\n    movzx eax, al\n";
                storeInt(o, fr, in.dst, Ty::I32, "eax");
                break;
            }

            case Op::Load: {
                loadInt(o, fr, in.a, Ty::Ptr, "rax");
                if (fp) {
                    o << "    mov" << sseSuf(in.ty) << " xmm0, "
                        << iszOf(in.ty) << " ptr [rax]\n";
                    storeFloat(o, fr, in.dst, in.ty, "xmm0");
                }
                else {
                    o << "    mov " << rAx(in.ty) << ", "
                        << iszOf(in.ty) << " ptr [rax]\n";
                    storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                }
                break;
            }
            case Op::Store: {
                if (fp) {
                    loadFloat(o, fr, in.a, in.ty, "xmm0");
                    loadInt(o, fr, in.b, Ty::Ptr, "rcx");
                    o << "    mov" << sseSuf(in.ty) << " "
                        << iszOf(in.ty) << " ptr [rcx], xmm0\n";
                }
                else {
                    loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                    loadInt(o, fr, in.b, Ty::Ptr, "rcx");
                    o << "    mov " << iszOf(in.ty)
                        << " ptr [rcx], " << rAx(in.ty) << "\n";
                }
                break;
            }

            case Op::Alloc: {
                auto it = fr.allocaOfs.find(in.dst);
                if (it == fr.allocaOfs.end()) {
                    o << "    ; [warn] missing alloca slot\n";
                    break;
                }
                o << "    lea rax, [rbp" << it->second << "]\n";
                storeInt(o, fr, in.dst, Ty::Ptr, "rax");
                break;
            }

            case Op::Trunc: case Op::Zext: case Op::Sext: {
                loadInt(o, fr, in.a, in.srcTy, rAx(in.srcTy));
                if (in.op != Op::Trunc)
                    emitExtend(o, in.srcTy, in.ty, in.op == Op::Sext);
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Sitofp: {
                loadInt(o, fr, in.a, in.srcTy, rAx(in.srcTy));
                bool srcIs64 = (in.srcTy == Ty::I64 || in.srcTy == Ty::Ptr);
                if (srcIs64)
                    o << "    cvtsi2" << sseSuf(in.ty) << " xmm0, rax\n";
                else
                    o << "    cvtsi2" << sseSuf(in.ty) << " xmm0, eax\n";
                storeFloat(o, fr, in.dst, in.ty, "xmm0");
                break;
            }
            case Op::Fptosi: {
                loadFloat(o, fr, in.a, in.srcTy, "xmm0");
                bool dstIs64 = (in.ty == Ty::I64 || in.ty == Ty::Ptr);
                if (dstIs64)
                    o << "    cvtt" << sseSuf(in.srcTy) << "2si rax, xmm0\n";
                else
                    o << "    cvtt" << sseSuf(in.srcTy) << "2si eax, xmm0\n";
                storeInt(o, fr, in.dst, in.ty, rAx(in.ty));
                break;
            }

            case Op::Jmp: {
                o << "    jmp " << fnSym << "_" << in.label << "\n";
                break;
            }
            case Op::Jnz: {
                loadInt(o, fr, in.a, in.ty, rAx(in.ty));
                o << "    test " << rAx(in.ty) << ", " << rAx(in.ty) << "\n";
                o << "    jne " << fnSym << "_" << in.label << "\n";
                o << "    jmp " << fnSym << "_" << in.label2 << "\n";
                break;
            }

            case Op::Ret: {
                if (in.a.val != NOVAL || in.a.isImm) {
                    if (fp) {
                        loadFloat(o, fr, in.a, in.ty, "xmm0");
                    }
                    else if (in.a.isImm) {
                        o << "    mov eax, " << in.a.imm << "\n";
                    }
                    else {
                        Ty rty = (in.ty == Ty::Void) ? Ty::I32 : in.ty;
                        loadInt(o, fr, in.a, rty, rAx(rty));
                    }
                }
                o << "    jmp " << epilogue << "\n";
                break;
            }

            case Op::Call: {
                static const char* argReg64[6] = { "rdi","rsi","rdx","rcx","r8","r9" };
                size_t n = in.phiArgs.size();
                if (n > 6) n = 6;
                for (size_t i = 0; i < n; ++i) {
                    ValueId v = in.phiArgs[i].first;
                    auto it = fr.ra.loc.find(v);
                    if (it == fr.ra.loc.end()) continue;
                    const Loc& l = it->second;
                    if (l.kind == Loc::Reg) {
                        const char* src = poolIntName(l.idx, Ty::I64);
                        o << "    mov " << argReg64[i] << ", " << src << "\n";
                    }
                    else {
                        int off = slotOffset(fr, l.idx);
                        o << "    mov " << argReg64[i]
                            << ", qword ptr [rbp" << off << "]\n";
                    }
                }
                o << "    call " << symName(in.label) << "\n";
                if (in.dst != NOVAL) {
                    if (fp) {
                        storeFloat(o, fr, in.dst, in.ty, "xmm0");
                    }
                    else {
                        Ty rty = (in.ty == Ty::Void) ? Ty::I32 : in.ty;
                        storeInt(o, fr, in.dst, rty, rAx(rty));
                    }
                }
                break;
            }

            case Op::Phi: break;
            }
        }

        // ============================================================
        // Phi copies on predecessor edges
        // ============================================================
        void emitPhiCopies(std::ostream& o, Function& f, Block& b, const Frame& fr) {
            if (b.instrs.empty()) return;
            Instr& last = b.instrs.back();
            std::vector<const std::string*> succs;
            if (last.op == Op::Jmp) succs.push_back(&last.label);
            else if (last.op == Op::Jnz) {
                succs.push_back(&last.label);
                succs.push_back(&last.label2);
            }
            else return;

            for (const std::string* sp : succs) {
                Block* tb = f.findBlock(*sp);
                if (!tb) continue;
                for (auto& pin : tb->instrs) {
                    if (pin.op != Op::Phi) break;
                    for (auto& pa : pin.phiArgs) {
                        if (pa.second != b.name) continue;
                        // Move source -> dst via rax/xmm0.
                        if (pin.ty == Ty::F32 || pin.ty == Ty::F64) {
                            loadFloat(o, fr, Operand::V(pa.first), pin.ty, "xmm0");
                            storeFloat(o, fr, pin.dst, pin.ty, "xmm0");
                        }
                        else {
                            loadInt(o, fr, Operand::V(pa.first), Ty::I64, "rax");
                            storeInt(o, fr, pin.dst, Ty::I64, "rax");
                        }
                        break;
                    }
                }
            }
        }

    } // anon

// ============================================================
// Entry
// ============================================================
    void codegen(Module& m, std::ostream& finalOut) {
        finalOut << ".intel_syntax noprefix\n";
        finalOut << ".text\n\n";

        for (auto& fp : m.funcs) {
            Function& f = *fp;
            const std::string fnSym = symName(f.name);

            std::ostringstream fnBuf;
            std::ostream& out = fnBuf;

            Frame fr;
            fr.ra = regalloc(f);

            for (size_t i = 0; i < fr.ra.usedCalleeSaved.size(); ++i)
                fr.csOfs[fr.ra.usedCalleeSaved[i]] = -(int)(8 * (i + 1));

            int csBytes = 8 * (int)fr.ra.usedCalleeSaved.size();
            int spillBytes = 8 * fr.ra.spillCount;
            int cursor = -(csBytes + spillBytes);

            for (auto& b : f.blocks) {
                for (auto& in : b.instrs) {
                    if (in.op == Op::Alloc) {
                        int64_t bytes = (in.b.isImm && in.b.imm > 0) ? in.b.imm : 8;
                        cursor -= (int)bytes;
                        cursor &= ~7;
                        fr.allocaOfs[in.dst] = cursor;
                    }
                }
            }

            int total = -cursor;
            total = (total + 15) & ~15;
            fr.totalSize = total;

            // ---- Prologue ----
            if (f.exported) out << ".globl " << fnSym << "\n";
            out << fnSym << ":\n";
            out << "    push rbp\n";
            out << "    mov rbp, rsp\n";
            if (fr.totalSize)
                out << "    sub rsp, " << fr.totalSize << "\n";

            for (auto& kv : fr.csOfs) {
                const char* r = INT_POOL[kv.first].r64;
                out << "    mov qword ptr [rbp" << kv.second << "], " << r << "\n";
            }

            static const char* argReg64[6] = { "rdi","rsi","rdx","rcx","r8","r9" };
            static const char* argReg32[6] = { "edi","esi","edx","ecx","r8d","r9d" };
            static const char* argReg16[6] = { "di","si","dx","cx","r8w","r9w" };
            static const char* argReg8[6] = { "dil","sil","dl","cl","r8b","r9b" };

            for (size_t i = 0; i < f.params.size() && i < 6; ++i) {
                auto& p = f.params[i];
                auto it = fr.ra.loc.find(p.id);
                if (it == fr.ra.loc.end()) continue;
                const Loc& l = it->second;
                const char* src = argReg64[i];
                if (p.ty == Ty::I32) src = argReg32[i];
                else if (p.ty == Ty::I16) src = argReg16[i];
                else if (p.ty == Ty::I8)  src = argReg8[i];

                if (l.kind == Loc::Reg) {
                    const char* dst = poolIntName(l.idx, p.ty);
                    if (std::string(src) != dst)
                        out << "    mov " << dst << ", " << src << "\n";
                }
                else {
                    int off = slotOffset(fr, l.idx);
                    out << "    mov " << iszOf(p.ty)
                        << " ptr [rbp" << off << "], " << src << "\n";
                }
            }

            // ---- Body ----
            std::string epilogue = ".L_epilogue_" + fnSym;

            for (auto& b : f.blocks) {
                out << fnSym << "_" << b.name << ":\n";
                for (size_t i = 0; i < b.instrs.size(); ++i) {
                    Instr& in = b.instrs[i];
                    if (in.op == Op::Jmp || in.op == Op::Jnz)
                        emitPhiCopies(out, f, b, fr);
                    if (in.op == Op::Phi) continue;
                    emitOneInstr(out, in, fr, epilogue, fnSym);
                }
            }

            // ---- Epilogue ----
            out << epilogue << ":\n";
            for (auto& kv : fr.csOfs) {
                const char* r = INT_POOL[kv.first].r64;
                out << "    mov " << r << ", qword ptr [rbp" << kv.second << "]\n";
            }
            out << "    leave\n";
            out << "    ret\n";

            // ---- Peephole + flush ----
            std::string cleaned = peepholeText(fnBuf.str());
            finalOut << cleaned << "\n";
        }
    }

} // namespace vcb