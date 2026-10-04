#include "vcb/X64.hpp"
#include "vcb/Runtime.hpp"
#include "vcb/X64Common.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcb {

    using namespace vcb::x64common;

    namespace {

        // ---- PE import-table construction -----------------------------
        // Kept here pending Phase 27 Part 12, which will move it into
        // writePe() so it runs after the .idata RVA is assigned.

        struct ImportSpec {
            std::string              dll;
            std::vector<std::string> funcs;
        };

        struct ImportLayout {
            std::vector<uint8_t>                      idata;
            uint32_t                                  importRva = 0;
            uint32_t                                  importSize = 0;
            uint32_t                                  iatRva = 0;
            uint32_t                                  iatSize = 0;
            std::unordered_map<std::string, uint32_t> iatByName;
        };

        ImportLayout buildImports(uint32_t idataRva,
            const std::vector<ImportSpec>& imports) {
            const uint32_t DESC_SIZE = 40;
            uint32_t descTotal = DESC_SIZE * (uint32_t)(imports.size() + 1);

            uint32_t iltTotal = 0, iatTotal = 0;
            for (auto& im : imports) {
                uint32_t n = (uint32_t)im.funcs.size() + 1;
                iltTotal += n * 8;
                iatTotal += n * 8;
            }

            std::vector<uint32_t> hintOffsets;
            uint32_t hintTotal = 0;
            for (auto& im : imports) {
                for (auto& f : im.funcs) {
                    hintOffsets.push_back(hintTotal);
                    uint32_t len = 2 + (uint32_t)f.size() + 1;
                    if (len & 1) ++len;
                    hintTotal += len;
                }
            }

            std::vector<uint32_t> dllNameOffsets;
            uint32_t dllNamesTotal = 0;
            for (auto& im : imports) {
                dllNameOffsets.push_back(dllNamesTotal);
                uint32_t len = (uint32_t)im.dll.size() + 1;
                if (len & 1) ++len;
                dllNamesTotal += len;
            }

            uint32_t offDesc = 0;
            uint32_t offIlt = offDesc + descTotal;
            uint32_t offIat = offIlt + iltTotal;
            uint32_t offHint = offIat + iatTotal;
            uint32_t offDll = offHint + hintTotal;
            uint32_t total = offDll + dllNamesTotal;

            ImportLayout L;
            L.idata.assign(total, 0);
            L.importRva = idataRva + offDesc;
            L.importSize = descTotal;
            L.iatRva = idataRva + offIat;
            L.iatSize = iatTotal;

            auto putU32 = [&](uint32_t at, uint32_t v) {
                L.idata[at + 0] = (uint8_t)(v);
                L.idata[at + 1] = (uint8_t)(v >> 8);
                L.idata[at + 2] = (uint8_t)(v >> 16);
                L.idata[at + 3] = (uint8_t)(v >> 24);
                };
            auto putU64 = [&](uint32_t at, uint64_t v) {
                for (int i = 0; i < 8; ++i) L.idata[at + i] = (uint8_t)(v >> (8 * i));
                };

            uint32_t iltCur = offIlt, iatCur = offIat, hintCur = 0;

            for (size_t di = 0; di < imports.size(); ++di) {
                uint32_t descAt = offDesc + (uint32_t)di * DESC_SIZE;
                uint32_t thisIlt = iltCur;
                uint32_t thisIat = iatCur;
                uint32_t thisDll = idataRva + offDll + dllNameOffsets[di];

                putU32(descAt + 0, idataRva + thisIlt);
                putU32(descAt + 4, 0);
                putU32(descAt + 8, 0);
                putU32(descAt + 12, thisDll);
                putU32(descAt + 16, idataRva + thisIat);

                for (size_t fi = 0; fi < imports[di].funcs.size(); ++fi) {
                    uint32_t hnRva = idataRva + offHint + hintOffsets[hintCur];
                    putU64(thisIlt + (uint32_t)fi * 8, (uint64_t)hnRva);
                    putU64(thisIat + (uint32_t)fi * 8, (uint64_t)hnRva);
                    L.iatByName[imports[di].funcs[fi]] =
                        idataRva + thisIat + (uint32_t)fi * 8;
                    uint32_t hnAt = offHint + hintOffsets[hintCur];
                    L.idata[hnAt + 0] = 0;
                    L.idata[hnAt + 1] = 0;
                    std::memcpy(&L.idata[hnAt + 2],
                        imports[di].funcs[fi].c_str(),
                        imports[di].funcs[fi].size() + 1);
                    ++hintCur;
                }
                iltCur += ((uint32_t)imports[di].funcs.size() + 1) * 8;
                iatCur += ((uint32_t)imports[di].funcs.size() + 1) * 8;
            }
            for (size_t di = 0; di < imports.size(); ++di) {
                std::memcpy(&L.idata[offDll + dllNameOffsets[di]],
                    imports[di].dll.c_str(),
                    imports[di].dll.size() + 1);
            }
            return L;
        }

    } // namespace

    CodegenResult codegenX64Pe(const Module& m) {
        CodegenResult r;
        const uint32_t textRva = r.textRva;
        const uint32_t textLimit = r.rdataRva - textRva;

        std::vector<ImportSpec> imports = {
            { "kernel32.dll", { "ExitProcess", "GetStdHandle", "WriteFile",
                                "GetProcessHeap", "HeapAlloc", "HeapReAlloc" } }
        };
        constexpr uint32_t kIdataRva = 0x3000;
        ImportLayout layout = buildImports(kIdataRva, imports);

        r.idata = std::move(layout.idata);
        r.importRva = layout.importRva;
        r.importSize = layout.importSize;
        r.iatRva = layout.iatRva;
        r.iatSize = layout.iatSize;

        RuntimeImports ri;
        ri.iatGetStdHandle = layout.iatByName.at("GetStdHandle");
        ri.iatWriteFile = layout.iatByName.at("WriteFile");
        ri.iatExitProcess = layout.iatByName.at("ExitProcess");
        ri.iatGetProcessHeap = layout.iatByName.at("GetProcessHeap");
        ri.iatHeapAlloc = layout.iatByName.at("HeapAlloc");
        ri.iatHeapReAlloc = layout.iatByName.at("HeapReAlloc");

        auto symbolOffsets = emitRuntime(r.text, textRva, ri, m,
            &r.unwindEntries);

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

        struct EmittedFunc {
            uint32_t startRva = 0;
            uint32_t endRva = 0;
            uint32_t frameSize = 0;
        };
        std::vector<EmittedFunc> userFuncs;
        userFuncs.reserve(m.functions.size());

        for (auto& fn : m.functions) {
            uint32_t startOff = (uint32_t)r.text.size();
            symbolOffsets[fn.name] = startOff;
            FunctionEmitter fe(r.text, fn, frames[fn.name],
                callFixups, strings, stringFixups);
            fe.run();
            uint32_t endOff = (uint32_t)r.text.size();
            userFuncs.push_back({
                textRva + startOff,
                textRva + endOff,
                (uint32_t)frames[fn.name].frameSize
                });
        }

        if (!symbolOffsets.count("main"))
            throw std::runtime_error(
                "codegen: no 'main' function defined; cannot build an executable");

        // ---- Entry stub -----------------------------------------------
        // sub rsp, 40
        // call main
        // mov ecx, eax
        // call [ExitProcess]
        // int3
        r.entryOffset = (uint32_t)r.text.size();
        {
            Asm a(r.text);
            a.subRspImm32(40);
            uint32_t callPos = (uint32_t)r.text.size() + 1;
            a.callRel32Placeholder();
            callFixups.push_back({ callPos, "main" });
            a.mov32RegReg(RCX, RAX);
            uint32_t iatRelPos = (uint32_t)r.text.size() + 2;
            a.callIndirectRip();
            a.int3();
            uint32_t instrRva = textRva + (iatRelPos - 2);
            int32_t  rel = (int32_t)ri.iatExitProcess -
                (int32_t)(instrRva + 6);
            std::memcpy(&r.text[iatRelPos], &rel, 4);
        }

        // ---- String blob goes to .rdata -------------------------------
        r.rdata = strings.blob;

        for (auto& sf : stringFixups) {
            uint32_t blobAbs = r.rdataRva + sf.blobOff;
            int32_t  rel = (int32_t)blobAbs - (int32_t)(sf.pos + 4);
            std::memcpy(&r.text[sf.pos], &rel, 4);
        }

        if (r.text.size() > textLimit)
            throw std::runtime_error(
                "codegen: .text exceeds 4 KB; move more code into .rdata "
                "or add runtime pruning");

        for (auto& cf : callFixups) {
            auto it = symbolOffsets.find(cf.target);
            if (it == symbolOffsets.end())
                throw std::runtime_error(
                    "codegen: undefined function '" + cf.target + "'");
            int32_t rel = (int32_t)it->second - (int32_t)(cf.pos + 4);
            std::memcpy(&r.text[cf.pos], &rel, 4);
        }

        // ---- Build .xdata (UNWIND_INFO blob) and .pdata (RUNTIME_FUNCTION
        //      array).
        //
        // r.unwindEntries currently contains, unsorted and without
        // .xdata offsets:
        //   - every runtime function with a non-empty prolog, pushed by
        //     emitRuntime()
        //   - every user function, pushed below
        //   - the entry stub, pushed below
        //
        // Sort by funcOffset, then assign each entry a .xdata offset as
        // its UNWIND_INFO record is appended.
        {
            auto pad4 = [](std::vector<uint8_t>& v) {
                while (v.size() % 4) v.push_back(0);
                };

            auto makeFrameUnwind = [&](uint32_t frameSize) {
                std::vector<uint8_t> info;
                info.push_back(0x01);   // Version=1, Flags=0
                info.push_back(11);     // SizeOfProlog (bytes)
                info.push_back(5);      // CountOfCodes
                info.push_back(0x05);   // FrameRegister=RBP, FrameOffset=0
                // Code[0..2]: UWOP_ALLOC_LARGE (Op=1, OpInfo=1, 32-bit size)
                info.push_back(4);      // CodeOffset of "sub rsp"
                info.push_back(0x11);   // (OpInfo=1 << 4) | UnwindOp=1
                info.push_back((uint8_t)(frameSize));
                info.push_back((uint8_t)(frameSize >> 8));
                info.push_back((uint8_t)(frameSize >> 16));
                info.push_back((uint8_t)(frameSize >> 24));
                // Code[3]: UWOP_SET_FPREG (Op=3, OpInfo=0)
                info.push_back(1);
                info.push_back(0x03);
                // Code[4]: UWOP_PUSH_NONVOL rbp (Op=0, OpInfo=5)
                info.push_back(0);
                info.push_back(0x50);
                pad4(info);
                return info;
                };

            auto makeEntryStubUnwind = [&](uint32_t frameSize) {
                std::vector<uint8_t> info;
                info.push_back(0x01);
                info.push_back(7);
                info.push_back(1);
                info.push_back(0x00);
                uint8_t infoBits = (uint8_t)((frameSize / 8) - 1);
                info.push_back(0);
                info.push_back((uint8_t)((infoBits << 4) | 2));
                pad4(info);
                return info;
                };

            // User functions
            for (auto& f : userFuncs) {
                UnwindEntry ue;
                ue.funcOffset = f.startRva - textRva;
                ue.funcSize = f.endRva - f.startRva;
                ue.frameSize = f.frameSize;
                ue.stubOnly = false;
                r.unwindEntries.push_back(ue);
            }

            // Entry stub
            {
                UnwindEntry ue;
                ue.funcOffset = r.entryOffset;
                ue.funcSize = (uint32_t)r.text.size() - r.entryOffset;
                ue.frameSize = 40;
                ue.stubOnly = true;
                r.unwindEntries.push_back(ue);
            }

            // Sort by BeginAddress (required by the Windows loader).
            std::stable_sort(r.unwindEntries.begin(), r.unwindEntries.end(),
                [](const UnwindEntry& a, const UnwindEntry& b) {
                    return a.funcOffset < b.funcOffset;
                });

            // Build .xdata and assign unwindOffset for each entry.
            for (auto& ue : r.unwindEntries) {
                std::vector<uint8_t> uw = ue.stubOnly
                    ? makeEntryStubUnwind(ue.frameSize)
                    : makeFrameUnwind(ue.frameSize);
                ue.unwindOffset = (uint32_t)r.xdata.size();
                r.xdata.insert(r.xdata.end(), uw.begin(), uw.end());
            }
        }

        return r;
    }

} // namespace vcb