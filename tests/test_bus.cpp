#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"

using namespace swtpc;

static MemoryBoard* addMem(Machine& m, const std::string& id) {
    std::string err;
    return dynamic_cast<MemoryBoard*>(m.add("memory", id, err));
}
static Region ram(uint16_t at, uint32_t size) {
    Region r;
    r.kind = RegionKind::Ram;
    r.at = at;
    r.size = size;
    return r;
}

void test_bus() {
    SECTION("the bus (DESIGN.md 4) -- it carries signals; it does not decide");

    {
        // The floating bus: ONE rule for everything nobody drives.
        Machine m;
        CHECK(m.bus.memRead(0x1234) == 0xFF, "unmapped memory reads FF -- nobody drives the bus");
        m.bus.memWrite(0x1234, 0x42);
        CHECK(m.bus.lastUnclaimed(), "an unclaimed write is simply GONE -- nobody latched it");
    }

    {
        // Contention is REPORTED, not resolved. A simulator that picks a winner
        // is lying to you about a fault a real backplane would have handed you.
        Machine m;
        std::string err;
        auto* a = addMem(m, "mem0");
        auto* b = addMem(m, "mem1");
        a->addRegion(ram(0x0000, 0x1000), err);
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();

        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0x0500;
        CHECK(m.bus.respondersTo(c).size() == 2, "two boards both decode 0500");

        m.bus.clearLog();
        m.bus.memRead(0x0500);
        CHECK(m.bus.drain().size() == 1, "and the bus SAYS SO");
        CHECK(m.bus.drain()[0].find("mem0") != std::string::npos &&
                  m.bus.drain()[0].find("mem1") != std::string::npos,
              "naming both boards");
    }

    {
        // SET BUS UNCLAIMED (DESIGN.md 4.6.1): a guest reaching an address no board
        // decodes reads 0xFF for ever and hangs -- this names the address and the PC.
        // On the memory-mapped 6800 an undecoded MEMORY access is the bug; default
        // Silent, de-duped once per address+direction per run.
        std::string err;
        Machine m;                 // empty backplane: no board decodes anything
        m.bus.setInstrPc(0x0113);  // what the run loop publishes each instruction

        // Default is Silent -- the whole point is that no existing machine gains a line.
        m.bus.clearLog();
        m.bus.memWrite(0xC000, 0x01);
        CHECK(m.bus.drain().empty(), "SILENT is the default -- an unclaimed write says nothing");
        CHECK(!m.bus.takeUnclaimedHalt(), "and it does not arm a halt");

        // WARN names the address, the PC, the byte and the direction, in the doc's words.
        m.bus.setUnclaimedPolicy(Unclaimed::Warn);
        m.bus.clearLog();
        m.bus.memWrite(0xC000, 0x01);
        CHECK(m.bus.drain().size() == 1, "WARN emits exactly one line for an unclaimed write");
        CHECK(m.bus.drain()[0] ==
                  "warning: write C000 <- 01 at PC=0113: no board decodes address 0xC000. "
                  "the byte is gone.",
              "and it is the line DESIGN.md 4.6.1 documents, verbatim");
        CHECK(!m.bus.takeUnclaimedHalt(), "WARN runs on -- it does not arm a halt");

        // De-dup: the same address+direction again this run is silent -- a poll loop on an
        // absent UART must not bury the console.
        m.bus.clearLog();
        m.bus.memWrite(0xC000, 0x02);
        CHECK(m.bus.drain().empty(), "the SAME address+direction warns once per run, not every time");

        // The other direction on the same address is a different fact, and IS reported.
        m.bus.clearLog();
        m.bus.memRead(0xC000);
        CHECK(m.bus.drain().size() == 1, "a read of the same address is a separate warning");
        CHECK(m.bus.drain()[0].rfind("warning: read C000 -> FF", 0) == 0, "and it floated to FF");

        // resetUnclaimedWarnings re-arms, as the start of each RUN does.
        m.bus.resetUnclaimedWarnings();
        m.bus.clearLog();
        m.bus.memWrite(0xC000, 0x03);
        CHECK(m.bus.drain().size() == 1, "resetUnclaimedWarnings re-arms the address for a new run");

        // A DECODED address is never warned -- only undecoded space floats.
        auto* r = addMem(m, "ram0");
        r->addRegion(ram(0x0000, 0x1000), err);
        m.power();
        m.bus.setUnclaimedPolicy(Unclaimed::Warn);
        m.bus.resetUnclaimedWarnings();
        m.bus.clearLog();
        m.bus.memRead(0x0100);
        m.bus.memWrite(0x0100, 0x00);
        CHECK(m.bus.drain().empty(), "a decoded address is silent -- only undecoded space floats");
    }

    {
        // HALT logs AND arms the boundary stop the run loop honors (takeUnclaimedHalt),
        // once, naming the offending access.
        Machine m;
        m.bus.setInstrPc(0x0200);
        m.bus.setUnclaimedPolicy(Unclaimed::Halt);
        m.bus.clearLog();
        m.bus.memWrite(0xEF00, 0xAB);
        CHECK(m.bus.drain().size() == 1, "HALT still logs the warning line");
        CHECK(m.bus.haltAddr() == 0xEF00 && m.bus.haltWasWrite(), "and records which access tripped it");
        CHECK(m.bus.takeUnclaimedHalt(), "HALT arms the boundary stop");
        CHECK(!m.bus.takeUnclaimedHalt(), "take is read-and-clear -- it fires once");
    }

    SECTION("peekBytes -- n peek()s in one call, and the same bytes");

    {
        // RAM in 0000-0FFF and F000-FFFF, NOTHING between: a run of three bytes can
        // cross a page, wrap past FFFF, or step off the end of a card into the hole.
        // Every such start must read exactly what three peek()s would.
        Machine m;
        std::string err;
        auto* mem = addMem(m, "mem0");
        mem->addRegion(ram(0x0000, 0x1000), err);
        mem->addRegion(ram(0xF000, 0x1000), err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();
        for (uint32_t a = 0; a < 0x10000; ++a) m.bus.memWrite((uint16_t)a, (uint8_t)(a * 7 + 3));

        bool same = true;
        for (uint16_t start : {0x0000, 0x00FE, 0x0FFE, 0x1000, 0x7FFF, 0xEFFE, 0xFFFE, 0xFFFF}) {
            uint8_t got[3];
            m.bus.peekBytes(start, got, 3);
            for (int k = 0; k < 3; ++k)
                if (got[k] != m.bus.peek((uint16_t)(start + k))) same = false;
        }
        CHECK(same, "peekBytes matches peek() across a page, past FFFF and into the hole");

        uint8_t hole[3];
        m.bus.peekBytes(0x2000, hole, 3);
        CHECK(hole[0] == 0xFF && hole[1] == 0xFF && hole[2] == 0xFF,
              "and an empty page floats to FF, byte for byte");
    }

    SECTION("the pre-access veto -- a cycle can be abandoned BEFORE any board is touched");

    // The debugger installs this to stop a cycle breakpoint WITH the PC on the
    // instruction (nothing read, nothing written). The bus's own contract is narrow:
    // consult the predicate at the top of a cycle, and if it says so, throw
    // CycleBreakBefore before driving anyone. Absent or false, it is a complete no-op.
    {
        Machine m;
        std::string err;
        MemoryBoard* mem = addMem(m, "mem0");
        mem->addRegion(ram(0, 0x1000), err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();
        m.bus.memWrite(0x0010, 0x5A);  // a byte to prove the read below is real

        // No veto: an ordinary read and write, untouched.
        CHECK(m.bus.memRead(0x0010) == 0x5A, "with no veto installed, a read returns the byte");

        // A veto that vetoes ONE address: reads elsewhere pass, the armed one throws
        // and -- the whole point -- leaves memory unchanged because it threw first.
        m.bus.setPreAccessVeto([](const BusCycle& c) { return c.addr == 0x0010; });
        CHECK(m.bus.memRead(0x0020) == 0x00, "a cycle the veto ignores runs normally");

        bool threw = false;
        try {
            m.bus.memWrite(0x0010, 0xFF);
        } catch (const CycleBreakBefore&) {
            threw = true;
        }
        CHECK(threw, "a vetoed cycle throws CycleBreakBefore");

        m.bus.clearPreAccessVeto();
        CHECK(m.bus.memRead(0x0010) == 0x5A,
              "and it was thrown BEFORE the write landed -- the old byte is still there");
    }
}
