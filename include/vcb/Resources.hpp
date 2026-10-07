#pragma once
#include <cstdint>
#include <vector>

namespace vcb {

    // Build a .rsrc section containing:
    //   - RT_MANIFEST (id 1, en-US) : asInvoker + dpiAware
    //   - RT_VERSION  (id 1, en-US) : minimal VS_VERSION_INFO
    //
    // Returns the raw bytes of the section.  `rsrcRva` is the RVA at
    // which the section will be mapped; it is required because the
    // resource directory stores RVAs, not file offsets.
    std::vector<uint8_t> buildResourceSection(uint32_t rsrcRva);

    // Build a Rich header (MSVC-compatible) for placement at file
    // offset 0x40, occupying 0x40 bytes (0x40..0x7F).  The standard
    // DOS stub is preserved at 0x80..; PE header follows at 0x80.
    //
    // If your layout places the PE header elsewhere, adjust in Pe.cpp.
    std::vector<uint8_t> buildRichHeader();

} // namespace vcb