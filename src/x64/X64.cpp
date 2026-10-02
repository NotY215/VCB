#include "vcb/X64.hpp"
#include "vcb/Runtime.hpp"
#include <algorithm>
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
            void setcc(uint8_t fullOpcode, uint8_t reg) {
                if (reg >= 4 && reg <= 7) rex(false, 0, reg);
                b(0x0F); b(fullOpcode);
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
            void movRegIndReg(uint8_t dst, uint8_t base) {
                rex(true, dst, base);
                b(0x8B);
                b(0x00 | ((dst & 7) << 3) | (base & 7));
            }
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

            // SSE2
            void movqXmmFromGpr(uint8_t xmm, uint8_t gpr) {
                b(0x66);
                uint8_t rexByte = 0x48;
                if (xmm >= 8) rexByte |= 0x04;
                if (gpr >= 8) rexByte |= 0x01;
                b(rexByte);
                b(0x0F); b(0x6E);
                b(0xC0 | ((xmm & 7) << 3) | (gpr & 7));
            }
            void movqGprFromXmm(uint8_t gpr, uint8_t xmm) {
                b(0x66);
                uint8_t rexByte = 0x48;
                if (xmm >= 8) rexByte |= 0x04;
                if (gpr >= 8) rexByte |= 0x01;
                b(rexByte);
                b(0x0F); b(0x7E);
                b(0xC0 | ((xmm & 7) << 3) | (gpr & 7));
            }
            void ssd(uint8_t op, uint8_t dst, uint8_t src) {
                b(0xF2); b(0x0F); b(op);
                uint8_t r = 0x40;
                if (dst >= 8) r |= 0x04;
                if (src >= 8) r |= 0x01;
                if (r != 0x40) b(r);
                b(0xC0 | ((dst & 7) << 3) | (src & 7));
            }
            void addsd(uint8_t d, uint8_t s) { ssd(0x58, d, s); }
            void subsd(uint8_t d, uint8_t s) { ssd(0x5C, d, s); }
            void mulsd(uint8_t d, uint8_t s) { ssd(0x59, d, s); }
            void divsd(uint8_t d, uint8_t s) { ssd(0x5E, d, s); }
            void comisd(uint8_t a, uint8_t b2) {
                b(0x66);
                uint8_t r = 0x40;
                if (a >= 8) r |= 0x04;
                if (b2 >= 8) r |= 0x01;
                if (r != 0x40) b(r);
                b(0x0F); b(0x2F);
                b(0xC0 | ((a & 7) << 3) | (b2 & 7));
            }
            void cvtsi2sd(uint8_t xmm, uint8_t gpr) {
                b(0xF2);
                uint8_t r = 0x48;
                if (xmm >= 8) r |= 0x04;
                if (gpr >= 8) r |= 0x01;
                b(r);
                b(0x0F); b(0x2A);
                b(0xC0 | ((xmm & 7) << 3) | (gpr & 7));
            }
            void cvttsd2si(uint8_t gpr, uint8_t xmm) {
                b(0xF2);
                uint8_t r = 0x48;
                if (gpr >= 8) r |= 0x04;
                if (xmm >= 8) r |= 0x01;
                b(r);
                b(0x0F); b(0x2C);
                b(0xC0 | ((gpr & 7) << 3) | (xmm & 7));
            }

            // Control flow
            void jmpRel32Placeholder() { b(0xE9); b32(0); }
            void jnzRel32Placeholder() { b(0x0F); b(0x85); b32(0); }
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
            std::unordered_map<std::string, int> slot;
            std::unordered_map<std::string, int> alloca_offset;
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
            std::string target;
        };

        struct StringFixup {
            uint32_t pos;      // where the disp32 goes in .text
            uint32_t blobOff;  // offset within .rdata
        };

        struct StringTable {
            std::vector<uint8_t>                      blob;
            std::unordered_map<std::string, uint32_t> offset;

            uint32_t intern(const std::string& s) {
                auto it = offset.find(s);
                if (it != offset.end()) return it->second;
                uint32_t off = (uint32_t)blob.size();
                offset[s] = off;
                uint64_t n = (uint64_t)s.size();
                for (int i = 0; i < 8; ++i) blob.push_back((uint8_t)(n >> (i * 8)));
                blob.insert(blob.end(), s.begin(), s.end());
                return off;
            }
        };

        class FunctionEmitter {
        public:
            FunctionEmitter(std::vector<uint8_t>& text, const Function& fn,
                const Frame& frame,
                std::vector<CallFixup>& callFixups,
                StringTable& strings,
                std::vector<StringFixup>& stringFixups)
                : text_(text), fn_(fn), frame_(frame),
                callFixups_(callFixups), a_(text),
                strings_(strings), stringFixups_(stringFixups) {
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
            StringTable& strings_;
            std::vector<StringFixup>& stringFixups_;
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
                case OpKind::ConstStr: {
                    uint32_t blobOff = strings_.intern(op.strVal);
                    uint32_t fixPos = (uint32_t)text_.size() + 3;
                    a_.b(0x48); a_.b(0x8D); a_.b(0x05); a_.b32(0);
                    stringFixups_.push_back({ fixPos, blobOff });
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

                case OpKind::FAdd: case OpKind::FSub:
                case OpKind::FMul: case OpKind::FDiv: {
                    load(op.args.at(0), RAX);
                    load(op.args.at(1), RCX);
                    a_.movqXmmFromGpr(0, RAX);
                    a_.movqXmmFromGpr(1, RCX);
                    switch (op.kind) {
                    case OpKind::FAdd: a_.addsd(0, 1); break;
                    case OpKind::FSub: a_.subsd(0, 1); break;
                    case OpKind::FMul: a_.mulsd(0, 1); break;
                    case OpKind::FDiv: a_.divsd(0, 1); break;
                    default: break;
                    }
                    a_.movqGprFromXmm(RAX, 0);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::FNeg: {
                    load(op.args.at(0), RAX);
                    a_.movImm64(RCX, 0x8000000000000000ULL);
                    a_.xorReg(RAX, RCX);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::FCmpLT: case OpKind::FCmpLE:
                case OpKind::FCmpGT: case OpKind::FCmpGE:
                case OpKind::FCmpEQ: case OpKind::FCmpNE: {
                    load(op.args.at(0), RAX);
                    load(op.args.at(1), RCX);
                    a_.movqXmmFromGpr(0, RAX);
                    a_.movqXmmFromGpr(1, RCX);
                    a_.comisd(0, 1);
                    uint8_t opc = 0;
                    switch (op.kind) {
                    case OpKind::FCmpEQ: opc = 0x94; break;
                    case OpKind::FCmpNE: opc = 0x95; break;
                    case OpKind::FCmpLT: opc = 0x92; break;
                    case OpKind::FCmpLE: opc = 0x96; break;
                    case OpKind::FCmpGT: opc = 0x97; break;
                    case OpKind::FCmpGE: opc = 0x93; break;
                    default: break;
                    }
                    a_.setcc(opc, RAX);
                    a_.movzxReg8(RAX, RAX);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::Sitof: {
                    load(op.args.at(0), RAX);
                    a_.cvtsi2sd(0, RAX);
                    a_.movqGprFromXmm(RAX, 0);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::Fptosi: {
                    load(op.args.at(0), RAX);
                    a_.movqXmmFromGpr(0, RAX);
                    a_.cvttsd2si(RAX, 0);
                    store(op.dst, RAX);
                    break;
                }

                case OpKind::Eq: case OpKind::Ne:
                case OpKind::Lt: case OpKind::Le:
                case OpKind::Gt: case OpKind::Ge: {
                    load(op.args.at(0), RAX);
                    load(op.args.at(1), RCX);
                    a_.cmpReg(RAX, RCX);
                    uint8_t opc = 0;
                    switch (op.kind) {
                    case OpKind::Eq: opc = 0x94; break;
                    case OpKind::Ne: opc = 0x95; break;
                    case OpKind::Lt: opc = 0x9C; break;
                    case OpKind::Le: opc = 0x9E; break;
                    case OpKind::Gt: opc = 0x9F; break;
                    case OpKind::Ge: opc = 0x9D; break;
                    default: break;
                    }
                    a_.setcc(opc, RAX);
                    a_.movzxReg8(RAX, RAX);
                    store(op.dst, RAX);
                    break;
                }

                case OpKind::Alloca: {
                    auto it = frame_.alloca_offset.find(op.dst);
                    if (it == frame_.alloca_offset.end())
                        throw std::runtime_error("codegen: alloca has no frame slot");
                    a_.leaRbp(RAX, -(int32_t)it->second);
                    store(op.dst, RAX);
                    break;
                }
                case OpKind::Load:
                    load(op.args.at(0), RAX);
                    a_.movRegIndReg(RAX, RAX);
                    store(op.dst, RAX);
                    break;
                case OpKind::Store:
                    load(op.args.at(0), RAX);
                    load(op.args.at(1), RCX);
                    a_.movIndRegReg(RCX, RAX);
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

                    uint32_t jnzRelPos = (uint32_t)text_.size() + 2;
                    a_.jnzRel32Placeholder();

                    emitPhiStores(op.targetFalse, currentBlock_);
                    uint32_t jmpFPos = (uint32_t)text_.size() + 1;
                    a_.jmpRel32Placeholder();
                    blockFixups_.push_back({ jmpFPos, op.targetFalse });

                    uint32_t here = (uint32_t)text_.size();
                    int32_t rel = (int32_t)here - (int32_t)(jnzRelPos + 4);
                    std::memcpy(&text_[jnzRelPos], &rel, 4);

                    emitPhiStores(op.targetTrue, currentBlock_);
                    uint32_t jmpTPos = (uint32_t)text_.size() + 1;
                    a_.jmpRel32Placeholder();
                    blockFixups_.push_back({ jmpTPos, op.targetTrue });
                    break;
                }
                case OpKind::Phi:
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
                    if (!op.dst.empty()) store(op.dst, RDX == 1 ? RAX : RAX);
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

        struct ImportSpec {
            std::string              dll;
            std::vector<std::string> funcs;
        };

        struct ImportLayout {
            std::vector<uint8_t>                      idata;
            uint32_t                                  importRva = 0;
            uint32_t                                  importSize = 0;
            uint32_t                                  iatRva = 0;
            uint32_t                                  iatSize = 0;
            std::unordered_map<std::string, uint32_t> iatByName;
        };

        ImportLayout buildImports(uint32_t idataRva,
            const std::vector<ImportSpec>& imports) {
            const uint32_t DESC_SIZE = 40;
            uint32_t descTotal = DESC_SIZE * (uint32_t)(imports.size() + 1);

            uint32_t iltTotal = 0, iatTotal = 0;
            for (auto& im : imports) {
                uint32_t n = (uint32_t)im.funcs.size() + 1;
                iltTotal += n * 8;
                iatTotal += n * 8;
            }

            std::vector<uint32_t> hintOffsets;
            uint32_t hintTotal = 0;
            for (auto& im : imports) {
                for (auto& f : im.funcs) {
                    hintOffsets.push_back(hintTotal);
                    uint32_t len = 2 + (uint32_t)f.size() + 1;
                    if (len & 1) ++len;
                    hintTotal += len;
                }
            }

            std::vector<uint32_t> dllNameOffsets;
            uint32_t dllNamesTotal = 0;
            for (auto& im : imports) {
                dllNameOffsets.push_back(dllNamesTotal);
                uint32_t len = (uint32_t)im.dll.size() + 1;
                if (len & 1) ++len;
                dllNamesTotal += len;
            }

            uint32_t offDesc = 0;
            uint32_t offIlt = offDesc + descTotal;
            uint32_t offIat = offIlt + iltTotal;
            uint32_t offHint = offIat + iatTotal;
            uint32_t offDll = offHint + hintTotal;
            uint32_t total = offDll + dllNamesTotal;

            ImportLayout L;
            L.idata.assign(total, 0);
            L.importRva = idataRva + offDesc;
            L.importSize = descTotal;
            L.iatRva = idataRva + offIat;
            L.iatSize = iatTotal;

            auto putU32 = [&](uint32_t at, uint32_t v) {
                L.idata[at + 0] = (uint8_t)(v);
                L.idata[at + 1] = (uint8_t)(v >> 8);
                L.idata[at + 2] = (uint8_t)(v >> 16);
                L.idata[at + 3] = (uint8_t)(v >> 24);
                };
            auto putU64 = [&](uint32_t at, uint64_t v) {
                for (int i = 0; i < 8; ++i) L.idata[at + i] = (uint8_t)(v >> (8 * i));
                };

            uint32_t iltCur = offIlt, iatCur = offIat, hintCur = 0;

            for (size_t di = 0; di < imports.size(); ++di) {
                uint32_t descAt = offDesc + (uint32_t)di * DESC_SIZE;
                uint32_t thisIlt = iltCur;
                uint32_t thisIat = iatCur;
                uint32_t thisDll = idataRva + offDll + dllNameOffsets[di];

                putU32(descAt + 0, idataRva + thisIlt);
                putU32(descAt + 4, 0);
                putU32(descAt + 8, 0);
                putU32(descAt + 12, thisDll);
                putU32(descAt + 16, idataRva + thisIat);

                for (size_t fi = 0; fi < imports[di].funcs.size(); ++fi) {
                    uint32_t hnRva = idataRva + offHint + hintOffsets[hintCur];
                    putU64(thisIlt + (uint32_t)fi * 8, (uint64_t)hnRva);
                    putU64(thisIat + (uint32_t)fi * 8, (uint64_t)hnRva);
                    L.iatByName[imports[di].funcs[fi]] =
                        idataRva + thisIat + (uint32_t)fi * 8;
                    uint32_t hnAt = offHint + hintOffsets[hintCur];
                    L.idata[hnAt + 0] = 0;
                    L.idata[hnAt + 1] = 0;
                    std::memcpy(&L.idata[hnAt + 2],
                        imports[di].funcs[fi].c_str(),
                        imports[di].funcs[fi].size() + 1);
                    ++hintCur;
                }
                iltCur += ((uint32_t)imports[di].funcs.size() + 1) * 8;
                iatCur += ((uint32_t)imports[di].funcs.size() + 1) * 8;
            }
            for (size_t di = 0; di < imports.size(); ++di) {
                std::memcpy(&L.idata[offDll + dllNameOffsets[di]],
                    imports[di].dll.c_str(),
                    imports[di].dll.size() + 1);
            }
            return L;
        }

    } // namespace

    CodegenResult codegenX64Pe(const Module& m) {
        CodegenResult r;
        // .text at 0x1000, .rdata at 0x2000, .idata at 0x3000.
        // SizeOfImage for a 4 KB program is 0x4000 = 16 KB, ratio 4.x.
        const uint32_t textRva = r.textRva;
        const uint32_t textLimit = r.rdataRva - textRva;

        std::vector<ImportSpec> imports = {
            { "kernel32.dll", { "ExitProcess", "GetStdHandle", "WriteFile",
                                "GetProcessHeap", "HeapAlloc", "HeapReAlloc" } }
        };
        ImportLayout layout = buildImports(r.idataRva, imports);

        r.idata = std::move(layout.idata);
        r.importRva = layout.importRva;
        r.importSize = layout.importSize;
        r.iatRva = layout.iatRva;
        r.iatSize = layout.iatSize;

        RuntimeImports ri;
        ri.iatGetStdHandle = layout.iatByName.at("GetStdHandle");
        ri.iatWriteFile = layout.iatByName.at("WriteFile");
        ri.iatExitProcess = layout.iatByName.at("ExitProcess");
        ri.iatGetProcessHeap = layout.iatByName.at("GetProcessHeap");
        ri.iatHeapAlloc = layout.iatByName.at("HeapAlloc");
        ri.iatHeapReAlloc = layout.iatByName.at("HeapReAlloc");

        auto symbolOffsets = emitRuntime(r.text, textRva, ri, m);

        std::vector<CallFixup>    callFixups;
        std::vector<StringFixup>  stringFixups;
        StringTable               strings;
        std::unordered_map<std::string, Frame> frames;

        for (auto& fn : m.functions) {
            if (symbolOffsets.count(fn.name))
                throw std::runtime_error(
                    "codegen: function '" + fn.name +
                    "' conflicts with a runtime symbol");
            frames[fn.name] = layoutFunction(fn);
        }

        // Emit user functions and record (startRva, endRva, frameSize)
        // for each so we can build .pdata / .xdata afterwards.
        struct EmittedFunc {
            uint32_t startRva = 0;
            uint32_t endRva = 0;
            uint32_t frameSize = 0;
        };
        std::vector<EmittedFunc> userFuncs;
        userFuncs.reserve(m.functions.size());

        for (auto& fn : m.functions) {
            uint32_t startOff = (uint32_t)r.text.size();
            symbolOffsets[fn.name] = startOff;
            FunctionEmitter fe(r.text, fn, frames[fn.name],
                callFixups, strings, stringFixups);
            fe.run();
            uint32_t endOff = (uint32_t)r.text.size();
            userFuncs.push_back({
                textRva + startOff,
                textRva + endOff,
                (uint32_t)frames[fn.name].frameSize
                });
        }

        if (!symbolOffsets.count("main"))
            throw std::runtime_error(
                "codegen: no 'main' function defined; cannot build an executable");

        // ---- Entry stub -----------------------------------------------
        // sub rsp, 40
        // call main
        // mov ecx, eax
        // call [ExitProcess]
        // int3
        r.entryOffset = (uint32_t)r.text.size();
        {
            Asm a(r.text);
            a.subRspImm32(40);
            uint32_t callPos = (uint32_t)r.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RCX, RAX);
            uint32_t iatRelPos = (uint32_t)r.text.size() + 2;
            a.callIndirectRip();
            a.int3();
            uint32_t instrRva = textRva + (iatRelPos - 2);
            int32_t  rel = (int32_t)ri.iatExitProcess -
                (int32_t)(instrRva + 6);
            std::memcpy(&r.text[iatRelPos], &rel, 4);
        }

        // ---- String blob goes to .rdata -------------------------------
        r.rdata = strings.blob;

        for (auto& sf : stringFixups) {
            uint32_t blobAbs = r.rdataRva + sf.blobOff;
            int32_t  rel = (int32_t)blobAbs - (int32_t)(sf.pos + 4);
            std::memcpy(&r.text[sf.pos], &rel, 4);
        }

        if (r.text.size() > textLimit)
            throw std::runtime_error(
                "codegen: .text exceeds 4 KB; move more code into .rdata "
                "or add runtime pruning");

        for (auto& cf : callFixups) {
            auto it = symbolOffsets.find(cf.target);
            if (it == symbolOffsets.end())
                throw std::runtime_error(
                    "codegen: undefined function '" + cf.target + "'");
            int32_t rel = (int32_t)it->second - (int32_t)(cf.pos + 4);
            std::memcpy(&r.text[cf.pos], &rel, 4);
        }

        // ---- Build .xdata (UNWIND_INFO blob) and .pdata (RUNTIME_FUNCTION
        //      array) for user functions and the entry stub.
        //
        // Every FunctionEmitter prolog is exactly:
        //     push rbp          ; 1 byte
        //     mov rbp, rsp      ; 3 bytes
        //     sub rsp, frameSize ; 7 bytes (REX.W + 81 /5 imm32)
        // Total 11 bytes.  The entry stub's prolog is only:
        //     sub rsp, 40       ; 7 bytes
        // Total 7 bytes.
        //
        // Runtime emitters do not get .pdata entries in this drop; a
        // fault inside one terminates the process cleanly, which is
        // adequate until runtime unwind metadata lands.
        {
            auto pad4 = [](std::vector<uint8_t>& v) {
                while (v.size() % 4) v.push_back(0);
                };

            auto makeFrameUnwind = [&](uint32_t frameSize) {
                std::vector<uint8_t> info;
                info.push_back(0x01);   // Version=1, Flags=0
                info.push_back(11);     // SizeOfProlog (bytes)
                info.push_back(5);      // CountOfCodes (slots)
                info.push_back(0x05);   // FrameRegister=RBP, FrameOffset=0
                // Code[0..2]: UWOP_ALLOC_LARGE (Op=1, OpInfo=1, 32-bit size)
                info.push_back(4);      // CodeOffset of "sub rsp"
                info.push_back(0x11);   // (OpInfo=1 << 4) | UnwindOp=1
                info.push_back((uint8_t)(frameSize));
                info.push_back((uint8_t)(frameSize >> 8));
                info.push_back((uint8_t)(frameSize >> 16));
                info.push_back((uint8_t)(frameSize >> 24));
                // Code[3]: UWOP_SET_FPREG (Op=3, OpInfo=0)
                info.push_back(1);      // CodeOffset of "mov rbp, rsp"
                info.push_back(0x03);
                // Code[4]: UWOP_PUSH_NONVOL rbp (Op=0, OpInfo=5)
                info.push_back(0);      // CodeOffset of "push rbp"
                info.push_back(0x50);   // (OpInfo=5 << 4) | UnwindOp=0
                pad4(info);
                return info;
                };

            auto makeEntryStubUnwind = [&](uint32_t frameSize) {
                std::vector<uint8_t> info;
                info.push_back(0x01);   // Version=1, Flags=0
                info.push_back(7);      // SizeOfProlog
                info.push_back(1);      // CountOfCodes
                info.push_back(0x00);   // FrameRegister=0, FrameOffset=0
                // Code[0]: UWOP_ALLOC_SMALL (Op=2).  OpInfo = size/8 - 1.
                uint8_t infoBits = (uint8_t)((frameSize / 8) - 1);
                info.push_back(0);      // CodeOffset
                info.push_back((uint8_t)((infoBits << 4) | 2));
                pad4(info);
                return info;
                };

            for (auto& f : userFuncs) {
                std::vector<uint8_t> uw = makeFrameUnwind(f.frameSize);
                UnwindEntry ue;
                ue.funcOffset = f.startRva - textRva;
                ue.funcSize = f.endRva - f.startRva;
                ue.unwindOffset = (uint32_t)r.xdata.size();
                r.xdata.insert(r.xdata.end(), uw.begin(), uw.end());
                r.unwindEntries.push_back(ue);
            }

            // Entry stub (emitted above, size = text.size() - entryOffset).
            {
                std::vector<uint8_t> uw = makeEntryStubUnwind(40);
                UnwindEntry ue;
                ue.funcOffset = r.entryOffset;
                ue.funcSize = (uint32_t)r.text.size() - r.entryOffset;
                ue.unwindOffset = (uint32_t)r.xdata.size();
                r.xdata.insert(r.xdata.end(), uw.begin(), uw.end());
                r.unwindEntries.push_back(ue);
            }

            // .pdata entries must be sorted by BeginAddress.
            std::stable_sort(r.unwindEntries.begin(), r.unwindEntries.end(),
                [](const UnwindEntry& a, const UnwindEntry& b) {
                    return a.funcOffset < b.funcOffset;
                });
        }

        return r;
    }

} // namespace vcb