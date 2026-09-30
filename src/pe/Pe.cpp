#include "vcb/Pe.hpp"
#include <cstdio>
#include <cstring>
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

    std::vector<uint8_t> writePe(const PeInputs& in) {
        const uint32_t sectionAlignment = 0x1000;
        const uint32_t fileAlignment = 0x200;
        const uint64_t imageBase = 0x140000000ULL;
        const uint32_t numSections = 3;   // .text .rdata .idata

        const uint32_t dosHeaderSize = 64;
        const uint32_t dosStubSize = 64;
        const uint32_t peOffset = dosHeaderSize + dosStubSize;
        const uint32_t coffHeaderSize = 20;
        const uint32_t optHeaderSize = 240;
        const uint32_t sectHeaderSize = 40;

        const uint32_t headersEnd =
            peOffset + 4 + coffHeaderSize + optHeaderSize +
            numSections * sectHeaderSize;
        const uint32_t sizeOfHeaders = alignUp(headersEnd, fileAlignment);

        const uint32_t textRawSize = alignUp((uint32_t)in.text->size(), fileAlignment);
        const uint32_t textRawOffset = sizeOfHeaders;

        const uint32_t rdataRawSize = alignUp((uint32_t)in.rdata->size(), fileAlignment);
        const uint32_t rdataRawOffset = textRawOffset + textRawSize;

        const uint32_t idataRawSize = alignUp((uint32_t)in.idata->size(), fileAlignment);
        const uint32_t idataRawOffset = rdataRawOffset + rdataRawSize;

        const uint32_t totalFileSize = idataRawOffset + idataRawSize;

        const uint32_t textVirtualSize = textRawSize;
        const uint32_t rdataVirtualSize = rdataRawSize;
        const uint32_t idataVirtualSize = idataRawSize;

        const uint32_t textEndRva = in.textRva + alignUp(textVirtualSize, sectionAlignment);
        const uint32_t rdataEndRva = in.rdataRva + alignUp(rdataVirtualSize, sectionAlignment);
        const uint32_t idataEndRva = in.idataRva + alignUp(idataVirtualSize, sectionAlignment);
        uint32_t sizeOfImage = textEndRva;
        if (rdataEndRva > sizeOfImage) sizeOfImage = rdataEndRva;
        if (idataEndRva > sizeOfImage) sizeOfImage = idataEndRva;

        std::vector<uint8_t> out(totalFileSize, 0);

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
        put32(out, p + 4, 0);
        put32(out, p + 8, 0);
        put32(out, p + 12, 0);
        put16(out, p + 16, (uint16_t)optHeaderSize);
        put16(out, p + 18, 0x0022);
        p += 20;

        put16(out, p + 0, 0x020B);
        out[p + 2] = 14;
        out[p + 3] = 0;
        put32(out, p + 4, textRawSize);
        put32(out, p + 8, rdataRawSize + idataRawSize);
        put32(out, p + 12, 0);
        put32(out, p + 16, in.textRva + in.entryOffset);
        put32(out, p + 20, in.textRva);
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
        put32(out, p + 64, 0);
        put16(out, p + 68, 3);
        put16(out, p + 70, 0x8160);            // ASLR + NX + HIGHENT + TSA
        put64(out, p + 72, 0x100000ULL);
        put64(out, p + 80, 0x1000ULL);
        put64(out, p + 88, 0x100000ULL);
        put64(out, p + 96, 0x1000ULL);
        put32(out, p + 104, 0);
        put32(out, p + 108, 16);
        p += 112;

        put32(out, p + 1 * 8 + 0, in.importRva);
        put32(out, p + 1 * 8 + 4, in.importSize);
        put32(out, p + 12 * 8 + 0, in.iatRva);
        put32(out, p + 12 * 8 + 4, in.iatSize);
        p += 128;

        std::memcpy(&out[p + 0], ".text\0\0\0", 8);
        put32(out, p + 8, textVirtualSize);
        put32(out, p + 12, in.textRva);
        put32(out, p + 16, textRawSize);
        put32(out, p + 20, textRawOffset);
        put32(out, p + 24, 0);
        put32(out, p + 28, 0);
        put16(out, p + 32, 0);
        put16(out, p + 34, 0);
        put32(out, p + 36, 0x60000020);
        p += 40;

        std::memcpy(&out[p + 0], ".rdata\0\0", 8);
        put32(out, p + 8, rdataVirtualSize);
        put32(out, p + 12, in.rdataRva);
        put32(out, p + 16, rdataRawSize);
        put32(out, p + 20, rdataRawOffset);
        put32(out, p + 24, 0);
        put32(out, p + 28, 0);
        put16(out, p + 32, 0);
        put16(out, p + 34, 0);
        put32(out, p + 36, 0x40000040);        // INITIALIZED_DATA | READ
        p += 40;

        std::memcpy(&out[p + 0], ".idata\0\0", 8);
        put32(out, p + 8, idataVirtualSize);
        put32(out, p + 12, in.idataRva);
        put32(out, p + 16, idataRawSize);
        put32(out, p + 20, idataRawOffset);
        put32(out, p + 24, 0);
        put32(out, p + 28, 0);
        put16(out, p + 32, 0);
        put16(out, p + 34, 0);
        put32(out, p + 36, 0xC0000040);
        p += 40;

        std::memcpy(&out[textRawOffset], in.text->data(), in.text->size());
        std::memcpy(&out[rdataRawOffset], in.rdata->data(), in.rdata->size());
        std::memcpy(&out[idataRawOffset], in.idata->data(), in.idata->size());

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

        if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') {
            std::fprintf(stderr, "vcb: '%s' is not a PE file (bad MZ)\n",
                path.c_str());
            return 1;
        }
        uint32_t pe = getU32(f, 0x3C);
        if (pe + 0x18 > f.size()) {
            std::fprintf(stderr, "vcb: e_lfanew out of range\n");
            return 1;
        }
        if (f[pe] != 'P' || f[pe + 1] != 'E') {
            std::fprintf(stderr, "vcb: no PE signature at 0x%X\n", pe);
            return 1;
        }
        size_t coff = pe + 4;
        size_t opt = coff + 20;
        size_t optEnd = opt + getU16(f, coff + 16);
        uint16_t numSects = getU16(f, coff + 2);

        std::printf("file size          : %lld bytes\n", sz);
        std::printf("NumberOfSections   : %u\n", numSects);
        std::printf("SizeOfCode         : 0x%X\n", getU32(f, opt + 4));
        std::printf("SizeOfInitData     : 0x%X\n", getU32(f, opt + 8));
        std::printf("AddressOfEntryPoint: 0x%X\n", getU32(f, opt + 16));
        std::printf("SizeOfImage        : 0x%X\n", getU32(f, opt + 56));
        std::printf("CheckSum           : 0x%X\n", getU32(f, opt + 64));
        std::printf("Subsystem          : %u\n", getU16(f, opt + 68));
        std::printf("DllCharacteristics : 0x%04X  (ASLR=%d NX=%d HIGHENT=%d)\n",
            getU16(f, opt + 70),
            (getU16(f, opt + 70) & 0x0040) ? 1 : 0,
            (getU16(f, opt + 70) & 0x0100) ? 1 : 0,
            (getU16(f, opt + 70) & 0x0020) ? 1 : 0);

        for (uint32_t i = 0; i < numSects; ++i) {
            size_t s = optEnd + 40 * i;
            char name[9] = { 0 };
            std::memcpy(name, &f[s], 8);
            std::printf("  section[%u] '%s'  vsize=0x%X  vaddr=0x%X  "
                "rawsz=0x%X  rawoff=0x%X  flags=0x%X\n",
                i, name,
                getU32(f, s + 8), getU32(f, s + 12),
                getU32(f, s + 16), getU32(f, s + 20),
                getU32(f, s + 36));
        }
        return 0;
    }

} // namespace vcb