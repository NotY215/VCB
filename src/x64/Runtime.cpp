#include "vcb/Runtime.hpp"
#include <cstring>
#include <stdexcept>

namespace vcb {

    namespace {

        struct MiniAsm {
            std::vector<uint8_t>& body;
            uint32_t                                    textRva;
            uint32_t                                    baseOffset;
            std::vector<std::pair<size_t, std::string>> shortJumps;
            std::unordered_map<std::string, size_t>     labels;

            MiniAsm(std::vector<uint8_t>& t, uint32_t rva, uint32_t base)
                : body(t), textRva(rva), baseOffset(base) {
            }

            void b(uint8_t x) { body.push_back(x); }
            void b32(uint32_t x) {
                for (int i = 0; i < 4; ++i) body.push_back((x >> (i * 8)) & 0xFF);
            }
            void b64(uint64_t x) {
                for (int i = 0; i < 8; ++i) body.push_back((x >> (i * 8)) & 0xFF);
            }

            void label(const std::string& name) { labels[name] = body.size(); }

            void jmpShort(const std::string& lbl) {
                body.push_back(0xEB);
                shortJumps.push_back({ body.size(), lbl });
                body.push_back(0);
            }
            void jzShort(const std::string& lbl) {
                body.push_back(0x74);
                shortJumps.push_back({ body.size(), lbl });
                body.push_back(0);
            }
            void jnzShort(const std::string& lbl) {
                body.push_back(0x75);
                shortJumps.push_back({ body.size(), lbl });
                body.push_back(0);
            }
            void jnsShort(const std::string& lbl) {
                body.push_back(0x79);
                shortJumps.push_back({ body.size(), lbl });
                body.push_back(0);
            }

            void callRip(uint32_t iatRva) {
                uint32_t insnStartAbs = textRva + baseOffset + (uint32_t)body.size();
                body.push_back(0xFF);
                body.push_back(0x15);
                int32_t rel = (int32_t)iatRva - (int32_t)(insnStartAbs + 6);
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

        void emitExit(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatExitProcess) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.callRip(iatExitProcess);
            a.b(0xCC);
            a.finalize();
        }

        void emitPrintLn(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(0x0A);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(1);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);
            a.b(0xC9);
            a.b(0xC3);
            a.finalize();
        }

        void emitPrintBool(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x85); a.b(0xC9);
            a.jzShort("false_path");
            a.b(0x48); a.b(0xB8); a.b64(0x0000000A65757274ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(5);
            a.jmpShort("write");
            a.label("false_path");
            a.b(0x48); a.b(0xB8); a.b64(0x00000A65736C6166ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(6);
            a.label("write");
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);
            a.b(0xC9);
            a.b(0xC3);
            a.finalize();
        }

        void emitPrintInt(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(128);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0xC8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("abs_ok");
            a.b(0x48); a.b(0xF7); a.b(0xD8);
            a.label("abs_ok");
            a.b(0x4C); a.b(0x8D); a.b(0x55); a.b(0xDF);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x0A);
            a.b(0x48); a.b(0x83); a.b(0xF8); a.b(0x00);
            a.jnzShort("loop_start");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);
            a.jmpShort("sign_check");
            a.label("loop_start");
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);
            a.label("loop");
            a.b(0x48); a.b(0x31); a.b(0xD2);
            a.b(0x48); a.b(0xF7); a.b(0xF1);
            a.b(0x80); a.b(0xC2); a.b(0x30);
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0x88); a.b(0x12);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnzShort("loop");
            a.label("sign_check");
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("write_now");
            a.b(0x49); a.b(0xFF); a.b(0xCA);
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);
            a.label("write_now");
            a.b(0x4C); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x48); a.b(0x8D); a.b(0x45); a.b(0xE0);
            a.b(0x4C); a.b(0x29); a.b(0xD0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24); a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);
            a.b(0xC9);
            a.b(0xC3);
            a.finalize();
        }

    } // namespace

    std::unordered_map<std::string, uint32_t> emitRuntime(
        std::vector<uint8_t>& text, uint32_t textRva,
        uint32_t iatGetStdHandle, uint32_t iatWriteFile, uint32_t iatExitProcess)
    {
        std::unordered_map<std::string, uint32_t> syms;

        syms["vayu_exit"] = (uint32_t)text.size();
        emitExit(text, textRva, syms["vayu_exit"], iatExitProcess);

        syms["vayu_print_ln"] = (uint32_t)text.size();
        emitPrintLn(text, textRva, syms["vayu_print_ln"],
            iatGetStdHandle, iatWriteFile);

        syms["vayu_print_bool"] = (uint32_t)text.size();
        emitPrintBool(text, textRva, syms["vayu_print_bool"],
            iatGetStdHandle, iatWriteFile);

        syms["vayu_print_int"] = (uint32_t)text.size();
        emitPrintInt(text, textRva, syms["vayu_print_int"],
            iatGetStdHandle, iatWriteFile);

        return syms;
    }

} // namespace vcb