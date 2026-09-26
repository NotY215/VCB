#include "vcb/X64.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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
            void leaRbp(uint8_t reg, int32_t disp) {
                rex(true, reg, RBP);
                b(0x8D);
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
            // mov dst, [base]   (base must NOT be RSP or RBP)
            void movRegIndReg(uint8_t dst, uint8_t base) {
                rex(true, dst, base);
                b(0x8B);
                b(0x00 | ((dst & 7) << 3) | (base & 7));
            }
            // mov [base], src   (base must NOT be RSP or RBP)
            void movIndRegReg(uint8_t base, uint8_t src) {
                rex(true, src, base);
                b(0x89);
                b(0x00 | ((src & 7) << 3) | (base & 7));
            }
            void testReg(uint8_t reg) {
                rex(true, reg, reg);
                b(0x85);
                b(0xC0 | ((reg & 7) << 3) | (reg & 7));
            }

            void jmpRel32Placeholder() { b(0xE9); b32(0); }
            void jnzRel32Placeholder() { b(0x0F); b(0x85); b32(0); }
            void jzRel32Placeholder() { b(0x0F); b(0x84); b32(0); }
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
            std::unordered_map<std::string, int> slot;           // SSA name -> slot index
            std::unordered_map<std::string, int> alloca_offset;  // alloca dst -> distance below rbp
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

            // Reserve 8 bytes per alloca op, below the value slots.
            int na = 0;
            for (auto& blk : fn.blocks) {
                for (auto& op : blk.ops) {
                    if (op.kind == OpKind::Alloca && !op.dst.empty()) {
                        f.alloca_offset[op.dst] = f.nSlots * 8 + na * 8 + 8;
                        ++na;
                    }
                }
            }

            int bytes = f.nSlots * 8 + na * 8 + 32;
            f.frameSize = (bytes + 15) & ~15;
            return f;
        }

        struct CallFixup {
            uint32_t    pos;
            std::string target;
        };

        struct BlockFixup {
            uint32_t    pos;
            std::string target;   // block name within the current function
        };

        class FunctionEmitter {
        public:
            FunctionEmitter(std::vector<uint8_t>& text, const Function& fn,
                const Frame& frame, std::vector<CallFixup>& callFixups)
                : text_(text), fn_(fn), frame_(frame),
                callFixups_(callFixups), a_(text) {
                for (auto& blk : fn_.blocks)
                    for (auto& op : blk.ops)
                        if (op.kind == OpKind::Phi)
                            phisByBlock_[blk.name].push_back(&op);
            }

            void run() {
                a_.pushRbp();
                a_.movRegReg(RBP, RSP);
                a_.subRspImm32((uint32_t)frame_.frameSize);

                static const uint8_t argRegs[4] = { RCX, RDX, R8, R9 };
                if (fn_.params.size() > 4)
                    throw std::runtime_error("codegen: >4 params not yet supported");
                for (size_t i = 0; i < fn_.params.size(); ++i) {
                    int slot = frame_.slot.at(fn_.params[i].name);
                    a_.storeRbp(-(slot + 1) * 8, argRegs[i]);
                }

                for (auto& blk : fn_.blocks) {
                    blockOffsets_[blk.name] = (uint32_t)text_.size();
                    currentBlock_ = blk.name;
                    for (auto& op : blk.ops) emitOp(op);
                }

                for (auto& fx : blockFixups_) {
                    auto it = blockOffsets_.find(fx.target);
                    if (it == blockOffsets_.end())
                        throw std::runtime_error(
                            "codegen: undefined block '" + fx.target + "'");
                    int32_t rel = (int32_t)it->second - (int32_t)(fx.pos + 4);
                    std::memcpy(&text_[fx.pos], &rel, 4);
                }
            }

        private:
            std::vector<uint8_t>& text_;
            const Function& fn_;
            const Frame& frame_;
            std::vector<CallFixup>& callFixups_;
            Asm                                                     a_;
            std::unordered_map<std::string, std::vector<const Op*>> phisByBlock_;
            std::unordered_map<std::string, uint32_t>               blockOffsets_;
            std::vector<BlockFixup>                                 blockFixups_;
            std::string                                             currentBlock_;

            void load(const std::string& name, uint8_t reg) {
                auto it = frame_.slot.find(name);
                if (it == frame_.slot.end())
                    throw std::runtime_error("codegen: unknown value '" + name + "'");
                a_.loadRbp(reg, -(it->second + 1) * 8);
            }
            void store(const std::string& name, uint8_t reg) {
                auto it = frame_.slot.find(name);
                if (it == frame_.slot.end())
                    throw std::runtime_error("codegen: unknown destination '" + name + "'");
                a_.storeRbp(-(it->second + 1) * 8, reg);
            }

            // Emit stores for every phi in `target` whose incoming block
            // matches `pred`.  Called at the terminator of `pred` before the
            // jump to `target`.
            void emitPhiStores(const std::string& target, const std::string& pred) {
                auto it = phisByBlock_.find(target);
                if (it == phisByBlock_.end()) return;
                for (auto* phi : it->second) {
                    for (auto& pr : phi->phiPairs) {
                        if (pr.second == pred) {
                            load(pr.first, RAX);
                            store(phi->dst, RAX);
                            break;
                        }
                    }
                }
            }

            void emitOp(const Op& op) {
                switch (op.kind) {

                case OpKind::ConstI:
                    a_.movImm64(RAX, (uint64_t)op.immI);
                    store(op.dst, RAX);
                    break;
                case OpKind::ConstF: {
                    uint64_t bits;
                    std::memcpy(&bits, &op.immF, 8);
                    a_.movImm64(RAX, bits);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::Copy:
                    load(op.args.at(0), RAX);
                    store(op.dst, RAX);
                    break;
                case OpKind::Neg:
                    load(op.args.at(0), RAX);
                    a_.negReg(RAX);
                    store(op.dst, RAX);
                    break;

                case OpKind::Add: case OpKind::Sub: case OpKind::Mul:
                case OpKind::And: case OpKind::Or:  case OpKind::Xor:
                case OpKind::Div: case OpKind::Mod:
                case OpKind::Shl: case OpKind::Shr: {
                    load(op.args.at(0), RAX);
                    load(op.args.at(1), RCX);
                    switch (op.kind) {
                    case OpKind::Add: a_.addReg(RAX, RCX); break;
                    case OpKind::Sub: a_.subReg(RAX, RCX); break;
                    case OpKind::Mul: a_.imulReg(RAX, RCX); break;
                    case OpKind::And: a_.andReg(RAX, RCX); break;
                    case OpKind::Or:  a_.orReg(RAX, RCX);  break;
                    case OpKind::Xor: a_.xorReg(RAX, RCX); break;
                    case OpKind::Div: a_.cqo(); a_.idivReg(RCX); break;
                    case OpKind::Mod: a_.cqo(); a_.idivReg(RCX); a_.movRegReg(RAX, RDX); break;
                    case OpKind::Shl: a_.shlCl(RAX); break;
                    case OpKind::Shr: a_.sarCl(RAX); break;
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
                    a_.cmpReg(RAX, RCX);
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
                    a_.setcc(cc, RAX);
                    a_.movzxReg8(RAX, RAX);
                    store(op.dst, RAX);
                    break;
                }

                case OpKind::Alloca: {
                    auto it = frame_.alloca_offset.find(op.dst);
                    if (it == frame_.alloca_offset.end())
                        throw std::runtime_error(
                            "codegen: alloca has no frame slot");
                    a_.leaRbp(RAX, -(int32_t)it->second);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::Load:
                    load(op.args.at(0), RAX);
                    a_.movRegIndReg(RAX, RAX);      // mov rax, [rax]
                    store(op.dst, RAX);
                    break;
                case OpKind::Store:
                    load(op.args.at(0), RAX);       // value
                    load(op.args.at(1), RCX);       // pointer
                    a_.movIndRegReg(RCX, RAX);      // mov [rcx], rax
                    break;

                case OpKind::Jmp: {
                    emitPhiStores(op.targetTrue, currentBlock_);
                    uint32_t relPos = (uint32_t)text_.size() + 1;
                    a_.jmpRel32Placeholder();
                    blockFixups_.push_back({ relPos, op.targetTrue });
                    break;
                }

                case OpKind::Br: {
                    if (op.args.empty())
                        throw std::runtime_error("codegen: br missing condition");
                    load(op.args.at(0), RAX);
                    a_.testReg(RAX);

                    // jnz Ltrue
                    uint32_t jnzRelPos = (uint32_t)text_.size() + 2;
                    a_.jnzRel32Placeholder();

                    // false path: phi stores for false target, then jmp
                    emitPhiStores(op.targetFalse, currentBlock_);
                    uint32_t jmpFPos = (uint32_t)text_.size() + 1;
                    a_.jmpRel32Placeholder();
                    blockFixups_.push_back({ jmpFPos, op.targetFalse });

                    // Ltrue: patch jnz rel32 to fall right here
                    uint32_t here = (uint32_t)text_.size();
                    int32_t rel = (int32_t)here - (int32_t)(jnzRelPos + 4);
                    std::memcpy(&text_[jnzRelPos], &rel, 4);

                    // true path: phi stores for true target, then jmp
                    emitPhiStores(op.targetTrue, currentBlock_);
                    uint32_t jmpTPos = (uint32_t)text_.size() + 1;
                    a_.jmpRel32Placeholder();
                    blockFixups_.push_back({ jmpTPos, op.targetTrue });
                    break;
                }

                case OpKind::Phi:
                    // No code at the definition site; see emitPhiStores.
                    break;

                case OpKind::Call: {
                    static const uint8_t argRegs[4] = { RCX, RDX, R8, R9 };
                    if (op.args.size() > 4)
                        throw std::runtime_error("codegen: >4 args not yet supported");
                    for (size_t i = 0; i < op.args.size(); ++i)
                        load(op.args[i], argRegs[i]);
                    uint32_t relPos = (uint32_t)text_.size() + 1;
                    a_.callRel32Placeholder();
                    callFixups_.push_back({ relPos, op.callee });
                    if (!op.dst.empty()) store(op.dst, RAX);
                    break;
                }

                case OpKind::Ret:
                    if (!op.args.empty()) load(op.args[0], RAX);
                    a_.leave();
                    a_.ret();
                    break;

                default:
                    throw std::runtime_error(
                        std::string("codegen: op not yet implemented: ") +
                        opName(op.kind));
                }
            }
        };

        // -------- .idata layout (kernel32!ExitProcess) -----------------------

        static const uint32_t IDATA_IMPORT_OFF = 0x00;
        static const uint32_t IDATA_ILT_OFF = 0x28;
        static const uint32_t IDATA_IAT_OFF = 0x38;
        static const uint32_t IDATA_HN_OFF = 0x48;
        static const uint32_t IDATA_DLL_OFF = 0x58;
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

            put32(idata, IDATA_IMPORT_OFF + 0, iltRva);
            put32(idata, IDATA_IMPORT_OFF + 4, 0);
            put32(idata, IDATA_IMPORT_OFF + 8, 0);
            put32(idata, IDATA_IMPORT_OFF + 12, dllRva);
            put32(idata, IDATA_IMPORT_OFF + 16, iatRva);

            uint64_t hn = (uint64_t)hnRva;
            std::memcpy(&idata[IDATA_ILT_OFF + 0], &hn, 8);
            std::memcpy(&idata[IDATA_IAT_OFF + 0], &hn, 8);

            const char* name = "ExitProcess";
            idata[IDATA_HN_OFF + 0] = 0;
            idata[IDATA_HN_OFF + 1] = 0;
            std::memcpy(&idata[IDATA_HN_OFF + 2], name, std::strlen(name) + 1);

            const char* dll = "kernel32.dll";
            std::memcpy(&idata[IDATA_DLL_OFF], dll, std::strlen(dll) + 1);
        }

    } // namespace

    CodegenResult codegenX64Pe(const Module& m) {
        CodegenResult r;
        r.idataRva = 0x2000;
        const uint32_t textRva = 0x1000;
        const uint32_t textLimit = r.idataRva - textRva;

        std::vector<CallFixup> callFixups;
        std::unordered_map<std::string, uint32_t> funcOffsets;

        std::unordered_map<std::string, Frame> frames;
        for (auto& fn : m.functions) frames[fn.name] = layoutFunction(fn);

        for (auto& fn : m.functions) {
            funcOffsets[fn.name] = (uint32_t)r.text.size();
            FunctionEmitter fe(r.text, fn, frames[fn.name], callFixups);
            fe.run();
        }

        // Entry stub: call main, pass eax to ExitProcess.
        r.entryOffset = (uint32_t)r.text.size();
        {
            Asm a(r.text);
            a.subRspImm32(40);                              // 32 shadow + 8 align
            uint32_t callPos = (uint32_t)r.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RCX, RAX);                        // mov ecx, eax
            uint32_t iatRelPos = (uint32_t)r.text.size() + 2;
            a.callIndirectRip();
            a.int3();
            uint32_t iatRva = r.idataRva + IDATA_IAT_OFF;
            uint32_t instrRva = textRva + (iatRelPos - 2);
            int32_t  rel = (int32_t)iatRva - (int32_t)(instrRva + 6);
            std::memcpy(&r.text[iatRelPos], &rel, 4);
        }

        if (r.text.size() > textLimit)
            throw std::runtime_error(
                "codegen: .text exceeds 0x1000 bytes; layout needs widening");

        for (auto& cf : callFixups) {
            auto it = funcOffsets.find(cf.target);
            if (it == funcOffsets.end())
                throw std::runtime_error(
                    "codegen: undefined function '" + cf.target + "'");
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