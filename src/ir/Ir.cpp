#include "vcb/Ir.hpp"
#include <string>

namespace vcb {

    const char* typeName(Type t) {
        switch (t) {
        case Type::Void: return "void";
        case Type::I1:   return "i1";
        case Type::I8:   return "i8";
        case Type::I16:  return "i16";
        case Type::I32:  return "i32";
        case Type::I64:  return "i64";
        case Type::F32:  return "f32";
        case Type::F64:  return "f64";
        case Type::Ptr:  return "ptr";
        }
        return "?";
    }

    const char* opName(OpKind k) {
        switch (k) {
        case OpKind::ConstI:  return "const.i";
        case OpKind::ConstF:  return "const.f";
        case OpKind::Copy:    return "copy";
        case OpKind::Add:     return "add";
        case OpKind::Sub:     return "sub";
        case OpKind::Mul:     return "mul";
        case OpKind::Div:     return "div";
        case OpKind::Mod:     return "mod";
        case OpKind::Neg:     return "neg";
        case OpKind::Eq:      return "eq";
        case OpKind::Ne:      return "ne";
        case OpKind::Lt:      return "lt";
        case OpKind::Le:      return "le";
        case OpKind::Gt:      return "gt";
        case OpKind::Ge:      return "ge";
        case OpKind::And:     return "and";
        case OpKind::Or:      return "or";
        case OpKind::Xor:     return "xor";
        case OpKind::Shl:     return "shl";
        case OpKind::Shr:     return "shr";
        case OpKind::Call:    return "call";
        case OpKind::Ret:     return "ret";
        case OpKind::Jmp:     return "jmp";
        case OpKind::Br:      return "br";
        case OpKind::Phi:     return "phi";
        case OpKind::Alloca:  return "alloca";
        case OpKind::Load:    return "load";
        case OpKind::Store:   return "store";
        case OpKind::Bitcast: return "bitcast";
        case OpKind::Sitof:   return "sitof";
        case OpKind::Fptosi:  return "fptosi";
        }
        return "?";
    }

    std::string freshValue(const Function&, int& counter) {
        return "%t" + std::to_string(counter++);
    }

} // namespace vcb