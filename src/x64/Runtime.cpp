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
                uint32_t insnStartAbs = textRva + (uint32_t)body.size();
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

        void emitPrintChar(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint8_t ch,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0xC6); a.b(0x45); a.b(0xF8); a.b(ch);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(1);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24);
            a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);
            a.b(0xC9);
            a.b(0xC3);
            a.finalize();
        }

        void emitPrintStr(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);
            a.b(0x48); a.b(0x89); a.b(0xE5);
            a.b(0x48); a.b(0x83); a.b(0xEC); a.b(0x40);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);
            a.b(0x48); a.b(0x89); a.b(0xCA);
            a.b(0x48); a.b(0x83); a.b(0xC2); a.b(0x08);
            a.b(0x48); a.b(0x89); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x01);
            a.b(0x4C); a.b(0x89); a.b(0x45); a.b(0xE8);
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xF0);
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xE8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xE0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24);
            a.b(0x20); a.b32(0);
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
            a.b(0x48); a.b(0xB8); a.b64(0x0000000065757274ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(4);
            a.jmpShort("write");
            a.label("false_path");
            a.b(0x48); a.b(0xB8); a.b64(0x00000065736C6166ULL);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);
            a.b(0x41); a.b(0xB8); a.b32(5);
            a.label("write");
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);
            a.b(0x48); a.b(0x8D); a.b(0x55); a.b(0xF8);
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24);
            a.b(0x20); a.b32(0);
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
            a.b(0x4C); a.b(0x8D); a.b(0x55); a.b(0xE0);
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
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24);
            a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);
            a.b(0xC9);
            a.b(0xC3);
            a.finalize();
        }

        // vayu_print_float(bits)
        //   Renders an f64 as "[sign]int.frac" with exactly 6 fractional
        //   digits.  No trailing newline.  Layout:
        //     [rbp-8]   original bits
        //     [rbp-16]  written (for WriteFile 4th arg)
        //     [rbp-32]  int_part
        //     [rbp-40]  frac_int
        //     [rbp-48]  buffer start
        //     [rbp-56]  length
        //     r10 starts at [rbp-64] and walks down; contents live below.
        void emitPrintFloat(std::vector<uint8_t>& text, uint32_t textRva, uint32_t base,
            uint32_t iatGetStdHandle, uint32_t iatWriteFile) {
            MiniAsm a(text, textRva, base);
            a.b(0x55);                                     // push rbp
            a.b(0x48); a.b(0x89); a.b(0xE5);               // mov rbp, rsp
            a.b(0x48); a.b(0x81); a.b(0xEC); a.b32(512);   // sub rsp, 512

            // [rbp-8] = original bits; xmm0 = v
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xF8);    // mov [rbp-8], rcx
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC1);   // movq xmm0, rcx

            // r10 = rbp - 64  (cursor at top of buffer; digits go below)
            a.b(0x4C); a.b(0x8D); a.b(0x95); a.b32(0xFFFFFFC0u);

            // Sign: if negative, flip sign bit in the stored bits and
            // reload xmm0 with the positive value.
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);    // mov rax, [rbp-8]
            a.b(0x48); a.b(0x85); a.b(0xC0);               // test rax, rax
            a.jnsShort("pf_abs_ok");
            a.b(0x48); a.b(0xB9); a.b64(0x8000000000000000ULL);
            a.b(0x48); a.b(0x31); a.b(0xC8);               // xor rax, rcx
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xF8);    // [rbp-8] = positive bits
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC0);   // movq xmm0, rax
            a.label("pf_abs_ok");

            // int_part = cvttsd2si(v)
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);    // [rbp-32] = int_part

            // (double)int_part -> xmm1 ; frac = v - int_part
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2A); a.b(0xC8);
            a.b(0xF2); a.b(0x0F); a.b(0x5C); a.b(0xC1);    // subsd xmm0, xmm1

            // frac *= 1e6
            a.b(0x48); a.b(0xB9); a.b64(0x412E848000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x59); a.b(0xC1);    // mulsd xmm0, xmm1
            // frac += 0.5 (round-to-nearest)
            a.b(0x48); a.b(0xB9); a.b64(0x3FE0000000000000ULL);
            a.b(0x66); a.b(0x48); a.b(0x0F); a.b(0x6E); a.b(0xC9);
            a.b(0xF2); a.b(0x0F); a.b(0x58); a.b(0xC1);    // addsd xmm0, xmm1

            // frac_int = cvttsd2si(frac)
            a.b(0xF2); a.b(0x48); a.b(0x0F); a.b(0x2C); a.b(0xC8);
            a.b(0x48); a.b(0x89); a.b(0x4D); a.b(0xD8);    // [rbp-40] = frac_int

            // carry: if frac_int == 1000000, reset to 0 and int_part++
            a.b(0x48); a.b(0x81); a.b(0xF9); a.b32(1000000);
            a.jnzShort("pf_no_carry");
            a.b(0x48); a.b(0xC7); a.b(0x45); a.b(0xD8); a.b32(0);
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xE0);
            a.b(0x48); a.b(0xFF); a.b(0xC0);
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xE0);
            a.label("pf_no_carry");

            // 6 fractional digits (frac_int in [0, 999999])
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xD8);    // mov rcx, [rbp-40]
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(6);     // mov rbx, 6
            a.label("pf_frac_loop");
            a.b(0x48); a.b(0x89); a.b(0xC8);               // mov rax, rcx
            a.b(0x48); a.b(0xC7); a.b(0xC1); a.b32(10);    // mov rcx, 10
            a.b(0x48); a.b(0x31); a.b(0xD2);               // xor rdx, rdx
            a.b(0x48); a.b(0xF7); a.b(0xF1);               // div rcx
            a.b(0x80); a.b(0xC2); a.b(0x30);               // add dl, '0'
            a.b(0x49); a.b(0xFF); a.b(0xCA);               // dec r10
            a.b(0x41); a.b(0x88); a.b(0x12);               // mov [r10], dl
            a.b(0x48); a.b(0x89); a.b(0xC1);               // mov rcx, rax
            a.b(0x48); a.b(0xFF); a.b(0xCB);               // dec rbx
            a.jnzShort("pf_frac_loop");

            // '.'
            a.b(0x49); a.b(0xFF); a.b(0xCA);               // dec r10
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2E);    // mov byte [r10], '.'

            // int_part digits
            a.b(0x48); a.b(0x8B); a.b(0x4D); a.b(0xE0);    // mov rcx, [rbp-32]
            a.b(0x48); a.b(0x85); a.b(0xC9);               // test rcx, rcx
            a.jnzShort("pf_int_start");
            a.b(0x49); a.b(0xFF); a.b(0xCA);               // dec r10
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x30);    // mov byte [r10], '0'
            a.jmpShort("pf_int_done");
            a.label("pf_int_start");
            a.b(0x48); a.b(0xC7); a.b(0xC3); a.b32(10);    // mov rbx, 10
            a.label("pf_int_loop");
            a.b(0x48); a.b(0x89); a.b(0xC8);               // mov rax, rcx
            a.b(0x48); a.b(0x31); a.b(0xD2);               // xor rdx, rdx
            a.b(0x48); a.b(0xF7); a.b(0xF3);               // div rbx
            a.b(0x80); a.b(0xC2); a.b(0x30);               // add dl, '0'
            a.b(0x49); a.b(0xFF); a.b(0xCA);               // dec r10
            a.b(0x41); a.b(0x88); a.b(0x12);               // mov [r10], dl
            a.b(0x48); a.b(0x89); a.b(0xC1);               // mov rcx, rax
            a.b(0x48); a.b(0x85); a.b(0xC9);               // test rcx, rcx
            a.jnzShort("pf_int_loop");
            a.label("pf_int_done");

            // '-' if sign was set
            a.b(0x48); a.b(0x8B); a.b(0x45); a.b(0xF8);    // mov rax, [rbp-8]
            a.b(0x48); a.b(0x85); a.b(0xC0);
            a.jnsShort("pf_write");
            a.b(0x49); a.b(0xFF); a.b(0xCA);               // dec r10
            a.b(0x41); a.b(0xC6); a.b(0x02); a.b(0x2D);    // mov byte [r10], '-'
            a.label("pf_write");

            // length = (rbp-64) - r10 ; buffer start = r10
            a.b(0x4C); a.b(0x89); a.b(0x55); a.b(0xD0);    // [rbp-48] = r10
            a.b(0x48); a.b(0x8D); a.b(0x45); a.b(0xC0);    // lea rax, [rbp-64]
            a.b(0x4C); a.b(0x29); a.b(0xD0);               // sub rax, r10
            a.b(0x48); a.b(0x89); a.b(0x45); a.b(0xC8);    // [rbp-56] = length

            // WriteFile(GetStdHandle(-11), buffer, length, &written, NULL)
            a.b(0xB9); a.b32(STD_OUTPUT_HANDLE_M11);
            a.callRip(iatGetStdHandle);
            a.b(0x48); a.b(0x89); a.b(0xC1);               // mov rcx, rax
            a.b(0x48); a.b(0x8B); a.b(0x55); a.b(0xD0);    // mov rdx, [rbp-48]
            a.b(0x4C); a.b(0x8B); a.b(0x45); a.b(0xC8);    // mov r8, [rbp-56]
            a.b(0x4C); a.b(0x8D); a.b(0x4D); a.b(0xF0);    // lea r9, [rbp-16]
            a.b(0x48); a.b(0xC7); a.b(0x44); a.b(0x24);
            a.b(0x20); a.b32(0);
            a.callRip(iatWriteFile);

            a.b(0xC9);                                     // leave
            a.b(0xC3);                                     // ret
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
        emitPrintChar(text, textRva, syms["vayu_print_ln"],
            0x0A, iatGetStdHandle, iatWriteFile);

        syms["vayu_print_space"] = (uint32_t)text.size();
        emitPrintChar(text, textRva, syms["vayu_print_space"],
            0x20, iatGetStdHandle, iatWriteFile);

        syms["vayu_print_bool"] = (uint32_t)text.size();
        emitPrintBool(text, textRva, syms["vayu_print_bool"],
            iatGetStdHandle, iatWriteFile);

        syms["vayu_print_str"] = (uint32_t)text.size();
        emitPrintStr(text, textRva, syms["vayu_print_str"],
            iatGetStdHandle, iatWriteFile);

        syms["vayu_print_int"] = (uint32_t)text.size();
        emitPrintInt(text, textRva, syms["vayu_print_int"],
            iatGetStdHandle, iatWriteFile);

        syms["vayu_print_float"] = (uint32_t)text.size();
        emitPrintFloat(text, textRva, syms["vayu_print_float"],
            iatGetStdHandle, iatWriteFile);

        return syms;
    }

} // namespace vcb