// VCB — Vayu Compiler Backend
// parser.cpp — IR text -> vcb::Module
#include "vcb.hpp"
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace vcb {

    namespace {

        std::string trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace((unsigned char)s[a])) ++a;
            while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        std::string stripComment(const std::string& s) {
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == ';' || s[i] == '#')
                    return s.substr(0, i);
                if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
                    return s.substr(0, i);
            }
            return s;
        }

        bool parseTypeTok(const std::string& t, Ty& out) {
            if (t == "void") out = Ty::Void;
            else if (t == "i8")   out = Ty::I8;
            else if (t == "i16")  out = Ty::I16;
            else if (t == "i32")  out = Ty::I32;
            else if (t == "i64")  out = Ty::I64;
            else if (t == "f32")  out = Ty::F32;
            else if (t == "f64")  out = Ty::F64;
            else if (t == "ptr")  out = Ty::Ptr;
            else return false;
            return true;
        }

        bool parseValueTok(const std::string& t, ValueId& out) {
            if (t.empty() || t[0] != '%') return false;
            out = (ValueId)std::strtoul(t.c_str() + 1, nullptr, 10);
            return true;
        }

        bool parseImmTok(const std::string& t, int64_t& out) {
            if (t.empty()) return false;
            char* end = nullptr;
            long long v = std::strtoll(t.c_str(), &end, 0);
            if (end == t.c_str()) return false;
            out = v;
            return true;
        }

        bool parseOperand(const std::string& t, Operand& out) {
            ValueId v;
            if (parseValueTok(t, v)) { out = Operand::V(v); return true; }
            int64_t i;
            if (parseImmTok(t, i)) { out = Operand::I(i); return true; }
            return false;
        }

        std::vector<std::string> splitCommas(const std::string& s) {
            std::vector<std::string> out;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); ++i) {
                if (i == s.size() || s[i] == ',') {
                    out.push_back(trim(s.substr(start, i - start)));
                    start = i + 1;
                }
            }
            return out;
        }

        bool parseOpName(const std::string& s, Op& out) {
            static const std::pair<const char*, Op> tbl[] = {
                {"nop", Op::Nop}, {"copy", Op::Copy},
                {"add", Op::Add}, {"sub", Op::Sub}, {"mul", Op::Mul},
                {"div", Op::Div}, {"rem", Op::Rem},
                {"and", Op::And}, {"or", Op::Or}, {"xor", Op::Xor},
                {"shl", Op::Shl}, {"shr", Op::Shr}, {"sar", Op::Sar},
                {"neg", Op::Neg}, {"not", Op::Not},
                {"ceq", Op::Ceq}, {"cne", Op::Cne},
                {"clt", Op::Clt}, {"cle", Op::Cle},
                {"cgt", Op::Cgt}, {"cge", Op::Cge},
                {"load", Op::Load}, {"store", Op::Store}, {"alloc", Op::Alloc},
                {"trunc", Op::Trunc}, {"zext", Op::Zext}, {"sext", Op::Sext},
                {"sitofp", Op::Sitofp}, {"fptosi", Op::Fptosi},
                {"jmp", Op::Jmp}, {"jnz", Op::Jnz}, {"ret", Op::Ret},
                {"call", Op::Call}, {"phi", Op::Phi},
            };
            for (auto& p : tbl) if (s == p.first) { out = p.second; return true; }
            return false;
        }

    } // anon

    bool parseIR(const std::string& text, Module& m, std::string& err) {
        std::istringstream in(text);
        std::string raw;
        int lineNo = 0;

        Function* cur = nullptr;
        Block* curBlock = nullptr;

        while (std::getline(in, raw)) {
            ++lineNo;
            std::string line = trim(stripComment(raw));
            if (line.empty()) continue;

            // ---- Function start ----
            {
                std::string rest = line;
                bool exported = false;
                if (rest.rfind("export ", 0) == 0) {
                    exported = true;
                    rest = trim(rest.substr(7));
                }
                if (rest.rfind("function ", 0) == 0) {
                    rest = trim(rest.substr(9));   // "i32 plus3(i32 %1, i32 %2, i32 %3) {"

                    // Locate the parameter parens in the WHOLE line.
                    size_t lp = rest.find('(');
                    size_t rp = (lp == std::string::npos)
                        ? std::string::npos
                        : rest.find(')', lp);
                    if (lp == std::string::npos || rp == std::string::npos) {
                        err = "line " + std::to_string(lineNo)
                            + ": function header needs '<ret> <name>(<params>)'";
                        return false;
                    }

                    std::string before = trim(rest.substr(0, lp));   // "i32 plus3"
                    std::string plist = rest.substr(lp + 1, rp - lp - 1);

                    // Split "i32 plus3" on the LAST space → retty / fname.
                    size_t sp = before.rfind(' ');
                    if (sp == std::string::npos) {
                        err = "line " + std::to_string(lineNo)
                            + ": function header needs '<ret> <name>'";
                        return false;
                    }
                    std::string retty = trim(before.substr(0, sp));
                    std::string fname = trim(before.substr(sp + 1));

                    auto fn = std::make_unique<Function>();
                    fn->name = fname;
                    fn->exported = exported;
                    Ty rt;
                    if (!parseTypeTok(retty, rt)) {
                        err = "line " + std::to_string(lineNo) + ": bad return type";
                        return false;
                    }
                    fn->ret = rt;

                    if (!plist.empty()) {
                        for (auto& part : splitCommas(plist)) {
                            if (part.empty()) continue;
                            std::istringstream ps(part);
                            std::string pty, pname;
                            ps >> pty >> pname;
                            Ty pt;
                            if (!parseTypeTok(pty, pt)) {
                                err = "line " + std::to_string(lineNo) + ": bad param type";
                                return false;
                            }
                            ValueId pid;
                            if (!parseValueTok(pname, pid)) {
                                err = "line " + std::to_string(lineNo)
                                    + ": param needs value id like %1";
                                return false;
                            }
                            fn->params.push_back({ pt, pid });
                            if (pid >= fn->nextValue) fn->nextValue = pid + 1;
                        }
                    }

                    cur = fn.get();
                    m.funcs.push_back(std::move(fn));
                    curBlock = nullptr;
                    continue;
                }
            }

            if (line == "}") {
                cur = nullptr;
                curBlock = nullptr;
                continue;
            }

            if (!cur) {
                err = "line " + std::to_string(lineNo) + ": top-level junk: " + line;
                return false;
            }

            if (line.back() == ':' && line[0] != '%') {
                std::string lname = trim(line.substr(0, line.size() - 1));
                Block b;
                b.name = lname;
                cur->blocks.push_back(std::move(b));
                curBlock = &cur->blocks.back();
                continue;
            }

            if (!curBlock) {
                Block b;
                b.name = "entry";
                cur->blocks.push_back(std::move(b));
                curBlock = &cur->blocks.back();
            }

            Instr ins;
            std::string work = line;
            if (work[0] == '%') {
                size_t eq = work.find('=');
                if (eq == std::string::npos) {
                    err = "line " + std::to_string(lineNo) + ": expected '='";
                    return false;
                }
                std::string dstTok = trim(work.substr(0, eq));
                ValueId dv;
                if (!parseValueTok(dstTok, dv)) {
                    err = "line " + std::to_string(lineNo) + ": bad dst";
                    return false;
                }
                ins.dst = dv;
                if (dv >= cur->nextValue) cur->nextValue = dv + 1;
                work = trim(work.substr(eq + 1));
            }

            std::istringstream ws(work);
            std::string opName;
            ws >> opName;
            if (!parseOpName(opName, ins.op)) {
                err = "line " + std::to_string(lineNo) + ": unknown op '" + opName + "'";
                return false;
            }
            std::string rest;
            std::getline(ws, rest);
            rest = trim(rest);

            switch (ins.op) {
            case Op::Nop: break;

            case Op::Ret: {
                if (!rest.empty()) {
                    std::istringstream rs(rest);
                    std::string tyTok, valTok;
                    rs >> tyTok >> valTok;
                    Ty t;
                    if (!parseTypeTok(tyTok, t)) {
                        err = "line " + std::to_string(lineNo) + ": ret needs type";
                        return false;
                    }
                    ins.ty = t;
                    if (!valTok.empty()) {
                        if (!parseOperand(valTok, ins.a)) {
                            err = "line " + std::to_string(lineNo) + ": bad ret operand";
                            return false;
                        }
                    }
                }
                break;
            }

            case Op::Jmp: {
                ins.label = trim(rest);
                if (ins.label.empty()) {
                    err = "line " + std::to_string(lineNo) + ": jmp needs label";
                    return false;
                }
                break;
            }

            case Op::Jnz: {
                auto parts = splitCommas(rest);
                if (parts.size() != 3) {
                    err = "line " + std::to_string(lineNo) + ": jnz needs cond, then, else";
                    return false;
                }
                if (!parseOperand(parts[0], ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad jnz cond";
                    return false;
                }
                ins.ty = Ty::I32;
                ins.label = parts[1];
                ins.label2 = parts[2];
                break;
            }

            case Op::Call: {
                std::string retTy, symArgs;
                std::istringstream cs(rest);
                cs >> retTy;
                std::string symPart;
                std::getline(cs, symPart);
                symPart = trim(symPart);
                Ty rt = Ty::Void;
                if (!parseTypeTok(retTy, rt)) {
                    symPart = rest;
                    rt = Ty::Void;
                }
                ins.ty = rt;
                size_t lp = symPart.find('(');
                std::string sym = symPart;
                std::string argList;
                if (lp != std::string::npos) {
                    sym = trim(symPart.substr(0, lp));
                    size_t rp = symPart.find(')', lp);
                    if (rp != std::string::npos)
                        argList = symPart.substr(lp + 1, rp - lp - 1);
                }
                ins.label = sym;
                if (!argList.empty()) {
                    for (auto& a : splitCommas(argList)) {
                        if (a.empty()) continue;
                        Operand op;
                        if (!parseOperand(a, op)) {
                            err = "line " + std::to_string(lineNo) + ": bad call arg";
                            return false;
                        }
                        if (op.isImm) {
                            err = "line " + std::to_string(lineNo)
                                + ": immediate call args unsupported";
                            return false;
                        }
                        ins.phiArgs.push_back({ op.val, "" });
                    }
                }
                break;
            }

            case Op::Phi: {
                std::istringstream ps(rest);
                std::string tyTok;
                ps >> tyTok;
                Ty t;
                if (!parseTypeTok(tyTok, t)) {
                    err = "line " + std::to_string(lineNo) + ": phi needs type";
                    return false;
                }
                ins.ty = t;
                std::string body;
                std::getline(ps, body);
                body = trim(body);
                size_t pos = 0;
                while (pos < body.size()) {
                    size_t lb = body.find('[', pos);
                    if (lb == std::string::npos) break;
                    size_t rb = body.find(']', lb);
                    if (rb == std::string::npos) break;
                    std::string inside = body.substr(lb + 1, rb - lb - 1);
                    auto parts = splitCommas(inside);
                    if (parts.size() != 2) {
                        err = "line " + std::to_string(lineNo)
                            + ": phi entry needs [val, pred]";
                        return false;
                    }
                    ValueId v;
                    if (!parseValueTok(parts[0], v)) {
                        err = "line " + std::to_string(lineNo) + ": bad phi value";
                        return false;
                    }
                    ins.phiArgs.push_back({ v, parts[1] });
                    pos = rb + 1;
                }
                break;
            }

            case Op::Alloc: {
                int64_t sz = 0;
                if (parseImmTok(rest, sz)) {
                    ins.b = Operand::I(sz);
                }
                else {
                    ins.b = Operand::I(8);
                }
                ins.ty = Ty::Ptr;
                break;
            }

            case Op::Load: {
                std::istringstream ls(rest);
                std::string tyTok, ptrTok;
                ls >> tyTok >> ptrTok;
                Ty t;
                if (!parseTypeTok(tyTok, t)) {
                    err = "line " + std::to_string(lineNo) + ": load needs type";
                    return false;
                }
                ins.ty = t;
                if (!parseOperand(ptrTok, ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad load ptr";
                    return false;
                }
                break;
            }

            case Op::Store: {
                std::istringstream ss(rest);
                std::string tyTok, rest2;
                ss >> tyTok;
                std::getline(ss, rest2);
                Ty t;
                if (!parseTypeTok(tyTok, t)) {
                    err = "line " + std::to_string(lineNo) + ": store needs type";
                    return false;
                }
                ins.ty = t;
                auto parts = splitCommas(rest2);
                if (parts.size() != 2) {
                    err = "line " + std::to_string(lineNo) + ": store needs val, ptr";
                    return false;
                }
                if (!parseOperand(parts[0], ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad store val";
                    return false;
                }
                if (!parseOperand(parts[1], ins.b)) {
                    err = "line " + std::to_string(lineNo) + ": bad store ptr";
                    return false;
                }
                break;
            }

            case Op::Neg: case Op::Not: {
                std::istringstream us(rest);
                std::string tyTok, valTok;
                us >> tyTok >> valTok;
                Ty t;
                if (!parseTypeTok(tyTok, t)) {
                    err = "line " + std::to_string(lineNo) + ": " + opName + " needs type";
                    return false;
                }
                ins.ty = t;
                ins.srcTy = t;
                if (!parseOperand(valTok, ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad operand";
                    return false;
                }
                break;
            }

            case Op::Trunc: case Op::Zext: case Op::Sext:
            case Op::Sitofp: case Op::Fptosi: {
                std::istringstream us(rest);
                std::string dstTyTok, srcTyTok, valTok;
                us >> dstTyTok >> srcTyTok >> valTok;
                Ty dt, st;
                if (!parseTypeTok(dstTyTok, dt)) {
                    err = "line " + std::to_string(lineNo) + ": " + opName + " needs destination type";
                    return false;
                }
                if (!parseTypeTok(srcTyTok, st)) {
                    err = "line " + std::to_string(lineNo) + ": " + opName + " needs source type";
                    return false;
                }
                ins.ty = dt;
                ins.srcTy = st;
                if (!parseOperand(valTok, ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad operand";
                    return false;
                }
                break;
            }

            default: {
                std::istringstream bs(rest);
                std::string tyTok, tail;
                bs >> tyTok;
                std::getline(bs, tail);
                Ty t;
                if (!parseTypeTok(tyTok, t)) {
                    err = "line " + std::to_string(lineNo) + ": " + opName + " needs type";
                    return false;
                }
                ins.ty = t;
                auto parts = splitCommas(tail);
                if (parts.size() != 2) {
                    err = "line " + std::to_string(lineNo) + ": " + opName + " needs 2 operands";
                    return false;
                }
                if (!parseOperand(parts[0], ins.a)) {
                    err = "line " + std::to_string(lineNo) + ": bad op1";
                    return false;
                }
                if (!parseOperand(parts[1], ins.b)) {
                    err = "line " + std::to_string(lineNo) + ": bad op2";
                    return false;
                }
                break;
            }
            }

            curBlock->instrs.push_back(std::move(ins));
        }

        return true;
    }

} // namespace vcb