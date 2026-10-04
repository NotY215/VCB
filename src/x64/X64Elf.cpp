#include "vcb/X64.hpp"
#include "vcb/Runtime.hpp"
#include "vcb/X64Common.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcb {

    using namespace vcb::x64common;

    CodegenResult codegenX64Elf(const Module& m) {
        CodegenResult r;
        const uint32_t textRva = r.textRva;
        const uint32_t textLimit = r.rdataRva - textRva;

        auto symbolOffsets = emitRuntimeLinux(r.text, textRva, m);

        std::vector<CallFixup>    callFixups;
        std::vector<StringFixup>  stringFixups;
        StringTable               strings;
        std::unordered_map<std::string, Frame> frames;

        for (auto& fn : m.functions) {
            if (symbolOffsets.count(fn.name))
                throw std::runtime_error(
                    "codegen: function '" + fn.name +
                    "' conflicts with a runtime symbol");
            frames[fn.name] = layoutFunction(fn);
        }

        for (auto& fn : m.functions) {
            symbolOffsets[fn.name] = (uint32_t)r.text.size();
            FunctionEmitter fe(r.text, fn, frames[fn.name],
                callFixups, strings, stringFixups);
            fe.run();
        }

        if (!symbolOffsets.count("main"))
            throw std::runtime_error(
                "codegen: no 'main' function defined; cannot build an executable");

        // Linux entry stub:
        //   sub rsp, 40
        //   call main
        //   mov edi, eax
        //   mov eax, 60      ; SYS_exit
        //   syscall
        //   int3
        r.entryOffset = (uint32_t)r.text.size();
        {
            Asm a(r.text);
            a.subRspImm32(40);
            uint32_t callPos = (uint32_t)r.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RDI, RAX);
            a.movImm32(RAX, 60);
            a.syscall();
            a.int3();
        }

        r.rdata = strings.blob;

        for (auto& sf : stringFixups) {
            uint32_t blobAbs = r.rdataRva + sf.blobOff;
            int32_t  rel = (int32_t)blobAbs - (int32_t)(sf.pos + 4);
            std::memcpy(&r.text[sf.pos], &rel, 4);
        }

        if (r.text.size() > textLimit)
            throw std::runtime_error(
                "codegen: .text exceeds 4 KB; move more code into .rodata "
                "or add runtime pruning");

        for (auto& cf : callFixups) {
            auto it = symbolOffsets.find(cf.target);
            if (it == symbolOffsets.end())
                throw std::runtime_error(
                    "codegen: undefined function '" + cf.target + "'");
            int32_t rel = (int32_t)it->second - (int32_t)(cf.pos + 4);
            std::memcpy(&r.text[cf.pos], &rel, 4);
        }

        return r;
    }

} // namespace vcb