#pragma once
#include "vcb/Ir.hpp"
#include "vcb/Obj.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    // One entry per function that needs a .pdata RUNTIME_FUNCTION.
    //   funcOffset   -- offset of function within .text (bytes)
    //   funcSize     -- size of function (bytes)
    //   unwindOffset -- offset of UNWIND_INFO record within .xdata
    struct UnwindEntry {
        uint32_t funcOffset = 0;
        uint32_t funcSize = 0;
        uint32_t unwindOffset = 0;
        // Used only while building .xdata.  Runtime and user functions
        // use the full frame prolog (push rbp; mov rbp, rsp; sub rsp, N)
        // and set stubOnly=false.  The entry stub uses a bare sub rsp, N
        // and sets stubOnly=true.  frameSize is N.
        uint32_t frameSize = 0;
        bool     stubOnly = false;
    };

    // One DLL and the functions imported from it.  Consumed by
    // writePe() in Pe.cpp (Part 12).  If your X64.cpp still contains
    // buildImports(), leave this struct in place -- it is harmless.
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

    // Phase 28.1 -- COFF object emission.  Same code paths as
    // codegenX64Pe, but external references (IAT calls and string LEAs)
    // become relocations instead of absolute RVAs.
    CoffFile codegenX64Coff(const Module& m);

    // Phase 28.2 -- ELF64 REL object emission.
    ElfFile codegenX64ElfObj(const Module& m);

} // namespace vcb