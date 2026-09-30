#include "vcb/Elf.hpp"
#include <cstdio>
#include <cstring>
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

        struct Elf64_Ehdr {
            uint8_t  e_ident[16];
            uint16_t e_type;
            uint16_t e_machine;
            uint32_t e_version;
            uint64_t e_entry;
            uint64_t e_phoff;
            uint64_t e_shoff;
            uint32_t e_flags;
            uint16_t e_ehsize;
            uint16_t e_phentsize;
            uint16_t e_phnum;
            uint16_t e_shentsize;
            uint16_t e_shnum;
            uint16_t e_shstrndx;
        };

        struct Elf64_Phdr {
            uint32_t p_type;
            uint32_t p_flags;
            uint64_t p_offset;
            uint64_t p_vaddr;
            uint64_t p_paddr;
            uint64_t p_filesz;
            uint64_t p_memsz;
            uint64_t p_align;
        };

        struct Elf64_Shdr {
            uint32_t sh_name;
            uint32_t sh_type;
            uint64_t sh_flags;
            uint64_t sh_addr;
            uint64_t sh_offset;
            uint64_t sh_size;
            uint32_t sh_link;
            uint32_t sh_info;
            uint64_t sh_addralign;
            uint64_t sh_entsize;
        };

        static_assert(sizeof(Elf64_Ehdr) == 64, "Ehdr");
        static_assert(sizeof(Elf64_Phdr) == 56, "Phdr");
        static_assert(sizeof(Elf64_Shdr) == 64, "Shdr");

        uint32_t alignUp(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

        constexpr uint64_t BASE = 0x400000;
        constexpr uint32_t PAGE = 0x1000;

    } // namespace

    std::vector<uint8_t> writeElf(const ElfInputs& in) {
        static const std::vector<uint8_t> empty;
        const auto& text = in.text ? *in.text : empty;
        const auto& rodata = in.rodata ? *in.rodata : empty;

        const uint32_t textFileOff = PAGE;
        const uint32_t textVaddr = (uint32_t)(BASE + PAGE);

        const uint32_t textSize = (uint32_t)text.size();
        const uint32_t textPadded = alignUp(textSize, 16);
        const uint32_t rodataFileOff = textFileOff + textPadded;
        const uint32_t rodataVaddr = textVaddr + textPadded;
        const uint32_t rodataSize = (uint32_t)rodata.size();

        const uint32_t loadEnd = rodataFileOff + rodataSize;

        static const char shstr[] = "\0.text\0.rodata\0.shstrtab";
        const uint32_t shstrSize = sizeof(shstr);
        const uint32_t shstrFileOff = alignUp(loadEnd, 4);
        const uint32_t shoff = alignUp(shstrFileOff + shstrSize, 8);

        const uint32_t nameText = 1;
        const uint32_t nameRodata = 7;
        const uint32_t nameShstr = 15;

        const uint32_t numSections = 4;
        const uint32_t shentsize = sizeof(Elf64_Shdr);
        const uint32_t shSize = numSections * shentsize;
        const uint32_t totalSize = shoff + shSize;

        if (sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) > PAGE)
            throw std::runtime_error("elf: header larger than one page");

        std::vector<uint8_t> out(totalSize, 0);

        Elf64_Ehdr eh{};
        eh.e_ident[0] = 0x7F;
        eh.e_ident[1] = 'E'; eh.e_ident[2] = 'L'; eh.e_ident[3] = 'F';
        eh.e_ident[4] = 2;   // ELFCLASS64
        eh.e_ident[5] = 1;   // ELFDATA2LSB
        eh.e_ident[6] = 1;   // EV_CURRENT
        eh.e_type = 2;       // ET_EXEC
        eh.e_machine = 0x3E; // EM_X86_64
        eh.e_version = 1;
        eh.e_entry = BASE + PAGE + in.entryOffset;
        eh.e_phoff = sizeof(Elf64_Ehdr);
        eh.e_shoff = shoff;
        eh.e_ehsize = sizeof(Elf64_Ehdr);
        eh.e_phentsize = sizeof(Elf64_Phdr);
        eh.e_phnum = 1;
        eh.e_shentsize = (uint16_t)shentsize;
        eh.e_shnum = (uint16_t)numSections;
        eh.e_shstrndx = 3;
        std::memcpy(out.data(), &eh, sizeof(eh));

        Elf64_Phdr ph{};
        ph.p_type = 1;            // PT_LOAD
        ph.p_flags = 5;           // R+X
        ph.p_offset = 0;
        ph.p_vaddr = BASE;
        ph.p_paddr = BASE;
        ph.p_filesz = loadEnd;
        ph.p_memsz = loadEnd;
        ph.p_align = PAGE;
        std::memcpy(out.data() + sizeof(Elf64_Ehdr), &ph, sizeof(ph));

        if (textSize) std::memcpy(out.data() + textFileOff, text.data(), textSize);
        if (rodataSize) std::memcpy(out.data() + rodataFileOff, rodata.data(), rodataSize);
        std::memcpy(out.data() + shstrFileOff, shstr, shstrSize);

        {
            Elf64_Shdr sh{};
            sh.sh_name = nameText;
            sh.sh_type = 1;      // PROGBITS
            sh.sh_flags = 6;     // ALLOC | EXECINSTR
            sh.sh_addr = textVaddr;
            sh.sh_offset = textFileOff;
            sh.sh_size = textSize;
            sh.sh_addralign = 16;
            std::memcpy(out.data() + shoff + 1 * shentsize, &sh, sizeof(sh));
        }
        {
            Elf64_Shdr sh{};
            sh.sh_name = nameRodata;
            sh.sh_type = 1;
            sh.sh_flags = 2;     // ALLOC
            sh.sh_addr = rodataVaddr;
            sh.sh_offset = rodataFileOff;
            sh.sh_size = rodataSize;
            sh.sh_addralign = 16;
            std::memcpy(out.data() + shoff + 2 * shentsize, &sh, sizeof(sh));
        }
        {
            Elf64_Shdr sh{};
            sh.sh_name = nameShstr;
            sh.sh_type = 3;      // STRTAB
            sh.sh_flags = 0;
            sh.sh_addr = 0;
            sh.sh_offset = shstrFileOff;
            sh.sh_size = shstrSize;
            sh.sh_addralign = 1;
            std::memcpy(out.data() + shoff + 3 * shentsize, &sh, sizeof(sh));
        }

        return out;
    }

    int writeElfAtomic(const std::string& finalPath,
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

    int dumpElfHeaders(const std::string& path) {
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

        if (f.size() < sizeof(Elf64_Ehdr)) {
            std::fprintf(stderr, "vcb: '%s' too small for ELF\n", path.c_str());
            return 1;
        }
        if (f[0] != 0x7F || f[1] != 'E' || f[2] != 'L' || f[3] != 'F') {
            std::fprintf(stderr, "vcb: '%s' has bad ELF magic\n", path.c_str());
            return 1;
        }

        Elf64_Ehdr eh{};
        std::memcpy(&eh, f.data(), sizeof(eh));

        std::printf("file size          : %lld bytes\n", sz);
        std::printf("Magic              : 7F 45 4C 46\n");
        std::printf("Class              : %u (%s)\n", eh.e_ident[4],
            eh.e_ident[4] == 2 ? "ELF64" : "?");
        std::printf("Data encoding      : %u (%s)\n", eh.e_ident[5],
            eh.e_ident[5] == 1 ? "little-endian" : "?");
        std::printf("e_type             : %u (%s)\n", eh.e_type,
            eh.e_type == 2 ? "ET_EXEC" :
            eh.e_type == 3 ? "ET_DYN" : "?");
        std::printf("e_machine          : 0x%X (%s)\n", eh.e_machine,
            eh.e_machine == 0x3E ? "x86-64" : "?");
        std::printf("e_entry            : 0x%llX\n",
            (unsigned long long)eh.e_entry);
        std::printf("e_phoff            : 0x%llX\n",
            (unsigned long long)eh.e_phoff);
        std::printf("e_shoff            : 0x%llX\n",
            (unsigned long long)eh.e_shoff);
        std::printf("e_phnum            : %u\n", eh.e_phnum);
        std::printf("e_shnum            : %u\n", eh.e_shnum);
        std::printf("e_shstrndx         : %u\n", eh.e_shstrndx);

        for (uint16_t i = 0; i < eh.e_phnum; ++i) {
            size_t off = (size_t)eh.e_phoff + i * eh.e_phentsize;
            if (off + sizeof(Elf64_Phdr) > f.size()) break;
            Elf64_Phdr ph{};
            std::memcpy(&ph, f.data() + off, sizeof(ph));
            const char* pt = (ph.p_type == 1) ? "PT_LOAD" : "?";
            std::printf("  phdr[%u] %s flags=%c%c%c off=0x%llX vaddr=0x%llX "
                "filesz=0x%llX memsz=0x%llX align=0x%llX\n",
                i, pt,
                (ph.p_flags & 4) ? 'R' : '-',
                (ph.p_flags & 2) ? 'W' : '-',
                (ph.p_flags & 1) ? 'X' : '-',
                (unsigned long long)ph.p_offset,
                (unsigned long long)ph.p_vaddr,
                (unsigned long long)ph.p_filesz,
                (unsigned long long)ph.p_memsz,
                (unsigned long long)ph.p_align);
        }

        if (eh.e_shoff && eh.e_shnum) {
            size_t strOff = 0;
            if (eh.e_shstrndx < eh.e_shnum) {
                size_t shOff = (size_t)eh.e_shoff +
                    eh.e_shstrndx * eh.e_shentsize;
                if (shOff + sizeof(Elf64_Shdr) <= f.size()) {
                    Elf64_Shdr shstr{};
                    std::memcpy(&shstr, f.data() + shOff, sizeof(shstr));
                    strOff = (size_t)shstr.sh_offset;
                }
            }
            for (uint16_t i = 0; i < eh.e_shnum; ++i) {
                size_t off = (size_t)eh.e_shoff + i * eh.e_shentsize;
                if (off + sizeof(Elf64_Shdr) > f.size()) break;
                Elf64_Shdr sh{};
                std::memcpy(&sh, f.data() + off, sizeof(sh));
                const char* name = "?";
                if (strOff && strOff + sh.sh_name < f.size())
                    name = (const char*)(f.data() + strOff + sh.sh_name);
                std::printf("  shdr[%u] '%s' type=%u addr=0x%llX "
                    "off=0x%llX size=0x%llX flags=0x%llX\n",
                    i, name, sh.sh_type,
                    (unsigned long long)sh.sh_addr,
                    (unsigned long long)sh.sh_offset,
                    (unsigned long long)sh.sh_size,
                    (unsigned long long)sh.sh_flags);
            }
        }

        return 0;
    }

} // namespace vcb