// VCB — Vayu Compiler Backend
// Single umbrella header. No external deps.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <ostream>
#include <utility>

namespace vcb {

// ============================================================
// Types
// ============================================================
enum class Ty : uint8_t {
    Void,
    I8, I16, I32, I64,
    F32, F64,
    Ptr
};

size_t      typeSize(Ty t);
const char* typeName(Ty t);
bool        isFloat(Ty t);
bool        isInt(Ty t);

// ============================================================
// IR
// ============================================================
using ValueId = uint32_t;
constexpr ValueId NOVAL = 0xFFFFFFFFu;

struct Operand {
    ValueId val   = NOVAL;
    int64_t imm   = 0;
    bool    isImm = false;

    static Operand V(ValueId v) { Operand o; o.val = v; return o; }
    static Operand I(int64_t i) { Operand o; o.imm = i; o.isImm = true; return o; }
};

enum class Op : uint8_t {
    Nop, Copy,
    Add, Sub, Mul, Div, Rem,
    And, Or, Xor, Shl, Shr, Sar,
    Neg, Not,
    Ceq, Cne, Clt, Cle, Cgt, Cge,
    Load, Store, Alloc,
    Trunc, Zext, Sext, Sitofp, Fptosi,
    Jmp, Jnz, Ret, Call, Phi,
};

struct Instr {
    Op      op    = Op::Nop;
    Ty      ty    = Ty::Void;   // destination / result type
    Ty      srcTy = Ty::Void;   // source type (used by Trunc/Zext/Sext/Sitofp/Fptosi)
    ValueId dst   = NOVAL;
    Operand a;
    Operand b;
    std::string label;   // jump target / call symbol
    std::string label2;  // second branch target
    std::vector<std::pair<ValueId, std::string>> phiArgs;  // also used as call-arg list
};

struct Block {
    std::string          name;
    std::vector<Instr>   instrs;
};

struct Param { Ty ty; ValueId id; };

struct Function {
    std::string            name;
    Ty                     ret      = Ty::Void;
    std::vector<Param>     params;
    std::vector<Block>     blocks;
    uint32_t               nextValue = 1;
    bool                   exported  = false;

    Block* findBlock(const std::string& n) {
        for (auto& b : blocks) if (b.name == n) return &b;
        return nullptr;
    }
};

struct Module {
    std::vector<std::unique_ptr<Function>> funcs;

    Function* find(const std::string& n) {
        for (auto& f : funcs) if (f->name == n) return f.get();
        return nullptr;
    }
};

// ============================================================
// Register allocation
// ============================================================
struct Loc {
    enum Kind : uint8_t { Reg, Slot } kind = Slot;
    int  idx     = 0;      // pool index (Reg) OR spill index (Slot)
    bool isFloat = false;
};

struct RegAllocResult {
    std::unordered_map<ValueId, Loc> loc;
    std::vector<int> usedCalleeSaved;   // int-pool indices (2..6)
    int              spillCount = 0;
};

RegAllocResult regalloc(Function& f);

// ============================================================
// Analysis
// ============================================================
struct Liveness {
    std::unordered_map<std::string, std::unordered_set<ValueId>> in, out;
};
Liveness computeLiveness(Function& f);

// ============================================================
// Passes
// ============================================================
void optimize(Module& m);
void lower(Module& m);

// ============================================================
// Target
// ============================================================
struct TargetInfo {
    const char*  name;
    int          numArgRegs;
    const char*  argRegs[6];
    const char*  retReg;
    int          frameAlign;
};
extern const TargetInfo SysV_x86_64;

// ============================================================
// Codegen
// ============================================================
void codegen(Module& m, std::ostream& out);

// ============================================================
// Parser
// ============================================================
bool parseIR(const std::string& text, Module& m, std::string& err);

} // namespace vcb