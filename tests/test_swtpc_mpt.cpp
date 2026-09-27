#include "test.h"

#include "boards/s100-memory.h"
#include "boards/swtpc-mpt.h"
#include "core/machine.h"
#include "core/statefile.h"

#include <string>

using namespace swtpc;

namespace {

// A machine with the MP-T in its default slot ($8010) and a scrap of RAM low down, which
// does not decode page $80. The clock runs at 1 MHz, so one cycle is one microsecond and the
// MK5009's periods read straight off as cycle counts.
struct Rig {
    Machine   m;
    MptBoard* mpt = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x400;
        mem->addRegion(ram, err);

        mpt = dynamic_cast<MptBoard*>(m.add("mpt", "mpt0", err));
        m.add("6800", "cpu0", err);
        m.power();
        m.clock.setHz(1000000);
    }

    // What INTCLK does: DDRB all outputs, CRB = $3D (CB1 IRQ on, falling edge, data
    // register selected), hold the chain with $80, then start it at `code`.
    void start(uint8_t code, uint8_t crb = 0x3D) {
        m.bus.memWrite(0x8012, 0xFF);   // CRB bit 2 is 0 after reset: this is DDRB
        m.bus.memWrite(0x8013, crb);
        m.bus.memWrite(0x8012, 0x80);
        m.bus.memWrite(0x8012, code);
    }

    bool flag() { return (m.bus.memRead(0x8013) & 0x80) != 0; }

    // Run the clock forward to absolute cycle `t` (deadlines fire on the way).
    void to(uint64_t t) {
        if (t > m.clock.now()) m.clock.advance(t - m.clock.now());
    }
};

} // namespace

void test_swtpc_mpt() {
    SECTION("mpt -- the 6820 fills the slot at $8010-$8013, and moves with `base`");
    {
        Rig      g;
        BusCycle c;
        c.type = Cycle::MemRead;
        for (uint16_t a = 0x8010; a <= 0x8013; ++a) {
            c.addr = a;
            CHECK(g.mpt->decodes(c), "each of the four slot addresses is the PIA's");
        }
        c.addr = 0x800F;
        CHECK(!g.mpt->decodes(c), "nothing just below the slot");
        c.addr = 0x8014;
        CHECK(!g.mpt->decodes(c), "nothing in the next slot (the DC-4's latch lives there)");

        std::string err;
        CHECK(setProperty(*g.mpt, "base", "E010", err), "E010 is a slot on a 6809 motherboard");
        c.addr = 0xE012;
        CHECK(g.mpt->decodes(c), "and the board answers there");
        c.addr = 0x8012;
        CHECK(!g.mpt->decodes(c), "and no longer at $8012");
        CHECK(!setProperty(*g.mpt, "base", "8011", err), "8011 is not a slot base");
    }

    SECTION("mpt -- an output line reads back what the guest drove (the 6820, not a latch)");
    {
        Rig g;
        g.start(0x06);
        CHECK(g.m.bus.memRead(0x8012) == 0x06, "PRB reads back the rate code written to it");
        g.m.bus.memWrite(0x8013, 0x39);           // CRB bit 2 off: the address is DDRB
        CHECK(g.m.bus.memRead(0x8012) == 0xFF, "with CRB bit 2 clear, base+2 is DDRB");
    }

    SECTION("mpt -- the rate table: the first edge falls one period after the chain starts");
    {
        const struct { uint8_t code; uint64_t us; } rates[] = {
            {0x0, 1},        {0x1, 10},        {0x2, 100},        {0x3, 1000},
            {0x4, 10000},    {0x5, 100000},    {0x6, 1000000},    {0x7, 10000000},
            {0x8, 100000000}, {0x9, 60000000}, {0xA, 3600000000ULL}, {0xB, 600000000},
            {0xE, 20000},
        };
        for (const auto& r : rates) {
            Rig      g;
            g.start(r.code);
            uint64_t t0 = g.m.clock.now();
            if (r.us > 1) {
                g.to(t0 + r.us - 1);
                CHECK(!g.flag(), "no edge before the period is out");
            }
            g.to(t0 + r.us);
            CHECK(g.flag(), "CRB bit 7 sets when the period is out");
            CHECK(MptBoard::periodUs(r.code) == r.us, "periodUs agrees with the data sheet");
        }
        for (uint8_t code : {0x0C, 0x0D, 0x0F}) {
            Rig g;
            g.start(code);
            g.to(g.m.clock.now() + 4000000000ULL);
            CHECK(!g.flag(), "codes C, D and F select the grounded external input: no output");
            CHECK(g.m.clock.queued() == 0, "and no deadline is set for them");
        }
    }

    SECTION("mpt -- a PRB read clears the flag, and the next edge keeps the chain's phase");
    {
        Rig      g;
        g.start(0x03);                            // 1 ms
        uint64_t t0 = g.m.clock.now();
        g.to(t0 + 1500);                          // well past the first edge, before the second
        CHECK(g.flag(), "the first edge set the flag");
        CHECK(g.mpt->assertsInt(), "and with CRB bit 0 set it pulls IRQ");
        g.m.bus.memRead(0x8012);
        CHECK(!g.flag(), "reading PRB clears it");
        CHECK(!g.mpt->assertsInt(), "and lets go of IRQ");
        g.to(t0 + 1999);
        CHECK(!g.flag(), "the next edge is not 1 ms after the read...");
        g.to(t0 + 2000);
        CHECK(g.flag(), "...it is at t0 + 2 ms, where the divider chain puts it");
    }

    SECTION("mpt -- PB7 holds the chain; releasing it starts the count again (the stopwatch)");
    {
        Rig g;
        g.start(0x80);                            // held, whatever the code
        g.to(g.m.clock.now() + 5000000);
        CHECK(!g.flag(), "no edge while PB7 holds RESET 0 high");
        CHECK(g.m.clock.queued() == 0, "and no deadline set");

        g.m.bus.memWrite(0x8012, 0x03);           // release at 1 ms
        uint64_t t0 = g.m.clock.now();
        g.to(t0 + 700);
        g.m.bus.memWrite(0x8012, 0x83);           // hold again partway through
        g.m.bus.memWrite(0x8012, 0x03);           // and restart
        uint64_t t1 = g.m.clock.now();
        g.to(t0 + 1000);
        CHECK(!g.flag(), "the old phase is gone: nothing at the first start's + 1 ms");
        g.to(t1 + 1000);
        CHECK(g.flag(), "the edge comes 1 ms after the restart");
    }

    SECTION("mpt -- changing the rate keeps the reset's phase (one chain, many taps)");
    {
        Rig      g;
        g.start(0x03);                            // 1 ms
        uint64_t t0 = g.m.clock.now();
        g.to(t0 + 350);
        g.m.bus.memWrite(0x8012, 0x02);           // switch to 100 us, chain still running
                                                  // (a restart here would put it at 450)
        g.to(t0 + 399);
        CHECK(!g.flag(), "the 100 us tap has no edge at 399 us");
        g.to(t0 + 400);
        CHECK(g.flag(), "its next edge is at 400 us -- counted from the reset, not the switch");
    }

    SECTION("mpt -- CRB bit 1 set: CB1 takes the rising edge, half a period in");
    {
        Rig      g;
        g.start(0x03, 0x3F);                      // $3D with bit 1 set
        uint64_t t0 = g.m.clock.now();
        g.to(t0 + 499);
        CHECK(!g.flag(), "no rising edge before P/2");
        g.to(t0 + 500);
        CHECK(g.flag(), "the output rises at P/2");
        g.m.bus.memRead(0x8012);
        g.to(t0 + 1499);
        CHECK(!g.flag(), "the falling edge at P does not count");
        g.to(t0 + 1500);
        CHECK(g.flag(), "the next rising edge is at 3P/2");
    }

    SECTION("mpt -- without CRB bit 0 the flag still sets, but IRQ stays quiet");
    {
        Rig g;
        g.start(0x03, 0x3C);
        g.to(g.m.clock.now() + 1000);
        CHECK(g.flag(), "the flag is there for a program that polls");
        CHECK(!g.mpt->assertsInt(), "and no interrupt is asked for");
    }

    SECTION("mpt -- a 1 us rate is one pending deadline, not a million");
    {
        Rig g;
        g.start(0x00);
        CHECK(g.m.clock.queued() == 1, "one deadline armed");
        g.to(g.m.clock.now() + 1000000);          // a whole second with nobody servicing it
        CHECK(g.flag(), "the flag is up");
        CHECK(g.m.clock.queued() == 0, "and nothing is queued while it stays up");
        for (int i = 0; i < 100; ++i) g.m.bus.memRead(0x8013);
        CHECK(g.m.clock.queued() == 0, "polling CRB does not queue anything either");
        g.m.bus.memRead(0x8012);
        CHECK(g.m.clock.queued() == 1, "a PRB read arms exactly one again");
    }

    SECTION("mpt -- RESET clears the PIA and holds the chain");
    {
        Rig g;
        g.start(0x03);
        g.to(g.m.clock.now() + 1000);
        CHECK(g.mpt->assertsInt(), "interrupting before the reset");
        g.m.reset(Reset::Bus);
        CHECK(!g.mpt->assertsInt(), "the bus RESET resets the 6820 -- no IRQ from the monitor");
        CHECK(g.m.clock.queued() == 0, "and the chain is held: no deadline");
    }

    SECTION("mpt -- a snapshot carries the running chain, and the edge re-arms on restore");
    {
        Rig      g;
        g.start(0x03);
        uint64_t t0 = g.m.clock.now();
        g.to(t0 + 400);

        StateWriter w;
        g.m.clock.serialize(w);
        g.mpt->serialize(w);

        Rig         h;
        StateReader rd(w.data());
        h.m.clock.deserialize(rd);
        h.mpt->deserialize(rd);
        CHECK(h.m.clock.queued() == 1, "the restored board re-armed its edge");
        h.to(t0 + 999);
        CHECK(!h.flag(), "not before the first start's + 1 ms");
        h.to(t0 + 1000);
        CHECK(h.flag(), "and exactly then -- the phase travelled");
    }
}
