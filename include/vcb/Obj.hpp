#pragma once
#include "vcb/X64Common.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    // COFF relocation type codes (numeric values identical to the
    // IMAGE_REL_AMD64_* constants).  Reuse the shared type so a single
    // Reloc vector can be fed either into the PE writer or the COFF
    // writer without conversion.
    using CoffRelocType = x64common::RelocType;
    using CoffReloc     = x64common::Reloc;

    struct CoffSection {
        std::string                   name;
        uint32_t                      characteristics = 0;
        std::vector<uint8_t>          data;
        std::vector<x64common::Reloc> relocs;
    };

    struct CoffSymbol {
        std::string name;
        int16_t     section = 0;        // 1-based; 0 = undefined
        uint32_t    value = 0;
        uint8_t     storageClass = 2;   // 2 = EXTERNAL, 3 = STATIC
        uint16_t    type = 0;           // 0x20 = DTYPE_FUNCTION
    };

    struct CoffFile {
        std::vector<CoffSection> sections;
        std::vector<CoffSymbol>  symbols;
    };

    // Name of the EXTERNAL symbol that codegenX64Coff() emits for the
    // process entry stub.  Pass "/entry:vayu_entry" to lld-link.
    extern const char* const kCoffEntrySymbol;

    // -----------------------------------------------------------------
    // Phase 28.2 -- ELF64 REL object files (Linux).
    // -----------------------------------------------------------------

    // STB_* / STT_* bind+type packed into a single byte.
    constexpr uint8_t kElfBindLocal  = 0;
    constexpr uint8_t kElfBindGlobal = 1;
    constexpr uint8_t kElfTypeNone   = 0;
    constexpr uint8_t kElfTypeObject = 1;
    constexpr uint8_t kElfTypeFunc   = 2;

    // R_X86_64_* relocation types.  Only PC32 (RIP-relative) is used.
    constexpr uint32_t kElfRelPc32 = 2;

    struct ElfSymbol {
        std::string name;
        uint8_t     info = 0;         // (bind << 4) | type
        uint16_t    shndx = 0;        // section index; 0 = SHN_UNDEF
        uint64_t    value = 0;        // offset within section (ET_REL)
        uint64_t    size = 0;
    };

    struct ElfTextReloc {
        uint64_t offset = 0;          // byte offset within .text
        uint32_t symbolIdx = 0;       // index into ElfFile::symbols
        uint32_t type = kElfRelPc32;
        int64_t  addend = -4;         // PC-relative: S + A - P, A = -4
    };

    struct ElfFile {
        std::vector<uint8_t>       text;
        std::vector<uint8_t>       rodata;
        std::vector<ElfSymbol>     symbols;   // symbols[0] must be null
        std::vector<ElfTextReloc>  textRelocs;
    };

    std::vector<uint8_t> writeElfObj(const ElfFile& f);

    int writeElfObjAtomic(const std::string& finalPath,
                          const std::vector<uint8_t>& bytes);

    // Deterministic serialisation: TimeDateStamp = 0, no rand(), no
    // address-of.  Byte-for-byte reproducible for the same input.
    std::vector<uint8_t> writeCoff(const CoffFile& f);

    int writeCoffAtomic(const std::string& finalPath,
                        const std::vector<uint8_t>& bytes);

} // namespace vcb