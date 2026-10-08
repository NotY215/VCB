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

    const char* const kCoffEntrySymbol = "vayu_entry";

    namespace {

        class StringTable {
        public:
            StringTable() { bytes_.resize(4, '\0'); }

            uint32_t intern(const std::string& s) {
                auto it = offsets_.find(s);
                if (it != offsets_.end()) return it->second;
                uint32_t off = (uint32_t)bytes_.size();
                bytes_.insert(bytes_.end(), s.begin(), s.end());
                bytes_.push_back('\0');
                offsets_[s] = off;
                return off;
            }

            uint32_t find(const std::string& s) const {
                auto it = offsets_.find(s);
                if (it == offsets_.end())
                    throw std::runtime_error(
                        "StringTable::find: name not pre-interned");
                return it->second;
            }

            std::vector<uint8_t> finalize() const {
                std::vector<uint8_t> out(bytes_.size());
                std::memcpy(out.data(), bytes_.data(), bytes_.size());
                uint32_t len = (uint32_t)bytes_.size();
                out[0] = (uint8_t)len;
                out[1] = (uint8_t)(len >> 8);
                out[2] = (uint8_t)(len >> 16);
                out[3] = (uint8_t)(len >> 24);
                return out;
            }

        private:
            std::vector<uint8_t>                      bytes_;
            std::unordered_map<std::string, uint32_t> offsets_;
        };

        void writeName(uint8_t* dst, const std::string& name,
                       const StringTable& strtab) {
            if (name.size() <= 8) {
                std::memset(dst, 0, 8);
                std::memcpy(dst, name.data(), name.size());
                return;
            }
            uint32_t off = strtab.find(name);
            dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 0;
            dst[4] = (uint8_t)off;
            dst[5] = (uint8_t)(off >> 8);
            dst[6] = (uint8_t)(off >> 16);
            dst[7] = (uint8_t)(off >> 24);
        }

    } // namespace

    std::vector<uint8_t> writeCoff(const CoffFile& f) {
        const uint32_t nsec = (uint32_t)f.sections.size();
        const uint32_t nsym = (uint32_t)f.symbols.size();

        // Pre-intern every name that will be emitted via writeName so
        // the string table size is stable before we compute the layout.
        StringTable strtab;
        for (auto& s : f.symbols)  strtab.intern(s.name);
        for (auto& s : f.sections) strtab.intern(s.name);

        std::unordered_map<std::string, uint32_t> symIndex;
        for (uint32_t i = 0; i < nsym; ++i)
            symIndex.emplace(f.symbols[i].name, i);

        const uint32_t headerSize  = 20;
        const uint32_t sectHdrSize = 40 * nsec;

        std::vector<uint32_t> rawOff(nsec, 0);
        std::vector<uint32_t> relocOff(nsec, 0);

        uint32_t cur = headerSize + sectHdrSize;
        for (uint32_t i = 0; i < nsec; ++i) {
            rawOff[i] = cur;
            cur += (uint32_t)f.sections[i].data.size();
            cur = (cur + 3) & ~3u;
        }
        for (uint32_t i = 0; i < nsec; ++i) {
            relocOff[i] = cur;
            cur += (uint32_t)f.sections[i].relocs.size() * 10;
            cur = (cur + 3) & ~3u;
        }
        const uint32_t symOff = cur;
        const uint32_t strOff = symOff + nsym * 18;
        std::vector<uint8_t> strBytes = strtab.finalize();
        const uint32_t total = strOff + (uint32_t)strBytes.size();

        std::vector<uint8_t> out(total, 0);

        auto put16 = [&](uint32_t at, uint16_t v) {
            out[at] = (uint8_t)v;
            out[at + 1] = (uint8_t)(v >> 8);
        };
        auto put32 = [&](uint32_t at, uint32_t v) {
            out[at] = (uint8_t)v;
            out[at + 1] = (uint8_t)(v >> 8);
            out[at + 2] = (uint8_t)(v >> 16);
            out[at + 3] = (uint8_t)(v >> 24);
        };

        // File header.
        put16(0, 0x8664);            // Machine = AMD64
        put16(2, (uint16_t)nsec);
        put32(4, 0);                 // TimeDateStamp = 0 (deterministic)
        put32(8, symOff);
        put32(12, nsym);
        put16(16, 0);                // SizeOfOptionalHeader
        put16(18, 0);                // Characteristics

        // Section headers.
        for (uint32_t i = 0; i < nsec; ++i) {
            const auto& s = f.sections[i];
            uint32_t p = headerSize + 40 * i;
            writeName(&out[p], s.name, strtab);
            put32(p +  8, 0);                          // VirtualSize
            put32(p + 12, 0);                          // VirtualAddress
            put32(p + 16, (uint32_t)s.data.size());    // SizeOfRawData
            put32(p + 20, rawOff[i]);                  // PointerToRawData
            put32(p + 24, relocOff[i]);                // PointerToRelocations
            put32(p + 28, 0);                          // PointerToLinenumbers
            put16(p + 32, (uint16_t)s.relocs.size());
            put16(p + 34, 0);
            put32(p + 36, s.characteristics);
        }

        // Section raw data.
        for (uint32_t i = 0; i < nsec; ++i) {
            const auto& s = f.sections[i];
            if (!s.data.empty())
                std::memcpy(&out[rawOff[i]], s.data.data(), s.data.size());
        }

        // Relocations.
        for (uint32_t i = 0; i < nsec; ++i) {
            uint32_t at = relocOff[i];
            for (auto& r : f.sections[i].relocs) {
                auto it = symIndex.find(r.symbol);
                if (it == symIndex.end())
                    throw std::runtime_error(
                        "writeCoff: relocation references undefined "
                        "symbol '" + r.symbol + "'");
                put32(at + 0, r.offset);
                put32(at + 4, it->second);
                put16(at + 8, (uint16_t)r.type);
                at += 10;
            }
        }

        // Symbol table.
        for (uint32_t i = 0; i < nsym; ++i) {
            const auto& s = f.symbols[i];
            uint32_t p = symOff + 18 * i;
            writeName(&out[p], s.name, strtab);
            put32(p +  8, s.value);
            put16(p + 12, (uint16_t)s.section);
            put16(p + 14, s.type);
            out[p + 16] = s.storageClass;
            out[p + 17] = 0;   // NumberOfAuxSymbols
        }

        // String table.
        std::memcpy(&out[strOff], strBytes.data(), strBytes.size());

        return out;
    }

    int writeCoffAtomic(const std::string& finalPath,
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