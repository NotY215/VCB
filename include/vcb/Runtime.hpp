#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcb {

    struct RuntimeImports {
        uint32_t iatGetStdHandle = 0;
        uint32_t iatWriteFile = 0;
        uint32_t iatExitProcess = 0;
        uint32_t iatMalloc = 0;
        uint32_t iatRealloc = 0;
        uint32_t iatFree = 0;
    };

    std::unordered_map<std::string, uint32_t> emitRuntime(
        std::vector<uint8_t>& text,
        uint32_t              textRva,
        const RuntimeImports& imports);

} // namespace vcb