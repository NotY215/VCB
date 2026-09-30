#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vcb {

    struct PeInputs {
        const std::vector<uint8_t>* text = nullptr;
        const std::vector<uint8_t>* rdata = nullptr;
        const std::vector<uint8_t>* idata = nullptr;
        uint32_t textRva = 0x1000;
        uint32_t rdataRva = 0x2000;
        uint32_t idataRva = 0x3000;
        uint32_t entryOffset = 0;
        uint32_t iatRva = 0;
        uint32_t iatSize = 0;
        uint32_t importRva = 0;
        uint32_t importSize = 0;
    };

    std::vector<uint8_t> writePe(const PeInputs& in);

    // Write `bytes` to `finalPath` atomically: create <path>.tmp,
    // flush, close, then MoveFileEx/rename onto finalPath.  Closes
    // the race where Defender holds an exclusive handle on a
    // just-created output file.
    int writePeAtomic(const std::string& finalPath,
        const std::vector<uint8_t>& bytes);

    int dumpPeHeaders(const std::string& path);

} // namespace vcb