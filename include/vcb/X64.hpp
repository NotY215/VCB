#pragma once
#include "vcb/Ir.hpp"
#include <cstdint>
#include <string>
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

    // One DLL and the functions imported from it.  Consumed by
    // writePe() in Pe.cpp (Part 12).  If your X64.cpp still contains
    // buildImports(), leave this struct in place — it is harmless.
    struct ImportDll {
        std::string dll;
        std::vector<std::string> funcs;
    };

    struct CodegenResult {
        std::vector<uint8_t> text;
        std::vector<uint8_t> rdata;
        std::vector<uint8_t> idata;

        // .xdata is a UNWIND_INFO blob.  .pdata is a RUNTIME_FUNCTION
        // array derived from unwindEntries.  Both are empty for
        // --target elf.
        std::vector<uint8_t> xdata;
        std::vector<UnwindEntry> unwindEntries;

        // Import table inputs for writePe().  Populated by codegenX64Pe
        // when Part 12 is applied; ignored otherwise.
        std::vector<ImportDll> importDlls;

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