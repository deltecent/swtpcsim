#include "test.h"

#include "boards/swtpc-mpid.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/stream.h"

#include <string>

using namespace swtpc;

namespace {

// A machine with the MP-ID at its standard E080. The clock runs at 1.2 MHz so that a
// line pulse is a whole number of cycles: 10000 at 60 Hz (120 a second), 12000 at 50 Hz.
constexpr uint64_t kPulse = 10000;

struct Rig {
    Machine         m;
    MpidBoard*      id  = nullptr;
    ScriptedStream* lpt = nullptr;

    explicit Rig(const char* lineHz = "60") {
        std::string err;
        m.bus.setVerify(true);
        id = dynamic_cast<MpidBoard*>(m.add("mpid", "mpid0", err));
        setProperty(*id, "line_hz", lineHz, err);
        id->connect("lpt", "scripted", err);
        lpt = dynamic_cast<ScriptedStream*>(id->unitStream("lpt"));
        Board* cpu = m.add("6809", "cpu0", err);
        setProperty(*cpu, "clock_hz", "1200000", err);  // the CPU board publishes the rate
        m.power();
    }

    void    wr(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    uint8_t rd(uint16_t a) { return m.bus.memRead(a); }

    // LDX: the MSB, which puts the LSB in the buffer the next address reads.
    uint16_t counter(int timer) {
        uint16_t a  = (uint16_t)(0xE090 + 2 * timer + 2);
        uint8_t  hi = rd(a);
        return (uint16_t)((hi << 8) | rd((uint16_t)(a + 1)));
    }

    // Run to just after the n-th line pulse from power.
    void pulses(uint64_t n) {
        uint64_t t = n * kPulse;
        if (t > m.clock.now()) m.clock.advance(t - m.clock.now());
    }

    // FLEX9's TIME.CMD setup: side A all inputs on the data register, timer 1 on C1,
    // continuous, O1 enabled, latch FFFF (reference/MP-ID Interface Driver Board.md).
    void flexTime(uint16_t latch = 0xFFFF) {
        wr(0xE081, 0x30);
        wr(0xE080, 0x00);
        wr(0xE081, 0x34);
        wr(0xE091, 0x81);
        wr(0xE090, 0x80);
        wr(0xE092, (uint8_t)(latch >> 8));
        wr(0xE093, (uint8_t)latch);
        wr(0xE091, 0x80);
    }
};

} // namespace

void test_swtpc_mpid() {
    SECTION("mpid -- the PIA at E080-E08F, the 6840 at E090-E09F, and `base` moves both");
    {
        Rig      g;
        BusCycle c;
        c.type = Cycle::MemRead;
        c.addr = 0xE080;
        CHECK(g.id->decodes(c), "E080 is the PIA");
        c.addr = 0xE09F;
        CHECK(g.id->decodes(c), "E09F is the 6840's mirror");
        c.addr = 0xE07F;
        CHECK(!g.id->decodes(c), "E07F is I/O port 7, not the MP-ID");
        c.addr = 0xE0A0;
        CHECK(!g.id->decodes(c), "E0A0 is past it");

        std::string err;
        CHECK(setProperty(*g.id, "base", "C080", err), "C080 is port 8 of the C000 block");
        c.addr = 0xC090;
        CHECK(g.id->decodes(c), "and the 6840 moves with it");
        CHECK(!setProperty(*g.id, "base", "E090", err), "E090 is not a port-8 base");
        CHECK(!setProperty(*g.id, "base", "8080", err), "8080 is not in a block the jumper offers");
        CHECK(!setProperty(*g.id, "line_hz", "55", err), "line_hz is 50 or 60");
    }

    SECTION("mpid -- the 6840 decodes RS0-RS2 from A0-A2, mirrored at +8");
    {
        Rig g;
        g.flexTime();
        CHECK((g.rd(0xE099) & 0x01) == 0, "the status register at E091 and E099");
        g.wr(0xE09A, 0x12);                        // the MSB buffer through the mirror
        g.wr(0xE09B, 0x34);                        // timer 1's latches through the mirror
        CHECK(g.id->ptm().latch(0) == 0x1234, "a latch written through the mirror");
    }

    SECTION("mpid -- the line pulse comes 2 x line_hz times a second");
    {
        Rig g;
        CHECK(g.id->nextPulse() == kPulse, "at 60 Hz the first pulse is 1/120 s after power");
        g.pulses(1);
        CHECK(g.id->nextPulse() == 2 * kPulse, "and one period later the next");
        Rig h("50");
        CHECK(h.id->nextPulse() == 12000, "at 50 Hz the pulses are 1/100 s apart");
    }

    SECTION("mpid -- the line pulse clocks timer 1 (C1), and counts show in the counter");
    {
        Rig g;
        g.flexTime();
        g.pulses(5);
        CHECK(g.counter(0) == 0xFFFA, "five pulses, five counts down from FFFF");
        g.pulses(125);
        CHECK(g.counter(0) == 0xFF82, "120 more in the next second");
    }

    SECTION("mpid -- IC7 counts O1 onto PA: PA reads the number of timer-1 time-outs");
    {
        Rig g;
        g.flexTime(0x0003);                        // a time-out every 4 pulses
        CHECK(g.rd(0xE080) == 0x00, "nothing counted yet");
        g.pulses(4);
        CHECK(g.rd(0xE080) == 0x01, "the first time-out: O1 high on PA0, IC7 still 0");
        g.pulses(8);
        CHECK(g.rd(0xE080) == 0x02, "the second: O1 fell, IC7 counted it");
        CHECK(g.id->ic7() == 1, "IC7 holds one falling edge");
        g.pulses(4 * 13);
        CHECK(g.rd(0xE080) == 13, "PA is the time-out count");
    }

    SECTION("mpid -- PA7 drives CA1: the count wrapping to 0 sets CRA bit 7");
    {
        Rig g;
        g.flexTime(0x0000);                        // a time-out every pulse
        g.pulses(128);
        CHECK(g.rd(0xE080) == 0x80, "PA7 rose at 128 time-outs");
        CHECK((g.rd(0xE081) & 0x80) == 0, "CRA = 34 takes CA1's falling edge: no flag yet");
        g.pulses(256);
        CHECK((g.rd(0xE081) & 0x80) != 0, "PA7 falling set the CA1 flag");
        CHECK(g.rd(0xE080) == 0x00, "PA wrapped at 256");
        CHECK((g.rd(0xE081) & 0x80) == 0, "and reading PRA cleared it");
    }

    SECTION("mpid -- O3 is wired to C2, and C3 takes the line pulse (INT jumper)");
    {
        Rig g;
        g.wr(0xE091, 0x00);                        // CR2: C2, CR20 = 0 (address 0 is CR3)
        g.wr(0xE090, 0x80);                        // CR3: O3 on, C3, continuous 16-bit
        g.wr(0xE096, 0x00);
        g.wr(0xE097, 0x00);                        // timer 3 latch 0: O3 toggles each pulse
        g.wr(0xE094, 0xFF);
        g.wr(0xE095, 0xFF);                        // timer 2 latch FFFF
        g.wr(0xE091, 0x01);                        // CR20 = 1 (address 0 is CR1)
        g.wr(0xE090, 0x00);                        // CR1: release the timers
        g.pulses(8);
        CHECK(g.counter(2) == 0x0000, "timer 3 counts line pulses too");
        CHECK(g.counter(1) == 0xFFFB, "timer 2 counted O3's four falling edges");
    }

    SECTION("mpid -- the 6840's IRQ reaches the bus, and the RS-RT reads clear it");
    {
        Rig g;
        g.wr(0xE091, 0x01);
        g.wr(0xE090, 0x40);                        // CR1: IRQ on, C1, continuous
        g.wr(0xE092, 0x00);
        g.wr(0xE093, 0x01);                        // a time-out every 2 pulses
        g.pulses(1);
        CHECK(!g.id->assertsInt(), "no IRQ before the time-out");
        g.pulses(2);
        CHECK(g.id->assertsInt(), "the time-out pulls IRQ");
        CHECK(g.rd(0xE091) == 0x81, "status: timer 1's flag and the composite");
        g.counter(0);
        CHECK(!g.id->assertsInt(), "the counter read after the status read lets go");
    }

    SECTION("mpid -- a timer on the E clock interrupts on time with no one looking");
    {
        Rig g;
        g.pulses(1);
        uint64_t t0 = g.m.clock.now();
        g.wr(0xE091, 0x01);
        g.wr(0xE092, 0x03);
        g.wr(0xE093, 0xE7);                        // latch 999
        g.wr(0xE090, 0x42);                        // CR1: IRQ on, E clock; this starts it
        g.m.clock.advance(999);
        CHECK(!g.id->assertsInt(), "not after 999 E cycles");
        g.m.clock.advance(1);
        CHECK(g.id->assertsInt(), "at N + 1 = 1000 the deadline fired and IRQ is up");
        CHECK(g.m.clock.now() == t0 + 1000, "(the clock itself was only advanced)");
    }

    SECTION("mpid -- the printer port: a PRB write goes out on `lpt` and ACKs on CB1");
    {
        Rig         g;
        UnitDef     u;
        std::string err;
        CHECK(g.id->findUnit("LPT", u) && u.kind == UnitKind::Serial, "one unit, lpt");
        CHECK(!g.id->connect("in", "scripted", err), "and no other");
        g.wr(0xE082, 0xFF);                        // DDRB: all outputs
        g.wr(0xE083, 0x04);                        // CRB: the data register
        g.wr(0xE082, 'H');
        g.wr(0xE082, 'i');
        g.id->pump();
        CHECK(g.lpt->out() == "Hi", "the bytes reached the printer");
        CHECK((g.rd(0xE083) & 0x80) != 0, "the ACK set CRB bit 7");
        g.rd(0xE082);
        CHECK((g.rd(0xE083) & 0x80) == 0, "reading PRB clears it");
        g.wr(0xE083, 0x05);                        // CB1 IRQ on
        g.wr(0xE082, '!');
        CHECK(g.id->assertsInt(), "with CRB bit 0 set the ACK interrupts");
    }

    SECTION("mpid -- the bus RESET resets both chips and clears IC7; the line runs on");
    {
        Rig g;
        g.flexTime(0x0000);
        g.pulses(7);
        CHECK(g.rd(0xE080) == 7, "counting before the reset");
        uint64_t next = g.id->nextPulse();
        g.m.reset(Reset::Bus);
        CHECK(g.id->ic7() == 0, "IC7 is cleared");
        CHECK(g.id->ptm().control(0) == 0x01, "the 6840 is held by CR10");
        CHECK(g.id->nextPulse() == next, "the mains keeps its phase");
        g.pulses(20);
        CHECK(g.counter(0) == 0xFFFF, "a held timer does not count the pulses");
    }

    SECTION("mpid -- a snapshot carries the chips, IC7 and the line phase");
    {
        Rig g;
        g.flexTime(0x0003);
        g.pulses(10);

        StateWriter w;
        g.m.clock.serialize(w);
        g.id->serialize(w);

        Rig         h;
        StateReader rd(w.data());
        h.m.clock.deserialize(rd);
        h.id->deserialize(rd);
        CHECK(h.id->nextPulse() == 11 * kPulse, "the next pulse is where it was");
        CHECK(h.rd(0xE080) == 0x02, "PA came across");
        h.pulses(12);
        CHECK(h.rd(0xE080) == 0x03, "and the restored machine counts on");
        CHECK(h.counter(0) == 0x0003, "timer 1 recycled at the third time-out");
    }
}
