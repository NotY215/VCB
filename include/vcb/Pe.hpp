#pragma once
#include "vcb/X64.hpp"     // for UnwindEntry
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    // Section characteristics (subset we use).
    constexpr uint32_t SCN_CNT_CODE = 0x00000020;
    constexpr uint32_t SCN_CNT_INITIALIZED_DATA = 0x00000040;
    constexpr uint32_t SCN_MEM_EXECUTE = 0x20000000;
    constexpr uint32_t SCN_MEM_READ = 0x40000000;
    constexpr uint32_t SCN_MEM_WRITE = 0x80000000;

    struct PeInputs {
        const std::vector<uint8_t>* text = nullptr;
        const std::vector<uint8_t>* rdata = nullptr;
        const std::vector<uint8_t>* idata = nullptr;

        // Unwind metadata.  If `xdata` is non-null and non-empty, a
        // .xdata section is emitted.  If `unwindEntries` is non-empty,
        // a .pdata section is emitted with one RUNTIME_FUNCTION per
        // entry.  Both must be provided together (a .pdata entry
        // references an offset into .xdata).
        const std::vector<uint8_t>* xdata = nullptr;
        const std::vector<UnwindEntry>* unwindEntries = nullptr;

        // Enable Windows ASLR (DYNAMIC_BASE | HIGH_ENTROPY_VA) and
        // emit a minimal .reloc section.  The generated image is fully
        // position-independent (all references are RIP-relative or
        // section-relative RVAs), so the .reloc contains zero
        // relocation entries — but its presence, combined with
        // DYNAMIC_BASE and clearing IMAGE_FILE_RELOCS_STRIPPED, is a
        // coherent, loader-accepted configuration.
        bool enableAslr = false;

        // Inputs.  `entryOffset` is required (offset within .text of
        // the entry point).  `importRva`/`importSize`/`iatRva`/`iatSize`
        // are required (produced by codegenX64Pe against .idata RVA
        // 0x3000, which this writer always honours).
        uint32_t entryOffset = 0;
        uint32_t iatRva = 0;
        uint32_t iatSize = 0;
        uint32_t importRva = 0;
        uint32_t importSize = 0;

        // Outputs.  Filled in by writePe() with the actual RVAs used
        // in the emitted image.
        uint32_t textRva = 0;
        uint32_t rdataRva = 0;
        uint32_t idataRva = 0;
        uint32_t pdataRva = 0;
        uint32_t xdataRva = 0;
        uint32_t relocRva = 0;
    };

    std::vector<uint8_t> writePe(PeInputs& in);

    int writePeAtomic(const std::string& finalPath,
        const std::vector<uint8_t>& bytes);

    int dumpPeHeaders(const std::string& path);

} // namespace vcb