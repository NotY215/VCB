#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    // Static Linux x86-64 ELF, ET_EXEC, single PT_LOAD (R+X).
    // Layout: ELF header + program header + .text + .rodata in one
    // loadable segment starting at vaddr 0x400000.  Section headers
    // (.text/.rodata/.shstrtab) present for tooling, not loadable.
    struct ElfInputs {
        const std::vector<uint8_t>* text = nullptr;
        const std::vector<uint8_t>* rodata = nullptr;
        uint32_t entryOffset = 0;    // offset within .text
    };

    std::vector<uint8_t> writeElf(const ElfInputs& in);

    int writeElfAtomic(const std::string& finalPath,
        const std::vector<uint8_t>& bytes);

    // Print the ELF header, program headers, and section headers for a
    // built image.  Used by `vcb elfheaders` to verify structural
    // correctness when no Linux host is available.
    int dumpElfHeaders(const std::string& path);

} // namespace vcb