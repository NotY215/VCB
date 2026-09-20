// VCB — Vayu Compiler Backend
// codegen.cpp — x86-64 System V code generation
//
// Symbol policy:
//   Every function except `main` is emitted as `_vcb_<name>`, and every
//   block label is `<mangled_fn>_<block>`. This prevents GAS Intel-syntax
//   tokenizer from confusing labels like `add3:` with `add` mnemonic + 3.

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

        constexpr int SLOT_SIZE = 8;

        // ------------------------------------------------------------
        // Symbol mangling
        // ------------------------------------------------------------
        inline std::string symName(const std::string& user) {
            if (user == "main") return user;
            return "_vcb_" + user;
        }

        inline std::string mangledSymbol(const std::string& user) {
            return symName(user);
        }

        // ============================================================
        // Frame layout
        // ============================================================
        struct Frame {
            std::unordered_map<ValueId, int> slot;
            std::unordered_map<ValueId, int> allocOfs;
            int totalSize = 0;
        };

        // ============================================================
        // Register naming
        // ============================================================
        inline const char* iszOf(Ty t) {
            switch (t) {
            case Ty::I8:  return "byte";
            case Ty::I16: return "word";
            case Ty::I32: return "dword";
            case Ty::I64:
            case Ty::Ptr: return "qword";
            case Ty::F32: return "dword";
            case Ty::F64: return "qword";
            default:      return "qword";
            }
        }

        inline const char* rA(Ty t) {
            switch (t) {
            case Ty::I8:  return "al";
            case Ty::I16: return "ax";
            case Ty::I32: return "eax";
            default:      return "rax";
            }
        }

        inline const char* rB(Ty t) {
            switch (t) {
            case Ty::I8:  return "cl";
            case Ty::I16: return "cx";
            case Ty::I32: return "ecx";
            default:      return "rcx";
            }
        }

        inline const char* scratchReg(int i, Ty t) {
            static const char* full[] = { "rax","rcx","rdx","r10","r11" };
            static const char* w32[] = { "eax","ecx","edx","r10d","r11d" };
            static const char* w16[] = { "ax","cx","dx","r10w","r11w" };
            static const char* w8[] = { "al","cl","dl","r10b","r11b" };
            if (i < 0 || i > 4) return "rax";
            switch (t) {
            case Ty::I8:  return w8[i];
            case Ty::I16: return w16[i];
            case Ty::I32: return w32[i];
            default:      return full[i];
            }
        }

        inline const char* xmmReg(int i) {
            static const char* names[] = { "xmm0","xmm1","xmm2","xmm3" };
            if (i < 0 || i > 3) return "xmm0";
            return names[i];
        }

        inline const char* sseSuf(Ty t) {
            return (t == Ty::F32) ? "ss" : "sd";
        }

        // ============================================================
        // Slot helpers — int
        // ============================================================
        void emitLoadIntSlot(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, int scratch = 0)
        {
            auto it = fr.slot.find(v);
            if (it == fr.slot.end()) {
                o << "    ; [warn] missing int slot for %" << v << "\n";
                return;
            }
            o << "    mov " << scratchReg(scratch, t)
                << ", " << iszOf(t) << " ptr [rbp" << it->second << "]\n";
        }

        void emitStoreIntSlot(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, int scratch = 0)
        {
            auto it = fr.slot.find(v);
            if (it == fr.slot.end()) return;
            o << "    mov " << iszOf(t) << " ptr [rbp" << it->second << "], "
                << scratchReg(scratch, t) << "\n";
        }

        void emitLoadIntOperand(std::ostream& o, const Frame& fr,
            const Operand& op, Ty t, int scratch)
        {
            if (op.isImm) {
                o << "    mov " << scratchReg(scratch, t) << ", " << op.imm << "\n";
            }
            else {
                emitLoadIntSlot(o, fr, op.val, t, scratch);
            }
        }

        // ============================================================
        // Slot helpers — float
        // ============================================================
        void emitLoadFloatSlot(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, int scratch)
        {
            auto it = fr.slot.find(v);
            if (it == fr.slot.end()) {
                o << "    ; [warn] missing float slot for %" << v << "\n";
                return;
            }
            const char* suf = sseSuf(t);
            o << "    mov" << suf << " " << xmmReg(scratch)
                << ", " << iszOf(t) << " ptr [rbp" << it->second << "]\n";
        }

        void emitStoreFloatSlot(std::ostream& o, const Frame& fr, ValueId v,
            Ty t, int scratch)
        {
            auto it = fr.slot.find(v);
            if (it == fr.slot.end()) return;
            const char* suf = sseSuf(t);
            o << "    mov" << suf << " " << iszOf(t) << " ptr [rbp" << it->second
                << "], " << xmmReg(scratch) << "\n";
        }

        void emitLoadFloatOperand(std::ostream& o, const Frame& fr,
            const Operand& op, Ty t, int scratch)
        {
            if (op.isImm) {
                o << "    mov rax, " << op.imm << "\n";
                o << "    cvtsi2" << sseSuf(t) << " " << xmmReg(scratch) << ", rax\n";
            }
            else {
                emitLoadFloatSlot(o, fr, op.val, t, scratch);
            }
        }

        // ============================================================
        // Extend / truncate
        // ============================================================
        void emitExtend(std::ostream& o, Ty srcTy, Ty dstTy, bool sign) {
            auto bytes = [](Ty t) -> int {
                switch (t) {
                case Ty::I8:  return 1;
                case Ty::I16: return 2;
                case Ty::I32: return 4;
                case Ty::I64:
                case Ty::Ptr: return 8;
                default:      return 4;
                }
                };
            int s = bytes(srcTy);
            int d = bytes(dstTy);
            if (s >= d) return;

            if (sign) {
                if (s == 1) o << "    movsx eax, al\n";
                else if (s == 2) o << "    movsx eax, ax\n";
                else if (s == 4) o << "    movsxd rax, eax\n";
                return;
            }
            if (s == 1) o << "    movzx eax, al\n";
            else if (s == 2) o << "    movzx eax, ax\n";
            else if (s == 4) o << "    mov eax, eax\n";
        }

        // ============================================================
        // Single instruction
        // ============================================================
        void emitOneInstr(std::ostream& o, Instr& in, const Frame& fr,
            const std::string& epilogue, const std::string& fnMangled)
        {
            const bool fp = isFloat(in.ty);

            switch (in.op) {

            case Op::Nop:
                break;

            case Op::Copy: {
                if (fp) {
                    emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                }
                else {
                    emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                    emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                }
                break;
            }

            case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: {
                if (fp) {
                    const char* suf = sseSuf(in.ty);
                    emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    emitLoadFloatOperand(o, fr, in.b, in.ty, 1);
                    const char* mn =
                        in.op == Op::Add ? "add" :
                        in.op == Op::Sub ? "sub" :
                        in.op == Op::Mul ? "mul" : "div";
                    o << "    " << mn << suf << " "
                        << xmmReg(0) << ", " << xmmReg(1) << "\n";
                    emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                    break;
                }
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                const char* mn =
                    in.op == Op::Add ? "add" :
                    in.op == Op::Sub ? "sub" :
                    in.op == Op::Div ? "idiv" : "imul";

                if (in.op == Op::Mul) {
                    if (in.b.isImm) {
                        o << "    imul " << rA(in.ty) << ", " << rA(in.ty)
                            << ", " << in.b.imm << "\n";
                    }
                    else {
                        emitLoadIntSlot(o, fr, in.b.val, in.ty, 1);
                        o << "    imul " << rA(in.ty) << ", " << rB(in.ty) << "\n";
                    }
                    emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                    break;
                }
                if (in.op == Op::Div) {
                    if (in.b.isImm) {
                        o << "    mov " << rB(in.ty) << ", " << in.b.imm << "\n";
                    }
                    else {
                        emitLoadIntSlot(o, fr, in.b.val, in.ty, 1);
                    }
                    if (in.ty == Ty::I32 || in.ty == Ty::I16 || in.ty == Ty::I8) {
                        o << "    cdq\n    idiv ecx\n";
                    }
                    else {
                        o << "    cqo\n    idiv rcx\n";
                    }
                    emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                    break;
                }
                if (in.b.isImm) {
                    o << "    " << mn << " " << rA(in.ty)
                        << ", " << in.b.imm << "\n";
                }
                else {
                    emitLoadIntSlot(o, fr, in.b.val, in.ty, 1);
                    o << "    " << mn << " " << rA(in.ty)
                        << ", " << rB(in.ty) << "\n";
                }
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Rem: {
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                if (in.b.isImm) {
                    o << "    mov " << rB(in.ty) << ", " << in.b.imm << "\n";
                }
                else {
                    emitLoadIntSlot(o, fr, in.b.val, in.ty, 1);
                }
                if (in.ty == Ty::I32) {
                    o << "    cdq\n    idiv ecx\n    mov eax, edx\n";
                }
                else {
                    o << "    cqo\n    idiv rcx\n    mov rax, rdx\n";
                }
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::And: case Op::Or: case Op::Xor: {
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                const char* mn =
                    in.op == Op::And ? "and" :
                    in.op == Op::Or ? "or" : "xor";
                if (in.b.isImm) {
                    o << "    " << mn << " " << rA(in.ty)
                        << ", " << in.b.imm << "\n";
                }
                else {
                    emitLoadIntSlot(o, fr, in.b.val, in.ty, 1);
                    o << "    " << mn << " " << rA(in.ty)
                        << ", " << rB(in.ty) << "\n";
                }
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Shl: case Op::Shr: case Op::Sar: {
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                if (in.b.isImm) {
                    o << "    mov cl, " << (in.b.imm & 63) << "\n";
                }
                else {
                    emitLoadIntSlot(o, fr, in.b.val, Ty::I8, 1);
                }
                const char* mn =
                    in.op == Op::Shl ? "shl" :
                    in.op == Op::Shr ? "shr" : "sar";
                o << "    " << mn << " " << rA(in.ty) << ", cl\n";
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Neg: {
                if (fp) {
                    emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    if (in.ty == Ty::F32) {
                        o << "    mov eax, 0x80000000\n";
                        o << "    movd xmm1, eax\n";
                        o << "    xorps xmm0, xmm1\n";
                    }
                    else {
                        o << "    mov rax, 0x8000000000000000\n";
                        o << "    movq xmm1, rax\n";
                        o << "    xorpd xmm0, xmm1\n";
                    }
                    emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                    break;
                }
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                o << "    neg " << rA(in.ty) << "\n";
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Not: {
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                o << "    not " << rA(in.ty) << "\n";
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Ceq: case Op::Cne: case Op::Clt:
            case Op::Cle: case Op::Cgt: case Op::Cge: {
                if (fp) {
                    const char* suf = sseSuf(in.ty);
                    emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    emitLoadFloatOperand(o, fr, in.b, in.ty, 1);
                    o << "    ucomi" << suf << " " << xmmReg(0)
                        << ", " << xmmReg(1) << "\n";
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
                    emitStoreIntSlot(o, fr, in.dst, Ty::I32, 0);
                    break;
                }
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                emitLoadIntOperand(o, fr, in.b, in.ty, 1);
                o << "    cmp " << rA(in.ty) << ", " << rB(in.ty) << "\n";
                const char* setcc =
                    in.op == Op::Ceq ? "sete" :
                    in.op == Op::Cne ? "setne" :
                    in.op == Op::Clt ? "setl" :
                    in.op == Op::Cle ? "setle" :
                    in.op == Op::Cgt ? "setg" : "setge";
                o << "    " << setcc << " al\n";
                o << "    movzx eax, al\n";
                emitStoreIntSlot(o, fr, in.dst, Ty::I32, 0);
                break;
            }

            case Op::Load: {
                emitLoadIntOperand(o, fr, in.a, Ty::Ptr, 0);
                if (fp) {
                    const char* suf = sseSuf(in.ty);
                    o << "    mov" << suf << " " << xmmReg(0)
                        << ", " << iszOf(in.ty) << " ptr [rax]\n";
                    emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                }
                else {
                    o << "    mov " << rA(in.ty)
                        << ", " << iszOf(in.ty) << " ptr [rax]\n";
                    emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                }
                break;
            }
            case Op::Store: {
                if (fp) {
                    emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    emitLoadIntOperand(o, fr, in.b, Ty::Ptr, 1);
                    const char* suf = sseSuf(in.ty);
                    o << "    mov" << suf << " " << iszOf(in.ty)
                        << " ptr [rcx], " << xmmReg(0) << "\n";
                }
                else {
                    emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                    emitLoadIntOperand(o, fr, in.b, Ty::Ptr, 1);
                    o << "    mov " << iszOf(in.ty)
                        << " ptr [rcx], " << rA(in.ty) << "\n";
                }
                break;
            }

            case Op::Alloc: {
                auto it = fr.allocOfs.find(in.dst);
                if (it == fr.allocOfs.end()) {
                    o << "    ; [warn] alloca slot missing\n";
                    break;
                }
                o << "    lea rax, [rbp" << it->second << "]\n";
                emitStoreIntSlot(o, fr, in.dst, Ty::Ptr, 0);
                break;
            }

            case Op::Trunc: case Op::Zext: case Op::Sext: {
                emitLoadIntOperand(o, fr, in.a, in.srcTy, 0);
                if (in.op != Op::Trunc)
                    emitExtend(o, in.srcTy, in.ty, in.op == Op::Sext);
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Sitofp: {
                emitLoadIntOperand(o, fr, in.a, in.srcTy, 0);
                const char* suf = sseSuf(in.ty);
                bool srcIs64 = (in.srcTy == Ty::I64 || in.srcTy == Ty::Ptr);
                if (srcIs64)
                    o << "    cvtsi2" << suf << " " << xmmReg(0) << ", rax\n";
                else
                    o << "    cvtsi2" << suf << " " << xmmReg(0) << ", eax\n";
                emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Fptosi: {
                emitLoadFloatOperand(o, fr, in.a, in.srcTy, 0);
                const char* suf = sseSuf(in.srcTy);
                bool dstIs64 = (in.ty == Ty::I64 || in.ty == Ty::Ptr);
                if (dstIs64)
                    o << "    cvtt" << suf << "2si rax, " << xmmReg(0) << "\n";
                else
                    o << "    cvtt" << suf << "2si eax, " << xmmReg(0) << "\n";
                emitStoreIntSlot(o, fr, in.dst, in.ty, 0);
                break;
            }

            case Op::Jmp: {
                o << "    jmp " << fnMangled << "_" << in.label << "\n";
                break;
            }
            case Op::Jnz: {
                emitLoadIntOperand(o, fr, in.a, in.ty, 0);
                o << "    test " << rA(in.ty) << ", " << rA(in.ty) << "\n";
                o << "    jne " << fnMangled << "_" << in.label << "\n";
                o << "    jmp " << fnMangled << "_" << in.label2 << "\n";
                break;
            }

            case Op::Ret: {
                if (in.a.val != NOVAL || in.a.isImm) {
                    if (fp) {
                        emitLoadFloatOperand(o, fr, in.a, in.ty, 0);
                    }
                    else if (in.a.isImm) {
                        o << "    mov eax, " << in.a.imm << "\n";
                    }
                    else {
                        Ty rty = (in.ty == Ty::Void) ? Ty::I32 : in.ty;
                        emitLoadIntSlot(o, fr, in.a.val, rty, 0);
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
                    auto it = fr.slot.find(v);
                    if (it == fr.slot.end()) continue;
                    o << "    mov " << argReg64[i]
                        << ", qword ptr [rbp" << it->second << "]\n";
                }
                // Mangle callee symbol.
                o << "    call " << mangledSymbol(in.label) << "\n";
                if (in.dst != NOVAL) {
                    if (fp) {
                        emitStoreFloatSlot(o, fr, in.dst, in.ty, 0);
                    }
                    else {
                        Ty rty = (in.ty == Ty::Void) ? Ty::I32 : in.ty;
                        emitStoreIntSlot(o, fr, in.dst, rty, 0);
                    }
                }
                break;
            }

            case Op::Phi:
                break;
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

                std::vector<std::pair<ValueId, ValueId>> moves;
                for (auto& pin : tb->instrs) {
                    if (pin.op != Op::Phi) break;
                    for (auto& pa : pin.phiArgs) {
                        if (pa.second == b.name) {
                            moves.push_back({ pa.first, pin.dst });
                            break;
                        }
                    }
                }
                if (moves.empty()) continue;

                const size_t maxScratch = 5;
                size_t n = moves.size();
                if (n > maxScratch) n = maxScratch;

                for (size_t i = 0; i < n; ++i)
                    emitLoadIntSlot(o, fr, moves[i].first, Ty::I64, (int)i);
                for (size_t i = 0; i < n; ++i)
                    emitStoreIntSlot(o, fr, moves[i].second, Ty::I64, (int)i);
            }
        }

    } // anonymous namespace

    // ============================================================
    // Entry
    // ============================================================
    void codegen(Module& m, std::ostream& out) {
        out << ".intel_syntax noprefix\n";
        out << ".text\n\n";

        for (auto& fp : m.funcs) {
            Function& f = *fp;
            const std::string fnSym = symName(f.name);

            // ---- Frame layout ----
            Frame fr;
            int cursor = 0;

            for (auto& b : f.blocks) {
                for (auto& in : b.instrs) {
                    if (in.op == Op::Alloc) {
                        int64_t bytes = (in.b.isImm && in.b.imm > 0) ? in.b.imm : 8;
                        cursor -= (int)bytes;
                        cursor &= ~7;
                        fr.allocOfs[in.dst] = cursor;
                    }
                }
            }
            for (auto& p : f.params) {
                if (!fr.slot.count(p.id)) {
                    cursor -= SLOT_SIZE;
                    fr.slot[p.id] = cursor;
                }
            }
            for (auto& b : f.blocks) {
                for (auto& in : b.instrs) {
                    if (in.dst != NOVAL && !fr.slot.count(in.dst)) {
                        cursor -= SLOT_SIZE;
                        fr.slot[in.dst] = cursor;
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

            static const char* argReg64[6] = { "rdi","rsi","rdx","rcx","r8","r9" };
            static const char* argReg32[6] = { "edi","esi","edx","ecx","r8d","r9d" };
            static const char* argReg16[6] = { "di","si","dx","cx","r8w","r9w" };
            static const char* argReg8[6] = { "dil","sil","dl","cl","r8b","r9b" };

            for (size_t i = 0; i < f.params.size() && i < 6; ++i) {
                auto& p = f.params[i];
                auto it = fr.slot.find(p.id);
                if (it == fr.slot.end()) continue;
                if (isFloat(p.ty)) {
                    const char* suf = (p.ty == Ty::F32) ? "ss" : "sd";
                    out << "    mov" << suf << " " << iszOf(p.ty)
                        << " ptr [rbp" << it->second << "], xmm" << i << "\n";
                    continue;
                }
                const char* src = argReg64[i];
                if (p.ty == Ty::I32) src = argReg32[i];
                else if (p.ty == Ty::I16) src = argReg16[i];
                else if (p.ty == Ty::I8)  src = argReg8[i];
                out << "    mov " << iszOf(p.ty)
                    << " ptr [rbp" << it->second << "], " << src << "\n";
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
            out << "    leave\n";
            out << "    ret\n\n";
        }
    }

} // namespace vcb