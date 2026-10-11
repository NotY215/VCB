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
            void jleShort(const std::string& l) {
                body.push_back(0x7E);
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

        // ====================================================================
        // Windows primitives
        // ====================================================================

        void emitExit(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
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

        void emitAlloc(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva, im);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x30);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.callIat(im.iatGetProcessHeap);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x31); a.b(0xD2);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.callIat(im.iatHeapAlloc);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("ok");
            a.b(0xB9); a.b32(1);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.label("ok");
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ====================================================================
        // Shared string helpers
        // ====================================================================

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

        void emitStrConcat(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x03); a.b(0x02);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF8);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xE0);
            a.b(0x49); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("cc1");
            a.b(0x4C); a.b(0x3B); a.b(0xC1);
            a.jgeShort("cc1e");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x02);
            a.b(0x43); a.b(0x88); a.b(0x04); a.b(0x02);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("cc1");
            a.label("cc1e");
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x4C); a.b(0x8B); a.b(0x55); a.b(0xE0);
            a.b(0x49); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x00);
            a.b(0x49); a.b(0x01); a.b(0xC2);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("cc2");
            a.b(0x4C); a.b(0x3B); a.b(0xC1);
            a.jgeShort("cc2e");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x02);
            a.b(0x43); a.b(0x88); a.b(0x04); a.b(0x02);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("cc2");
            a.label("cc2e");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ====================================================================
        // Windows list / map runtime
        // ====================================================================

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

        void emitListPush(std::vector<uint8_t>& t, uint32_t rva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x60);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);
            a.jlShort("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x85); a.b(0xD2);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got_cap");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xD2);
            a.label("got_cap");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE0);
            a.b(0x48); a.b(0x89); a.b(0xD1);
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x03);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x71); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x7D); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE8);
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x03);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("cpy");
            a.b(0x49); a.b(0x39); a.b(0xC8);
            a.jgeShort("cpy_done");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x06);
            a.b(0x42); a.b(0x88); a.b(0x04); a.b(0x07);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("cpy");
            a.label("cpy_done");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x08);
            a.label("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0x0C); a.b(0xC2);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0xFF); a.b(0x01);
            a.b(0xC9); a.b(0xC3);
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

        void emitMapPut(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t strEqRva, uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(128);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE8);
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
            a.b(0xC9); a.b(0xC3);
            a.label("nf");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);
            a.jlShort("hr");
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xD8);
            a.b(0x48); a.b(0x85); a.b(0xD2);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xD2);
            a.label("got");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0xD1);
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x04);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xC8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x71); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x7D); a.b(0xC8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0xC1); a.b(0xE1); a.b(0x04);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
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
            a.b(0x48); a.b(0x89); a.b(0x11);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0xFF); a.b(0x01);
            a.b(0xC9); a.b(0xC3);
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

        // ====================================================================
        // Phase 30 -- integer exponentiation
        // ====================================================================

        void emitPowInt(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0xB8); a.b(0x01); a.b(0x00); a.b(0x00); a.b(0x00); // mov eax, 1
            a.label("loop");
            a.b(0x48); a.b(0x85); a.b(0xD2);                     // test rdx, rdx
            a.jzShort("done");
            a.b(0xF6); a.b(0xC2); a.b(0x01);                     // test dl, 1
            a.jzShort("skip");
            a.b(0x48); a.b(0x0F); a.b(0xAF); a.b(0xC1);          // imul rax, rcx
            a.label("skip");
            a.b(0x48); a.b(0x0F); a.b(0xAF); a.b(0xC9);          // imul rcx, rcx
            a.b(0x48); a.b(0xD1); a.b(0xEA);                     // shr rdx, 1
            a.jmpShort("loop");
            a.label("done");
            a.b(0xC3);
            a.finalize();
        }

        // ====================================================================
        // Phase 30.2 + 31 -- str_slice, int_to_str, bool_to_str, str_to_int,
        // tuple_*, set_add, set_contains
        // ====================================================================

        void emitStrSlice(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x50);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jnsShort("a_nn");
            a.b(0x48); a.b(0x31); a.b(0xC9);
            a.label("a_nn");
            a.b(0x48); a.b(0x3B); a.b(0x4D); a.b(0xE0);
            a.jleShort("a_ok");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE0);
            a.label("a_ok");
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x3B); a.b(0x55); a.b(0xF0);
            a.jgeShort("b_ok1");
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.label("b_ok1");
            a.b(0x48); a.b(0x3B); a.b(0x55); a.b(0xE0);
            a.jleShort("b_ok2");
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE0);
            a.label("b_ok2");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0xD0);
            a.b(0x48); a.b(0x2B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xD0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);
            a.b(0x48); a.b(0x89); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.b(0x48); a.b(0x03); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xD0);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("sl");
            a.b(0x4C); a.b(0x3B); a.b(0x45); a.b(0xD8);
            a.jgeShort("sld");
            a.b(0x42); a.b(0x8A); a.b(0x04); a.b(0x01);
            a.b(0x42); a.b(0x88); a.b(0x04); a.b(0x02);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("sl");
            a.label("sld");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitIntToStr(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x50);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(0);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("nn");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(1);
            a.b(0x48); a.b(0xF7); a.b(0xD8);
            a.label("nn");
            a.b(0x4C); a.b(0x8D); a.b(0x55); a.b(0xD8);
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(10);
            a.label("dl");
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF3);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("dl");
            a.b(0x48); a.b(0x83); a.b(0x7D); a.b(0xF0); a.b(0x00);
            a.jzShort("ns");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("ns");
            a.b(0x48); a.b(0x8D); a.b(0x4D); a.b(0xD8);
            a.b(0x4C); a.b(0x29); a.b(0xD1);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.callText(allocRva);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xD0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0x89); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xD0);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("cl");
            a.b(0x4C); a.b(0x3B); a.b(0x45); a.b(0xE0);
            a.jgeShort("cd");
            a.b(0x47); a.b(0x8A); a.b(0x04); a.b(0x02);
            a.b(0x42); a.b(0x88); a.b(0x04); a.b(0x02);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("cl");
            a.label("cd");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD0);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitBoolToStr(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jzShort("bf");
            a.b(0xB9); a.b32(12);
            a.callText(allocRva);
            a.b(0x48); a.b(0xC7); a.b(0x00); a.b32(4);
            a.b(0xC7); a.b(0x40); a.b(0x08); a.b32(0x65757274);
            a.b(0xC9); a.b(0xC3);
            a.label("bf");
            a.b(0xB9); a.b32(13);
            a.callText(allocRva);
            a.b(0x48); a.b(0xC7); a.b(0x00); a.b32(5);
            a.b(0xC7); a.b(0x40); a.b(0x08); a.b32(0x736c6166);
            a.b(0xC6); a.b(0x40); a.b(0x0C); a.b(0x65);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitStrToInt(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.b(0x4D); a.b(0x31); a.b(0xC9);
            a.b(0x4D); a.b(0x31); a.b(0xD2);
            a.label("sw");
            a.b(0x4C); a.b(0x39); a.b(0xC2);
            a.jleShort("sd");
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x1C); a.b(0x01);
            a.b(0x80); a.b(0xFB); a.b(0x20);
            a.jneShort("sd");
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("sw");
            a.label("sd");
            a.b(0x4C); a.b(0x39); a.b(0xC2);
            a.jleShort("pn");
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x1C); a.b(0x01);
            a.b(0x80); a.b(0xFB); a.b(0x2D);
            a.jneShort("pn");
            a.b(0x49); a.b(0xFF); a.b(0xC2);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.label("pn");
            a.label("dl");
            a.b(0x4C); a.b(0x39); a.b(0xC2);
            a.jleShort("dn");
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x1C); a.b(0x01);
            a.b(0x80); a.b(0xFB); a.b(0x30);
            a.jlShort("dn");
            a.b(0x80); a.b(0xFB); a.b(0x39);
            a.jgShort("dn");
            a.b(0x4D); a.b(0x6B); a.b(0xC9); a.b(0x0A);
            a.b(0x0F); a.b(0xB6); a.b(0xDB);
            a.b(0x49); a.b(0x83); a.b(0xEB); a.b(0x30);
            a.b(0x4D); a.b(0x01); a.b(0xD9);
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("dl");
            a.label("dn");
            a.b(0x4C); a.b(0x89); a.b(0xC8);
            a.b(0x4D); a.b(0x85); a.b(0xD2);
            a.jzShort("rt");
            a.b(0x48); a.b(0xF7); a.b(0xD8);
            a.label("rt");
            a.b(0xC3);
            a.finalize();
        }

        // ---- Tuple: [0]=len, [8+8*i]=item i ------------------------------

        void emitTupleNew(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t allocRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x03);
            a.b(0x48); a.b(0x83); a.b(0xC0); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.callText(allocRva);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x08);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitTupleSet(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x4C); a.b(0x89); a.b(0x44); a.b(0xD1); a.b(0x08);
            a.b(0xC3);
            a.finalize();
        }

        void emitTupleGet(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x44); a.b(0xD1); a.b(0x08);
            a.b(0xC3);
            a.finalize();
        }

        void emitTupleLen(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0xC3);
            a.finalize();
        }

        // ---- Set: same layout as list ([len][cap][data]) -----------------

        void emitSetAdd(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t listPushRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE8); a.b32(0);
            a.label("sl");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("sa");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x0C); a.b(0xC1);
            a.b(0x48); a.b(0x3B); a.b(0x4D); a.b(0xF0);
            a.jeShort("sd");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE8);
            a.jmpShort("sl");
            a.label("sa");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.callText(listPushRva);
            a.label("sd");
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitSetContains(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x4C); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x4D); a.b(0x31); a.b(0xC0);
            a.label("sl");
            a.b(0x4C); a.b(0x39); a.b(0xC0);
            a.jleShort("no");
            a.b(0x4F); a.b(0x8B); a.b(0x14); a.b(0xC1);
            a.b(0x49); a.b(0x39); a.b(0xD2);
            a.jeShort("yes");
            a.b(0x49); a.b(0xFF); a.b(0xC0);
            a.jmpShort("sl");
            a.label("yes");
            a.b(0xB8); a.b(0x01); a.b(0x00); a.b(0x00); a.b(0x00);
            a.b(0xC3);
            a.label("no");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.finalize();
        }

        // Phase 31.2 -- vayu_print_tuple.  Same shape as emitPrintList,
        // but tuple layout is [len][data] (data starts at +8).
        void emitPrintTuple(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t printIntRva, uint32_t printCharRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0xB9); a.b(0x28); a.b(0x00); a.b(0x00); a.b(0x00);
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
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x08);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0x8B); a.b(0x0C); a.b(0xC8);
            a.callText(printIntRva);
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xF0);
            a.jmpShort("loop");
            a.label("done");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x83); a.b(0xF8); a.b(0x01);
            a.jneShort("no_trailing");
            a.b(0xB9); a.b(0x2C); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.label("no_trailing");
            a.b(0xB9); a.b(0x29); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // Phase 31.2 -- vayu_set_remove(s, v).  Linear scan; on match,
        // shift tail down by one slot and dec len.  No-op if absent.
        void emitSetRemove(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xE8); a.b32(0);
            a.label("sl");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x48); a.b(0x3B); a.b(0x01);
            a.jgeShort("done");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x0C); a.b(0xC1);
            a.b(0x48); a.b(0x3B); a.b(0x4D); a.b(0xF0);
            a.jeShort("found");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xE8);
            a.jmpShort("sl");
            a.label("found");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x71); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.label("shift");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x09);
            a.b(0x48); a.b(0x83); a.b(0xE9); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x45); a.b(0xE8);
            a.jgeShort("done");
            a.b(0x48); a.b(0x8B); a.b(0x0C); a.b(0xC6);
            a.b(0x48); a.b(0x89); a.b(0x0C); a.b(0x86);
            a.b(0x48); a.b(0xFF); a.b(0xC0);
            a.jmpShort("shift");
            a.label("done");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0xFF); a.b(0x09);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // Phase 31.2 -- vayu_set_clear(s): set len = 0.
        void emitSetClear(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0xC7); a.b(0x01); a.b(0x00); a.b(0x00);
            a.b(0x00); a.b(0x00);
            a.b(0xC3);
            a.finalize();
        }

        // ====================================================================
        // Linux runtime
        // ====================================================================

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
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x48); a.b(0x8B); a.b(0x75); a.b(0xD0);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xC8);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitExitLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x89); a.b(0xCF);
            a.b(0xB8); a.b32(60);
            a.b(0x0F); a.b(0x05);
            a.b(0xCC);
            a.finalize();
        }

        void emitPrintCharLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0x88); a.b(0x4D); a.b(0xF8);
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x48); a.b(0x8D); a.b(0x75); a.b(0xF8);
            a.b(0xBA); a.b32(1);
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
            a.b(0x48); a.b(0x85); a.b(0xC9);
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
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x11);
            a.b(0x48); a.b(0x8D); a.b(0x71); a.b(0x08);
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
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x45); a.b(0x31); a.b(0xDB);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("li_abs");
            a.b(0x41); a.b(0xBB); a.b32(1);
            a.b(0x48); a.b(0xF7); a.b(0xD8);
            a.label("li_abs");
            a.b(0x4C); a.b(0x89); a.b(0xEA);
            a.b(0x48); a.b(0x85); a.b(0xC0);
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
            a.b(0x4C); a.b(0x89); a.b(0xD6);
            a.b(0x48); a.b(0x89); a.b(0xEA);
            a.b(0x4C); a.b(0x29); a.b(0xD2);
            a.b(0xB8); a.b32(1);
            a.b(0xBF); a.b32(1);
            a.b(0x0F); a.b(0x05);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitAllocLinux(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x10);
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x0F);
            a.b(0x48); a.b(0x83); a.b(0xE1); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x31); a.b(0xFF);
            a.b(0xB8); a.b32(12);
            a.b(0x0F); a.b(0x05);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0xC7);
            a.b(0x48); a.b(0x03); a.b(0x7D); a.b(0xF8);
            a.b(0xB8); a.b32(12);
            a.b(0x0F); a.b(0x05);
            a.b(0x48); a.b(0x39); a.b(0xF8);
            a.jgeShort("ok");
            a.b(0xBF); a.b32(1);
            a.b(0xB8); a.b32(60);
            a.b(0x0F); a.b(0x05);
            a.b(0xCC);
            a.label("ok");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0xC9); a.b(0xC3);
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
            if (s == "vayu_str_slice")   return { "vayu_alloc" };
            if (s == "vayu_int_to_str")  return { "vayu_alloc" };
            if (s == "vayu_bool_to_str") return { "vayu_alloc" };
            if (s == "vayu_tuple_new")   return { "vayu_alloc" };
            if (s == "vayu_set_new")     return { "vayu_alloc" };
            if (s == "vayu_set_add")     return { "vayu_alloc", "vayu_list_push" };
            if (s == "vayu_print_tuple") return { "vayu_print_int",
                                              "vayu_print_char" };
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

        if (has("vayu_pow_int"))
            emitAndMark("vayu_pow_int", 0,
                [&] { emitPowInt(text, textRva); });
        if (has("vayu_str_slice"))
            emitAndMark("vayu_str_slice", 80,
                [&] { emitStrSlice(text, textRva, allocRva); });
        if (has("vayu_int_to_str"))
            emitAndMark("vayu_int_to_str", 80,
                [&] { emitIntToStr(text, textRva, allocRva); });
        if (has("vayu_bool_to_str"))
            emitAndMark("vayu_bool_to_str", 32,
                [&] { emitBoolToStr(text, textRva, allocRva); });
        if (has("vayu_str_to_int"))
            emitAndMark("vayu_str_to_int", 0,
                [&] { emitStrToInt(text, textRva); });
        if (has("vayu_tuple_new"))
            emitAndMark("vayu_tuple_new", 32,
                [&] { emitTupleNew(text, textRva, allocRva); });
        if (has("vayu_tuple_set"))
            emitAndMark("vayu_tuple_set", 0,
                [&] { emitTupleSet(text, textRva); });
        if (has("vayu_tuple_get"))
            emitAndMark("vayu_tuple_get", 0,
                [&] { emitTupleGet(text, textRva); });
        if (has("vayu_tuple_len"))
            emitAndMark("vayu_tuple_len", 0,
                [&] { emitTupleLen(text, textRva); });
        if (has("vayu_set_new"))
            emitAndMark("vayu_set_new", 32,
                [&] { emitListNew(text, textRva, allocRva); });
        if (has("vayu_set_add")) {
            uint32_t lpRva = 0;
            auto it = syms.find("vayu_list_push");
            if (it != syms.end()) lpRva = textRva + it->second;
            emitAndMark("vayu_set_add", 32,
                [&] { emitSetAdd(text, textRva, lpRva); });
        }
        if (has("vayu_set_contains"))
            emitAndMark("vayu_set_contains", 0,
                [&] { emitSetContains(text, textRva); });
        if (has("vayu_set_len"))
            emitAndMark("vayu_set_len", 0,
                [&] { emitListLen(text, textRva); });
        if (has("vayu_set_remove"))
            emitAndMark("vayu_set_remove", 32,
                [&] { emitSetRemove(text, textRva); });
        if (has("vayu_set_clear"))
            emitAndMark("vayu_set_clear", 0,
                [&] { emitSetClear(text, textRva); });
        if (has("vayu_print_tuple"))
            emitAndMark("vayu_print_tuple", 64,
                [&] { emitPrintTuple(text, textRva, printIntRva, printCharRva); });

        return syms;
    }

    // =========================================================================
    // emitRuntimeLinux (ELF)
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
            if (s == "vayu_str_slice")   return { "vayu_alloc" };
            if (s == "vayu_int_to_str")  return { "vayu_alloc" };
            if (s == "vayu_bool_to_str") return { "vayu_alloc" };
            if (s == "vayu_tuple_new")   return { "vayu_alloc" };
            if (s == "vayu_set_new")     return { "vayu_alloc" };
            if (s == "vayu_set_add")     return { "vayu_alloc", "vayu_list_push" };
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

        if (has("vayu_exit")) { mark("vayu_exit"); emitExitLinux(text, textRva); }
        if (has("vayu_print_char")) { printCharRva = mark("vayu_print_char"); emitPrintCharLinux(text, textRva); }
        if (has("vayu_print_ln")) { mark("vayu_print_ln"); emitPrintLnLinux(text, textRva); }
        if (has("vayu_print_space")) { mark("vayu_print_space"); emitPrintSpaceLinux(text, textRva); }
        if (has("vayu_print_bool")) { mark("vayu_print_bool"); emitPrintBoolLinux(text, textRva); }
        if (has("vayu_print_str")) { printStrRva = mark("vayu_print_str"); emitPrintStrLinux(text, textRva); }
        if (has("vayu_print_int")) { printIntRva = mark("vayu_print_int"); emitPrintIntLinux(text, textRva); }
        if (has("vayu_print_float")) { mark("vayu_print_float"); emitPrintFloatLinux(text, textRva); }
        if (has("vayu_alloc")) { allocRva = mark("vayu_alloc"); emitAllocLinux(text, textRva); }
        if (has("vayu_str_len")) { mark("vayu_str_len"); emitStrLen(text, textRva); }
        if (has("vayu_str_eq")) { strEqRva = mark("vayu_str_eq"); emitStrEq(text, textRva); }
        if (has("vayu_str_starts_with")) { mark("vayu_str_starts_with"); emitStrStartsWith(text, textRva); }
        if (has("vayu_str_ends_with")) { mark("vayu_str_ends_with"); emitStrEndsWith(text, textRva); }
        if (has("vayu_str_contains")) { mark("vayu_str_contains"); emitStrContains(text, textRva); }
        if (has("vayu_str_find")) { mark("vayu_str_find"); emitStrFind(text, textRva); }
        if (has("vayu_str_concat")) { mark("vayu_str_concat"); emitStrConcat(text, textRva, allocRva); }
        if (has("vayu_str_upper")) { mark("vayu_str_upper"); emitStrUpper(text, textRva, allocRva); }
        if (has("vayu_str_lower")) { mark("vayu_str_lower"); emitStrLower(text, textRva, allocRva); }
        if (has("vayu_list_new")) { mark("vayu_list_new"); emitListNew(text, textRva, allocRva); }
        if (has("vayu_list_push")) { mark("vayu_list_push"); emitListPush(text, textRva, allocRva); }
        if (has("vayu_list_get")) { mark("vayu_list_get"); emitListGet(text, textRva); }
        if (has("vayu_list_set")) { mark("vayu_list_set"); emitListSet(text, textRva); }
        if (has("vayu_list_len")) { mark("vayu_list_len"); emitListLen(text, textRva); }
        if (has("vayu_map_new")) { mark("vayu_map_new"); emitMapNew(text, textRva, allocRva); }
        if (has("vayu_map_put")) { mark("vayu_map_put"); emitMapPut(text, textRva, strEqRva, allocRva); }
        if (has("vayu_map_get")) { mark("vayu_map_get"); emitMapGet(text, textRva, strEqRva); }
        if (has("vayu_map_has")) { mark("vayu_map_has"); emitMapHas(text, textRva, strEqRva); }
        if (has("vayu_map_len")) { mark("vayu_map_len"); emitMapLen(text, textRva); }
        if (has("vayu_print_list")) { mark("vayu_print_list"); emitPrintList(text, textRva, printIntRva, printCharRva); }
        if (has("vayu_print_map")) { mark("vayu_print_map"); emitPrintMap(text, textRva, printIntRva, printStrRva, printCharRva); }
        if (has("vayu_pow_int")) { mark("vayu_pow_int"); emitPowInt(text, textRva); }
        if (has("vayu_str_slice")) { mark("vayu_str_slice"); emitStrSlice(text, textRva, allocRva); }
        if (has("vayu_int_to_str")) { mark("vayu_int_to_str"); emitIntToStr(text, textRva, allocRva); }
        if (has("vayu_bool_to_str")) { mark("vayu_bool_to_str"); emitBoolToStr(text, textRva, allocRva); }
        if (has("vayu_str_to_int")) { mark("vayu_str_to_int"); emitStrToInt(text, textRva); }
        if (has("vayu_tuple_new")) { mark("vayu_tuple_new"); emitTupleNew(text, textRva, allocRva); }
        if (has("vayu_tuple_set")) { mark("vayu_tuple_set"); emitTupleSet(text, textRva); }
        if (has("vayu_tuple_get")) { mark("vayu_tuple_get"); emitTupleGet(text, textRva); }
        if (has("vayu_tuple_len")) { mark("vayu_tuple_len"); emitTupleLen(text, textRva); }
        if (has("vayu_set_new")) { mark("vayu_set_new"); emitListNew(text, textRva, allocRva); }
        if (has("vayu_set_add")) {
            uint32_t lpRva = 0;
            auto it = syms.find("vayu_list_push");
            if (it != syms.end()) lpRva = textRva + it->second;
            mark("vayu_set_add");
            emitSetAdd(text, textRva, lpRva);
        }
        if (has("vayu_set_contains")) { mark("vayu_set_contains"); emitSetContains(text, textRva); }
        if (has("vayu_set_len")) { mark("vayu_set_len"); emitListLen(text, textRva); }

        return syms;
    }

} // namespace vcb