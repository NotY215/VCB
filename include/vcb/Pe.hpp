#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    struct PeInputs {
        const std::vector<uint8_t>* text = nullptr;
        const std::vector<uint8_t>* idata = nullptr;
        uint32_t                    entryOffset = 0;
        uint32_t                    textRva = 0x1000;
        uint32_t                    idataRva = 0x2000;
        uint32_t                    iatRva = 0;
        uint32_t                    iatSize = 0;
        uint32_t                    importRva = 0;
        uint32_t                    importSize = 0;
    };

    std::vector<uint8_t> writePe(const PeInputs& in);

    // Write `bytes` to `finalPath` atomically: create `finalPath.tmp`,
    // fsync it, then MoveFileEx (Windows) / rename (POSIX) onto the
    // final name.  This closes the race where Windows Defender's
    // file-write scanner holds an exclusive handle on a just-created
    // file, causing cmd.exe to report "Access is denied" on an
    // immediate launch.  Returns 0 on success, non-zero on failure.
    int writePeAtomic(const std::string& finalPath,
        const std::vector<uint8_t>& bytes);

    // Human-readable dump of the PE header of an existing file.  Used
    // by `vcb headers <file>` to diagnose load failures without a
    // disassembler.
    int dumpPeHeaders(const std::string& path);

} // namespace vcb