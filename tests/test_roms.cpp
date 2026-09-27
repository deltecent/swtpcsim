#include "test.h"

#include "core/crc32.h"
#include "core/roms.h"

using namespace swtpc;

// DESIGN.md 0.1 applied to binaries. A ROM image is a HARDWARE FACT: if a bit is
// wrong, every piece of software above it gets debugged against the wrong ground
// truth, and it looks like a software bug for a very long time. So the CRC of
// every built-in is checked here, against the value recorded in docs/roms.md --
// and someone's editor mangling a binary becomes a failing test instead of a
// mystery.
//
// This is the Motorola 6800 line: the built-ins are the Altair 680b PROM monitor and
// its KCACR cassette companion, all in Motorola S-record form (decoded through
// loadSrec, not loadHex). The 8080/S-100 loaders and monitors lived in altairsim.
void test_roms() {
    SECTION("built-in ROMs -- provenance (docs/roms.md)");

    CHECK(!builtinRoms().empty(), "at least one ROM is compiled in");

    struct Case {
        const char* name;
        uint16_t lo, hi;
        size_t size;
        uint32_t crc;
        bool contiguous;
    };
    const Case cases[] = {
        // The Altair 680b PROM Monitor, in Motorola S-record form (the .S19 loader, and
        // the machine it was built for -- machines/altair680.toml). A single 256-byte
        // PROM at FF00-FFFF: PROM 1, the highest, holding the monitor and the reset and
        // interrupt vectors (Theory of Operation 3). swimon is the same monitor with SWI
        // vectored through $0010 for breakpoints -- same window, a different image, a
        // different CRC. These decode through loadSrec, not loadHex.
        {"mon680", 0xFF00, 0xFFFF, 256, 0x397E717Fu, true},
        {"swimon", 0xFF00, 0xFFFF, 256, 0x2ABE348Fu, true},
        // The KCACR cassette loader/punch PROM (socket V, FD00-FDFF): loads and dumps
        // memory over the 680b KCACR in Motorola S-record form, calling the MON680
        // console routines (so it lives one PROM below the monitor). Also an S19, and
        // its status/data equates are $F010/$F011 -- the 680kcacr board's registers.
        {"kcacr", 0xFD00, 0xFDFF, 256, 0xA89ADB57u, true},
        // The SWTPC 6800 SWTBUG monitor (1977, MIKBUG replacement): a 1 KB 2716 at
        // E000-E3FF that prints the $ prompt. Its console is the MC6850 at $8004/$8005
        // (the mps board) and its scratchpad is $A000; the reset/interrupt vectors are
        // in-ROM at E3F8-E3FF and the board mirror-decodes the part across the top 8 K
        // so FFF8-FFFF reads them. An S19, decoded through loadSrec.
        {"swtbug", 0xE000, 0xE3FF, 1024, 0xF9130EF4u, true},
        // The SWTPC 6809 S-BUG monitor v1.8: the 2K part in the MP-09's IC4 at
        // F800-FFFF, decoded on the physical address. Its vectors are in-ROM at
        // FFF2-FFFF (RESET -> FF00, START). An S19, decoded through loadSrec.
        {"sbug", 0xF800, 0xFFFF, 2048, 0x10A045A7u, true},
    };
    for (const auto& c : cases) {
        std::string tag = std::string("builtin:") + c.name;
        const BuiltinRom* r = findRom(c.name);
        CHECK(r != nullptr, (tag + " exists").c_str());
        if (!r) continue;

        Image ri;
        std::string rerr;
        CHECK(decodeRom(*r, 0, ri, rerr), (tag + " decodes (every record checksums)").c_str());
        CHECK(ri.lo() == c.lo && ri.hi() == c.hi,
              (tag + " occupies the range docs/roms.md records").c_str());
        CHECK(ri.flat().size() == c.size, (tag + " decodes to its recorded size").c_str());
        CHECK(ri.contiguous() == c.contiguous,
              (tag + " gap structure matches docs/roms.md").c_str());
        CHECK(crc32(ri.flat()) == c.crc, (tag + " CRC32 matches docs/roms.md").c_str());
    }
}
