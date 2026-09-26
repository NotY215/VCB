#pragma once
#include <cstdint>
#include <vector>

namespace vcb {

    struct PeInputs {
        const std::vector<uint8_t>* text = nullptr;
        const std::vector<uint8_t>* idata = nullptr;
        uint32_t                    entryOffset = 0;
        uint32_t                    textRva = 0x1000;
        uint32_t                    idataRva = 0x2000;
        uint32_t                    iatRva = 0;
        uint32_t                    importRva = 0;
        uint32_t                    importSize = 0;
    };

    std::vector<uint8_t> writePe(const PeInputs& in);

} // namespace vcb