#include "test.h"

#include "boards/s100-memory.h"
#include "boards/swtpc-mps.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <string>

using namespace swtpc;

namespace {

// A machine with the MP-S serial console in it and a scripted terminal on its one line,
// bound through the REAL connect path so the test drives the same wiring an operator's
// CONNECT does. A `memory` board carries a scrap of RAM low down; it does not decode the
// $8004/$8005 window, so the mps is the only thing that answers there.
struct Rig {
    Machine         m;
    MpsBoard*       mps = nullptr;
    ScriptedStream* tty = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x400;  // 1K -- well clear of the $8004 window
        mem->addRegion(ram, err);

        mps = dynamic_cast<MpsBoard*>(m.add("mps", "mps0", err));
        mps->connect("tty", "scripted", err);
        tty = dynamic_cast<ScriptedStream*>(mps->unitStream("tty"));

        m.add("6800", "cpu0", err);
        m.power();
    }
};

int base(MpsBoard& b) {
    for (Property& p : b.properties())
        if (p.name == "base") return (int)p.get().i();
    return -1;
}

} // namespace

void test_swtpc_mps() {
    SECTION("mps -- the 6850 fills the whole slot, base+2/+3 mirroring base/+1 (A0 = RS)");
    {
        Rig g;
        BusCycle c;

        c.type = Cycle::MemRead;
        c.addr = 0x8004;
        CHECK(g.mps->decodes(c), "8004 (ACIA status/control) is ours");
        c.addr = 0x8005;
        CHECK(g.mps->decodes(c), "8005 (ACIA Rx/Tx data) is ours");
        c.addr = 0x8006;
        CHECK(g.mps->decodes(c), "8006 is ours too -- it mirrors 8004 (only A0 reaches RS)");
        c.addr = 0x8007;
        CHECK(g.mps->decodes(c), "8007 is ours -- it mirrors 8005");
        c.addr = 0x8003;
        CHECK(!g.mps->decodes(c), "but nothing just below the slot");
        c.addr = 0x8008;
        CHECK(!g.mps->decodes(c), "and nothing in the next slot");

        c.type = Cycle::MemWrite;
        c.addr = 0x8004;
        CHECK(g.mps->decodes(c), "8004 decodes a write (the ACIA control register)");

        // The mirror is load-bearing: SWTBUG's probe writes 8004 and reads it back at
        // 8006 to recognize an ACIA. Master-reset the ACIA, then confirm 8006 == 8004.
        g.m.bus.memWrite(0x8004, 0x03);
        CHECK(g.m.bus.memRead(0x8006) == g.m.bus.memRead(0x8004),
              "8006 reads the same status byte as 8004 -- the mirror SWTBUG's probe needs");
    }

    SECTION("mps -- 8004/8005 route to the onboard 6850");
    {
        Rig g;

        // Program the ACIA the way SWTBUG does: master reset (3), then divide-16 8N2 (D1).
        g.m.bus.memWrite(0x8004, 0x03);
        g.m.bus.memWrite(0x8004, 0xD1);

        // Transmit: a byte written to 8005 (TxData) leaves on the connected line.
        g.m.bus.memWrite(0x8005, 'A');
        g.mps->pump();
        CHECK(g.tty->out().find('A') != std::string::npos, "a byte written to 8005 reaches the wire");

        // Receive: a byte on the line shows up as RDRF in the 8004 status and comes back
        // at 8005 (RxData). The first pump lets the receiver OBSERVE the byte begin to
        // arrive; it still needs one character-time to shift in before RDRF sets (a real
        // 9600-baud line, modeled by Mc6850's rxIdle_ -- see mc6850.h), so advance the
        // clock past it and pump again.
        g.tty->feed("Z");
        g.mps->pump();
        g.m.clock.advance(10000);
        g.mps->pump();
        CHECK((g.m.bus.memRead(0x8004) & 0x01) != 0, "RDRF (status bit 0) sets when a byte arrives");
        CHECK(g.m.bus.memRead(0x8005) == 'Z', "and 8005 hands the guest the byte");
        CHECK((g.m.bus.memRead(0x8004) & 0x01) == 0, "reading the data clears RDRF");
    }

    SECTION("mps -- the slot base relocates via the `base` property");
    {
        Rig         g;
        std::string err;
        BusCycle    c;
        c.type = Cycle::MemRead;

        // Move the card to slot 2 ($8008). The default window must go dark and the new
        // one answer -- the whole point of a configurable SS-30 base.
        CHECK(setProperty(*g.mps, "base", "8008", err), "8008 is a legal SS-30 slot base (slot 2)");
        CHECK(base(*g.mps) == 0x8008, "and the property reports the move");
        c.addr = 0x8008;
        CHECK(g.mps->decodes(c), "8008 (the new status/control) is now ours");
        c.addr = 0x8009;
        CHECK(g.mps->decodes(c), "8009 (the new Rx/Tx data) is now ours");
        c.addr = 0x8004;
        CHECK(!g.mps->decodes(c), "and the old $8004 window is dark");

        // A base that is not on a 4-byte slot boundary, or outside $8000-$801C, is refused.
        CHECK(!setProperty(*g.mps, "base", "8005", err), "$8005 is not a slot boundary -- rejected");
        CHECK(!setProperty(*g.mps, "base", "8020", err), "$8020 is outside the SS-30 I/O window -- rejected");
        CHECK(base(*g.mps) == 0x8008, "and a rejected set leaves the base where it was");
    }

    SECTION("mps -- idle, the ACIA is not asking for an interrupt");
    {
        Rig g;
        CHECK(!g.mps->assertsInt(), "a freshly powered 6850 with no int-enable is quiet");
    }

    SECTION("mps -- snapshot carries the slot base");
    {
        Rig         g;
        std::string err;
        setProperty(*g.mps, "base", "800C", err);

        StateWriter w;
        g.mps->serialize(w);

        MpsBoard    fresh;
        StateReader rd(w.data());
        fresh.deserialize(rd);
        CHECK(base(fresh) == 0x800C, "the slot base survives serialize/deserialize");
    }
}
