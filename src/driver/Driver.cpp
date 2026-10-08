#include "vcb/Driver.hpp"
#include "vcb/Parser.hpp"
#include "vcb/Printer.hpp"
#include "vcb/X64.hpp"
#include "vcb/Pe.hpp"
#include "vcb/Elf.hpp"
#include "vcb/Obj.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace vcb {

    namespace {

        void printUsage() {
            std::fprintf(stderr,
                "usage: vcb <command> [args]\n"
                "\n"
                "Commands:\n"
                "  vcb version                                print version\n"
                "  vcb dump    <file.vcbir>                   parse and pretty-print\n"
                "  vcb build   <file.vcbir> -o <out> [--target pe|elf]\n"
                "                                             emit a native image\n"
                "  vcb emit-obj <file.vcbir> -o <out.obj> [--target pe|elf]\n"
                "                                             emit an object file\n"
                "  vcb link    <in.obj> -o <out> [--target pe|elf]\n"
                "                       [--entry <sym>] [--lib <path>]...\n"
                "                                             invoke lld-link or ld.lld\n"
                "  vcb headers    <file.exe>                  dump a PE header\n"
                "  vcb elfheaders <file.elf>                  dump an ELF header\n"
                "\n"
                "Notes:\n"
                "  vcb emits final machine code directly.  It does not use an\n"
                "  external assembler, linker, or QBE.  The container (PE or\n"
                "  ELF) is written by vcb itself.\n");
        }

        int cmdVersion() {
            std::printf("vcb 0.6.2 (Phase 27 complete, no signing)\n");
            return 0;
        }

        int cmdDump(const std::string& path) {
            try {
                Module m = parseFile(path);
                std::string out = printModule(m);
                std::fwrite(out.data(), 1, out.size(), stdout);
                return 0;
            }
            catch (const ParseError& e) {
                if (e.line > 0)
                    std::fprintf(stderr, "%s:%d: parse error: %s\n",
                        path.c_str(), e.line, e.what());
                else
                    std::fprintf(stderr, "%s: parse error: %s\n",
                        path.c_str(), e.what());
                return 1;
            }
        }

        int cmdBuild(const std::string& input, const std::string& output,
            const std::string& target) {
            // Create the output directory if it does not exist.  Without
            // this, writePeAtomic / writeElfAtomic fail at fopen(tmp)
            // with "cannot write '<out>.tmp'" when the parent is missing.
                {
                    std::error_code ec;
                    auto parent = std::filesystem::path(output).parent_path();
                    if (!parent.empty())
                        std::filesystem::create_directories(parent, ec);
                }
                if (target != "pe" && target != "elf") {
                    std::fprintf(stderr,
                        "vcb: --target '%s' not implemented yet (pe|elf)\n",
                        target.c_str());
                    return 1;
                }
                try {
                    Module m = parseFile(input);

                    if (target == "elf") {
                        CodegenResult cg = codegenX64Elf(m);
                        ElfInputs ei;
                        ei.text = &cg.text;
                        ei.rodata = &cg.rdata;
                        ei.entryOffset = cg.entryOffset;
                        std::vector<uint8_t> image = writeElf(ei);
                        int wrc = writeElfAtomic(output, image);
                        if (wrc != 0) return 1;
                        std::fprintf(stderr, "vcb: wrote %s (%zu bytes)\n",
                            output.c_str(), image.size());
                        return 0;
                    }

                    CodegenResult cg = codegenX64Pe(m);

                    PeInputs pi;
                    pi.text = &cg.text;
                    pi.rdata = &cg.rdata;
                    pi.idata = &cg.idata;
                    pi.xdata = &cg.xdata;
                    pi.unwindEntries = &cg.unwindEntries;
                    // Fixed-base image.  All references are RIP-relative or
                    // RVAs; there are no absolute VAs in .text, so ASLR would
                    // require a zero-entry .reloc that some Windows 11 builds
                    // reject.  RELOCS_STRIPPED is the coherent configuration.
                    pi.enableAslr = false;
                    pi.entryOffset = cg.entryOffset;
                    pi.iatRva = cg.iatRva;
                    pi.iatSize = cg.iatSize;
                    pi.importRva = cg.importRva;
                    pi.importSize = cg.importSize;

                    std::vector<uint8_t> image = writePe(pi);

                    int wrc = writePeAtomic(output, image);
                    if (wrc != 0) return 1;
                    std::fprintf(stderr, "vcb: wrote %s (%zu bytes)\n",
                        output.c_str(), image.size());
                    return 0;
                }
                catch (const ParseError& e) {
                    if (e.line > 0)
                        std::fprintf(stderr, "%s:%d: parse error: %s\n",
                            input.c_str(), e.line, e.what());
                    else
                        std::fprintf(stderr, "%s: parse error: %s\n",
                            input.c_str(), e.what());
                    return 1;
                }
                catch (const std::exception& e) {
                    std::fprintf(stderr, "vcb: %s\n", e.what());
                    return 1;
                }
        }

        int cmdEmitObj(const std::string& input,
            const std::string& output,
            const std::string& target) {
            std::error_code ec;
            auto parent = std::filesystem::path(output).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, ec);

            try {
                Module m = parseFile(input);
                std::vector<uint8_t> bytes;
                if (target == "pe") {
                    CoffFile f = codegenX64Coff(m);
                    bytes = writeCoff(f);
                    int wrc = writeCoffAtomic(output, bytes);
                    if (wrc != 0) return 1;
                }
                else if (target == "elf") {
                    ElfFile f = codegenX64ElfObj(m);
                    bytes = writeElfObj(f);
                    int wrc = writeElfObjAtomic(output, bytes);
                    if (wrc != 0) return 1;
                }
                else {
                    std::fprintf(stderr,
                        "vcb: emit-obj --target '%s' not implemented yet "
                        "(pe|elf)\n", target.c_str());
                    return 1;
                }
                std::fprintf(stderr, "vcb: wrote %s (%zu bytes)\n",
                    output.c_str(), bytes.size());
                return 0;
            }
            catch (const ParseError& e) {
                if (e.line > 0)
                    std::fprintf(stderr, "%s:%d: parse error: %s\n",
                        input.c_str(), e.line, e.what());
                else
                    std::fprintf(stderr, "%s: parse error: %s\n",
                        input.c_str(), e.what());
                return 1;
            }
            catch (const std::exception& e) {
                std::fprintf(stderr, "vcb: %s\n", e.what());
                return 1;
            }
        }

        // ---- Linker discovery -----------------------------------------
        // Look for tools\lld-link.exe (PE) or tools\ld.lld.exe (ELF).
        // Search order:
        //   1. VCB_TOOLS_DIR environment variable
        //   2. <cwd>/tools/
        //   3. <cwd>/../tools/  up to <cwd>/../../../../tools/
        //   4. <exe_dir>/tools/
        std::string findLinker(const char* exeName) {
            auto exists = [](const std::string& p) -> bool {
                std::FILE* f = std::fopen(p.c_str(), "rb");
                if (!f) return false;
                std::fclose(f);
                return true;
            };

            if (const char* env = std::getenv("VCB_TOOLS_DIR")) {
                std::string p = std::string(env) + "\\" + exeName;
                if (exists(p)) return p;
            }

            for (int up = 0; up <= 4; ++up) {
                std::string prefix;
                for (int k = 0; k < up; ++k) prefix += "..\\";
                std::string p = prefix + "tools\\" + exeName;
                if (exists(p)) return p;
            }

#ifdef _WIN32
            char buf[MAX_PATH];
            DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                std::string dir(buf, n);
                auto slash = dir.find_last_of("\\/");
                if (slash != std::string::npos) {
                    std::string p = dir.substr(0, slash + 1) +
                        "tools\\" + exeName;
                    if (exists(p)) return p;
                }
            }
#endif
            return {};
        }

        int cmdLink(const std::string& input,
            const std::string& output,
            const std::string& target,
            const std::string& entry,
            const std::vector<std::string>& libs) {
            std::error_code ec;
            auto parent = std::filesystem::path(output).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, ec);

            std::string cmd;
            if (target == "pe") {
                std::string lld = findLinker("lld-link.exe");
                if (lld.empty()) {
                    std::fprintf(stderr,
                        "vcb: cannot find lld-link.exe; place it in "
                        "tools\\lld-link.exe or set VCB_TOOLS_DIR\n");
                    return 1;
                }
                std::string e = entry.empty() ? "vayu_entry" : entry;
                cmd = "\"" + lld + "\" \"" + input + "\""
                    + " /entry:" + e
                    + " /subsystem:console"
                    + " /out:\"" + output + "\"";
                for (auto& l : libs) cmd += " \"" + l + "\"";
            }
            else if (target == "elf") {
                std::string lld = findLinker("ld.lld.exe");
                if (lld.empty()) {
                    std::fprintf(stderr,
                        "vcb: cannot find ld.lld.exe; place it in "
                        "tools\\ld.lld.exe or set VCB_TOOLS_DIR\n");
                    return 1;
                }
                std::string e = entry.empty() ? "vayu_entry" : entry;
                cmd = "\"" + lld + "\" -o \"" + output + "\""
                    + " \"" + input + "\""
                    + " -e " + e
                    + " --no-dynamic-linker"
                    + " -static";
            }
            else {
                std::fprintf(stderr,
                    "vcb: link --target '%s' not supported (pe|elf)\n",
                    target.c_str());
                return 1;
            }

            std::fprintf(stderr, "vcb: link: %s\n", cmd.c_str());
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                std::fprintf(stderr, "vcb: linker failed (exit %d)\n", rc);
                return 1;
            }
            std::fprintf(stderr, "vcb: linked %s\n", output.c_str());
            return 0;
        }

    } // namespace

    int runDriver(int argc, char** argv) {
        if (argc < 2) { printUsage(); return 2; }
        const char* cmd = argv[1];

        if (std::strcmp(cmd, "version") == 0) return cmdVersion();

        if (std::strcmp(cmd, "dump") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: dump requires a path\n");
                return 2;
            }
            return cmdDump(argv[2]);
        }

        if (std::strcmp(cmd, "headers") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: headers requires a path\n");
                return 2;
            }
            return dumpPeHeaders(argv[2]);
        }

        if (std::strcmp(cmd, "elfheaders") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: elfheaders requires a path\n");
                return 2;
            }
            return dumpElfHeaders(argv[2]);
        }

        if (std::strcmp(cmd, "link") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: link requires an input path\n");
                return 2;
            }
            std::string input;
            std::string output;
            std::string target = "pe";
            std::string entry;
            std::vector<std::string> libs;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-o") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr, "vcb: -o requires a path\n");
                        return 2;
                    }
                    output = argv[++i];
                }
                else if (a == "--target") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: --target requires an argument\n");
                        return 2;
                    }
                    target = argv[++i];
                }
                else if (a == "--entry") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: --entry requires a symbol name\n");
                        return 2;
                    }
                    entry = argv[++i];
                }
                else if (a == "--lib") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: --lib requires a path\n");
                        return 2;
                    }
                    libs.push_back(argv[++i]);
                }
                else if (input.empty()) {
                    input = a;
                }
                else {
                    std::fprintf(stderr,
                        "vcb: unexpected argument '%s'\n", a.c_str());
                    return 2;
                }
            }
            if (input.empty() || output.empty()) {
                std::fprintf(stderr,
                    "vcb: link requires <input> and -o <output>\n");
                return 2;
            }
            return cmdLink(input, output, target, entry, libs);
        }

        if (std::strcmp(cmd, "emit-obj") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: emit-obj requires a path\n");
                return 2;
            }
            std::string input;
            std::string output;
            std::string target = "pe";
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-o") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr, "vcb: -o requires a path\n");
                        return 2;
                    }
                    output = argv[++i];
                }
                else if (a == "--target") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: --target requires an argument\n");
                        return 2;
                    }
                    target = argv[++i];
                }
                else if (input.empty()) {
                    input = a;
                }
                else {
                    std::fprintf(stderr,
                        "vcb: unexpected argument '%s'\n", a.c_str());
                    return 2;
                }
            }
            if (input.empty() || output.empty()) {
                std::fprintf(stderr,
                    "vcb: emit-obj requires <input> and -o <output>\n");
                return 2;
            }
            return cmdEmitObj(input, output, target);
        }

        if (std::strcmp(cmd, "build") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: build requires a path\n");
                return 2;
            }
            std::string input;
            std::string output;
            std::string target = "pe";
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-o") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr, "vcb: -o requires a path\n");
                        return 2;
                    }
                    output = argv[++i];
                }
                else if (a == "--target") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: --target requires an argument\n");
                        return 2;
                    }
                    target = argv[++i];
                }
                else if (input.empty()) {
                    input = a;
                }
                else {
                    std::fprintf(stderr,
                        "vcb: unexpected argument '%s'\n", a.c_str());
                    return 2;
                }
            }
            if (input.empty() || output.empty()) {
                std::fprintf(stderr,
                    "vcb: build requires <input> and -o <output>\n");
                return 2;
            }
            return cmdBuild(input, output, target);
        }

        std::fprintf(stderr, "vcb: unknown command '%s'\n", cmd);
        printUsage();
        return 2;
    }

} // namespace vcb