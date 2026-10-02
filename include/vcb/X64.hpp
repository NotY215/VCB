#pragma once
#include "vcb/Ir.hpp"
#include <cstdint>
#include <vector>

namespace vcb {

    // One entry per function that needs a .pdata RUNTIME_FUNCTION.
    //   funcOffset   — offset of function within .text (bytes)
    //   funcSize     — size of function (bytes)
    //   unwindOffset — offset of UNWIND_INFO record within .xdata
    struct UnwindEntry {
        uint32_t funcOffset = 0;
        uint32_t funcSize = 0;
        uint32_t unwindOffset = 0;
    };

    struct CodegenResult {
        std::vector<uint8_t> text;
        std::vector<uint8_t> rdata;
        std::vector<uint8_t> idata;
        // UNWIND_INFO blob for .xdata.  Empty for --target elf.
        std::vector<uint8_t> xdata;
        // RUNTIME_FUNCTION entries for .pdata.  Empty for --target elf.
        std::vector<UnwindEntry> unwindEntries;
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