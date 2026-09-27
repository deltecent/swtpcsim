#include "test.h"

#include "boards/s100-memory.h"
#include "boards/swtpc-mpt.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "cpu/cpu.h"
#include "host/stream.h"

#include <string>
#include <vector>

using namespace swtpc;

namespace {

// A machine with the MP-T in its default slot ($8010), a scrap of RAM low down and a page
// at the top for the vectors, neither of which decodes page $80. The side-A unit `in` is
// wired to a scripted stream through the real connect path. The clock runs at 1 MHz, so one
// cycle is one microsecond and the MK5009's periods read straight off as cycle counts.
struct Rig {
    Machine         m;
    MptBoard*       mpt = nullptr;
    ScriptedStream* in  = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x400;
        mem->addRegion(ram, err);
        ram.at   = 0xFF00;
        ram.size = 0x100;
        mem->addRegion(ram, err);

        mpt = dynamic_cast<MptBoard*>(m.add("mpt", "mpt0", err));
        mpt->connect("in", "scripted", err);
        in = dynamic_cast<ScriptedStream*>(mpt->unitStream("in"));
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

    void load(uint16_t at, const std::vector<uint8_t>& bytes) {
        for (uint8_t b : bytes) m.bus.memWrite(at++, b);
    }

    // Run the 6800 until the clock reaches `t`, pumping the boards every 100 cycles the way
    // the run loop pumps them between slices.
    void runTo(uint64_t t) {
        CpuCore* cpu  = m.cpu();
        uint64_t next = m.clock.now() + 100;
        while (m.clock.now() < t) {
            m.clock.advance(cpu->step(m.bus).cycles);
            if (m.clock.now() >= next) {
                m.pump();
                next += 100;
            }
        }
    }

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

    SECTION("mpt -- DDRB = $FF with ORB still 0 starts the chain at 1 us: the flag goes up");
    {
        // After RESET the output register is 0, so the moment DDRB makes the lines outputs
        // they drive PB7 low (run) and select code 0. Until the program writes $80 the MK5009
        // counts at 1 us and CB1 sets the flag, enabled or not -- a 6820 flags every active
        // edge. So a program has to read PRB once before CLI, or its first interrupt is this
        // one. The CPU test below does.
        Rig g;
        g.m.bus.memWrite(0x8012, 0xFF);           // DDRB
        g.to(g.m.clock.now() + 5);
        CHECK(g.flag(), "the chain ran at 1 us while PB7 was low");
        g.m.bus.memWrite(0x8013, 0x3D);
        CHECK(g.mpt->assertsInt(), "enabling CB1's interrupt with the flag up pulls IRQ at once");
        g.m.bus.memWrite(0x8012, 0x80);
        g.m.bus.memRead(0x8012);
        CHECK(!g.mpt->assertsInt(), "holding the chain and reading PRB is what clears it");
    }

    SECTION("mpt -- the unit `in`: a byte arrives, latches, and strobes CA1");
    {
        Rig             g;
        std::string     err;
        UnitDef         u;
        CHECK(g.mpt->findUnit("IN", u) && u.kind == UnitKind::Serial, "the board has one unit, in");
        CHECK(u.state == "scripted", "and it names what is connected");
        CHECK(!g.mpt->connect("out", "scripted", err), "there is no other unit to connect");

        g.m.bus.memWrite(0x8011, 0x05);           // CRA: data register, CA1 IRQ on
        g.in->feed("AB");
        g.mpt->pump();
        CHECK((g.m.bus.memRead(0x8011) & 0x80) != 0, "CRA bit 7 sets when a byte is latched");
        CHECK(g.mpt->assertsInt(), "and with CRA bit 0 set it pulls IRQ");
        g.mpt->pump();
        CHECK(g.m.bus.memRead(0x8010) == 'A', "PRA hands over the first byte -- the second waited");
        CHECK(!g.mpt->assertsInt(), "reading PRA lets go of IRQ");
        CHECK((g.m.bus.memRead(0x8011) & 0x80) == 0, "and clears CRA bit 7");
        g.mpt->pump();
        CHECK(g.m.bus.memRead(0x8010) == 'B', "the next pump brings the second byte");

        g.m.bus.memWrite(0x8010, 0x55);
        CHECK(g.in->out().empty(), "a write to side A goes nowhere: the port is an input");

        CHECK(g.mpt->disconnect("in", err), "DISCONNECT in");
        CHECK(g.mpt->units()[0].state == "null", "leaves nothing on the port");
    }

    SECTION("mpt -- a snapshot carries a byte waiting in the side-A latch");
    {
        Rig g;
        g.m.bus.memWrite(0x8011, 0x05);
        g.in->feed("K");
        g.mpt->pump();

        StateWriter w;
        g.mpt->serialize(w);
        Rig         h;
        StateReader rd(w.data());
        h.mpt->deserialize(rd);
        CHECK(h.mpt->assertsInt(), "the restored board still asks for the interrupt");
        CHECK(h.m.bus.memRead(0x8010) == 'K', "and hands over the latched byte");
    }

    SECTION("mpt -- a 6800 program: the ISR counts timer ticks and takes the input byte");
    {
        Rig g;
        // The ISR asks each side of the PIA whether it interrupted. Reading the side's data
        // register is what clears its flag, so both must be read or the IRQ never lets go.
        g.load(0x0100, {
            0x8E, 0x00, 0xFF,        // 0100  LDS  #$00FF
            0x86, 0x05,              // 0103  LDAA #$05     CRA: data register, CA1 IRQ on
            0xB7, 0x80, 0x11,        // 0105  STAA $8011
            0x86, 0xFF,              // 0108  LDAA #$FF     DDRB: all outputs
            0xB7, 0x80, 0x12,        // 010A  STAA $8012
            0x86, 0x3D,              // 010D  LDAA #$3D     CRB: data, CB1 IRQ on, falling
            0xB7, 0x80, 0x13,        // 010F  STAA $8013
            0x86, 0x80,              // 0112  LDAA #$80     hold the chain
            0xB7, 0x80, 0x12,        // 0114  STAA $8012
            0x86, 0x03,              // 0117  LDAA #$03     start it: 1 ms
            0xB7, 0x80, 0x12,        // 0119  STAA $8012
            0xB6, 0x80, 0x12,        // 011C  LDAA $8012    drop the flag the setup raised
            0x0E,                    // 011F  CLI
            0x20, 0xFE,              // 0120  BRA  *
        });
        g.load(0x0200, {
            0xB6, 0x80, 0x11,        // 0200  LDAA $8011    CRA
            0x2A, 0x06,              // 0203  BPL  $020B    side A did not interrupt
            0xB6, 0x80, 0x10,        // 0205  LDAA $8010    take the byte (clears CRA bit 7)
            0xB7, 0x00, 0x11,        // 0208  STAA $0011
            0xB6, 0x80, 0x13,        // 020B  LDAA $8013    CRB
            0x2A, 0x06,              // 020E  BPL  $0216    the timer did not interrupt
            0xB6, 0x80, 0x12,        // 0210  LDAA $8012    clear CRB bit 7
            0x7C, 0x00, 0x10,        // 0213  INC  $0010    one more tick
            0x3B,                    // 0216  RTI
        });
        g.load(0xFFF8, {0x02, 0x00});
        g.m.bus.memWrite(0x0010, 0);
        g.m.bus.memWrite(0x0011, 0);
        g.m.cpu()->setPc(0x0100);

        g.runTo(5000);
        g.in->feed("Z");
        g.runTo(10500);
        CHECK(g.m.bus.memRead(0x0010) == 10, "ten 1 ms interrupts in 10.5 ms, each one serviced");
        CHECK(g.m.bus.memRead(0x0011) == 'Z', "and the input byte came in through the same ISR");
        CHECK(!g.mpt->assertsInt(), "nothing left pending: the ISR cleared both flags");
    }
}
