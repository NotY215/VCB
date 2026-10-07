#include "vcb/Resources.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace vcb {

    namespace {

        // ASCII / UTF-8 -> UTF-16LE.  Sufficient for the manifest and
        // version strings, which are pure ASCII.
        std::vector<uint8_t> utf8ToUtf16(const std::string& s) {
            std::vector<uint8_t> out;
            out.reserve(s.size() * 2 + 2);
            for (char c : s) {
                out.push_back((uint8_t)c);
                out.push_back(0);
            }
            out.push_back(0);
            out.push_back(0);
            return out;
        }

        // Minimal VS_VERSION_INFO.  Two languages (en-US and neutral)
        // are not strictly required; en-US alone is what most tools
        // expect to see.  The structure is standard: header, fixed
        // file info, and StringFileInfo / VarFileInfo children.
        //
        // This is deliberately minimal: no CompanyName or LegalCopyright,
        // only the fields Windows itself displays.  Adding more fields
        // is safe and cheap; add them here if desired.
        std::vector<uint8_t> buildVersionInfo() {
            // Build StringFileInfo child first
            auto makeString = [](const std::string& key, const std::string& val) {
                std::vector<uint8_t> k = utf8ToUtf16(key);
                std::vector<uint8_t> v = utf8ToUtf16(val);
                uint32_t keyBytes = (uint32_t)k.size();
                uint32_t valBytes = (uint32_t)v.size();
                // Header: wLength(2) wValueLength(2) wType(2)
                uint32_t headerBytes = 6;
                uint32_t afterKey = headerBytes + keyBytes;
                afterKey = (afterKey + 3) & ~3u;
                uint32_t totalBytes = afterKey + valBytes;
                totalBytes = (totalBytes + 3) & ~3u;

                std::vector<uint8_t> out(totalBytes, 0);
                auto put16 = [&](uint32_t at, uint16_t x) {
                    out[at] = (uint8_t)x; out[at + 1] = (uint8_t)(x >> 8);
                    };
                put16(0, (uint16_t)totalBytes);
                put16(2, (uint16_t)valBytes);  // wValueLength in bytes (type=string)
                put16(4, 1);                    // wType = 1 (text)
                std::memcpy(&out[6], k.data(), keyBytes);
                std::memcpy(&out[afterKey], v.data(), valBytes);
                return out;
                };

            std::vector<uint8_t> sfi;
            // StringFileInfo header
            {
                std::vector<uint8_t> k = utf8ToUtf16("StringFileInfo");
                uint32_t keyBytes = (uint32_t)k.size();
                uint32_t headerBytes = 6;
                uint32_t afterKey = headerBytes + keyBytes;
                afterKey = (afterKey + 3) & ~3u;
                uint32_t total = afterKey;
                (void)total;
            }

            // Build the three strings.
            std::vector<uint8_t> sFileDesc = makeString("FileDescription", "Vayu Compiled Program");
            std::vector<uint8_t> sFileVer = makeString("FileVersion", "1.0.0.0");
            std::vector<uint8_t> sProdName = makeString("ProductName", "Vayu Application");
            std::vector<uint8_t> sProdVer = makeString("ProductVersion", "1.0.0.0");
            std::vector<uint8_t> sOrigName = makeString("OriginalFilename", "program.exe");

            // StringTable
            std::vector<uint8_t> langKey = utf8ToUtf16("040904b0");
            uint32_t langKeyBytes = (uint32_t)langKey.size();
            uint32_t stHeader = 6 + langKeyBytes;
            stHeader = (stHeader + 3) & ~3u;
            uint32_t stChildren =
                (uint32_t)(sFileDesc.size() + sFileVer.size() +
                    sProdName.size() + sProdVer.size() + sOrigName.size());
            uint32_t stTotal = stHeader + stChildren;
            stTotal = (stTotal + 3) & ~3u;

            std::vector<uint8_t> stringTable(stTotal, 0);
            auto stPut16 = [&](uint32_t at, uint16_t x) {
                stringTable[at] = (uint8_t)x; stringTable[at + 1] = (uint8_t)(x >> 8);
                };
            stPut16(0, (uint16_t)stTotal);
            stPut16(2, 0);
            stPut16(4, 1);
            std::memcpy(&stringTable[6], langKey.data(), langKeyBytes);
            uint32_t off = stHeader;
            std::memcpy(&stringTable[off], sFileDesc.data(), sFileDesc.size());  off += (uint32_t)sFileDesc.size();
            std::memcpy(&stringTable[off], sFileVer.data(), sFileVer.size());   off += (uint32_t)sFileVer.size();
            std::memcpy(&stringTable[off], sProdName.data(), sProdName.size());  off += (uint32_t)sProdName.size();
            std::memcpy(&stringTable[off], sProdVer.data(), sProdVer.size());   off += (uint32_t)sProdVer.size();
            std::memcpy(&stringTable[off], sOrigName.data(), sOrigName.size());  off += (uint32_t)sOrigName.size();

            // StringFileInfo wraps the string table
            std::vector<uint8_t> sfiKey = utf8ToUtf16("StringFileInfo");
            uint32_t sfiKeyBytes = (uint32_t)sfiKey.size();
            uint32_t sfiHeader = 6 + sfiKeyBytes;
            sfiHeader = (sfiHeader + 3) & ~3u;
            uint32_t sfiTotal = sfiHeader + (uint32_t)stringTable.size();
            sfiTotal = (sfiTotal + 3) & ~3u;

            sfi.assign(sfiTotal, 0);
            auto sfiPut16 = [&](uint32_t at, uint16_t x) {
                sfi[at] = (uint8_t)x; sfi[at + 1] = (uint8_t)(x >> 8);
                };
            sfiPut16(0, (uint16_t)sfiTotal);
            sfiPut16(2, 0);
            sfiPut16(4, 1);
            std::memcpy(&sfi[6], sfiKey.data(), sfiKeyBytes);
            std::memcpy(&sfi[sfiHeader], stringTable.data(), stringTable.size());

            // VS_FIXEDFILEINFO (52 bytes)
            std::vector<uint8_t> fixed(52, 0);
            auto fPut32 = [&](uint32_t at, uint32_t x) {
                fixed[at] = (uint8_t)x; fixed[at + 1] = (uint8_t)(x >> 8);
                fixed[at + 2] = (uint8_t)(x >> 16); fixed[at + 3] = (uint8_t)(x >> 24);
                };
            fPut32(0, 0xFEEF04BDu);
            fPut32(4, 0x00010000u);   // struct version
            fPut32(8, 0x00010000u);   // file version MS = 1.0
            fPut32(12, 0x00000000u);   // file version LS = 0.0
            fPut32(16, 0x00010000u);   // product version MS
            fPut32(20, 0x00000000u);   // product version LS
            fPut32(24, 0x3Fu);         // file flags mask
            fPut32(28, 0x00000000u);   // file flags
            fPut32(32, 0x00040004u);   // VOS_NT_WINDOWS32
            fPut32(36, 0x00000001u);   // VFT_APP
            fPut32(40, 0x00000000u);
            fPut32(44, 0x00000000u);
            fPut32(48, 0x00000000u);

            // Assemble VS_VERSION_INFO
            std::vector<uint8_t> rootKey = utf8ToUtf16("VS_VERSION_INFO");
            uint32_t rootKeyBytes = (uint32_t)rootKey.size();
            uint32_t rootHeader = 6 + rootKeyBytes;
            rootHeader = (rootHeader + 3) & ~3u;
            uint32_t afterFixed = rootHeader + (uint32_t)fixed.size();
            afterFixed = (afterFixed + 3) & ~3u;
            uint32_t rootTotal = afterFixed + (uint32_t)sfi.size();
            rootTotal = (rootTotal + 3) & ~3u;

            std::vector<uint8_t> root(rootTotal, 0);
            auto rPut16 = [&](uint32_t at, uint16_t x) {
                root[at] = (uint8_t)x; root[at + 1] = (uint8_t)(x >> 8);
                };
            rPut16(0, (uint16_t)rootTotal);
            rPut16(2, 52);                       // wValueLength = sizeof fixed
            rPut16(4, 0);                        // wType = binary
            std::memcpy(&root[6], rootKey.data(), rootKeyBytes);
            std::memcpy(&root[rootHeader], fixed.data(), fixed.size());
            std::memcpy(&root[afterFixed], sfi.data(), sfi.size());
            return root;
        }

        std::string buildManifest() {
            // asInvoker + DPI aware.  Every Windows app has this.
            return
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" "
                "manifestVersion=\"1.0\">\r\n"
                "  <assemblyIdentity type=\"win32\" name=\"Vayu.App\" "
                "version=\"1.0.0.0\" processorArchitecture=\"amd64\"/>\r\n"
                "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
                "    <security>\r\n"
                "      <requestedPrivileges>\r\n"
                "        <requestedExecutionLevel level=\"asInvoker\" "
                "uiAccess=\"false\"/>\r\n"
                "      </requestedPrivileges>\r\n"
                "    </security>\r\n"
                "  </trustInfo>\r\n"
                "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
                "    <windowsSettings>\r\n"
                "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/"
                "WindowsSettings\">true</dpiAware>\r\n"
                "    </windowsSettings>\r\n"
                "  </application>\r\n"
                "</assembly>\r\n";
        }

    } // namespace

    std::vector<uint8_t> buildResourceSection(uint32_t rsrcRva) {
        std::vector<uint8_t> manifest =
            utf8ToUtf16(buildManifest());
        std::vector<uint8_t> version = buildVersionInfo();

        // Directory tree occupies 0xE0 bytes.  Manifest data follows
        // (aligned to 4), then version data.
        const uint32_t dirSize = 0xE0;
        uint32_t manifestOff = dirSize;
        uint32_t manifestSize = (uint32_t)manifest.size();
        uint32_t versionOff = (manifestOff + manifestSize + 3) & ~3u;
        uint32_t versionSize = (uint32_t)version.size();
        uint32_t total = versionOff + versionSize;

        std::vector<uint8_t> out(total, 0);

        auto put16 = [&](uint32_t at, uint16_t v) {
            out[at] = (uint8_t)v; out[at + 1] = (uint8_t)(v >> 8);
            };
        auto put32 = [&](uint32_t at, uint32_t v) {
            out[at] = (uint8_t)v; out[at + 1] = (uint8_t)(v >> 8);
            out[at + 2] = (uint8_t)(v >> 16); out[at + 3] = (uint8_t)(v >> 24);
            };

        // Root directory
        put16(0x00, 0);
        put16(0x02, 2);
        put32(0x04 + 0, 24);                    // RT_MANIFEST
        put32(0x04 + 4, 0x80000000 | 0x20);
        put32(0x0C + 0, 16);                    // RT_VERSION
        put32(0x0C + 4, 0x80000000 | 0x40);

        // RT_MANIFEST type subdir
        put16(0x20, 0);
        put16(0x22, 1);
        put32(0x24, 1);
        put32(0x28, 0x80000000 | 0x60);

        // RT_VERSION type subdir
        put16(0x40, 0);
        put16(0x42, 1);
        put32(0x44, 1);
        put32(0x48, 0x80000000 | 0x80);

        // Manifest name subdir (language en-US = 0x0409)
        put16(0x60, 0);
        put16(0x62, 1);
        put32(0x64, 0x0409);
        put32(0x68, 0xA0);                       // data entry offset

        // Version name subdir
        put16(0x80, 0);
        put16(0x82, 1);
        put32(0x84, 0x0409);
        put32(0x88, 0xC0);

        // Manifest data entry
        put32(0xA0, rsrcRva + manifestOff);
        put32(0xA4, manifestSize);
        put32(0xA8, 1200);                       // Unicode
        put32(0xAC, 0);

        // Version data entry
        put32(0xC0, rsrcRva + versionOff);
        put32(0xC4, versionSize);
        put32(0xC8, 1200);
        put32(0xCC, 0);

        std::memcpy(&out[manifestOff], manifest.data(), manifestSize);
        std::memcpy(&out[versionOff], version.data(), versionSize);

        return out;
    }

    std::vector<uint8_t> buildRichHeader() {
        // Placed at file offset 0x40, ending at 0x80.  Layout:
        //   "DanS" XOR key, 3 XOR'd zero dwords, N XOR'd product
        //   entries, "Rich" literal, XOR key.  The XOR key is a fixed
        //   non-zero value; nothing in the toolchain validates the
        //   checksum, it is only a marker.
        const uint32_t key = 0x56415955u;   // 'VAYU'

        std::vector<uint8_t> h;
        h.reserve(64);

        auto put32 = [&](uint32_t v) {
            h.push_back((uint8_t)v);
            h.push_back((uint8_t)(v >> 8));
            h.push_back((uint8_t)(v >> 16));
            h.push_back((uint8_t)(v >> 24));
            };

        // DanS = 0x536E6144
        put32(0x536E6144u ^ key);
        // 3 padding dwords (all zero XOR key)
        put32(0u ^ key);
        put32(0u ^ key);
        put32(0u ^ key);
        // Product entries: {product << 16 | version}
        put32(((0x0001u << 16) | 0x0000u) ^ key);   // import0
        put32(((0x00FFu << 16) | 0x0000u) ^ key);   // compiler/linker
        put32(((0x0100u << 16) | 0x0000u) ^ key);   // misc
        // Rich literal = 0x68636952
        put32(0x68636952u);
        // XOR key
        put32(key);

        // Pad to 0x40 bytes (0x40..0x80).
        while (h.size() < 0x40) h.push_back(0);
        h.resize(0x40, 0);
        return h;
    }

} // namespace vcb