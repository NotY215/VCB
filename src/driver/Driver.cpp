#include "vcb/Driver.hpp"
#include "vcb/Parser.hpp"
#include "vcb/Printer.hpp"
#include "vcb/X64.hpp"
#include "vcb/Pe.hpp"
#include <cstdio>
#include <cstring>
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
                "  vcb build   <file.vcbir> -o <out> [--target pe]\n"
                "                                             emit a native image\n");
        }

        int cmdVersion() {
            std::printf("vcb 0.2.0 (Phase 26 Part 2)\n");
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
            if (target != "pe") {
                std::fprintf(stderr,
                    "vcb: --target '%s' not implemented yet "
                    "(only 'pe' in Part 2)\n", target.c_str());
                return 1;
            }
            try {
                Module m = parseFile(input);
                CodegenResult cg = codegenX64Pe(m);

                PeInputs pi;
                pi.text = &cg.text;
                pi.idata = &cg.idata;
                pi.entryOffset = cg.entryOffset;
                pi.textRva = 0x1000;
                pi.idataRva = cg.idataRva;
                pi.iatRva = cg.iatRva;
                pi.importRva = cg.importRva;
                pi.importSize = cg.importSize;

                std::vector<uint8_t> image = writePe(pi);

                std::FILE* f = std::fopen(output.c_str(), "wb");
                if (!f) {
                    std::fprintf(stderr, "vcb: cannot write '%s'\n", output.c_str());
                    return 1;
                }
                std::fwrite(image.data(), 1, image.size(), f);
                std::fclose(f);
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