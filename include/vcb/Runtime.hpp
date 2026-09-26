#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcb {

    std::unordered_map<std::string, uint32_t> emitRuntime(
        std::vector<uint8_t>& text,
        uint32_t              textRva,
        uint32_t              iatGetStdHandle,
        uint32_t              iatWriteFile,
        uint32_t              iatExitProcess);

} // namespace vcb