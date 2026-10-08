#include "vcb/Runtime.hpp"
#include "vcb/X64.hpp"
#include "vcb/X64Common.hpp"
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace vcb {

    namespace {

        struct MiniAsm {
            std::vector<uint8_t>& body;
            uint32_t                                    textRva;
            std::vector<std::pair<size_t, std::string>> shortJumps;
            std::unordered_map<std::string, size_t>     labels;

            // Phase 28.1 -- object-file mode.  Non-null when the caller
            // wants external references captured as relocations.
            std::vector<x64common::Reloc>* relocs = nullptr;
            const std::unordered_map<uint32_t, std::string>* iatNameMap = nullptr;

            MiniAsm(std::vector<uint8_t>& t, uint32_t rva)
                : body(t), textRva(rva) {
            }
            MiniAsm(std::vector<uint8_t>& t, uint32_t rva,
                    const RuntimeImports& im)
                : body(t), textRva(rva),
                  relocs(im.relocs), iatNameMap(im.iatNameMap) {
            }

            void b(uint8_t x) { body.push_back(x); }
            void b32(uint32_t x) {
                for (int i = 0; i < 4; ++i) body.push_back((x >> (i * 8)) & 0xFF);
            }
            void b64(uint64_t x) {
                for (int i = 0; i < 8; ++i) body.push_back((x >> (i * 8)) & 0xFF);
            }
            void label(const std::string& name) { labels[name] = body.size(); }
            void jmpShort(const std::string& l) {
                body.push_back(0xEB);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jzShort(const std::string& l) {
                body.push_back(0x74);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jnzShort(const std::string& l) {
                body.push_back(0x75);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jnsShort(const std::string& l) {
                body.push_back(0x79);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jlShort(const std::string& l) {
                body.push_back(0x7C);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jgeShort(const std::string& l) {
                body.push_back(0x7D);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jgShort(const std::string& l) {
                body.push_back(0x7F);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }
            void jbeShort(const std::string& l) {
                body.push_back(0x76);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }

            void callIat(uint32_t iatRva) {
                if (relocs && iatNameMap) {
                    auto it = iatNameMap->find(iatRva);
                    if (it != iatNameMap->end()) {
                        body.push_back(0xFF);
                        body.push_back(0x15);
                        uint32_t dispOff = (uint32_t)body.size();
                        b32(0);
                        relocs->push_back({ dispOff, it->second,
                            x64common::RelocType::Rel32 });
                        return;
                    }
                }
                uint32_t here = textRva + (uint32_t)body.size();
                body.push_back(0xFF);
                body.push_back(0x15);
                int32_t rel = (int32_t)iatRva - (int32_t)(here + 6);
                b32((uint32_t)rel);
            }
            void callText(uint32_t targetRva) {
                uint32_t here = textRva + (uint32_t)body.size();
                body.push_back(0xE8);
                int32_t rel = (int32_t)targetRva - (int32_t)(here + 5);
                b32((uint32_t)rel);
            }

            void finalize() {
                for (auto& pr : shortJumps) {
                    auto it = labels.find(pr.second);
                    if (it == labels.end())
                        throw std::runtime_error(
                            "runtime: undefined label '" + pr.second + "'");
                    int8_t rel = (int8_t)((int)it->second - ((int)pr.first + 1));
                    body[pr.first] = (uint8_t)rel;
                }
            }
        };

        constexpr uint32_t STD_OUTPUT_HANDLE_M11 = 0xFFFFFFF5u;

        // ---- Windows primitives ------------------------------------------

        void emitExit(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55);                                      // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);               // mov rbp, rsp
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);    // sub rsp, 32 (shadow space)
            a.callIat(im.iatExitProcess);
            a.b(0xCC);                                      // int3 (unreachable)
            a.finalize();
        }

        void emitPrintChar(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x88); a.b(0x4D); a.b(0xF8);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(1);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintLn(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(0x0A);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(1);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintSpace(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(0x20);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(1);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintBool(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jzShort("fp");
            a.b(0x48); a.b(0xB8); a.b64(0x0000000065757274ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(4);
            a.jmpShort("w");
            a.label("fp");
            a.b(0x48); a.b(0xB8); a.b64(0x00000065736C6166ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(5);
            a.label("w");
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintStr(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0xCA);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x01);
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintInt(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(128);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("ak");
            a.b(0x48); a.b(0xF7); a.b(0xD8);
            a.label("ak");
            a.b(0x4C); a.b(0x8D); a.b(0x55); a.b(0xE0);
            a.b(0x48); a.b(0x83); a.b(0xF8); a.b(0x00);
            a.jnzShort("ls");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);
            a.jmpShort("sc");
            a.label("ls");
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);
            a.label("lp");
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF1);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("lp");
            a.label("sc");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("wn");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("wn");
            a.b(0x4C); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8D); a.b(0x45); a.b(0xE0);
            a.b(0x4C); a.b(0x29); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintFloat(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(512);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC1);
            a.b(0x4C); a.b(0x8D); a.b(0x95); a.b32(0xFFFFFFC0u);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("pa");
            a.b(0x48); a.b(0xB9); a.b64(0x8000000000000000ULL);
            a.b(0x48); a.b(0x31); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC0);
            a.label("pa");
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2A); a.b(0xC8);
            a.b(0xF2); a.b(0x0F); a.b(0x5C); a.b(0xC1);
            a.b(0x48); a.b(0xB9); a.b64(0x412E848000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x59); a.b(0xC1);
            a.b(0x48); a.b(0xB9); a.b64(0x3FE0000000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x58); a.b(0xC1);
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0x81); a.b(0xF9); a.b32(1000000);
            a.jnzShort("nc");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0xFF); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.label("nc");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(6);
            a.label("fl");
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF1);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0xFF); a.b(0xCB);
            a.jnzShort("fl");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2E);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jnzShort("is");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);
            a.jmpShort("id");
            a.label("is");
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(10);
            a.label("il");
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF3);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jnzShort("il");
            a.label("id");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("pw");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("pw");
            a.b(0x48); a.b(0x8D); a.b(0x5D); a.b(0xC0);
            a.label("pt");
            a.b(0x48); a.b(0xFF); a.b(0xCB);
            a.b(0x4C); a.b(0x39); a.b(0xD3);
            a.jbeShort("pt_done");
            a.b(0x80); a.b(0x3B); a.b(0x30);
            a.jzShort("pt");
            a.label("pt_done");
            a.b(0x80); a.b(0x3B); a.b(0x2E);
            a.jnzShort("pt_no_dot");
            a.b(0x48); a.b(0xFF); a.b(0xC3);
            a.label("pt_no_dot");
            a.b(0x48); a.b(0x8B); a.b(0xC3);
            a.b(0x48); a.b(0x83); a.b(0xC0); a.b(0x01);
            a.b(0x4C); a.b(0x29); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xC8);
            a.b(0x4C); a.b(0x89); a.b(0x55); a.b(0xD0);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callIat(im.iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xD0);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xC8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callIat(im.iatWriteFile);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // vayu_alloc(n) -- HeapAlloc(GetProcessHeap(), 0, n).
        //   Input:  RCX = n
        //   Output: RAX = pointer, or process exit(1) on failure.
        void emitAlloc(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55);                                    // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);              // mov rbp, rsp
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);   // sub rsp, 48 (32 shadow + 16 locals)
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);   // mov [rbp-8], rcx   ; size
            a.callIat(im.iatGetProcessHeap);              // rax = heap
            a.b(0x48); a.b(0x89); a.b(0xC1);              // mov rcx, rax       ; arg1 = hHeap
            a.b(0x31); a.b(0xD2);                          // xor edx, edx       ; arg2 = dwFlags = 0
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xF8);   // mov r8, [rbp-8]    ; arg3 = dwBytes
            a.callIat(im.iatHeapAlloc);
            a.b(0x48); a.b(0x85); a.b(0xC0);              // test rax, rax
            a.jnzShort("ok");
            a.b(0xB9); a.b32(1);                          // mov ecx, 1
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.label("ok");
            a.b(0xC9); a.b(0xC3);                          // leave; ret
            a.finalize();
        }

        // ---- Strings (PE) ------------------------------------------------

        void emitStrLen(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0xC3);
            a.finalize();
        }

        void emitStrEq(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x39); a.b(0xD1);
            a.jnzShort("diff");
            a.b(0xB8); a.b32(1);
            a.b(0xC3);
            a.label("diff");
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x02);
            a.jnzShort("no");
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("loop");
            a.b(0x49); a.b(0x39); a.b(0xC0);
            a.jgeShort("eq");
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x0C); a.b(0x01);
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x14); a.b(0x02);
            a.b(0x45); a.b(0x39); a.b(0xD1);
            a.jnzShort("no");
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("loop");
            a.label("eq");
            a.b(0xB8); a.b32(1);
            a.b(0xC3);
            a.label("no");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.finalize();
        }

        void emitStrStartsWith(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x4C); a.b(0x8B); a.b(0x02);
            a.b(0x4C); a.b(0x8B); a.b(0x09);
            a.b(0x49); a.b(0x39); a.b(0xC1);
            a.jlShort("sw_false");
            a.b(0x4D); a.b(0x31); a.b(0xD2);
            a.label("sw_loop");
            a.b(0x4D); a.b(0x39); a.b(0xC2);
            a.jgeShort("sw_true");
            a.b(0x42); a.b(0x0F); a.b(0xB6); a.b(0x44); a.b(0x11); a.b(0x08);
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x5C); a.b(0x12); a.b(0x08);
            a.b(0x44); a.b(0x39); a.b(0xD8);
            a.jnzShort("sw_false");
            a.b(0x49); a.b(0xFF); a.b(0xC2);
            a.jmpShort("sw_loop");
            a.label("sw_true");
            a.b(0xB8); a.b32(1);
            a.b(0xC3);
            a.label("sw_false");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.finalize();
        }

        void emitStrEndsWith(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x4C); a.b(0x8B); a.b(0x02);
            a.b(0x4C); a.b(0x8B); a.b(0x09);
            a.b(0x49); a.b(0x39); a.b(0xC1);
            a.jlShort("ew_false");
            a.b(0x4D); a.b(0x29); a.b(0xC1);
            a.b(0x4E); a.b(0x8D); a.b(0x4C); a.b(0x09); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xD2);
            a.label("ew_loop");
            a.b(0x4D); a.b(0x39); a.b(0xC2);
            a.jgeShort("ew_true");
            a.b(0x42); a.b(0x0F); a.b(0xB6); a.b(0x04); a.b(0x11);
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x5C); a.b(0x12); a.b(0x08);
            a.b(0x44); a.b(0x39); a.b(0xD8);
            a.jnzShort("ew_false");
            a.b(0x49); a.b(0xFF); a.b(0xC2);
            a.jmpShort("ew_loop");
            a.label("ew_true");
            a.b(0xB8); a.b32(1);
            a.b(0xC3);
            a.label("ew_false");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.finalize();
        }

        void emitStrContains(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x02);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("nc0");
            a.b(0xB8); a.b32(1);
            a.b(0xC9); a.b(0xC3);
            a.label("nc0");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x48); a.b(0x3B); a.b(0xC1);
            a.jlShort("cf_false");
            a.b(0x48); a.b(0x29); a.b(0xC1);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xE8);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE0); a.b32(0);
            a.label("cf_outer");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x3B); a.b(0x45); a.b(0xE8);
            a.jgShort("cf_false");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);
            a.label("cf_inner");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x48); a.b(0x39); a.b(0x4D); a.b(0xD8);
            a.jgeShort("cf_found");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x03); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x0F); a.b(0xB6); a.b(0x4C); a.b(0x01); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x0F); a.b(0xB6); a.b(0x54); a.b(0x02); a.b(0x08);
            a.b(0x39); a.b(0xD1);
            a.b(0x75); a.jmpShort("cf_next");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xD8);
            a.b(0xEB); a.jmpShort("cf_inner");
            a.label("cf_next");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE0);
            a.b(0xEB); a.jmpShort("cf_outer");
            a.label("cf_found");
            a.b(0xB8); a.b32(1);
            a.b(0xC9); a.b(0xC3);
            a.label("cf_false");
            a.b(0x31); a.b(0xC0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitStrFind(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x02);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("nf0");
            a.b(0x31); a.b(0xC0);
            a.b(0xC9); a.b(0xC3);
            a.label("nf0");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x48); a.b(0x3B); a.b(0xC1);
            a.jlShort("fn_miss");
            a.b(0x48); a.b(0x29); a.b(0xC1);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xE8);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE0); a.b32(0);
            a.label("fn_outer");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x3B); a.b(0x45); a.b(0xE8);
            a.jgShort("fn_miss");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);
            a.label("fn_inner");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x48); a.b(0x39); a.b(0x4D); a.b(0xD8);
            a.jgeShort("fn_hit");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x03); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x0F); a.b(0xB6); a.b(0x4C); a.b(0x01); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x0F); a.b(0xB6); a.b(0x54); a.b(0x02); a.b(0x08);
            a.b(0x39); a.b(0xD1);
            a.b(0x75); a.jmpShort("fn_next");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xD8);
            a.b(0xEB); a.jmpShort("fn_inner");
            a.label("fn_next");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE0);
            a.b(0xEB); a.jmpShort("fn_outer");
            a.label("fn_hit");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0xC9); a.b(0xC3);
            a.label("fn_miss");
            a.b(0x48); a.b(0xC7); a.b(0xC0); a.b32(0xFFFFFFFFu);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitStrUpper(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x83); a.b(0xC0); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x89); a.b(0x10);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("up_loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x4C); a.b(0x3B); a.b(0x01);
            a.jgeShort("up_done");
            a.b(0x42); a.b(0x0F); a.b(0xB6); a.b(0x54); a.b(0x01); a.b(0x08);
            a.b(0x80); a.b(0xFA); a.b(0x61);
            a.jlShort("up_nc");
            a.b(0x80); a.b(0xFA); a.b(0x7A);
            a.jgShort("up_nc");
            a.b(0x80); a.b(0xEA); a.b(0x20);
            a.label("up_nc");
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x42); a.b(0x88); a.b(0x54); a.b(0x02); a.b(0x08);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.b(0xEB); a.jmpShort("up_loop");
            a.label("up_done");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitStrLower(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x83); a.b(0xC0); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x89); a.b(0x10);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("lw_loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x4C); a.b(0x3B); a.b(0x01);
            a.jgeShort("lw_done");
            a.b(0x42); a.b(0x0F); a.b(0xB6); a.b(0x54); a.b(0x01); a.b(0x08);
            a.b(0x80); a.b(0xFA); a.b(0x41);
            a.jlShort("lw_nc");
            a.b(0x80); a.b(0xFA); a.b(0x5A);
            a.jgShort("lw_nc");
            a.b(0x80); a.b(0xC2); a.b(0x20);
            a.label("lw_nc");
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x42); a.b(0x88); a.b(0x54); a.b(0x02); a.b(0x08);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.b(0xEB); a.jmpShort("lw_loop");
            a.label("lw_done");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ---- List (PE) ---------------------------------------------------

        void emitListNew(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0xB9); a.b32(24);
            a.callText(allocRva);
            a.b(0x48); a.b(0xC7); a.b(0x00); a.b32(0);
            a.b(0x48); a.b(0xC7); a.b(0x40); a.b(0x08); a.b32(0);
            a.b(0x48); a.b(0xC7); a.b(0x40); a.b(0x10); a.b32(0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // vayu_list_push(list, value) -- portable (no CRT dependency).
//   list  = [0]=len [8]=cap [16]=data
//   value = i64
// Growth path: alloc(new_cap*8) + byte-copy old data + overwrite
// list.header.  The old buffer is intentionally leaked; VCB
// programs are short-lived and small, and realloc semantics on
// Windows (HeapReAlloc) would require an extra GetProcessHeap
// round-trip inside every push.
        void emitListPush(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);                                      // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);                // mov rbp, rsp
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x60);     // sub rsp, 96
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);     // [rbp-8]  = list
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);     // [rbp-16] = value
            a.b(0x48); a.b(0x8B); a.b(0x01);                // rax = len
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);     // cmp len, cap
            a.jlShort("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x08);     // rdx = old_cap
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE8);     // [rbp-24] = old_cap
            a.b(0x48); a.b(0x85); a.b(0xD2);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got_cap");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xD2);                // add rdx, rdx
            a.label("got_cap");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE0);     // [rbp-32] = new_cap
            a.b(0x48); a.b(0x89); a.b(0xD1);                // rcx = new_cap
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x03);     // shl rcx, 3
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xD8);     // [rbp-40] = new_data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);     // rcx = list
            a.b(0x48); a.b(0x8B); a.b(0x71); a.b(0x10);     // rsi = old_data
            a.b(0x48); a.b(0x8B); a.b(0x7D); a.b(0xD8);     // rdi = new_data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE8);     // rcx = old_cap
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x03);     // shl rcx, 3
            a.b(0x4D); a.b(0x31); a.b(0xC0);                // xor r8, r8
            a.label("cpy");
            a.b(0x49); a.b(0x39); a.b(0xC8);                // cmp r8, rcx
            a.jgeShort("cpy_done");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x06);     // mov al, [rsi+r8]
            a.b(0x42); a.b(0x88); a.b(0x04); a.b(0x07);     // mov [rdi+r8], al
            a.b(0x49); a.b(0xFF); a.b(0xC0);                // inc r8
            a.jmpShort("cpy");
            a.label("cpy_done");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);     // rcx = list
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);     // rax = new_data
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x10);     // [rcx+16] = rax
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);     // rax = new_cap
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x08);     // [rcx+8]  = rax
            a.label("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);     // rcx = list
            a.b(0x48); a.b(0x8B); a.b(0x01);                // rax = len
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x10);     // rdx = data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);     // rcx = value
            a.b(0x48); a.b(0x89); a.b(0x0C); a.b(0xC2);     // [rdx+rax*8] = rcx
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);     // rcx = list
            a.b(0x48); a.b(0xFF); a.b(0x01);                // inc qword [rcx]
            a.b(0xC9); a.b(0xC3);                            // leave; ret
            a.finalize();
        }

        void emitListGet(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x39); a.b(0xC2);
            a.jlShort("ok");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.label("ok");
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x04); a.b(0xD0);
            a.b(0xC3);
            a.finalize();
        }

        void emitListSet(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x39); a.b(0xC2);
            a.jlShort("ok");
            a.b(0xC3);
            a.label("ok");
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x10);
            a.b(0x4C); a.b(0x89); a.b(0x04); a.b(0xD0);
            a.b(0xC3);
            a.finalize();
        }

        void emitListLen(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0xC3);
            a.finalize();
        }

        void emitPrintList(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t printIntRva, uint32_t printCharRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0xB9); a.b(0x5B); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("done");
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jzShort("no_comma");
            a.b(0xB9); a.b(0x2C); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.label("no_comma");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x0C); a.b(0xC8);
            a.callText(printIntRva);
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xF0);
            a.jmpShort("loop");
            a.label("done");
            a.b(0xB9); a.b(0x5D); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ---- Map (PE) ----------------------------------------------------

        void emitMapNew(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0xB9); a.b32(24);
            a.callText(allocRva);
            a.b(0x48); a.b(0xC7); a.b(0x00); a.b32(0);
            a.b(0x48); a.b(0xC7); a.b(0x40); a.b(0x08); a.b32(0);
            a.b(0x48); a.b(0xC7); a.b(0x40); a.b(0x10); a.b32(0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // vayu_map_put(map, key, value) -- portable (no CRT dependency).
//   map   = [0]=len [8]=cap [16]=entries (16-byte {key, value})
//   key   = VayuStr*
//   value = i64
// Growth path mirrors vayu_list_push: alloc new + byte-copy old.
        void emitMapPut(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t strEqRva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);                                        // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);                  // mov rbp, rsp
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(128);      // sub rsp, 128
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);       // [rbp-8]  = map
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);       // [rbp-16] = key
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE8);       // [rbp-24] = value
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE0); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("nf");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.callText(strEqRva);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("found");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE0);
            a.jmpShort("loop");
            a.label("found");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);
            a.b(0xC9); a.b(0xC3);                             // leave; ret
            a.label("nf");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);
            a.jlShort("hr");
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x08);       // rdx = old_cap
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xD8);       // [rbp-40] = old_cap
            a.b(0x48); a.b(0x85); a.b(0xD2);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xD2);                  // add rdx, rdx
            a.label("got");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xD0);       // [rbp-48] = new_cap
            a.b(0x48); a.b(0x89); a.b(0xD1);                  // rcx = new_cap
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x04);       // shl rcx, 4
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xC8);       // [rbp-56] = new_data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x71); a.b(0x10);       // rsi = entries
            a.b(0x48); a.b(0x8B); a.b(0x7D); a.b(0xC8);       // rdi = new_data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);       // rcx = old_cap
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x04);       // shl rcx, 4
            a.b(0x4D); a.b(0x31); a.b(0xC0);                  // xor r8, r8
            a.label("cpy");
            a.b(0x49); a.b(0x39); a.b(0xC8);
            a.jgeShort("cpy_done");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x06);
            a.b(0x42); a.b(0x88); a.b(0x04); a.b(0x07);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("cpy");
            a.label("cpy_done");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x08);
            a.label("hr");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0x11);                  // [rcx]   = key
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);       // [rcx+8] = value
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0xFF); a.b(0x01);                  // inc qword [rcx]
            a.b(0xC9); a.b(0xC3);                             // leave; ret
            a.finalize();
        }

        void emitMapGet(std::vector<uint8_t>& t, uint32_t rva, uint32_t strEqRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE8); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("miss");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.callText(strEqRva);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("hit");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE8);
            a.jmpShort("loop");
            a.label("hit");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x08);
            a.b(0xC9); a.b(0xC3);
            a.label("miss");
            a.b(0x31); a.b(0xC0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitMapHas(std::vector<uint8_t>& t, uint32_t rva, uint32_t strEqRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE8); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("no");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.callText(strEqRva);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("yes");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE8);
            a.jmpShort("loop");
            a.label("yes");
            a.b(0xB8); a.b32(1);
            a.b(0xC9); a.b(0xC3);
            a.label("no");
            a.b(0x31); a.b(0xC0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitMapLen(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0xC3);
            a.finalize();
        }

        void emitPrintMap(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t printIntRva, uint32_t printStrRva,
            uint32_t printCharRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0xB9); a.b(0x7B); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x38); a.b(0x01);
            a.jgeShort("done");
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jzShort("no_comma");
            a.b(0xB9); a.b(0x2C); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.label("no_comma");
            a.b(0xB9); a.b(0x22); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.callText(printStrRva);
            a.b(0xB9); a.b(0x22); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x3A); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x08);
            a.callText(printIntRva);
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xF0);
            a.jmpShort("loop");
            a.label("done");
            a.b(0xB9); a.b(0x7D); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ---- Linux x86-64 syscall runtime --------------------------------
        //   sys_write  = 1   (rdi=fd, rsi=buf, rdx=count)
        //   sys_exit   = 60  (rdi=code)
        // Internal calling convention is unchanged (first arg in RCX).

        // vayu_print_float(bits) -- same algorithm as the Windows version,
        // write path replaced with sys_write.  Buffer at [rbp-0x40] going
        // down; final (rsi, rdx) pair comes from r10 and the length
        // computed after trailing-zero trimming.
        void emitPrintFloatLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(512);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC1);
            a.b(0x4C); a.b(0x8D); a.b(0x95); a.b32(0xFFFFFFC0u);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("pa");
            a.b(0x48); a.b(0xB9); a.b64(0x8000000000000000ULL);
            a.b(0x48); a.b(0x31); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC0);
            a.label("pa");
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2A); a.b(0xC8);
            a.b(0xF2); a.b(0x0F); a.b(0x5C); a.b(0xC1);
            a.b(0x48); a.b(0xB9); a.b64(0x412E848000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x59); a.b(0xC1);
            a.b(0x48); a.b(0xB9); a.b64(0x3FE0000000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x58); a.b(0xC1);
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0x81); a.b(0xF9); a.b32(1000000);
            a.jnzShort("nc");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0xFF); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.label("nc");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(6);
            a.label("fl");
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF1);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0xFF); a.b(0xCB);
            a.jnzShort("fl");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2E);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jnzShort("is");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);
            a.jmpShort("id");
            a.label("is");
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(10);
            a.label("il");
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF3);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jnzShort("il");
            a.label("id");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("pw");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("pw");
            a.b(0x48); a.b(0x8D); a.b(0x5D); a.b(0xC0);
            a.label("pt");
            a.b(0x48); a.b(0xFF); a.b(0xCB);
            a.b(0x4C); a.b(0x39); a.b(0xD3);
            a.jbeShort("pt_done");
            a.b(0x80); a.b(0x3B); a.b(0x30);
            a.jzShort("pt");
            a.label("pt_done");
            a.b(0x80); a.b(0x3B); a.b(0x2E);
            a.jnzShort("pt_no_dot");
            a.b(0x48); a.b(0xFF); a.b(0xC3);
            a.label("pt_no_dot");
            a.b(0x48); a.b(0x8B); a.b(0xC3);
            a.b(0x48); a.b(0x83); a.b(0xC0); a.b(0x01);
            a.b(0x4C); a.b(0x29); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xC8);
            a.b(0x4C); a.b(0x89); a.b(0x55); a.b(0xD0);
            a.b(0xB8); a.b32(1);                              // mov eax, 1  (sys_write)
            a.b(0xBF); a.b32(1);                              // mov edi, 1  (stdout)
            a.b(0x48); a.b(0x8B); a.b(0x75); a.b(0xD0);       // mov rsi, [rbp-0x30]
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xC8);       // mov rdx, [rbp-0x18]
            a.b(0x0F); a.b(0x05);                             // syscall
            a.b(0xC9); a.b(0xC3);                             // leave; ret
            a.finalize();
        }

        // ---- Linux x86-64 syscall runtime --------------------------------
        //   sys_write  = 1   (rdi=fd, rsi=buf, rdx=count)
        //   sys_exit   = 60  (rdi=code)
        // Internal calling convention is unchanged (first arg in RCX).

        // vayu_str_concat(a, b) -> new string.
        //   [0..7]  len(a) + len(b)
        //   [8..]   a.data || b.data
        // Platform-neutral: uses callText(allocRva) and nothing else.
        //   rcx = a  (VayuStr*)
        //   rdx = b  (VayuStr*)
        //   rax = result
        void emitStrConcat(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);                                      // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);                // mov rbp, rsp
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);     // sub rsp, 64
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);     // [rbp-8]  = a
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);     // [rbp-16] = b

            // rax = a.len + b.len
            a.b(0x48); a.b(0x8B); a.b(0x01);                // mov rax, [rcx]
            a.b(0x48); a.b(0x03); a.b(0x02);                // add rax, [rdx]
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE8);     // [rbp-24] = total

            // rcx = total + 8;  vayu_alloc(rcx)
            a.b(0x48); a.b(0x89); a.b(0xC1);                // mov rcx, rax
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);     // add rcx, 8
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);     // [rbp-32] = new_s

            // new_s.len = total
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE8);     // mov rcx, [rbp-24]
            a.b(0x48); a.b(0x89); a.b(0x08);                // mov [rax], rcx

            // ---- copy a.data -> new_s.data ----
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF8);     // mov rdx, [rbp-8]
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);     // add rdx, 8       ; rdx = a.data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);     // mov rcx, [rbp-8]
            a.b(0x48); a.b(0x8B); a.b(0x09);                // mov rcx, [rcx]   ; rcx = a.len
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xE0);     // mov r10, [rbp-32]
            a.b(0x49); a.b(0x83); a.b(0xC2); a.b(0x08);     // add r10, 8       ; r10 = new_s.data
            a.b(0x4D); a.b(0x31); a.b(0xC0);                // xor r8, r8       ; r8 = 0
            a.label("cc1");
            a.b(0x4C); a.b(0x3B); a.b(0xC1);                // cmp r8, rcx      ; index vs a.len
            a.jgeShort("cc1e");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x02);     // mov al, [rdx+r8]
            a.b(0x43); a.b(0x88); a.b(0x04); a.b(0x02);     // mov [r10+r8], al
            a.b(0x49); a.b(0xFF); a.b(0xC0);                // inc r8
            a.jmpShort("cc1");
            a.label("cc1e");

            // ---- copy b.data -> new_s.data + a.len ----
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);     // mov rdx, [rbp-16]
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);     // add rdx, 8       ; rdx = b.data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);     // mov rcx, [rbp-16]
            a.b(0x48); a.b(0x8B); a.b(0x09);                // mov rcx, [rcx]   ; rcx = b.len
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xE0);     // mov r10, [rbp-32]
            a.b(0x49); a.b(0x83); a.b(0xC2); a.b(0x08);     // add r10, 8       ; r10 = new_s.data
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);     // mov rax, [rbp-8]
            a.b(0x48); a.b(0x8B); a.b(0x00);                // mov rax, [rax]   ; rax = a.len
            a.b(0x49); a.b(0x01); a.b(0xC2);                // add r10, rax
            a.b(0x4D); a.b(0x31); a.b(0xC0);                // xor r8, r8
            a.label("cc2");
            a.b(0x4C); a.b(0x3B); a.b(0xC1);                // cmp r8, rcx
            a.jgeShort("cc2e");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x02);     // mov al, [rdx+r8]
            a.b(0x43); a.b(0x88); a.b(0x04); a.b(0x02);     // mov [r10+r8], al
            a.b(0x49); a.b(0xFF); a.b(0xC0);                // inc r8
            a.jmpShort("cc2");
            a.label("cc2e");

            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);     // mov rax, [rbp-32]
            a.b(0xC9); a.b(0xC3);                            // leave; ret
            a.finalize();
        }

        void emitExitLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x89); a.b(0xCF);       // mov rdi, rcx
            a.b(0xB8); a.b32(60);                  // mov eax, 60
            a.b(0x0F); a.b(0x05);                  // syscall
            a.b(0xCC);
            a.finalize();
        }

        void emitPrintCharLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0x88); a.b(0x4D); a.b(0xF8);       // mov [rbp-8], cl
            a.b(0xB8); a.b32(1);                   // mov eax, 1
            a.b(0xBF); a.b32(1);                   // mov edi, 1
            a.b(0x48); a.b(0x8D); a.b(0x75); a.b(0xF8);  // lea rsi, [rbp-8]
            a.b(0xBA); a.b32(1);                   // mov edx, 1
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintLnLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(0x0A);
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x48); a.b(0x8D); a.b(0x75); a.b(0xF8);
            a.b(0xBA); a.b32(1);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintSpaceLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(0x20);
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x48); a.b(0x8D); a.b(0x75); a.b(0xF8);
            a.b(0xBA); a.b32(1);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintBoolLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0x48); a.b(0x85); a.b(0xC9);       // test rcx, rcx
            a.jzShort("l_false");
            a.b(0xC7); a.b(0x45); a.b(0xF8); a.b(0x74); a.b(0x72); a.b(0x75); a.b(0x65);
            a.b(0xBA); a.b32(4);
            a.jmpShort("l_write");
            a.label("l_false");
            a.b(0xC7); a.b(0x45); a.b(0xF8); a.b(0x66); a.b(0x61); a.b(0x6C); a.b(0x73);
            a.b(0xC6); a.b(0x45); a.b(0xFC); a.b(0x65);
            a.b(0xBA); a.b32(5);
            a.label("l_write");
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x48); a.b(0x8D); a.b(0x75); a.b(0xF8);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintStrLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8); // mov [rbp-8], rcx
            a.b(0x48); a.b(0x8B); a.b(0x11);       // mov rdx, [rcx]      len
            a.b(0x48); a.b(0x8D); a.b(0x71); a.b(0x08);  // lea rsi, [rcx+8] data
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitPrintIntLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0xC8);       // mov rax, rcx
            a.b(0x45); a.b(0x31); a.b(0xDB);       // xor r11d, r11d
            a.b(0x48); a.b(0x85); a.b(0xC0);       // test rax, rax
            a.jnsShort("li_abs");
            a.b(0x41); a.b(0xBB); a.b32(1);        // mov r11d, 1
            a.b(0x48); a.b(0xF7); a.b(0xD8);       // neg rax
            a.label("li_abs");
            a.b(0x4C); a.b(0x89); a.b(0xEA);       // mov r10, rbp
            a.b(0x48); a.b(0x85); a.b(0xC0);       // test rax, rax
            a.jnzShort("li_loop");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);
            a.jmpShort("li_emit");
            a.label("li_loop");
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);
            a.label("li_loop_top");
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF1);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("li_loop_top");
            a.label("li_emit");
            a.b(0x45); a.b(0x85); a.b(0xDB);
            a.jzShort("li_write");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("li_write");
            a.b(0x4C); a.b(0x89); a.b(0xD6);       // mov rsi, r10
            a.b(0x48); a.b(0x89); a.b(0xEA);       // mov rdx, rbp
            a.b(0x4C); a.b(0x29); a.b(0xD2);       // sub rdx, r10
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // vayu_alloc(n) -- Linux bump allocator over brk(2).
        //   Input:  RCX = n (bytes)
        //   Output: RAX = pointer to n bytes
        // On brk failure, exits with status 1 via sys_exit (no libc).
        void emitAllocLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);                                    // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);              // mov rbp, rsp
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);   // sub rsp, 16
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x0F);   // add rcx, 15
            a.b(0x48); a.b(0x83); a.b(0xE1); a.b(0xF0);   // and rcx, -16
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);   // mov [rbp-8], rcx
            a.b(0x31); a.b(0xFF);                          // xor edi, edi
            a.b(0xB8); a.b32(12);                          // mov eax, 12 (SYS_brk)
            a.b(0x0F); a.b(0x05);                          // syscall
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF0);   // mov [rbp-16], rax
            a.b(0x48); a.b(0x89); a.b(0xC7);              // mov rdi, rax
            a.b(0x48); a.b(0x03); a.b(0x7D); a.b(0xF8);   // add rdi, [rbp-8]
            a.b(0xB8); a.b32(12);                          // mov eax, 12
            a.b(0x0F); a.b(0x05);                          // syscall
            a.b(0x48); a.b(0x39); a.b(0xF8);              // cmp rax, rdi
            a.jgeShort("ok");
            a.b(0xBF); a.b32(1);                           // mov edi, 1
            a.b(0xB8); a.b32(60);                          // mov eax, 60
            a.b(0x0F); a.b(0x05);                          // syscall
            a.b(0xCC);                                     // int3
            a.label("ok");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);   // mov rax, [rbp-16]
            a.b(0xC9); a.b(0xC3);                          // leave; ret
            a.finalize();
        }

    } // namespace

    // =========================================================================
    // emitRuntime (PE)
    // =========================================================================

    std::unordered_map<std::string, uint32_t> emitRuntime(
        std::vector<uint8_t>& text, uint32_t textRva,
        const RuntimeImports& im, const Module& userModule,
        std::vector<UnwindEntry>* outUnwinds)
    {
        std::unordered_set<std::string> need;
        for (auto& fn : userModule.functions)
            for (auto& blk : fn.blocks)
                for (auto& op : blk.ops)
                    if (op.kind == OpKind::Call && !op.callee.empty())
                        need.insert(op.callee);

        auto depends = [](const std::string& s) -> std::vector<std::string> {
            if (s == "vayu_list_new")   return { "vayu_alloc" };
            if (s == "vayu_list_push")  return { "vayu_alloc" };
            if (s == "vayu_map_new")    return { "vayu_alloc" };
            if (s == "vayu_map_put")    return { "vayu_alloc", "vayu_str_eq" };
            if (s == "vayu_map_get")    return { "vayu_str_eq" };
            if (s == "vayu_map_has")    return { "vayu_str_eq" };
            if (s == "vayu_print_list") return { "vayu_print_int",
                                             "vayu_print_char" };
            if (s == "vayu_print_map")  return { "vayu_print_int",
                                             "vayu_print_str",
                                             "vayu_print_char" };
            if (s == "vayu_str_concat") return { "vayu_alloc" };
            if (s == "vayu_str_upper")  return { "vayu_alloc" };
            if (s == "vayu_str_lower")  return { "vayu_alloc" };
            (void)s;
            return {};
            };

        std::vector<std::string> stack(need.begin(), need.end());
        while (!stack.empty()) {
            std::string s = stack.back(); stack.pop_back();
            for (auto& d : depends(s))
                if (need.insert(d).second) stack.push_back(d);
        }

        auto has = [&](const char* n) { return need.count(n) > 0; };

        std::unordered_map<std::string, uint32_t> syms;
        auto mark = [&](const char* name) -> uint32_t {
            uint32_t off = (uint32_t)text.size();
            syms[name] = off;
            return textRva + off;
            };

        // Emit a runtime function and, if it has a non-empty frame
        // prolog (frameSize != 0), record an unwind entry.
        auto emitAndMark = [&](const char* name, uint32_t frameSize,
            auto&& body) -> uint32_t {
                uint32_t before = (uint32_t)text.size();
                uint32_t rva = mark(name);
                body();
                if (outUnwinds && frameSize != 0) {
                    UnwindEntry ue;
                    ue.funcOffset = before;
                    ue.funcSize = (uint32_t)text.size() - before;
                    ue.frameSize = frameSize;
                    ue.stubOnly = false;
                    outUnwinds->push_back(ue);
                }
                return rva;
            };

        uint32_t printCharRva = 0, printStrRva = 0, printIntRva = 0;
        uint32_t allocRva = 0, strEqRva = 0;

        if (has("vayu_exit"))
            emitAndMark("vayu_exit", 32,
                [&] { emitExit(text, textRva, im); });
        if (has("vayu_print_char"))
            printCharRva = emitAndMark("vayu_print_char", 64,
                [&] { emitPrintChar(text, textRva, im); });
        if (has("vayu_print_ln"))
            emitAndMark("vayu_print_ln", 64,
                [&] { emitPrintLn(text, textRva, im); });
        if (has("vayu_print_space"))
            emitAndMark("vayu_print_space", 64,
                [&] { emitPrintSpace(text, textRva, im); });
        if (has("vayu_print_bool"))
            emitAndMark("vayu_print_bool", 64,
                [&] { emitPrintBool(text, textRva, im); });
        if (has("vayu_print_str"))
            printStrRva = emitAndMark("vayu_print_str", 64,
                [&] { emitPrintStr(text, textRva, im); });
        if (has("vayu_print_int"))
            printIntRva = emitAndMark("vayu_print_int", 128,
                [&] { emitPrintInt(text, textRva, im); });
        if (has("vayu_print_float"))
            emitAndMark("vayu_print_float", 512,
                [&] { emitPrintFloat(text, textRva, im); });
        if (has("vayu_alloc"))
            allocRva = emitAndMark("vayu_alloc", 48,
                [&] { emitAlloc(text, textRva, im); });

        // String helpers with no prolog (leaf) or with a prolog.
        if (has("vayu_str_len"))
            emitAndMark("vayu_str_len", 0,
                [&] { emitStrLen(text, textRva); });
        if (has("vayu_str_eq"))
            strEqRva = emitAndMark("vayu_str_eq", 0,
                [&] { emitStrEq(text, textRva); });
        if (has("vayu_str_starts_with"))
            emitAndMark("vayu_str_starts_with", 0,
                [&] { emitStrStartsWith(text, textRva); });
        if (has("vayu_str_ends_with"))
            emitAndMark("vayu_str_ends_with", 0,
                [&] { emitStrEndsWith(text, textRva); });
        if (has("vayu_str_contains"))
            emitAndMark("vayu_str_contains", 48,
                [&] { emitStrContains(text, textRva); });
        if (has("vayu_str_find"))
            emitAndMark("vayu_str_find", 48,
                [&] { emitStrFind(text, textRva); });
        if (has("vayu_str_concat"))
            emitAndMark("vayu_str_concat", 64,
                [&] { emitStrConcat(text, textRva, allocRva); });
        if (has("vayu_str_upper"))
            emitAndMark("vayu_str_upper", 48,
                [&] { emitStrUpper(text, textRva, allocRva); });
        if (has("vayu_str_lower"))
            emitAndMark("vayu_str_lower", 48,
                [&] { emitStrLower(text, textRva, allocRva); });

        if (has("vayu_list_new"))
            emitAndMark("vayu_list_new", 32,
                [&] { emitListNew(text, textRva, allocRva); });
        if (has("vayu_list_push"))
            emitAndMark("vayu_list_push", 96,
                [&] { emitListPush(text, textRva, allocRva); });
        if (has("vayu_list_get"))
            emitAndMark("vayu_list_get", 0,
                [&] { emitListGet(text, textRva); });
        if (has("vayu_list_set"))
            emitAndMark("vayu_list_set", 0,
                [&] { emitListSet(text, textRva); });
        if (has("vayu_list_len"))
            emitAndMark("vayu_list_len", 0,
                [&] { emitListLen(text, textRva); });

        if (has("vayu_map_new"))
            emitAndMark("vayu_map_new", 32,
                [&] { emitMapNew(text, textRva, allocRva); });
        if (has("vayu_map_put"))
            emitAndMark("vayu_map_put", 96,
                [&] { emitMapPut(text, textRva, strEqRva, allocRva); });
        if (has("vayu_map_get"))
            emitAndMark("vayu_map_get", 48,
                [&] { emitMapGet(text, textRva, strEqRva); });
        if (has("vayu_map_has"))
            emitAndMark("vayu_map_has", 48,
                [&] { emitMapHas(text, textRva, strEqRva); });
        if (has("vayu_map_len"))
            emitAndMark("vayu_map_len", 0,
                [&] { emitMapLen(text, textRva); });

        if (has("vayu_print_list"))
            emitAndMark("vayu_print_list", 64,
                [&] { emitPrintList(text, textRva, printIntRva, printCharRva); });
        if (has("vayu_print_map"))
            emitAndMark("vayu_print_map", 64,
                [&] { emitPrintMap(text, textRva, printIntRva, printStrRva,
                    printCharRva); });

        return syms;
    }
    // =========================================================================
    // emitRuntimeLinux -- Part 2: heap + collections + string methods.
    // vayu_print_float is still not implemented on Linux; a module that
    // calls it will fail the call-fixup stage with an "undefined
    // function" error rather than producing silently wrong output.
    //
    // Emission order is a topological sort of the runtime's call graph:
    //   exit / print_*
    //   alloc
    //   str_len / str_eq / str_starts_with / str_ends_with /
    //       str_contains / str_find
    //   str_upper / str_lower
    //   list_*
    //   map_*
    //   print_list / print_map
    // Every function that does callText(...) to another runtime symbol is
    // emitted after that symbol, so `mark()` always sees a live RVA.
    // =========================================================================

        // =========================================================================
    // emitRuntimeLinux -- full Linux runtime: heap (brk bump), strings,
    // lists, maps, print variants.  No CRT, no IATs.
    // =========================================================================

    std::unordered_map<std::string, uint32_t> emitRuntimeLinux(
        std::vector<uint8_t>& text, uint32_t textRva, const Module& userModule)
    {
        std::unordered_set<std::string> need;
        for (auto& fn : userModule.functions)
            for (auto& blk : fn.blocks)
                for (auto& op : blk.ops)
                    if (op.kind == OpKind::Call && !op.callee.empty())
                        need.insert(op.callee);

        auto depends = [](const std::string& s) -> std::vector<std::string> {
            if (s == "vayu_list_new")   return { "vayu_alloc" };
            if (s == "vayu_list_push")  return { "vayu_alloc" };
            if (s == "vayu_map_new")    return { "vayu_alloc" };
            if (s == "vayu_map_put")    return { "vayu_alloc", "vayu_str_eq" };
            if (s == "vayu_map_get")    return { "vayu_str_eq" };
            if (s == "vayu_map_has")    return { "vayu_str_eq" };
            if (s == "vayu_print_list") return { "vayu_print_int",
                                             "vayu_print_char" };
            if (s == "vayu_print_map")  return { "vayu_print_int",
                                             "vayu_print_str",
                                             "vayu_print_char" };
            if (s == "vayu_str_concat") return { "vayu_alloc" };
            if (s == "vayu_str_upper")  return { "vayu_alloc" };
            if (s == "vayu_str_lower")  return { "vayu_alloc" };
            (void)s;
            return {};
            };

        std::vector<std::string> stack(need.begin(), need.end());
        while (!stack.empty()) {
            std::string s = stack.back(); stack.pop_back();
            for (auto& d : depends(s))
                if (need.insert(d).second) stack.push_back(d);
        }

        auto has = [&](const char* n) { return need.count(n) > 0; };

        std::unordered_map<std::string, uint32_t> syms;
        auto mark = [&](const char* name) -> uint32_t {
            uint32_t off = (uint32_t)text.size();
            syms[name] = off;
            return textRva + off;
            };

        uint32_t printCharRva = 0, printStrRva = 0, printIntRva = 0;
        uint32_t allocRva = 0, strEqRva = 0;

        if (has("vayu_exit")) { mark("vayu_exit");         emitExitLinux(text, textRva); }
        if (has("vayu_print_char")) { printCharRva = mark("vayu_print_char"); emitPrintCharLinux(text, textRva); }
        if (has("vayu_print_ln")) { mark("vayu_print_ln");     emitPrintLnLinux(text, textRva); }
        if (has("vayu_print_space")) { mark("vayu_print_space");  emitPrintSpaceLinux(text, textRva); }
        if (has("vayu_print_bool")) { mark("vayu_print_bool");   emitPrintBoolLinux(text, textRva); }
        if (has("vayu_print_str")) { printStrRva = mark("vayu_print_str"); emitPrintStrLinux(text, textRva); }
        if (has("vayu_print_int")) { printIntRva = mark("vayu_print_int"); emitPrintIntLinux(text, textRva); }
        if (has("vayu_print_float")) { mark("vayu_print_float");  emitPrintFloatLinux(text, textRva); }
        if (has("vayu_alloc")) { allocRva = mark("vayu_alloc"); emitAllocLinux(text, textRva); }
        if (has("vayu_str_len")) { mark("vayu_str_len");      emitStrLen(text, textRva); }
        if (has("vayu_str_eq")) { strEqRva = mark("vayu_str_eq"); emitStrEq(text, textRva); }
        if (has("vayu_str_starts_with")) { mark("vayu_str_starts_with"); emitStrStartsWith(text, textRva); }
        if (has("vayu_str_ends_with")) { mark("vayu_str_ends_with");   emitStrEndsWith(text, textRva); }
        if (has("vayu_str_contains")) { mark("vayu_str_contains");    emitStrContains(text, textRva); }
        if (has("vayu_str_find")) { mark("vayu_str_find");        emitStrFind(text, textRva); }
        if (has("vayu_str_concat")) { mark("vayu_str_concat");      emitStrConcat(text, textRva, allocRva); }
        if (has("vayu_str_upper")) { mark("vayu_str_upper");       emitStrUpper(text, textRva, allocRva); }
        if (has("vayu_str_lower")) { mark("vayu_str_lower");       emitStrLower(text, textRva, allocRva); }
        if (has("vayu_list_new")) { mark("vayu_list_new");        emitListNew(text, textRva, allocRva); }
        if (has("vayu_list_push")) { mark("vayu_list_push");       emitListPush(text, textRva, allocRva); }
        if (has("vayu_list_get")) { mark("vayu_list_get");        emitListGet(text, textRva); }
        if (has("vayu_list_set")) { mark("vayu_list_set");        emitListSet(text, textRva); }
        if (has("vayu_list_len")) { mark("vayu_list_len");        emitListLen(text, textRva); }
        if (has("vayu_map_new")) { mark("vayu_map_new");         emitMapNew(text, textRva, allocRva); }
        if (has("vayu_map_put")) { mark("vayu_map_put");         emitMapPut(text, textRva, strEqRva, allocRva); }
        if (has("vayu_map_get")) { mark("vayu_map_get");         emitMapGet(text, textRva, strEqRva); }
        if (has("vayu_map_has")) { mark("vayu_map_has");         emitMapHas(text, textRva, strEqRva); }
        if (has("vayu_map_len")) { mark("vayu_map_len");         emitMapLen(text, textRva); }
        if (has("vayu_print_list")) { mark("vayu_print_list");      emitPrintList(text, textRva, printIntRva, printCharRva); }
        if (has("vayu_print_map")) { mark("vayu_print_map");       emitPrintMap(text, textRva, printIntRva, printStrRva, printCharRva); }

        return syms;
    }

} // namespace vcb