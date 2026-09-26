#include "vcb/Driver.hpp"
#include "vcb/Parser.hpp"
#include "vcb/Printer.hpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace vcb {

    namespace {

        void printUsage() {
            std::fprintf(stderr,
                "usage: vcb <command> [args]\n"
                "\n"
                "Commands:\n"
                "  vcb version                print version\n"
                "  vcb dump <file.vcbir>      parse and pretty-print\n"
                "\n"
                "Compilation is added in Phase 26 Part 2:\n"
                "  vcb build <file.vcbir> -o <out> --target <pe|elf>\n");
        }

        int cmdVersion() {
            std::printf("vcb 0.1.0 (Phase 26 Part 1)\n");
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

        std::fprintf(stderr, "vcb: unknown command '%s'\n", cmd);
        printUsage();
        return 2;
    }

} // namespace vcb