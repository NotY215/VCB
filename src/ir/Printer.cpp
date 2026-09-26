#include "vcb/Printer.hpp"
#include <cstdio>
#include <sstream>

namespace vcb {

    namespace {

        std::string formatConstI(Type t, int64_t v) {
            std::string suffix = typeName(t);
            return "const." + suffix + " " + std::to_string(v);
        }

        std::string formatConstF(Type t, double v) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v);
            std::string suffix = typeName(t);
            return "const." + suffix + " " + buf;
        }

        void printOp(std::ostringstream& o, const Op& op) {
            if (!op.dst.empty()) {
                o << "  " << typeName(op.type) << " " << op.dst << " = ";
            }
            else {
                o << "  ";
            }

            switch (op.kind) {
            case OpKind::ConstI:
                o << formatConstI(op.type, op.immI);
                break;
            case OpKind::ConstF:
                o << formatConstF(op.type, op.immF);
                break;
            case OpKind::Copy:
                o << "copy " << op.args.at(0);
                break;
            case OpKind::Neg:
                o << "neg " << op.args.at(0);
                break;
            case OpKind::Bitcast:
                o << "bitcast " << op.args.at(0);
                break;
            case OpKind::Sitof:
                o << "sitof " << op.args.at(0);
                break;
            case OpKind::Fptosi:
                o << "fptosi " << op.args.at(0);
                break;
            case OpKind::Add: case OpKind::Sub: case OpKind::Mul:
            case OpKind::Div: case OpKind::Mod:
            case OpKind::Eq:  case OpKind::Ne:  case OpKind::Lt:
            case OpKind::Le:  case OpKind::Gt:  case OpKind::Ge:
            case OpKind::And: case OpKind::Or:  case OpKind::Xor:
            case OpKind::Shl: case OpKind::Shr:
                o << opName(op.kind) << " " << op.args.at(0)
                    << ", " << op.args.at(1);
                break;
            case OpKind::Call:
                o << "call " << op.callee << "(";
                for (size_t i = 0; i < op.args.size(); ++i) {
                    if (i) o << ", ";
                    o << op.args[i];
                }
                o << ")";
                break;
            case OpKind::Ret:
                o << "ret";
                for (auto& a : op.args) o << " " << a;
                break;
            case OpKind::Jmp:
                o << "jmp " << op.targetTrue;
                break;
            case OpKind::Br:
                // br <cond>, <true>, <false>
                o << "br ";
                if (!op.args.empty()) o << op.args.at(0) << ", ";
                o << op.targetTrue << ", " << op.targetFalse;
                break;
            case OpKind::Phi: {
                o << "phi ";
                for (size_t i = 0; i < op.phiPairs.size(); ++i) {
                    if (i) o << ", ";
                    o << op.phiPairs[i].first << " [" << op.phiPairs[i].second << "]";
                }
                break;
            }
            case OpKind::Alloca:
                o << "alloca " << typeName(op.type);
                break;
            case OpKind::Load:
                o << "load " << op.args.at(0);
                break;
            case OpKind::Store:
                o << "store " << op.args.at(0) << ", " << op.args.at(1);
                break;
            }
            o << "\n";
        }

    } // namespace

    std::string printModule(const Module& m) {
        std::ostringstream o;
        o << "; vcb-ir v1\n\n";
        for (auto& fn : m.functions) {
            o << "func " << fn.name << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) o << ", ";
                o << fn.params[i].name << ": " << typeName(fn.params[i].type);
            }
            o << ")";
            if (fn.returnType != Type::Void)
                o << " -> " << typeName(fn.returnType);
            o << " {\n";
            for (auto& blk : fn.blocks) {
                o << blk.name << ":\n";
                for (auto& op : blk.ops) printOp(o, op);
            }
            o << "}\n\n";
        }
        return o.str();
    }

} // namespace vcb