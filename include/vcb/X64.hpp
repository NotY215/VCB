#pragma once
#include "vcb/Ir.hpp"
#include <cstdint>
#include <vector>

namespace vcb {

    struct CodegenResult {
        std::vector<uint8_t> text;
        std::vector<uint8_t> rdata;
        std::vector<uint8_t> idata;
        uint32_t textRva = 0x1000;
        uint32_t rdataRva = 0x2000;
        uint32_t idataRva = 0x3000;
        uint32_t entryOffset = 0;
        uint32_t iatRva = 0;
        uint32_t iatSize = 0;
        uint32_t importRva = 0;
        uint32_t importSize = 0;
    };

    CodegenResult codegenX64Pe(const Module& m);
    CodegenResult codegenX64Elf(const Module& m);

} // namespace vcb