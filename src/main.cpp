// VCB — Vayu Compiler Backend
// main.cpp — driver, CLI, pipeline orchestration
#include "vcb.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

static void usage() {
    std::fprintf(stderr,
        "VCB — Vayu Compiler Backend\n"
        "usage: vcb [input.vcb] [-o output.s]\n"
        "  no args  -> read from stdin, write to stdout\n");
}

int main(int argc, char** argv) {
    std::string inputPath, outputPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { outputPath = argv[++i]; }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] != '-') { inputPath = a; }
        else { usage(); return 1; }
    }

    std::string src;
    if (!inputPath.empty()) {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "vcb: cannot open %s\n", inputPath.c_str());
            return 1;
        }
        std::ostringstream ss; ss << in.rdbuf();
        src = ss.str();
    }
    else {
        std::ostringstream ss; ss << std::cin.rdbuf();
        src = ss.str();
    }

    vcb::Module mod;
    std::string err;
    if (!vcb::parseIR(src, mod, err)) {
        std::fprintf(stderr, "vcb: parse error: %s\n", err.c_str());
        return 1;
    }

    vcb::optimize(mod);
    vcb::lower(mod);

    std::ofstream outFile;
    std::ostream* out = &std::cout;
    if (!outputPath.empty()) {
        outFile.open(outputPath, std::ios::binary);
        if (!outFile) {
            std::fprintf(stderr, "vcb: cannot write %s\n", outputPath.c_str());
            return 1;
        }
        out = &outFile;
    }

    vcb::codegen(mod, *out);
    return 0;
}