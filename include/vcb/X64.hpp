#pragma once
#include "vcb/Ir.hpp"
#include <cstdint>
#include <vector>

namespace vcb {

    struct CodegenResult {
        std::vector<uint8_t> text;
        std::vector<uint8_t> idata;
        uint32_t             entryOffset = 0;
        uint32_t             iatRva = 0;
        uint32_t             importRva = 0;
        uint32_t             importSize = 0;
        uint32_t             idataRva = 0;
    };

    CodegenResult codegenX64Pe(const Module& m);

} // namespace vcb