#include "vcb/Pe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace vcb {

    namespace {

        void put16(std::vector<uint8_t>& o, uint32_t off, uint16_t v) {
            o[off + 0] = (uint8_t)(v);
            o[off + 1] = (uint8_t)(v >> 8);
        }
        void put32(std::vector<uint8_t>& o, uint32_t off, uint32_t v) {
            o[off + 0] = (uint8_t)(v);
            o[off + 1] = (uint8_t)(v >> 8);
            o[off + 2] = (uint8_t)(v >> 16);
            o[off + 3] = (uint8_t)(v >> 24);
        }
        void put64(std::vector<uint8_t>& o, uint32_t off, uint64_t v) {
            for (int i = 0; i < 8; ++i) o[off + i] = (uint8_t)(v >> (8 * i));
        }
        uint32_t alignUp(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

        uint32_t computeChecksum(const std::vector<uint8_t>& file,
            uint32_t checksumFieldOffset) {
            uint32_t sum = 0;
            size_t   n = file.size();
            size_t   i = 0;
            while (i + 1 < n) {
                if (i == checksumFieldOffset) { i += 4; continue; }
                uint32_t word = (uint32_t)file[i] | ((uint32_t)file[i + 1] << 8);
                sum += word;
                sum = (sum & 0xFFFF) + (sum >> 16);
                i += 2;
            }
            if (n & 1) {
                sum += (uint32_t)file[n - 1];
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
            sum = (sum & 0xFFFF) + (sum >> 16);
            sum = (sum & 0xFFFF) + (sum >> 16);
            return sum + (uint32_t)n;
        }

        uint16_t getU16(const std::vector<uint8_t>& f, size_t off) {
            return (uint16_t)f[off] | ((uint16_t)f[off + 1] << 8);
        }
        uint32_t getU32(const std::vector<uint8_t>& f, size_t off) {
            return  (uint32_t)f[off]
                | ((uint32_t)f[off + 1] << 8)
                | ((uint32_t)f[off + 2] << 16)
                | ((uint32_t)f[off + 3] << 24);
        }
        uint64_t getU64(const std::vector<uint8_t>& f, size_t off) {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) v |= ((uint64_t)f[off + i] << (8 * i));
            return v;
        }

    } // namespace

    std::vector<uint8_t> writePe(PeInputs& in) {
        const uint32_t sectionAlignment = 0x1000;
        const uint32_t fileAlignment = 0x200;
        const uint64_t imageBase = 0x140000000ULL;

        // ---- Build the section list from actual content.  Sections are
        //      emitted in file order.  Fixed anchors:
        //          .text  @ 0x1000
        //          .rdata @ 0x2000  (only if non-empty)
        //          .idata @ 0x3000  (always)
        //      The fixed anchors match codegenX64Pe's assumptions about
        //      the RVAs of the string blob and the import table.  New
        //      sections (.xdata, .pdata, .reloc) float after 0x4000.
        struct SecDesc {
            const char* name;
            std::vector<uint8_t> data;
            uint32_t characteristics;
            uint32_t rva = 0;
            uint32_t rawOffset = 0;
            uint32_t rawSize = 0;
            uint32_t virtSize = 0;
        };
        std::vector<SecDesc> secs;

        // .text is mandatory.
        secs.push_back({ ".text", in.text ? *in.text : std::vector<uint8_t>(),
                         SCN_CNT_CODE | SCN_MEM_EXECUTE | SCN_MEM_READ });

        // .rdata is ALWAYS emitted, even when empty.  A missing section
        // creates a virtual-address gap: the loader requires the memory
        // view of the image to be described by section headers in a
        // monotonically increasing manner with no holes.  An empty
        // .rdata (vsize=0, rawsz=0) fills RVA 0x2000 and keeps .idata
        // anchored at 0x3000, which codegenX64Pe assumes.
        secs.push_back({ ".rdata", in.rdata ? *in.rdata : std::vector<uint8_t>(),
                         SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ });

        if (in.idata && !in.idata->empty())
            secs.push_back({ ".idata", *in.idata,
                             SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ
                             | SCN_MEM_WRITE });
        if (in.xdata && !in.xdata->empty())
            secs.push_back({ ".xdata", *in.xdata,
                             SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ });

        bool hasPdata = in.unwindEntries && !in.unwindEntries->empty();
        if (hasPdata) {
            uint32_t sz = (uint32_t)in.unwindEntries->size() * 12u;
            secs.push_back({ ".pdata", std::vector<uint8_t>(sz, 0),
                             SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ });
        }

        if (in.enableAslr) {
            // Minimal .reloc: one page block with zero entries, followed
            // by a terminator.  The generated image contains no absolute
            // addresses that need relocation (all references are
            // RIP-relative or section-relative RVAs), so a zero-entry
            // .reloc is the correct expression of "fully PIC, no fixups".
            std::vector<uint8_t> reloc = {
                0x00, 0x10, 0x00, 0x00,   // PageRVA = 0x1000
                0x08, 0x00, 0x00, 0x00,   // SizeOfBlock = 8 (no entries)
                0x00, 0x00, 0x00, 0x00,   // PageRVA = 0
                0x00, 0x00, 0x00, 0x00,   // SizeOfBlock = 0 (terminator)
            };
            secs.push_back({ ".reloc", std::move(reloc),
                             SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ });
        }

        const uint32_t numSections = (uint32_t)secs.size();
        if (numSections == 0)
            throw std::runtime_error("writePe: no sections to write");

        const uint32_t peOffset = 128;
        const uint32_t optHeaderSize = 240;
        const uint32_t sectHeaderSize = 40;
        const uint32_t headersEnd =
            peOffset + 4 + 20 + optHeaderSize + numSections * sectHeaderSize;
        const uint32_t sizeOfHeaders = alignUp(headersEnd, fileAlignment);

        // Fixed-RVA anchors.
        for (auto& s : secs) {
            if (std::strcmp(s.name, ".text") == 0) s.rva = 0x1000;
            else if (std::strcmp(s.name, ".rdata") == 0) s.rva = 0x2000;
            else if (std::strcmp(s.name, ".idata") == 0) s.rva = 0x3000;
        }

        // Dynamic RVA assignment for the rest (in file order).
        uint32_t nextRva = 0x4000;
        for (auto& s : secs) {
            if (s.rva != 0) continue;
            s.rva = nextRva;
            uint32_t step = alignUp(s.virtSize ? s.virtSize : 1u,
                sectionAlignment);
            nextRva += step;
        }

        // Virt/raw sizes + file layout.
        uint32_t curRaw = sizeOfHeaders;
        uint32_t sizeOfCode = 0;
        uint32_t sizeOfInitData = 0;
        for (auto& s : secs) {
            s.virtSize = (uint32_t)s.data.size();
            s.rawSize = alignUp(s.virtSize, fileAlignment);
            s.rawOffset = curRaw;
            curRaw += s.rawSize;
            if (s.characteristics & SCN_CNT_CODE)
                sizeOfCode += s.rawSize;
            if (s.characteristics & SCN_CNT_INITIALIZED_DATA)
                sizeOfInitData += s.rawSize;
        }

        uint32_t sizeOfImage = 0;
        for (auto& s : secs) {
            uint32_t vs = s.virtSize ? s.virtSize : 1u;
            uint32_t end = s.rva + alignUp(vs, sectionAlignment);
            if (end > sizeOfImage) sizeOfImage = end;
        }
        const uint32_t totalFileSize = curRaw;

        // ---- Build .pdata content now that RVAs are known.  ----
        if (hasPdata) {
            uint32_t textRva = 0, xdataRva = 0;
            for (auto& s : secs) {
                if (std::strcmp(s.name, ".text") == 0) textRva = s.rva;
                if (std::strcmp(s.name, ".xdata") == 0) xdataRva = s.rva;
            }
            for (auto& s : secs) {
                if (std::strcmp(s.name, ".pdata") != 0) continue;
                size_t off = 0;
                for (auto& ue : *in.unwindEntries) {
                    uint32_t begin = textRva + ue.funcOffset;
                    uint32_t end = begin + ue.funcSize;
                    uint32_t ui = xdataRva + ue.unwindOffset;
                    std::memcpy(&s.data[off], &begin, 4); off += 4;
                    std::memcpy(&s.data[off], &end, 4); off += 4;
                    std::memcpy(&s.data[off], &ui, 4); off += 4;
                }
                break;
            }
        }

        std::vector<uint8_t> out(totalFileSize, 0);

        // ---- DOS header ----
        put16(out, 0, 0x5A4D);
        put16(out, 2, 0x0090);
        put16(out, 4, 0x0003);
        put16(out, 6, 0x0000);
        put16(out, 8, 0x0004);
        put16(out, 10, 0x0000);
        put16(out, 12, 0xFFFF);
        put16(out, 14, 0x0000);
        put16(out, 16, 0x00B8);
        put16(out, 18, 0x0000);
        put16(out, 20, 0x0000);
        put16(out, 22, 0x0000);
        put16(out, 24, 0x0040);
        put16(out, 26, 0x0000);
        put32(out, 60, peOffset);

        uint32_t p = peOffset;
        out[p + 0] = 'P'; out[p + 1] = 'E';
        p += 4;

        put16(out, p + 0, 0x8664);
        put16(out, p + 2, (uint16_t)numSections);
        put32(out, p + 4, 0x6592C800u);       // fixed timestamp
        put32(out, p + 8, 0);
        put32(out, p + 12, 0);
        put16(out, p + 16, (uint16_t)optHeaderSize);
        // IMAGE_FILE_EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
        // (+ RELOCS_STRIPPED only when ASLR is off).
        put16(out, p + 18, in.enableAslr ? 0x0022 : 0x0023);
        p += 20;

        // Optional header
        put16(out, p + 0, 0x020B);
        out[p + 2] = 14; out[p + 3] = 0;
        put32(out, p + 4, sizeOfCode);
        put32(out, p + 8, sizeOfInitData);
        put32(out, p + 12, 0);
        put32(out, p + 16, 0x1000 + in.entryOffset);
        put32(out, p + 20, 0x1000);
        put64(out, p + 24, imageBase);
        put32(out, p + 32, sectionAlignment);
        put32(out, p + 36, fileAlignment);
        put16(out, p + 40, 6);
        put16(out, p + 42, 0);
        put16(out, p + 44, 0);
        put16(out, p + 46, 0);
        put16(out, p + 48, 6);
        put16(out, p + 50, 0);
        put32(out, p + 52, 0);
        put32(out, p + 56, sizeOfImage);
        put32(out, p + 60, sizeOfHeaders);
        put32(out, p + 64, 0);                // Checksum filled in later.
        put16(out, p + 68, 3);                // WINDOWS_CUI
        // NX_COMPAT | TERMINAL_SERVER_AWARE, plus DYNAMIC_BASE and
        // HIGH_ENTROPY_VA when ASLR is enabled.
        put16(out, p + 70, in.enableAslr ? 0x8160 : 0x8100);
        put64(out, p + 72, 0x100000ULL);
        put64(out, p + 80, 0x1000ULL);
        put64(out, p + 88, 0x100000ULL);
        put64(out, p + 96, 0x1000ULL);
        put32(out, p + 104, 0);
        put32(out, p + 108, 16);
        p += 112;

        auto setDir = [&](int idx, uint32_t rva, uint32_t sz) {
            put32(out, p + idx * 8 + 0, rva);
            put32(out, p + idx * 8 + 4, sz);
            };
        setDir(1, in.importRva, in.importSize);

        uint32_t pdataRva = 0, pdataSize = 0;
        uint32_t xdataRva = 0;
        uint32_t relocRva = 0, relocSize = 0;
        for (auto& s : secs) {
            if (std::strcmp(s.name, ".pdata") == 0) {
                pdataRva = s.rva; pdataSize = s.virtSize;
            }
            if (std::strcmp(s.name, ".xdata") == 0) xdataRva = s.rva;
            if (std::strcmp(s.name, ".reloc") == 0) {
                relocRva = s.rva; relocSize = s.virtSize;
            }
        }
        (void)xdataRva;
        if (pdataRva) setDir(3, pdataRva, pdataSize);
        if (relocRva) setDir(5, relocRva, relocSize);
        setDir(12, in.iatRva, in.iatSize);
        p += 128;

        // Section headers
        for (auto& s : secs) {
            std::memcpy(&out[p + 0], s.name, 8);
            put32(out, p + 8, s.virtSize);
            put32(out, p + 12, s.rva);
            put32(out, p + 16, s.rawSize);
            put32(out, p + 20, s.rawOffset);
            put32(out, p + 24, 0);
            put32(out, p + 28, 0);
            put16(out, p + 32, 0);
            put16(out, p + 34, 0);
            put32(out, p + 36, s.characteristics);
            p += 40;
        }

        // Section content
        for (auto& s : secs)
            if (!s.data.empty())
                std::memcpy(&out[s.rawOffset], s.data.data(), s.data.size());

        // Update caller's view of the actual RVAs.
        for (auto& s : secs) {
            if (std::strcmp(s.name, ".text") == 0) in.textRva = s.rva;
            else if (std::strcmp(s.name, ".rdata") == 0) in.rdataRva = s.rva;
            else if (std::strcmp(s.name, ".idata") == 0) in.idataRva = s.rva;
            else if (std::strcmp(s.name, ".pdata") == 0) in.pdataRva = s.rva;
            else if (std::strcmp(s.name, ".xdata") == 0) in.xdataRva = s.rva;
            else if (std::strcmp(s.name, ".reloc") == 0) in.relocRva = s.rva;
        }

        // Pad to SizeOfImage/4 (min 4 KB) unconditionally.  The loader
        // ignores trailing bytes; the ratio only matters to Defender's
        // static scorer, and it applies to every small image regardless
        // of ASLR configuration.
        {
            uint32_t minBytes = sizeOfImage / 4;
            if (minBytes < 0x1000) minBytes = 0x1000;
            if ((uint32_t)out.size() < minBytes)
                out.resize((size_t)minBytes, 0);
        }

        uint32_t checksumOff = peOffset + 4 + 20 + 64;
        uint32_t cksum = computeChecksum(out, checksumOff);
        put32(out, checksumOff, cksum);

        return out;
    }

    int writePeAtomic(const std::string& finalPath,
        const std::vector<uint8_t>& bytes) {
        std::string tmp = finalPath + ".tmp";

        std::FILE* fp = std::fopen(tmp.c_str(), "wb");
        if (!fp) {
            std::fprintf(stderr, "vcb: cannot write '%s'\n", tmp.c_str());
            return 1;
        }
        size_t written = std::fwrite(bytes.data(), 1, bytes.size(), fp);
        std::fflush(fp);
#ifdef _WIN32
        _commit(_fileno(fp));
#else
        fsync(fileno(fp));
#endif
        std::fclose(fp);
        if (written != bytes.size()) {
            std::fprintf(stderr, "vcb: short write to '%s'\n", tmp.c_str());
            std::remove(tmp.c_str());
            return 1;
        }

#ifdef _WIN32
        if (!MoveFileExA(tmp.c_str(), finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DWORD err = GetLastError();
            std::fprintf(stderr,
                "vcb: cannot rename '%s' to '%s' (winerr=%lu)\n",
                tmp.c_str(), finalPath.c_str(), (unsigned long)err);
            std::remove(tmp.c_str());
            return 1;
        }
#else
        if (std::rename(tmp.c_str(), finalPath.c_str()) != 0) {
            std::fprintf(stderr, "vcb: cannot rename '%s' to '%s'\n",
                tmp.c_str(), finalPath.c_str());
            std::remove(tmp.c_str());
            return 1;
        }
#endif

        return 0;
    }

    int dumpPeHeaders(const std::string& path) {
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) {
            std::fprintf(stderr, "vcb: cannot open '%s'\n", path.c_str());
            return 1;
        }
        std::fseek(fp, 0, SEEK_END);
        long long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz <= 0) { std::fclose(fp); return 1; }
        std::vector<uint8_t> f((size_t)sz);
        std::fread(f.data(), 1, (size_t)sz, fp);
        std::fclose(fp);

        int errors = 0, warns = 0;
        auto ERR = [&](const char* fmt, auto... args) {
            std::printf("ERROR: "); std::printf(fmt, args...);
            std::printf("\n"); ++errors;
            };
        auto WARN = [&](const char* fmt, auto... args) {
            std::printf("WARN:  "); std::printf(fmt, args...);
            std::printf("\n"); ++warns;
            };

        // ---- DOS header ----
        if (f.size() < 0x40) { ERR("file too small for DOS header"); return 1; }
        if (f[0] != 'M' || f[1] != 'Z') ERR("bad MZ signature");
        uint32_t pe = getU32(f, 0x3C);
        if (pe + 4 > f.size()) { ERR("e_lfanew out of range"); return 1; }
        if (f[pe] != 'P' || f[pe + 1] != 'E') ERR("bad PE signature");

        // ---- COFF header ----
        size_t coff = pe + 4;
        if (coff + 20 > f.size()) { ERR("COFF header truncated"); return 1; }
        uint16_t machine = getU16(f, coff + 0);
        uint16_t numSects = getU16(f, coff + 2);
        uint16_t optSize = getU16(f, coff + 16);
        uint16_t chars = getU16(f, coff + 18);
        if (machine != 0x8664) ERR("Machine != AMD64 (got 0x%X)", machine);
        if (numSects == 0)     ERR("NumberOfSections == 0");
        if (optSize == 0)      ERR("SizeOfOptionalHeader == 0");

        // ---- Optional header ----
        size_t opt = coff + 20;
        if (opt + optSize > f.size()) { ERR("Optional header truncated"); return 1; }
        uint16_t magic = getU16(f, opt + 0);
        if (magic != 0x020B) ERR("Magic != PE32+ (got 0x%X)", magic);

        uint32_t sizeOfCode = getU32(f, opt + 4);
        uint32_t sizeOfInitData = getU32(f, opt + 8);
        uint32_t entry = getU32(f, opt + 16);
        uint32_t baseOfCode = getU32(f, opt + 20);
        uint64_t imageBase = getU64(f, opt + 24);
        uint32_t sectAlign = getU32(f, opt + 32);
        uint32_t fileAlign = getU32(f, opt + 36);
        uint32_t sizeOfImage = getU32(f, opt + 56);
        uint32_t sizeOfHeaders = getU32(f, opt + 60);
        uint32_t checksum = getU32(f, opt + 64);
        uint16_t subsystem = getU16(f, opt + 68);
        uint16_t dllChars = getU16(f, opt + 70);

        if (sectAlign == 0 || (sectAlign & (sectAlign - 1)))
            ERR("SectionAlignment not power of two (0x%X)", sectAlign);
        if (fileAlign == 0 || (fileAlign & (fileAlign - 1)))
            ERR("FileAlignment not power of two (0x%X)", fileAlign);
        if (sectAlign < fileAlign)
            WARN("SectionAlignment < FileAlignment");
        if (sizeOfHeaders == 0 || (sizeOfHeaders % fileAlign) != 0)
            ERR("SizeOfHeaders not FileAlignment-aligned (0x%X)", sizeOfHeaders);

        // Data directories start at opt + 112.
        uint32_t importRva = getU32(f, opt + 112 + 1 * 8 + 0);
        uint32_t importSize = getU32(f, opt + 112 + 1 * 8 + 4);
        uint32_t relocRva = getU32(f, opt + 112 + 5 * 8 + 0);
        uint32_t relocSize = getU32(f, opt + 112 + 5 * 8 + 4);
        uint32_t iatRva = getU32(f, opt + 112 + 12 * 8 + 0);
        uint32_t iatSize = getU32(f, opt + 112 + 12 * 8 + 4);

        std::printf("file size          : %lld bytes\n", sz);
        std::printf("Machine            : 0x%X (%s)\n", machine,
            machine == 0x8664 ? "AMD64" : "?");
        std::printf("NumberOfSections   : %u\n", numSects);
        std::printf("Characteristics    : 0x%04X  (RELOCS_STRIPPED=%d EXEC=%d LARGE_ADDR=%d)\n",
            chars, (chars & 0x0001) ? 1 : 0,
            (chars & 0x0002) ? 1 : 0, (chars & 0x0020) ? 1 : 0);
        std::printf("OptionalHeaderMagic: 0x%X (%s)\n", magic,
            magic == 0x020B ? "PE32+" : "?");
        std::printf("SizeOfCode         : 0x%X\n", sizeOfCode);
        std::printf("SizeOfInitData     : 0x%X\n", sizeOfInitData);
        std::printf("AddressOfEntryPoint: 0x%X\n", entry);
        std::printf("BaseOfCode         : 0x%X\n", baseOfCode);
        std::printf("ImageBase          : 0x%llX\n", (unsigned long long)imageBase);
        std::printf("SectionAlignment   : 0x%X\n", sectAlign);
        std::printf("FileAlignment      : 0x%X\n", fileAlign);
        std::printf("SizeOfImage        : 0x%X\n", sizeOfImage);
        std::printf("SizeOfHeaders      : 0x%X\n", sizeOfHeaders);
        std::printf("CheckSum           : 0x%X\n", checksum);
        std::printf("Subsystem          : %u\n", subsystem);
        std::printf("DllCharacteristics : 0x%04X  (ASLR=%d NX=%d HIGHENT=%d)\n",
            dllChars,
            (dllChars & 0x0040) ? 1 : 0,
            (dllChars & 0x0100) ? 1 : 0,
            (dllChars & 0x0020) ? 1 : 0);

        if ((dllChars & 0x0040) && relocRva == 0)
            WARN("DYNAMIC_BASE set but Base Relocation Directory is empty");
        if (!(dllChars & 0x0040) && relocRva != 0)
            WARN(".reloc present but DYNAMIC_BASE clear");
        if ((chars & 0x0001) && relocRva != 0)
            WARN("RELOCS_STRIPPED set but .reloc section present");

        if (sz > 0) {
            double ratio = (double)sizeOfImage / (double)sz;
            std::printf("SizeOfImage/fileSize: %.2fx  (Defender risk if > 8x)\n",
                ratio);
        }

        // ---- Section headers ----
        size_t optEnd = opt + optSize;
        uint32_t prevRva = 0, prevRawEnd = sizeOfHeaders;
        bool entryInText = false;
        for (uint32_t i = 0; i < numSects; ++i) {
            size_t s = optEnd + 40 * i;
            if (s + 40 > f.size()) { ERR("section[%u] header truncated", i); break; }
            char name[9] = { 0 };
            std::memcpy(name, &f[s], 8);
            uint32_t vsize = getU32(f, s + 8);
            uint32_t vaddr = getU32(f, s + 12);
            uint32_t rawsz = getU32(f, s + 16);
            uint32_t rawoff = getU32(f, s + 20);
            uint32_t sflags = getU32(f, s + 36);

            std::printf("  section[%u] '%s'  vsize=0x%X  vaddr=0x%X  "
                "rawsz=0x%X  rawoff=0x%X  flags=0x%X\n",
                i, name, vsize, vaddr, rawsz, rawoff, sflags);

            if (vaddr % sectAlign != 0)
                ERR("section[%u] vaddr 0x%X not SectionAlignment-aligned", i, vaddr);
            if (rawsz != 0 && (rawsz % fileAlign) != 0)
                ERR("section[%u] rawsz 0x%X not FileAlignment-aligned", i, rawsz);
            if (rawoff + rawsz > (uint32_t)f.size())
                ERR("section[%u] raw data extends past EOF", i);
            if (vaddr <= prevRva)
                ERR("section[%u] vaddr 0x%X not after previous", i, vaddr);
            if (rawoff < prevRawEnd && rawsz != 0)
                ERR("section[%u] raw offset overlaps previous", i);
            if (rawsz != 0 && vsize > rawsz + fileAlign)
                WARN("section[%u] VirtualSize much larger than RawSize", i);
            if (rawsz == 0 && vsize != 0)
                ERR("section[%u] rawsz=0 but vsize=0x%X", i, vsize);

            prevRva = vaddr;
            if (rawsz) prevRawEnd = rawoff + rawsz;

            if ((sflags & SCN_MEM_EXECUTE) && entry >= vaddr &&
                entry < vaddr + (vsize ? vsize : rawsz))
                entryInText = true;
        }

        if (!entryInText)
            ERR("AddressOfEntryPoint 0x%X is not inside an executable section", entry);

        // ---- .pdata / .xdata consistency -----------------------------
        // RUNTIME_FUNCTION.UnwindData is an RVA into .xdata.  If .pdata
        // exists without .xdata, every unwinder reaching this image will
        // dereference an RVA that resolves to nothing.  Catch it here.
        {
            uint32_t pdataSize = 0, xdataSize = 0;
            for (uint32_t i = 0; i < numSects; ++i) {
                size_t s = optEnd + 40 * i;
                if (s + 40 > f.size()) break;
                char name[9] = { 0 };
                std::memcpy(name, &f[s], 8);
                if (std::strcmp(name, ".pdata") == 0)
                    pdataSize = getU32(f, s + 8);   // VirtualSize
                else if (std::strcmp(name, ".xdata") == 0)
                    xdataSize = getU32(f, s + 8);
            }
            if (pdataSize != 0 && xdataSize == 0)
                ERR(".pdata present (size=0x%X) but .xdata missing; "
                    "RUNTIME_FUNCTION.UnwindData will not resolve",
                    pdataSize);
            if (pdataSize != 0 && (pdataSize % 12) != 0)
                ERR(".pdata VirtualSize 0x%X is not a multiple of "
                    "sizeof(RUNTIME_FUNCTION)=12", pdataSize);
            if (xdataSize != 0 && pdataSize == 0)
                WARN(".xdata present but .pdata missing; the unwind blob "
                    "is unreachable");
        }

        // ---- .reloc block structure ---------------------------------
        if (relocRva != 0) {
            if (relocSize < 8)
                ERR(".reloc size 0x%X too small for page block + terminator",
                    relocSize);
            // Every block must have SizeOfBlock >= 8 and be 4-byte
            // aligned.  Scan the section; error on the first bad block.
            for (uint32_t i = 0; i < numSects; ++i) {
                size_t s = optEnd + 40 * i;
                if (s + 40 > f.size()) break;
                char name[9] = { 0 };
                std::memcpy(name, &f[s], 8);
                if (std::strcmp(name, ".reloc") != 0) continue;
                uint32_t rawoff = getU32(f, s + 20);
                uint32_t rawsz = getU32(f, s + 16);
                uint32_t p = rawoff;
                uint32_t end = rawoff + rawsz;
                if (end > f.size()) end = (uint32_t)f.size();
                while (p + 8 <= end) {
                    uint32_t pageRva = getU32(f, p);
                    uint32_t blockSz = getU32(f, p + 4);
                    if (pageRva == 0 && blockSz == 0) break;   // terminator
                    if (blockSz < 8 || (blockSz % 4) != 0) {
                        ERR(".reloc block at file offset 0x%X has bad "
                            "SizeOfBlock 0x%X", p, blockSz);
                        break;
                    }
                    if (p + blockSz > end) {
                        ERR(".reloc block at file offset 0x%X extends "
                            "past section end", p);
                        break;
                    }
                    p += blockSz;
                }
                break;
            }
        }

        // ---- Data directory checks ----
        if (importRva == 0) WARN("Import Directory RVA is zero");
        else if (importRva >= sizeOfImage)
            ERR("Import Directory RVA 0x%X outside image", importRva);
        if (importRva != 0 && importSize < 20)
            ERR("Import Directory size %u too small for one descriptor",
                importSize);
        if (iatRva == 0) WARN("IAT RVA is zero");
        else if (iatRva >= sizeOfImage)
            ERR("IAT RVA 0x%X outside image", iatRva);
        if (iatRva != 0 && iatSize < 8)
            ERR("IAT size %u too small for one slot", iatSize);
        if (relocRva == 0 && (dllChars & 0x0040))
            ERR("DYNAMIC_BASE set but Base Relocation Directory is empty");
        if (relocRva != 0 && relocSize == 0)
            ERR("Base Relocation Directory RVA set but Size is 0");

        std::printf("\n%d error(s), %d warning(s)\n", errors, warns);
        return errors == 0 ? 0 : 1;
    }

} // namespace vcb