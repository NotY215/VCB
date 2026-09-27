#include "vcb/Runtime.hpp"
#include <cstring>
#include <stdexcept>

namespace vcb {

    namespace {

        struct MiniAsm {
            std::vector<uint8_t>& body;
            uint32_t                                    textRva;
            std::vector<std::pair<size_t, std::string>> shortJumps;
            std::unordered_map<std::string, size_t>     labels;

            MiniAsm(std::vector<uint8_t>& t, uint32_t rva)
                : body(t), textRva(rva) {
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
            void jbeShort(const std::string& l) {
                body.push_back(0x76);
                shortJumps.push_back({ body.size(), l });
                body.push_back(0);
            }

            void callIat(uint32_t iatRva) {
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

        // ---- primitives -------------------------------------------------

        void emitExit(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.finalize();
        }

        // vayu_print_char(ch)  -- ch in low 8 bits of RCX
        void emitPrintChar(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x88); a.b(0x4D); a.b(0xF8);              // mov byte [rbp-8], cl
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);       // mov ecx, -11
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
            MiniAsm a(t, rva);
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
            MiniAsm a(t, rva);
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
            MiniAsm a(t, rva);
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
            MiniAsm a(t, rva);
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
            MiniAsm a(t, rva);
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
            // Trim trailing zeros: rbx = rbp-64, decrement until rbx <= r10
            // or byte != '0'.
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

        // ---- heap -------------------------------------------------------

        void emitAlloc(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x20);
            a.callIat(im.iatMalloc);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("ok");
            a.b(0xB9); a.b32(1);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.label("ok");
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ---- strings ----------------------------------------------------

        // vayu_str_eq(a, b) -> i64
        void emitStrEq(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x39); a.b(0xD1);                 // cmp rcx, rdx
            a.jnzShort("diff");
            a.b(0xB8); a.b32(1);                             // same pointer -> 1
            a.b(0xC3);
            a.label("diff");
            a.b(0x48); a.b(0x8B); a.b(0x01);                 // mov rax, [rcx]
            a.b(0x48); a.b(0x3B); a.b(0x02);                 // cmp rax, [rdx]
            a.jnzShort("no");                                // len differs
            a.b(0x48); a.b(0x83); a.b(0xC1); a.b(0x08);      // rcx += 8
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);      // rdx += 8
            a.b(0x4D); a.b(0x31); a.b(0xC0);                 // xor r8, r8
            a.label("loop");
            a.b(0x49); a.b(0x39); a.b(0xC0);                 // cmp r8, rax
            a.jgeShort("eq");
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x0C); a.b(0x01);   // movzx r9d, [rcx+r8]
            a.b(0x46); a.b(0x0F); a.b(0xB6); a.b(0x14); a.b(0x02);   // movzx r10d, [rdx+r8]
            a.b(0x45); a.b(0x39); a.b(0xD1);                 // cmp r9d, r10d
            a.jnzShort("no");
            a.b(0x49); a.b(0xFF); a.b(0xC0);                 // inc r8
            a.jmpShort("loop");
            a.label("eq");
            a.b(0xB8); a.b32(1);
            a.b(0xC3);
            a.label("no");
            a.b(0x31); a.b(0xC0);
            a.b(0xC3);
            a.finalize();
        }

        // ---- list -------------------------------------------------------

        // header: [0]=len [8]=cap [16]=dataptr ; data[i] = i64
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

        // vayu_list_push(list, value)
        void emitListPush(std::vector<uint8_t>& t, uint32_t rva, const RuntimeImports& im) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);     // list
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);     // value
            a.b(0x48); a.b(0x8B); a.b(0x01);                // len
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);     // vs cap
            a.jlShort("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x08);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got_cap");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0xC2);
            a.label("got_cap");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xE8);
            // realloc(data, new_cap*8)
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0xC1); a.b(0xE2); a.b(0x03);
            a.callIat(im.iatRealloc);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("ok_re");
            a.b(0xB9); a.b32(1);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.label("ok_re");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE8);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);
            a.label("have_cap");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);                // len
            a.b(0x48); a.b(0x8B); a.b(0x51); a.b(0x10);     // data
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);     // value
            a.b(0x48); a.b(0x89); a.b(0x0C); a.b(0xC2);     // [rdx + len*8] = value
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0xFF); a.b(0x01);                // len++
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        void emitListGet(std::vector<uint8_t>& t, uint32_t rva) {
            MiniAsm a(t, rva);
            a.b(0x48); a.b(0x8B); a.b(0x01);                // rax = len
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

        // vayu_print_list(list) - prints [a, b, c]
        void emitPrintList(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t printIntRva, uint32_t printCharRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);     // list
            a.b(0xB9); a.b(0x5B); a.b(0x00); a.b(0x00); a.b(0x00);   // '['
            a.callText(printCharRva);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(0);    // i = 0
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x39); a.b(0x01);                // cmp i, len
            a.jgeShort("done");
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jzShort("no_comma");
            a.b(0xB9); a.b(0x2C); a.b(0x00); a.b(0x00); a.b(0x00);   // ','
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);   // ' '
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
            a.b(0xB9); a.b(0x5D); a.b(0x00); a.b(0x00); a.b(0x00);   // ']'
            a.callText(printCharRva);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

        // ---- map --------------------------------------------------------

        // header: [0]=len [8]=cap [16]=entriesptr ; entry = { key(str*), value }
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

        // vayu_map_put(map, key, value) - key is VayuStr*
        void emitMapPut(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t strEqRva, const RuntimeImports& im) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x60);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);     // map
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);     // key
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE0);     // value
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);   // i = 0
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0x39); a.b(0x01);
            a.jgeShort("nf");
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x11);                // rdx = key_i
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF0);     // rcx = arg key
            a.callText(strEqRva);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("found");
            a.b(0x48); a.b(0xFF); a.b(0x45); a.b(0xD8);
            a.jmpShort("loop");
            a.label("found");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE0);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);
            a.b(0xC9); a.b(0xC3);
            a.label("nf");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x01);
            a.b(0x48); a.b(0x3B); a.b(0x41); a.b(0x08);
            a.jlShort("hr");
            a.b(0x48); a.b(0x8B); a.b(0x41); a.b(0x08);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("dbl");
            a.b(0x48); a.b(0xC7); a.b(0xC2); a.b32(8);
            a.jmpShort("got");
            a.label("dbl");
            a.b(0x48); a.b(0x01); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0xC2);
            a.label("got");
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xD0);
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0xC1); a.b(0xE2); a.b(0x04);
            a.callIat(im.iatRealloc);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("ok_re");
            a.b(0xB9); a.b32(1);
            a.callIat(im.iatExitProcess);
            a.b(0xCC);
            a.label("ok_re");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0x41); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x51); a.b(0x08);
            a.label("hr");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x49); a.b(0x10);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xD8);
            a.b(0x48); a.b(0xC1); a.b(0xE0); a.b(0x04);
            a.b(0x48); a.b(0x01); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x89); a.b(0x11);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xE0);
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
            a.b(0x48); a.b(0x39); a.b(0x01);
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
            a.b(0x48); a.b(0x39); a.b(0x01);
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

        // vayu_print_map(map) - prints {"k": v, "k2": v2}
        void emitPrintMap(std::vector<uint8_t>& t, uint32_t rva,
            uint32_t printIntRva, uint32_t printStrRva,
            uint32_t printCharRva) {
            MiniAsm a(t, rva);
            a.b(0x55); a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0xB9); a.b(0x7B); a.b(0x00); a.b(0x00); a.b(0x00);   // '{'
            a.callText(printCharRva);
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xF0); a.b32(0);
            a.label("loop");
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF0);
            a.b(0x48); a.b(0x39); a.b(0x01);
            a.jgeShort("done");
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jzShort("no_comma");
            a.b(0xB9); a.b(0x2C); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);
            a.callText(printCharRva);
            a.label("no_comma");
            a.b(0xB9); a.b(0x22); a.b(0x00); a.b(0x00); a.b(0x00);   // '"'
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
            a.b(0xB9); a.b(0x3A); a.b(0x00); a.b(0x00); a.b(0x00);   // ':'
            a.callText(printCharRva);
            a.b(0xB9); a.b(0x20); a.b(0x00); a.b(0x00); a.b(0x00);   // ' '
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
            a.b(0xB9); a.b(0x7D); a.b(0x00); a.b(0x00); a.b(0x00);   // '}'
            a.callText(printCharRva);
            a.b(0xC9); a.b(0xC3);
            a.finalize();
        }

    } // namespace

    std::unordered_map<std::string, uint32_t> emitRuntime(
        std::vector<uint8_t>& text, uint32_t textRva, const RuntimeImports& im)
    {
        std::unordered_map<std::string, uint32_t> syms;

        auto mark = [&](const char* name) {
            uint32_t off = (uint32_t)text.size();
            syms[name] = off;
            return textRva + off;
            };

        uint32_t rva;

        rva = mark("vayu_exit");
        emitExit(text, textRva, im);

        rva = mark("vayu_print_char");
        emitPrintChar(text, textRva, im);

        rva = mark("vayu_print_ln");
        emitPrintLn(text, textRva, im);

        rva = mark("vayu_print_space");
        emitPrintSpace(text, textRva, im);

        rva = mark("vayu_print_bool");
        emitPrintBool(text, textRva, im);

        rva = mark("vayu_print_str");
        emitPrintStr(text, textRva, im);

        uint32_t printIntRva = mark("vayu_print_int");
        emitPrintInt(text, textRva, im);

        rva = mark("vayu_print_float");
        emitPrintFloat(text, textRva, im);

        uint32_t allocRva = mark("vayu_alloc");
        emitAlloc(text, textRva, im);

        mark("vayu_list_new");
        emitListNew(text, textRva, allocRva);

        rva = mark("vayu_list_push");
        emitListPush(text, textRva, im);

        rva = mark("vayu_list_get");
        emitListGet(text, textRva);

        rva = mark("vayu_list_set");
        emitListSet(text, textRva);

        rva = mark("vayu_list_len");
        emitListLen(text, textRva);

        uint32_t strEqRva = mark("vayu_str_eq");
        emitStrEq(text, textRva);

        mark("vayu_map_new");
        emitMapNew(text, textRva, allocRva);

        rva = mark("vayu_map_put");
        emitMapPut(text, textRva, strEqRva, im);

        rva = mark("vayu_map_get");
        emitMapGet(text, textRva, strEqRva);

        rva = mark("vayu_map_has");
        emitMapHas(text, textRva, strEqRva);

        rva = mark("vayu_map_len");
        emitMapLen(text, textRva);

        uint32_t printStrRva = syms["vayu_print_str"];
        uint32_t printCharRva = syms["vayu_print_char"];

        mark("vayu_print_list");
        emitPrintList(text, textRva, printIntRva, printCharRva);

        mark("vayu_print_map");
        emitPrintMap(text, textRva, printIntRva, printStrRva, printCharRva);

        return syms;
    }

} // namespace vcb