#pragma once
#include "vcb/Ir.hpp"
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
        const RuntimeImports& imports,
        const Module& userModule);

    // Linux x86-64 syscall runtime.  No IATs, no libc.  Emits the subset
    // of print/exit primitives that Part 1 supports.  Every emitter is
    // self-contained; no dependency closure is required.
    std::unordered_map<std::string, uint32_t> emitRuntimeLinux(
        std::vector<uint8_t>& text,
        uint32_t              textRva,
        const Module& userModule);

} // namespace vcb