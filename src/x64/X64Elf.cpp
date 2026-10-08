#include "vcb/X64.hpp"
#include "vcb/Runtime.hpp"
#include "vcb/X64Common.hpp"
#include "vcb/Obj.hpp"
#include <algorithm>
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
            // RIP at the end of the 7-byte LEA is textRva + sf.pos + 4.
            // Omitting textRva made every string pointer land 0x1000
            // bytes before the blob.  Same fix as the PE path.
            int32_t  rel = (int32_t)blobAbs - (int32_t)(textRva + sf.pos + 4);
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

    // -------------------------------------------------------------------
    // Phase 28.2 -- ELF64 REL object emission.
    // -------------------------------------------------------------------
    ElfFile codegenX64ElfObj(const Module& m) {
        ElfFile ef;

        std::vector<Reloc> textRelocs;

        // textRva = 0 makes every intra-.text call a section-local
        // displacement.  The runtime does not use IATs on Linux, so
        // there are no external references to record here.
        auto symbolOffsets = emitRuntimeLinux(ef.text, 0, m);

        std::vector<CallFixup>   callFixups;
        std::vector<StringFixup> stringFixups;
        StringTable              strings;
        std::unordered_map<std::string, Frame> frames;

        for (auto& fn : m.functions) {
            if (symbolOffsets.count(fn.name))
                throw std::runtime_error(
                    "codegen: function '" + fn.name +
                    "' conflicts with a runtime symbol");
            frames[fn.name] = layoutFunction(fn);
        }

        for (auto& fn : m.functions) {
            symbolOffsets[fn.name] = (uint32_t)ef.text.size();
            FunctionEmitter fe(ef.text, fn, frames[fn.name],
                callFixups, strings, stringFixups, &textRelocs);
            fe.run();
        }

        if (!symbolOffsets.count("main"))
            throw std::runtime_error(
                "codegen: no 'main' function defined; cannot build an executable");

        // Entry stub.  Linux sys_exit(60); rdi = exit code; rax = 60.
        uint32_t entryOffset = (uint32_t)ef.text.size();
        {
            Asm a(ef.text);
            a.outRelocs = &textRelocs;
            a.subRspImm32(40);
            uint32_t callPos = (uint32_t)ef.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RDI, RAX);
            a.movImm32(RAX, 60);
            a.syscall();
            a.int3();
        }
        symbolOffsets["vayu_entry"] = entryOffset;

        // Section-local patch of every call to another .text function.
        for (auto& cfix : callFixups) {
            auto it = symbolOffsets.find(cfix.target);
            if (it == symbolOffsets.end())
                throw std::runtime_error(
                    "codegen: undefined function '" + cfix.target + "'");
            int32_t rel = (int32_t)it->second - (int32_t)(cfix.pos + 4);
            std::memcpy(&ef.text[cfix.pos], &rel, 4);
        }

        // ---- .rodata ---------------------------------------------------
        ef.rodata = strings.blob;

        // ---- Symbol table ----------------------------------------------
        // Required shape: symbols[0] is null; LOCAL symbols (strings)
        // come before GLOBAL symbols (functions).  writeElfObj() reads
        // sh_info from the first GLOBAL.
        ElfSymbol nullSym;
        nullSym.info = (kElfBindLocal << 4) | kElfTypeNone;
        nullSym.shndx = 0;
        ef.symbols.push_back(std::move(nullSym));

        std::unordered_map<std::string, uint32_t> strSymIdx;
        for (size_t i = 0; i < strings.symbolNames.size(); ++i) {
            ElfSymbol s;
            s.name = strings.symbolNames[i];
            s.info = (kElfBindLocal << 4) | kElfTypeObject;
            s.shndx = 3;   // .rodata
            s.value = strings.symbolOffsets[i];
            s.size = 0;
            strSymIdx[s.name] = (uint32_t)ef.symbols.size();
            ef.symbols.push_back(std::move(s));
        }

        std::vector<std::pair<std::string, uint32_t>> ordered(
            symbolOffsets.begin(), symbolOffsets.end());
        std::sort(ordered.begin(), ordered.end(),
            [](const std::pair<std::string, uint32_t>& a,
               const std::pair<std::string, uint32_t>& b) {
                return a.first < b.first;
            });
        std::unordered_map<std::string, uint32_t> fnSymIdx;
        for (auto& kv : ordered) {
            ElfSymbol s;
            s.name = kv.first;
            s.info = (kElfBindGlobal << 4) | kElfTypeFunc;
            s.shndx = 1;   // .text
            s.value = kv.second;
            s.size = 0;
            fnSymIdx[s.name] = (uint32_t)ef.symbols.size();
            ef.symbols.push_back(std::move(s));
        }

        // ---- Relocations ----------------------------------------------
        // Every Reloc captured during emit maps to an .rodata symbol.
        for (auto& r : textRelocs) {
            auto it = strSymIdx.find(r.symbol);
            if (it == strSymIdx.end())
                throw std::runtime_error(
                    "codegen: relocation against unknown symbol '" +
                    r.symbol + "'");
            ElfTextReloc er;
            er.offset = r.offset;
            er.symbolIdx = it->second;
            er.type = kElfRelPc32;
            er.addend = -4;
            ef.textRelocs.push_back(er);
        }

        return ef;
    }

} // namespace vcb