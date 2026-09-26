#include "vcb/Pe.hpp"
#include <cstring>

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

    } // namespace

    std::vector<uint8_t> writePe(const PeInputs& in) {
        const uint32_t sectionAlignment = 0x1000;
        const uint32_t fileAlignment = 0x200;
        const uint64_t imageBase = 0x140000000ULL;

        const uint32_t dosHeaderSize = 64;
        const uint32_t dosStubSize = 64;
        const uint32_t peOffset = dosHeaderSize + dosStubSize;
        const uint32_t coffHeaderSize = 20;
        const uint32_t optHeaderSize = 240;
        const uint32_t numSections = 2;
        const uint32_t sectHeaderSize = 40;

        const uint32_t headersEnd =
            peOffset + 4 + coffHeaderSize + optHeaderSize + numSections * sectHeaderSize;
        const uint32_t sizeOfHeaders = alignUp(headersEnd, fileAlignment);

        const uint32_t textRawSize = alignUp((uint32_t)in.text->size(), fileAlignment);
        const uint32_t textRawOffset = sizeOfHeaders;
        const uint32_t idataRawSize = alignUp((uint32_t)in.idata->size(), fileAlignment);
        const uint32_t idataRawOffset = textRawOffset + textRawSize;

        const uint32_t totalFileSize = idataRawOffset + idataRawSize;

        const uint32_t textVirtualSize = (uint32_t)in.text->size();
        const uint32_t idataVirtualSize = (uint32_t)in.idata->size();
        const uint32_t textEndRva = in.textRva + alignUp(textVirtualSize, sectionAlignment);
        const uint32_t idataEndRva = in.idataRva + alignUp(idataVirtualSize, sectionAlignment);
        const uint32_t sizeOfImage = idataEndRva > textEndRva ? idataEndRva : textEndRva;

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
        put32(out, p + 8, idataRawSize);
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
        put16(out, p + 70, 0x8160);
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
        put32(out, p + 12 * 8 + 4, 16);
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
        std::memcpy(&out[idataRawOffset], in.idata->data(), in.idata->size());

        return out;
    }

} // namespace vcb