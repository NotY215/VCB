#include "vcb/Driver.hpp"
#include "vcb/Parser.hpp"
#include "vcb/Printer.hpp"
#include "vcb/X64.hpp"
#include "vcb/Pe.hpp"
#include "vcb/Elf.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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
                "  vcb headers    <file.exe>                  dump a PE header\n"
                "  vcb elfheaders <file.elf>                  dump an ELF header\n");
        }

        int cmdVersion() {
            std::printf("vcb 0.5.2 (Phase 27 Part 9, loader-consistent PE)\n");
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
            // this, writePeAtomic / writeElfAtomic fail at fopen(tmp) with
            // "cannot write '<out>.tmp'" when the parent is missing.
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
                    std::printf("vcb: wrote %s (%zu bytes)\n",
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
                // The image is fully PIC: every reference is RIP-relative
                // or an RVA that the loader resolves by adding the base
                // (import table entries).  There are no absolute VAs in
                // .text, so the .reloc section would legitimately be
                // empty.  Some Windows 11 builds reject images that set
                // DYNAMIC_BASE with a zero-entry .reloc, so disable ASLR
                // and set RELOCS_STRIPPED — the coherent, fixed-base
                // configuration.
                pi.enableAslr = false;
                pi.entryOffset = cg.entryOffset;
                pi.iatRva = cg.iatRva;
                pi.iatSize = cg.iatSize;
                pi.importRva = cg.importRva;
                pi.importSize = cg.importSize;

                std::vector<uint8_t> image = writePe(pi);

                int wrc = writePeAtomic(output, image);
                if (wrc != 0) return 1;
                std::printf("vcb: wrote %s (%zu bytes)\n",
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
                        std::fprintf(stderr, "vcb: --target requires an argument\n");
                        return 2;
                    }
                    target = argv[++i];
                }
                else if (input.empty()) {
                    input = a;
                }
                else {
                    std::fprintf(stderr, "vcb: unexpected argument '%s'\n", a.c_str());
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