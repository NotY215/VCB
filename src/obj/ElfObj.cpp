#include "vcb/Obj.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

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

        // Section indices in the emitted object file.  1-based; 0 is
        // the mandatory NULL section.
        constexpr uint16_t SECT_NULL       = 0;
        constexpr uint16_t SECT_TEXT       = 1;
        constexpr uint16_t SECT_RELA_TEXT  = 2;
        constexpr uint16_t SECT_RODATA     = 3;
        constexpr uint16_t SECT_SYMTAB     = 4;
        constexpr uint16_t SECT_STRTAB     = 5;
        constexpr uint16_t SECT_SHSTRTAB   = 6;
        constexpr uint16_t SECT_COUNT      = 7;

        constexpr uint16_t SHN_UNDEF = 0;

        // ELF64 section header types.
        constexpr uint32_t SHT_NULL     = 0;
        constexpr uint32_t SHT_PROGBITS = 1;
        constexpr uint32_t SHT_SYMTAB   = 2;
        constexpr uint32_t SHT_STRTAB   = 3;
        constexpr uint32_t SHT_RELA     = 4;

        // ELF64 section flags.
        constexpr uint64_t SHF_WRITE     = 0x1;
        constexpr uint64_t SHF_ALLOC     = 0x2;
        constexpr uint64_t SHF_EXECINSTR = 0x4;

        void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
            v[at] = (uint8_t)x;
            v[at + 1] = (uint8_t)(x >> 8);
        }
        void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
            v[at] = (uint8_t)x;
            v[at + 1] = (uint8_t)(x >> 8);
            v[at + 2] = (uint8_t)(x >> 16);
            v[at + 3] = (uint8_t)(x >> 24);
        }
        void put64(std::vector<uint8_t>& v, size_t at, uint64_t x) {
            for (int i = 0; i < 8; ++i)
                v[at + i] = (uint8_t)(x >> (8 * i));
        }

    } // namespace

    std::vector<uint8_t> writeElfObj(const ElfFile& f) {
        if (f.symbols.empty())
            throw std::runtime_error(
                "writeElfObj: symbols[0] must be the null symbol");

        // ---- .shstrtab --------------------------------------------------
        const std::string shstr =
            std::string("\0", 1) +
            ".text\0" + ".rela.text\0" + ".rodata\0" +
            ".symtab\0" + ".strtab\0" + ".shstrtab\0";
        const uint32_t nameText      = 1;
        const uint32_t nameRelaText  = 7;
        const uint32_t nameRodata    = 18;
        const uint32_t nameSymtab    = 26;
        const uint32_t nameStrtab    = 34;
        const uint32_t nameShstrtab  = 42;

        // ---- .strtab ----------------------------------------------------
        std::vector<uint8_t> strtab;
        strtab.push_back(0);
        std::unordered_map<std::string, uint32_t> nameOff;
        for (auto& s : f.symbols) {
            if (s.name.empty()) continue;
            uint32_t off = (uint32_t)strtab.size();
            nameOff[s.name] = off;
            strtab.insert(strtab.end(), s.name.begin(), s.name.end());
            strtab.push_back(0);
        }

        // ---- .symtab ----------------------------------------------------
        // Layout: null, then LOCAL, then GLOBAL.  The sh_info field of
        // the .symtab section header is the index of the first GLOBAL
        // symbol.  To keep the emitters simple, we require the caller
        // to have already put all LOCAL symbols before all GLOBAL
        // symbols in the ElfFile::symbols vector.
        const uint32_t symSize = (uint32_t)f.symbols.size() * 24;
        std::vector<uint8_t> symtab(symSize, 0);
        uint32_t firstGlobal = 1;   // reset below

        for (uint32_t i = 0; i < f.symbols.size(); ++i) {
            const auto& s = f.symbols[i];
            uint32_t at = i * 24;

            uint32_t nm = 0;
            if (!s.name.empty()) {
                auto it = nameOff.find(s.name);
                if (it == nameOff.end())
                    throw std::runtime_error(
                        "writeElfObj: symbol name not interned");
                nm = it->second;
            }

            put32(symtab, at +  0, nm);
            symtab[at + 4] = s.info;
            symtab[at + 5] = 0;
            put16(symtab, at +  6, s.shndx);
            put64(symtab, at +  8, s.value);
            put64(symtab, at + 16, s.size);

            bool isLocal = (s.info >> 4) == kElfBindLocal;
            if (!isLocal && firstGlobal == 1 && i > 0)
                firstGlobal = i;   // first symbol whose bind != LOCAL
        }
        // Fix up firstGlobal based on actual layout.
        for (uint32_t i = 1; i < f.symbols.size(); ++i) {
            if ((f.symbols[i].info >> 4) != kElfBindLocal) {
                firstGlobal = i;
                break;
            }
            firstGlobal = i + 1;
        }

        // ---- .rela.text -------------------------------------------------
        const uint32_t relaSize = (uint32_t)f.textRelocs.size() * 24;
        std::vector<uint8_t> rela(relaSize, 0);
        for (uint32_t i = 0; i < f.textRelocs.size(); ++i) {
            const auto& r = f.textRelocs[i];
            uint32_t at = i * 24;
            put64(rela, at +  0, r.offset);
            uint64_t info = ((uint64_t)r.symbolIdx << 32) | (r.type & 0xFFFFFFFFu);
            put64(rela, at +  8, info);
            put64(rela, at + 16, (uint64_t)r.addend);
        }

        // ---- Layout -----------------------------------------------------
        const uint32_t ehSize = 64;
        uint32_t off = ehSize;

        const uint32_t textOff   = off;  off += (uint32_t)f.text.size();   off = (off + 7) & ~7u;
        const uint32_t relaOff   = off;  off += relaSize;                  off = (off + 7) & ~7u;
        const uint32_t rodataOff = off;  off += (uint32_t)f.rodata.size(); off = (off + 7) & ~7u;
        const uint32_t symOff    = off;  off += symSize;                   off = (off + 7) & ~7u;
        const uint32_t strOff    = off;  off += (uint32_t)strtab.size();   off = (off + 7) & ~7u;
        const uint32_t shstrOff  = off;  off += (uint32_t)shstr.size();    off = (off + 7) & ~7u;
        const uint32_t shoff     = off;  off += SECT_COUNT * 64;

        std::vector<uint8_t> out(off, 0);

        // ---- ELF header -------------------------------------------------
        out[0] = 0x7F; out[1] = 'E'; out[2] = 'L'; out[3] = 'F';
        out[4] = 2;   // ELFCLASS64
        out[5] = 1;   // ELFDATA2LSB
        out[6] = 1;   // EV_CURRENT
        out[7] = 0;   // ELFOSABI_NONE
        put16(out, 16, 1);      // ET_REL
        put16(out, 18, 0x3E);   // EM_X86_64
        put32(out, 20, 1);      // EV_CURRENT
        put64(out, 24, 0);      // e_entry = 0
        put64(out, 32, 0);      // e_phoff = 0
        put64(out, 40, shoff);  // e_shoff
        put32(out, 48, 0);      // e_flags
        put16(out, 52, 64);     // e_ehsize
        put16(out, 54, 0);      // e_phentsize
        put16(out, 56, 0);      // e_phnum
        put16(out, 58, 64);     // e_shentsize
        put16(out, 60, SECT_COUNT);
        put16(out, 62, SECT_SHSTRTAB);

        // ---- Section contents ------------------------------------------
        if (!f.text.empty())
            std::memcpy(&out[textOff], f.text.data(), f.text.size());
        if (relaSize)
            std::memcpy(&out[relaOff], rela.data(), rela.size());
        if (!f.rodata.empty())
            std::memcpy(&out[rodataOff], f.rodata.data(), f.rodata.size());
        std::memcpy(&out[symOff], symtab.data(), symtab.size());
        std::memcpy(&out[strOff], strtab.data(), strtab.size());
        std::memcpy(&out[shstrOff], shstr.data(), shstr.size());

        // ---- Section headers -------------------------------------------
        auto shdr = [&](uint16_t idx,
                        uint32_t name,
                        uint32_t type,
                        uint64_t flags,
                        uint64_t addr,
                        uint64_t offset,
                        uint64_t size,
                        uint32_t link,
                        uint32_t info,
                        uint64_t align,
                        uint64_t entsize) {
            uint32_t at = shoff + idx * 64;
            put32(out, at +  0, name);
            put32(out, at +  4, type);
            put64(out, at +  8, flags);
            put64(out, at + 16, addr);
            put64(out, at + 24, offset);
            put64(out, at + 32, size);
            put32(out, at + 40, link);
            put32(out, at + 44, info);
            put64(out, at + 48, align);
            put64(out, at + 56, entsize);
        };

        shdr(SECT_NULL, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);

        shdr(SECT_TEXT, nameText, SHT_PROGBITS,
             SHF_ALLOC | SHF_EXECINSTR, 0,
             textOff, (uint64_t)f.text.size(),
             0, 0, 16, 0);

        shdr(SECT_RELA_TEXT, nameRelaText, SHT_RELA,
             0, 0,
             relaOff, relaSize,
             SECT_SYMTAB, SECT_TEXT,
             8, 24);

        shdr(SECT_RODATA, nameRodata, SHT_PROGBITS,
             SHF_ALLOC, 0,
             rodataOff, (uint64_t)f.rodata.size(),
             0, 0, 16, 0);

        shdr(SECT_SYMTAB, nameSymtab, SHT_SYMTAB,
             0, 0,
             symOff, symSize,
             SECT_STRTAB, firstGlobal,
             8, 24);

        shdr(SECT_STRTAB, nameStrtab, SHT_STRTAB,
             0, 0,
             strOff, (uint64_t)strtab.size(),
             0, 0, 1, 0);

        shdr(SECT_SHSTRTAB, nameShstrtab, SHT_STRTAB,
             0, 0,
             shstrOff, (uint64_t)shstr.size(),
             0, 0, 1, 0);

        return out;
    }

    int writeElfObjAtomic(const std::string& finalPath,
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
            std::fprintf(stderr, "vcb: cannot rename '%s' to '%s'\n",
                tmp.c_str(), finalPath.c_str());
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

} // namespace vcb