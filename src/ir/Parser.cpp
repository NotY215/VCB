#include "vcb/Parser.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace vcb {

    namespace {

        struct Lexer {
            const std::string& src;
            size_t pos = 0;
            int    line = 1;

            explicit Lexer(const std::string& s) : src(s) {}

            void skipWs() {
                for (;;) {
                    while (pos < src.size() &&
                        (src[pos] == ' ' || src[pos] == '\t' ||
                            src[pos] == '\r')) ++pos;
                    if (pos < src.size() && src[pos] == ';') {
                        while (pos < src.size() && src[pos] != '\n') ++pos;
                        continue;
                    }
                    if (pos < src.size() && src[pos] == '\n') {
                        ++line;
                        ++pos;
                        continue;
                    }
                    break;
                }
            }

            bool atEnd() { skipWs(); return pos >= src.size(); }

            std::string next() {
                skipWs();
                if (pos >= src.size()) return "";
                char c = src[pos];

                // Two-char arrow must be recognised before the number
                // branch and before the single-punct fallback.
                if (c == '-' && pos + 1 < src.size() && src[pos + 1] == '>') {
                    pos += 2;
                    return std::string("->");
                }

                if (std::isalpha((unsigned char)c) || c == '_' || c == '%' ||
                    c == '@') {
                    size_t start = pos++;
                    while (pos < src.size()) {
                        char d = src[pos];
                        if (std::isalnum((unsigned char)d) || d == '_' ||
                            d == '.' || d == '%' || d == '@') {
                            ++pos; continue;
                        }
                        if (d == '<') { // ptr<T>
                            int depth = 1; ++pos;
                            while (pos < src.size() && depth > 0) {
                                if (src[pos] == '<') ++depth;
                                else if (src[pos] == '>') --depth;
                                ++pos;
                            }
                            continue;
                        }
                        break;
                    }
                    return src.substr(start, pos - start);
                }
                if (std::isdigit((unsigned char)c) ||
                    (c == '-' && pos + 1 < src.size() &&
                        std::isdigit((unsigned char)src[pos + 1]))) {
                    size_t start = pos++;
                    while (pos < src.size() &&
                        (std::isdigit((unsigned char)src[pos]) ||
                            src[pos] == '.' || src[pos] == 'e' ||
                            src[pos] == 'E' || src[pos] == '+' ||
                            src[pos] == '-' || src[pos] == 'x')) {
                        if ((src[pos] == '+' || src[pos] == '-') &&
                            !(src[pos - 1] == 'e' || src[pos - 1] == 'E')) break;
                        ++pos;
                    }
                    return src.substr(start, pos - start);
                }
                pos++;
                return std::string(1, c);
            }

            std::string peek() {
                size_t savePos = pos; int saveLine = line;
                std::string t = next();
                pos = savePos; line = saveLine;
                return t;
            }

            bool eat(const char* tok) {
                skipWs();
                size_t savePos = pos;
                std::string t = next();
                if (t == tok) return true;
                pos = savePos;
                return false;
            }

            void expect(const char* tok, const char* what) {
                if (!eat(tok)) {
                    std::string got = peek();
                    throw ParseError("expected '" + std::string(tok) +
                        "' " + what + ", found '" + got + "'", line);
                }
            }
        };

        Type parseType(Lexer& lx) {
            std::string t = lx.next();
            if (t == "i1")  return Type::I1;
            if (t == "i8")  return Type::I8;
            if (t == "i16") return Type::I16;
            if (t == "i32") return Type::I32;
            if (t == "i64") return Type::I64;
            if (t == "f32") return Type::F32;
            if (t == "f64") return Type::F64;
            if (t == "void") return Type::Void;
            if (t == "ptr") return Type::Ptr;
            if (t.rfind("ptr<", 0) == 0) return Type::Ptr;
            throw ParseError("unknown type '" + t + "'", lx.line);
        }

        bool isResultType(Lexer& lx) {
            std::string t = lx.peek();
            return t == "i1" || t == "i8" || t == "i16" || t == "i32" ||
                t == "i64" || t == "f32" || t == "f64" || t == "void" ||
                t == "ptr" || t.rfind("ptr<", 0) == 0;
        }

        OpKind tokenToOp(const std::string& t) {
            if (t == "copy")    return OpKind::Copy;
            if (t == "add")     return OpKind::Add;
            if (t == "sub")     return OpKind::Sub;
            if (t == "mul")     return OpKind::Mul;
            if (t == "div")     return OpKind::Div;
            if (t == "mod")     return OpKind::Mod;
            if (t == "neg")     return OpKind::Neg;
            if (t == "eq")      return OpKind::Eq;
            if (t == "ne")      return OpKind::Ne;
            if (t == "lt")      return OpKind::Lt;
            if (t == "le")      return OpKind::Le;
            if (t == "gt")      return OpKind::Gt;
            if (t == "ge")      return OpKind::Ge;
            if (t == "and")     return OpKind::And;
            if (t == "or")      return OpKind::Or;
            if (t == "xor")     return OpKind::Xor;
            if (t == "shl")     return OpKind::Shl;
            if (t == "shr")     return OpKind::Shr;
            if (t == "call")    return OpKind::Call;
            if (t == "ret")     return OpKind::Ret;
            if (t == "jmp")     return OpKind::Jmp;
            if (t == "br")      return OpKind::Br;
            if (t == "phi")     return OpKind::Phi;
            if (t == "alloca")  return OpKind::Alloca;
            if (t == "load")    return OpKind::Load;
            if (t == "store")   return OpKind::Store;
            if (t == "bitcast") return OpKind::Bitcast;
            if (t == "sitof")   return OpKind::Sitof;
            if (t == "fptosi")  return OpKind::Fptosi;
            return OpKind::Copy;
        }

        Function parseFunction(Lexer& lx) {
            Function fn;
            fn.name = lx.next();
            if (fn.name != "func")
                throw ParseError("expected 'func', found '" + fn.name + "'", lx.line);
            fn.name = lx.next();
            lx.expect("(", "after function name");

            if (!lx.eat(")")) {
                for (;;) {
                    Param p;
                    p.name = lx.next();
                    if (lx.eat(":")) p.type = parseType(lx);
                    fn.params.push_back(std::move(p));
                    if (lx.eat(",")) continue;
                    lx.expect(")", "to close parameter list");
                    break;
                }
            }

            if (lx.eat("->")) fn.returnType = parseType(lx);

            lx.expect("{", "before function body");

            while (!lx.eat("}")) {
                if (lx.atEnd()) throw ParseError("unterminated function", lx.line);
                Block blk;
                std::string name = lx.next();
                if (lx.eat(":")) {
                    blk.name = name;
                }
                else {
                    throw ParseError("expected ':' after block label '" + name +
                        "'", lx.line);
                }

                while (!lx.atEnd()) {
                    std::string save = lx.peek();
                    if (save == "}") break;
                    size_t savePos = lx.pos; int saveLine = lx.line;
                    std::string maybeLabel = lx.next();
                    if (lx.eat(":")) {
                        lx.pos = savePos; lx.line = saveLine;
                        break;
                    }
                    lx.pos = savePos; lx.line = saveLine;

                    Op op;
                    op.line = lx.line;
                    std::string tok = lx.next();

                    if (tok.rfind("const.", 0) == 0) {
                        std::string suffix = tok.substr(6);
                        if (suffix == "f32" || suffix == "f64") {
                            op.kind = OpKind::ConstF;
                            op.type = (suffix == "f32") ? Type::F32 : Type::F64;
                        }
                        else {
                            op.kind = OpKind::ConstI;
                            if (suffix == "i1")  op.type = Type::I1;
                            else if (suffix == "i8")  op.type = Type::I8;
                            else if (suffix == "i16") op.type = Type::I16;
                            else if (suffix == "i32") op.type = Type::I32;
                            else if (suffix == "i64") op.type = Type::I64;
                            else throw ParseError("bad const type '" + suffix + "'",
                                lx.line);
                        }
                        std::string val = lx.next();
                        if (op.kind == OpKind::ConstF) op.immF = std::stod(val);
                        else                           op.immI = std::stoll(val, nullptr, 0);
                        blk.ops.push_back(std::move(op));
                        continue;
                    }

                    if (isResultType(lx)) {
                        size_t savePos2 = lx.pos; int saveLine2 = lx.line;
                        Type rt = parseType(lx);
                        std::string candidate = lx.next();
                        if (candidate.size() > 0 && candidate[0] == '%' && lx.eat("=")) {
                            op.type = rt;
                            op.dst = candidate;
                            tok = lx.next();
                        }
                        else {
                            lx.pos = savePos2; lx.line = saveLine2;
                        }
                    }

                    op.kind = tokenToOp(tok);

                    if (op.kind == OpKind::Call) {
                        op.callee = lx.next();
                        lx.expect("(", "after callee in call");
                        if (!lx.eat(")")) {
                            for (;;) {
                                op.args.push_back(lx.next());
                                if (lx.eat(",")) continue;
                                lx.expect(")", "to close argument list");
                                break;
                            }
                        }
                    }
                    else if (op.kind == OpKind::Ret) {
                        if (lx.eat("void")) {
                            // ret void
                        }
                        else {
                            std::string a = lx.peek();
                            if (!a.empty() && a != "}") op.args.push_back(lx.next());
                        }
                    }
                    else if (op.kind == OpKind::Jmp) {
                        op.targetTrue = lx.next();
                    }
                    else if (op.kind == OpKind::Br) {
                        op.targetTrue = lx.next();
                        lx.expect(",", "between br operands");
                        op.targetFalse = lx.next();
                    }
                    else if (op.kind == OpKind::Phi) {
                        for (;;) {
                            std::string v = lx.next();
                            lx.expect("[", "after phi value");
                            std::string blkName = lx.next();
                            lx.expect("]", "after phi block");
                            op.phiPairs.emplace_back(v, blkName);
                            if (lx.eat(",")) continue;
                            break;
                        }
                    }
                    else if (op.kind == OpKind::Alloca) {
                        op.type = parseType(lx);
                    }
                    else if (op.kind == OpKind::Load || op.kind == OpKind::Store ||
                        op.kind == OpKind::Neg || op.kind == OpKind::Copy ||
                        op.kind == OpKind::Bitcast ||
                        op.kind == OpKind::Sitof ||
                        op.kind == OpKind::Fptosi) {
                        op.args.push_back(lx.next());
                        if (op.kind == OpKind::Store) {
                            lx.expect(",", "between store operands");
                            op.args.push_back(lx.next());
                        }
                    }
                    else {
                        op.args.push_back(lx.next());
                        lx.expect(",", "between binary operands");
                        op.args.push_back(lx.next());
                    }

                    blk.ops.push_back(std::move(op));
                }
                fn.blocks.push_back(std::move(blk));
            }
            return fn;
        }

    } // namespace

    Module parseText(const std::string& text, const std::string& origin) {
        Lexer lx(text);
        Module m;
        while (!lx.atEnd()) {
            std::string save = lx.peek();
            if (save == "func") {
                m.functions.push_back(parseFunction(lx));
            }
            else {
                throw ParseError("expected 'func' at top level, found '" + save +
                    "'  (in " + origin + ")", lx.line);
            }
        }
        return m;
    }

    Module parseFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw ParseError("cannot open '" + path + "'", 0);
        std::stringstream ss; ss << in.rdbuf();
        return parseText(ss.str(), path);
    }

} // namespace vcb