#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    enum class Type {
        Void,
        I1, I8, I16, I32, I64,
        F32, F64,
        Ptr,
    };

    const char* typeName(Type t);

    enum class OpKind {
        ConstI,  ConstF,
        Copy,
        Add, Sub, Mul, Div, Mod, Neg,
        Eq, Ne, Lt, Le, Gt, Ge,
        And, Or, Xor, Shl, Shr,
        Call, Ret,
        Jmp, Br, Phi,
        Alloca, Load, Store,
        Bitcast, Sitof, Fptosi,
    };

    const char* opName(OpKind k);

    struct Op {
        OpKind                          kind = OpKind::Copy;
        Type                            type = Type::Void;
        std::string                     dst;
        std::vector<std::string>        args;
        int64_t                         immI = 0;
        double                          immF = 0.0;
        std::string                     callee;
        std::string                     targetTrue;
        std::string                     targetFalse;
        std::vector<std::pair<std::string, std::string>> phiPairs;
        int                             line = 0;
    };

    struct Block {
        std::string       name;
        std::vector<Op>   ops;
    };

    struct Param {
        std::string name;
        Type        type = Type::I64;
    };

    struct Function {
        std::string         name;
        std::vector<Param>  params;
        Type                returnType = Type::Void;
        std::vector<Block>  blocks;
    };

    struct Module {
        std::vector<Function> functions;
    };

    // Emit a fresh SSA name for the given function.  Used during lowering.
    std::string freshValue(const Function& fn, int& counter);

} // namespace vcb