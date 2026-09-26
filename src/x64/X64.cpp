#include "vcb/X64.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace vcb {

    namespace {

        enum Reg : uint8_t {
            RAX = 0, RCX = 1, RDX = 2, RBX = 3,
            RSP = 4, RBP = 5, RSI = 6, RDI = 7,
            R8 = 8, R9 = 9, R10 = 10, R11 = 11,
            R12 = 12, R13 = 13, R14 = 14, R15 = 15
        };

        struct Asm {
            std::vector<uint8_t>& c;
            explicit Asm(std::vector<uint8_t>& v) : c(v) {}

            void b(uint8_t x) { c.push_back(x); }
            void b32(uint32_t x) { for (int i = 0; i < 4; i++) c.push_back((x >> (8 * i)) & 0xFF); }
            void b64(uint64_t x) { for (int i = 0; i < 8; i++) c.push_back((x >> (8 * i)) & 0xFF); }

            void rex(bool w, uint8_t reg, uint8_t rm) {
                uint8_t r = 0x40;
                if (w)          r |= 0x08;
                if (reg >= 8)   r |= 0x04;
                if (rm >= 8)   r |= 0x01;
                if (r != 0x40) b(r);
            }

            void movImm64(uint8_t reg, uint64_t imm) {
                rex(true, 0, reg);
                b(0xB8 + (reg & 7));
                b64(imm);
            }
            void loadRbp(uint8_t reg, int32_t disp) {
                rex(true, reg, RBP);
                b(0x8B);
                b(0x80 | ((reg & 7) << 3) | 5);
                b32((uint32_t)disp);
            }
            void storeRbp(int32_t disp, uint8_t reg) {
                rex(true, reg, RBP);
                b(0x89);
                b(0x80 | ((reg & 7) << 3) | 5);
                b32((uint32_t)disp);
            }

            void addReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x01);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void subReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x29);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void andReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x21);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void orReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x09);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void xorReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x31);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void imulReg(uint8_t dst, uint8_t src) {
                rex(true, dst, src); b(0x0F); b(0xAF);
                b(0xC0 | ((dst & 7) << 3) | (src & 7));
            }
            void cqo() { b(0x48); b(0x99); }
            void idivReg(uint8_t reg) {
                rex(true, 0, reg); b(0xF7);
                b(0xC0 | (7 << 3) | (reg & 7));
            }
            void negReg(uint8_t reg) {
                rex(true, 0, reg); b(0xF7);
                b(0xC0 | (3 << 3) | (reg & 7));
            }
            void cmpReg(uint8_t a, uint8_t b2) {
                rex(true, b2, a); b(0x39);
                b(0xC0 | ((b2 & 7) << 3) | (a & 7));
            }
            void setcc(uint8_t cc, uint8_t reg) {
                if (reg >= 4 && reg <= 7) rex(false, 0, reg);
                b(0x0F); b(0x90 + cc);
                b(0xC0 | (reg & 7));
            }
            void movzxReg8(uint8_t dst, uint8_t src) {
                rex(true, dst, src); b(0x0F); b(0xB6);
                b(0xC0 | ((dst & 7) << 3) | (src & 7));
            }
            void shlCl(uint8_t reg) {
                rex(true, 0, reg); b(0xD3); b(0xC0 | (4 << 3) | (reg & 7));
            }
            void sarCl(uint8_t reg) {
                rex(true, 0, reg); b(0xD3); b(0xC0 | (7 << 3) | (reg & 7));
            }
            void movRegReg(uint8_t dst, uint8_t src) {
                rex(true, src, dst); b(0x89);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }
            void mov32RegReg(uint8_t dst, uint8_t src) {
                rex(false, src, dst); b(0x89);
                b(0xC0 | ((src & 7) << 3) | (dst & 7));
            }

            void callRel32Placeholder() { b(0xE8); b32(0); }
            void callIndirectRip() { b(0xFF); b(0x15); b32(0); }
            void ret() { b(0xC3); }
            void leave() { b(0xC9); }
            void pushRbp() { b(0x55); }
            void int3() { b(0xCC); }
            void subRspImm32(uint32_t imm) { b(0x48); b(0x81); b(0xEC); b32(imm); }
            void addRspImm32(uint32_t imm) { b(0x48); b(0x81); b(0xC4); b32(imm); }
        };

        struct Frame {
            std::unordered_map<std::string, int> slot; // name -> slot index
            int nSlots = 0;
            int frameSize = 0;
        };

        Frame layoutFunction(const Function& fn) {
            Frame f;
            int i = 0;
            for (auto& p : fn.params) f.slot[p.name] = i++;
            for (auto& blk : fn.blocks)
                for (auto& op : blk.ops)
                    if (!op.dst.empty()) f.slot[op.dst] = i++;
            f.nSlots = i;
            int bytes = i * 8 + 32;             // locals + shadow space
            f.frameSize = (bytes + 15) & ~15;
            return f;
        }

        struct CallFixup {
            uint32_t    pos;   // offset of rel32 field
            std::string target;
        };

        void emitOp(std::vector<uint8_t>& text, const Function& /*fn*/,
            const Frame& f, const Op& op,
            std::vector<CallFixup>& callFixups) {
            Asm a(text);
            auto load = [&](const std::string& name, uint8_t reg) {
                auto it = f.slot.find(name);
                if (it == f.slot.end())
                    throw std::runtime_error("codegen: unknown value '" + name + "'");
                a.loadRbp(reg, -(it->second + 1) * 8);
                };
            auto store = [&](const std::string& name, uint8_t reg) {
                auto it = f.slot.find(name);
                if (it == f.slot.end())
                    throw std::runtime_error("codegen: unknown destination '" + name + "'");
                a.storeRbp(-(it->second + 1) * 8, reg);
                };

            switch (op.kind) {
            case OpKind::ConstI:
                a.movImm64(RAX, (uint64_t)op.immI);
                store(op.dst, RAX);
                break;
            case OpKind::ConstF:
                a.movImm64(RAX, (uint64_t)op.immF);
                store(op.dst, RAX);
                break;
            case OpKind::Copy:
                load(op.args.at(0), RAX);
                store(op.dst, RAX);
                break;
            case OpKind::Neg:
                load(op.args.at(0), RAX);
                a.negReg(RAX);
                store(op.dst, RAX);
                break;

            case OpKind::Add: case OpKind::Sub: case OpKind::Mul:
            case OpKind::And: case OpKind::Or:  case OpKind::Xor:
            case OpKind::Div: case OpKind::Mod:
            case OpKind::Shl: case OpKind::Shr: {
                load(op.args.at(0), RAX);
                load(op.args.at(1), RCX);
                switch (op.kind) {
                case OpKind::Add: a.addReg(RAX, RCX); break;
                case OpKind::Sub: a.subReg(RAX, RCX); break;
                case OpKind::Mul: a.imulReg(RAX, RCX); break;
                case OpKind::And: a.andReg(RAX, RCX); break;
                case OpKind::Or:  a.orReg(RAX, RCX);  break;
                case OpKind::Xor: a.xorReg(RAX, RCX); break;
                case OpKind::Div: a.cqo(); a.idivReg(RCX); break;
                case OpKind::Mod: a.cqo(); a.idivReg(RCX); a.movRegReg(RAX, RDX); break;
                case OpKind::Shl: a.shlCl(RAX); break;
                case OpKind::Shr: a.sarCl(RAX); break;   // signed shift for now
                default: break;
                }
                store(op.dst, RAX);
                break;
            }

            case OpKind::Eq: case OpKind::Ne:
            case OpKind::Lt: case OpKind::Le:
            case OpKind::Gt: case OpKind::Ge: {
                load(op.args.at(0), RAX);
                load(op.args.at(1), RCX);
                a.cmpReg(RAX, RCX);
                uint8_t cc = 0;
                switch (op.kind) {
                case OpKind::Eq: cc = 0x94; break;
                case OpKind::Ne: cc = 0x95; break;
                case OpKind::Lt: cc = 0x9C; break;
                case OpKind::Le: cc = 0x9E; break;
                case OpKind::Gt: cc = 0x9F; break;
                case OpKind::Ge: cc = 0x9D; break;
                default: break;
                }
                a.setcc(cc, RAX);
                a.movzxReg8(RAX, RAX);
                store(op.dst, RAX);
                break;
            }

            case OpKind::Call: {
                static const uint8_t argRegs[4] = { RCX, RDX, R8, R9 };
                if (op.args.size() > 4)
                    throw std::runtime_error("codegen: >4 args not yet supported");
                for (size_t i = 0; i < op.args.size(); ++i)
                    load(op.args[i], argRegs[i]);
                uint32_t relPos = (uint32_t)text.size() + 1;
                a.callRel32Placeholder();
                callFixups.push_back({ relPos, op.callee });
                if (!op.dst.empty()) store(op.dst, RAX);
                break;
            }

            case OpKind::Ret: {
                if (!op.args.empty()) load(op.args[0], RAX);
                a.leave();
                a.ret();
                break;
            }

            default:
                throw std::runtime_error(std::string("codegen: op not yet implemented: ") +
                    opName(op.kind));
            }
        }

        void emitFunction(std::vector<uint8_t>& text, const Function& fn,
            const Frame& f, std::vector<CallFixup>& callFixups) {
            Asm a(text);
            a.pushRbp();
            a.movRegReg(RBP, RSP);
            a.subRspImm32((uint32_t)f.frameSize);

            static const uint8_t argRegs[4] = { RCX, RDX, R8, R9 };
            if (fn.params.size() > 4)
                throw std::runtime_error("codegen: >4 params not yet supported");
            for (size_t i = 0; i < fn.params.size(); ++i) {
                int slot = f.slot.at(fn.params[i].name);
                a.storeRbp(-(slot + 1) * 8, argRegs[i]);
            }

            for (auto& blk : fn.blocks)
                for (auto& op : blk.ops)
                    emitOp(text, fn, f, op, callFixups);
        }

        // Layout of .idata at its RVA.  Fixed offsets; only one import
        // (kernel32!ExitProcess) is supported in Part 2.
        static const uint32_t IDATA_IMPORT_OFF = 0x00; // 40 bytes (desc + null)
        static const uint32_t IDATA_ILT_OFF = 0x28; // 16 bytes
        static const uint32_t IDATA_IAT_OFF = 0x38; // 16 bytes
        static const uint32_t IDATA_HN_OFF = 0x48; // 14 bytes (2 + 12)
        static const uint32_t IDATA_DLL_OFF = 0x58; // "kernel32.dll"
        static const uint32_t IDATA_SIZE = 0x70;

        void put32(std::vector<uint8_t>& v, uint32_t off, uint32_t x) {
            v[off + 0] = (uint8_t)(x);
            v[off + 1] = (uint8_t)(x >> 8);
            v[off + 2] = (uint8_t)(x >> 16);
            v[off + 3] = (uint8_t)(x >> 24);
        }

        void buildIdata(std::vector<uint8_t>& idata, uint32_t idataRva) {
            idata.assign(IDATA_SIZE, 0);
            uint32_t iltRva = idataRva + IDATA_ILT_OFF;
            uint32_t iatRva = idataRva + IDATA_IAT_OFF;
            uint32_t hnRva = idataRva + IDATA_HN_OFF;
            uint32_t dllRva = idataRva + IDATA_DLL_OFF;

            // IMAGE_IMPORT_DESCRIPTOR
            put32(idata, IDATA_IMPORT_OFF + 0, iltRva);   // OriginalFirstThunk
            put32(idata, IDATA_IMPORT_OFF + 4, 0);        // TimeDateStamp
            put32(idata, IDATA_IMPORT_OFF + 8, 0);        // ForwarderChain
            put32(idata, IDATA_IMPORT_OFF + 12, dllRva);   // Name
            put32(idata, IDATA_IMPORT_OFF + 16, iatRva);   // FirstThunk
            // terminator (already zero)

            // ILT / IAT: one entry pointing to hint/name, followed by null
            uint64_t hn = (uint64_t)hnRva;
            std::memcpy(&idata[IDATA_ILT_OFF + 0], &hn, 8);
            std::memcpy(&idata[IDATA_IAT_OFF + 0], &hn, 8);

            // Hint/Name: 2-byte hint (0) + "ExitProcess\0"
            const char* name = "ExitProcess";
            idata[IDATA_HN_OFF + 0] = 0;
            idata[IDATA_HN_OFF + 1] = 0;
            std::memcpy(&idata[IDATA_HN_OFF + 2], name, std::strlen(name) + 1);

            // DLL name
            const char* dll = "kernel32.dll";
            std::memcpy(&idata[IDATA_DLL_OFF], dll, std::strlen(dll) + 1);
        }

    } // namespace

    CodegenResult codegenX64Pe(const Module& m) {
        CodegenResult r;
        r.idataRva = 0x2000;
        const uint32_t textRva = 0x1000;
        const uint32_t textLimit = r.idataRva - textRva; // 0x1000

        std::vector<CallFixup> callFixups;
        std::unordered_map<std::string, uint32_t> funcOffsets;

        // Frames first (deterministic slot assignment).
        std::unordered_map<std::string, Frame> frames;
        for (auto& fn : m.functions) frames[fn.name] = layoutFunction(fn);

        // Emit functions.
        for (auto& fn : m.functions) {
            funcOffsets[fn.name] = (uint32_t)r.text.size();
            emitFunction(r.text, fn, frames[fn.name], callFixups);
        }

        // Emit entry stub.
        r.entryOffset = (uint32_t)r.text.size();
        {
            Asm a(r.text);
            a.subRspImm32(40);                          // 32 shadow + 8 align
            uint32_t callPos = (uint32_t)r.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RCX, RAX);                    // mov ecx, eax
            uint32_t iatRelPos = (uint32_t)r.text.size() + 2;
            a.callIndirectRip();
            a.int3();
            // Patch rip-relative displacement once we know IAT RVA (fixed).
            uint32_t iatRva = r.idataRva + IDATA_IAT_OFF;
            uint32_t instrRva = textRva + (iatRelPos - 2);
            int32_t  rel = (int32_t)iatRva - (int32_t)(instrRva + 6);
            std::memcpy(&r.text[iatRelPos], &rel, 4);
        }

        if (r.text.size() > textLimit)
            throw std::runtime_error("codegen: .text exceeds 0x1000 bytes; layout needs widening");

        // Resolve call fixups.
        for (auto& cf : callFixups) {
            auto it = funcOffsets.find(cf.target);
            if (it == funcOffsets.end())
                throw std::runtime_error("codegen: undefined function '" + cf.target + "'");
            int32_t rel = (int32_t)it->second - (int32_t)(cf.pos + 4);
            std::memcpy(&r.text[cf.pos], &rel, 4);
        }

        buildIdata(r.idata, r.idataRva);
        r.iatRva = r.idataRva + IDATA_IAT_OFF;
        r.importRva = r.idataRva + IDATA_IMPORT_OFF;
        r.importSize = 40;
        return r;
    }

} // namespace vcb