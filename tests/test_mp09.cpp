#include "test.h"

#include "boards/s100-memory.h"
#include "boards/swtpc-mp09.h"
#include "boards/swtpc-mps.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/stream.h"

#include <string>
#include <vector>

using namespace swtpc;

// The SWTPC MP-09 (reference/MP-09 6809 CPU Board.md): the DAT between the 6809 and
// the backplane, and S-BUG in IC4 at physical F800-FFFF. The expectations come from
// the board manual and from S-BUG's own source (roms/SBUG/SBUG.ASM,
// reference/S-BUG Monitor.md).

namespace {

// The standard 56K system: the MP-09, an MP-S console at E004 (S-BUG's ACIAS), and
// 56K of RAM at physical 0000-DFFF. The bus runs verified, so a stale decode or IRQ
// wire on either side of the DAT aborts the test.
struct Rig {
    Machine         m;
    Mp09Board*      cpu = nullptr;
    ScriptedStream* tty = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        auto* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0xE000;
        mem->addRegion(ram, err);

        auto* mps = dynamic_cast<MpsBoard*>(m.add("mps", "mps0", err));
        setProperty(*mps, "base", "E004", err);
        mps->connect("tty", "scripted", err);
        tty = dynamic_cast<ScriptedStream*>(mps->unitStream("tty"));

        cpu = dynamic_cast<Mp09Board*>(m.add("mp09", "cpu0", err));
        m.power();
    }

    // Run until the console has printed `want`, or give up after `budget` steps.
    bool runUntil(const std::string& want, int budget = 2000000) {
        for (int done = 0; done < budget; done += 10000) {
            m.debug.run(10000);
            m.pump();
            if (tty->out().find(want) != std::string::npos) return true;
        }
        return false;
    }

    std::string dat() {
        for (Property& p : cpu->properties())
            if (p.name == "dat") return p.get().s();
        return "";
    }

    CpuCore& core() { return *cpu->activeCore(); }

    uint32_t reg(const char* name) {
        for (const RegDef& r : core().registers())
            if (r.name == name) return r.get();
        return 0xEEEEEEEE;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : core().registers())
            if (r.name == name) r.set(v);
    }

    // Code at PHYSICAL `at`, run from LOGICAL `at` -- the same address once S-BUG has
    // loaded the identity map.
    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        uint16_t a = at;
        for (uint8_t b : code) m.bus.memWrite(a++, b);
        core().setPc(at);
    }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) cpu->step(m.bus);
    }
};

} // namespace

void test_mp09() {
    SECTION("mp09 -- IC4 answers reads of PHYSICAL F800-FFFF, never a write");
    {
        Rig g;
        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xF800;
        CHECK(g.cpu->decodes(c), "F800 is IC4's first byte");
        c.addr = 0xFFFF;
        CHECK(g.cpu->decodes(c), "FFFF is its last");
        c.addr = 0xF7FF;
        CHECK(!g.cpu->decodes(c), "F7FF is below the 2K part");
        c.addr = 0xFFF0;
        c.type = Cycle::MemWrite;
        CHECK(!g.cpu->decodes(c), "a ROM does not answer a write");

        CHECK(g.m.bus.peek(0xFFFE) == 0xFF && g.m.bus.peek(0xFFFF) == 0x00,
              "the reset vector is FF00, S-BUG's START (SBUG.ASM)");
        CHECK(g.m.bus.peek(0xFF79) == 0xF1, "FF79 holds the unpatched F1 (the I/O-at-E000 image)");
    }

    SECTION("mp09 -- an empty socket decodes nothing; a bad image is refused");
    {
        Rig g;
        std::string err;
        CHECK(setProperty(*g.cpu, "rom", "", err), "rom = \"\" empties the socket");
        CHECK(g.m.bus.peek(0xFFFE) == 0xFF && g.m.bus.peek(0xFFFF) == 0xFF,
              "with no chip the vectors float");
        CHECK(!setProperty(*g.cpu, "rom", "builtin:swtbug", err),
              "an image outside F800-FFFF does not fit IC4");
        CHECK(err.find("F800-FFFF") != std::string::npos, ("it says why: " + err).c_str());
        CHECK(setProperty(*g.cpu, "rom", "builtin:sbug", err), "and S-BUG goes back in");
        CHECK(g.m.bus.peek(0xFFFF) == 0x00, "S-BUG's vector is back");
    }

    SECTION("mp09 -- S-BUG runs from a junk DAT to its sign-on, and leaves the identity map");
    {
        Rig g;
        CHECK(g.dat() != "0123456789ABCDEF", "the DAT powers up holding junk, not the identity map");
        bool up = g.runUntil("S-BUG 1.8 - 56K");
        CHECK(up, ("S-BUG signs on over the MP-S: " + g.tty->out()).c_str());
        CHECK(g.runUntil(">"), "and prompts with >");
        CHECK(g.dat() == "0123456789ABCDEF",
              ("56K of contiguous RAM leaves the identity map: " + g.dat()).c_str());
        CHECK(g.cpu->datEntry(0xF) == 0x0 && g.cpu->datEntry(0x0) == 0xF,
              "the chip holds the complement: F for physical 0, 0 for physical F");
    }

    SECTION("mp09 -- the DAT stores the complement and moves A12-A15 on the backplane");
    {
        Rig g;
        CHECK(g.runUntil("S-BUG 1.8 - 56K"), "S-BUG is up");

        // Logical 1xxx -> physical 3xxx: the chip is loaded with ~3 = C.
        g.m.bus.memWrite(0x3234, 0x5A);
        g.m.bus.memWrite(0x1234, 0xA5);
        g.load({0x86, 0xFC,              // LDA #$FC
                0xB7, 0xFF, 0xF1,        // STA $FFF1  -- DAT entry 1
                0xB6, 0x12, 0x34,        // LDA $1234
                0x86, 0x77,              // LDA #$77
                0xB7, 0x12, 0x35},       // STA $1235
               0x0100);

        std::vector<BusCycle> seen;
        int h = g.m.bus.observe([&](const BusCycle& c) { seen.push_back(c); });
        g.step(2);
        CHECK(g.cpu->datEntry(1) == 0xC, "a write to FFF1 loads entry 1 with its low nibble");
        CHECK(g.dat().substr(0, 4) == "0323", ("logical 1 now maps to physical 3: " + g.dat()).c_str());
        CHECK(g.cpu->toBus(0x1234) == 0x3234, "toBus translates the same way");

        seen.clear();
        g.step();
        CHECK(g.reg("A") == 0x5A, "LDA $1234 read physical 3234, not 1234");
        bool phys = false;
        for (const BusCycle& c : seen)
            if (c.type == Cycle::MemRead && c.addr == 0x3234) phys = true;
        CHECK(phys, "the backplane saw the read at the PHYSICAL address");

        seen.clear();
        g.step(2);
        CHECK(g.m.bus.peek(0x3235) == 0x77, "STA $1235 landed at physical 3235");
        CHECK(g.m.bus.peek(0x1234) == 0xA5, "physical 1234 still holds its own byte");
        g.m.bus.unobserve(h);
    }

    SECTION("mp09 -- FFF0-FFFF is write-only: a read is the ROM's vector, not the DAT");
    {
        Rig g;
        CHECK(g.runUntil("S-BUG 1.8 - 56K"), "S-BUG is up");
        g.load({0x86, 0xF5,              // LDA #$F5
                0xB7, 0xFF, 0xF8,        // STA $FFF8  -- DAT entry 8
                0xB6, 0xFF, 0xF8},       // LDA $FFF8
               0x0100);
        g.step(3);
        CHECK(g.cpu->datEntry(8) == 0x5, "the write reached the DAT");
        CHECK(g.reg("A") == g.m.bus.peek(0xFFF8), "the read back is the ROM byte at FFF8");
        CHECK(g.reg("A") == 0xFF, "S-BUG's IRQ vector high byte, FF");
    }

    SECTION("mp09 -- logical FF00-FFFF bypasses the DAT (the inferred bypass)");
    {
        Rig g;
        CHECK(g.runUntil("S-BUG 1.8 - 56K"), "S-BUG is up");
        g.load({0x86, 0xF8,              // LDA #$F8   -- logical F -> physical 7
                0xB7, 0xFF, 0xFF},       // STA $FFFF
               0x0100);
        g.step(2);
        CHECK(g.cpu->toBus(0xF800) == 0x7800, "logical F800 now reaches physical 7800");
        CHECK(g.cpu->toBus(0xFF00) == 0xFF00, "but logical FF00 still reaches FF00");
        CHECK(g.cpu->toBus(0xFFFE) == 0xFFFE, "and the vectors are always the ROM's");
        g.m.bus.memWrite(0x7F00, 0x12);
        g.load({0xB6, 0xFF, 0x00}, 0x0100);  // LDA $FF00
        g.step();
        CHECK(g.reg("A") == g.m.bus.peek(0xFF00), "LDA $FF00 read S-BUG, not physical 7F00");
    }

    SECTION("mp09 -- the bus IRQ wire reaches the core through the DAT");
    {
        Rig g;
        CHECK(g.runUntil("S-BUG 1.8 - 56K"), "S-BUG is up");
        // The ACIA's receive interrupt on (CR7), then a byte on the line: the MP-S pulls
        // IRQ. The core waits in a BRA * with I still set, so it cannot take it yet.
        g.load({0x86, 0x91,              // LDA #$91
                0xB7, 0xE0, 0x04,        // STA $E004
                0x20, 0xFE},             // BRA *
               0x0100);
        g.setReg("CC", g.reg("CC") | 0x50);
        g.step(2);
        g.tty->feed("X");
        for (int i = 0; i < 20000 && !g.m.bus.intPending(); ++i) {
            g.m.pump();
            g.m.debug.run(1);
        }
        CHECK(g.m.bus.intPending(), "the MP-S is pulling IRQ");
        CHECK(g.core().pc() == 0x0105, "and the core, masked, has not taken it");

        g.setReg("CC", g.reg("CC") & ~0x10u);   // clear I
        uint16_t vec = (uint16_t)((g.m.bus.peek(0xFFF8) << 8) | g.m.bus.peek(0xFFF9));
        g.step();
        CHECK(g.core().pc() == vec, "unmasked, the core takes IRQ through S-BUG's FFF8 vector");
    }

    SECTION("mp09 -- SNAPSHOT carries the DAT");
    {
        Rig g;
        CHECK(g.runUntil("S-BUG 1.8 - 56K"), "S-BUG is up");
        StateWriter w;
        g.cpu->serialize(w);
        std::string before = g.dat();

        g.load({0x86, 0xFA, 0xB7, 0xFF, 0xF3}, 0x0100);  // LDA #$FA ; STA $FFF3
        g.step(2);
        CHECK(g.dat() != before, "the DAT moved");

        StateReader r(w.data());
        g.cpu->deserialize(r);
        CHECK(r.ok(), "the state reads back");
        CHECK(g.dat() == before, "RESTORE puts the DAT back");
    }
}
