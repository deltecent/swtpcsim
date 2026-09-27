#include "test.h"

#include "chips/mc6840.h"
#include "core/clock.h"
#include "core/statefile.h"

#include <string>
#include <vector>

using namespace swtpc;

namespace {

// Register selects (RS2..RS0).
constexpr int kCr13   = 0;  // write: CR1 or CR3 (CR20 picks)
constexpr int kCr2    = 1;  // write: CR2; read: status
constexpr int kMsb1   = 2;  // write: MSB buffer; read: timer 1 counter MSB
constexpr int kLatch1 = 3;  // write: timer 1 latch; read: LSB buffer
constexpr int kMsb2   = 4;
constexpr int kMsb3   = 6;
constexpr int kLatch3 = 7;

struct Rig {
    Clock   clk;
    Ptm6840 ptm;
    std::vector<std::pair<int, bool>> edges;

    Rig() {
        ptm.reset(clk);
        ptm.onOutput = [this](int t, bool l) { edges.push_back({t, l}); };
    }
    void    w(int reg, uint8_t v) { ptm.write(reg, v, clk); }
    uint8_t r(int reg) { return ptm.read(reg, clk); }
    // Control registers the way FLEX does it: CR2 with CR20 = 1, then CR1.
    void cr1(uint8_t v) {
        w(kCr2, (uint8_t)(ptm.control(1) | 0x01));
        w(kCr13, v);
    }
    void cr3(uint8_t v) {
        w(kCr2, (uint8_t)(ptm.control(1) & ~0x01));
        w(kCr13, v);
    }
    void latch(int msbReg, uint16_t v) {
        w(msbReg, (uint8_t)(v >> 8));
        w(msbReg + 1, (uint8_t)v);
    }
    uint16_t counter(int msbReg) {
        uint8_t hi = r(msbReg);
        return (uint16_t)((hi << 8) | r(msbReg + 1));
    }
    // Advance, firing poll() at each deadline the way a board's Clock::at would.
    void run(uint64_t n) {
        uint64_t end = clk.now() + n;
        for (;;) {
            uint64_t at = ptm.nextEvent(clk);
            if (at == 0 || at > end) break;
            clk.advance(at - clk.now());
            ptm.poll(clk);
        }
        clk.advance(end - clk.now());
    }
};

void reset_state() {
    SECTION("mc6840: RESET");
    Rig g;
    CHECK(g.ptm.latch(0) == 0xFFFF && g.ptm.latch(1) == 0xFFFF && g.ptm.latch(2) == 0xFFFF,
          "RESET presets every latch to FFFF");
    CHECK(g.ptm.control(0) == 0x01 && g.ptm.control(1) == 0 && g.ptm.control(2) == 0,
          "RESET clears every control bit but CR10");
    CHECK(g.r(kCr2) == 0, "status reads 0 after RESET");
    CHECK(!g.ptm.irq(), "no IRQ after RESET");
    // Address 0 reaches CR3 after RESET (CR20 = 0).
    g.w(kCr13, 0x42);
    CHECK(g.ptm.control(2) == 0x42 && g.ptm.control(0) == 0x01, "address 0 is CR3 while CR20 = 0");
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x00);
    CHECK(g.ptm.control(0) == 0x00, "address 0 is CR1 once CR20 = 1");
}

void held_by_cr10() {
    SECTION("mc6840: CR10 holds every timer");
    Rig g;
    g.cr3(0x02);          // timer 3 on E
    g.w(kCr2, 0x03);      // timer 2 on E, CR20 = 1
    g.latch(kMsb2, 0x0010);
    g.run(1000);
    CHECK(g.counter(kMsb2) == 0x0010, "a held timer does not count");
    g.w(kCr13, 0x00);     // release
    g.run(5);
    CHECK(g.counter(kMsb2) == 0x000B, "released, it counts E cycles");
    g.w(kCr13, 0x01);     // hold again: preset from the latch
    CHECK(g.counter(kMsb2) == 0x0010, "CR10 = 1 presets the counter");
}

void decode_buffers() {
    SECTION("mc6840: MSB buffer, latch, LSB buffer");
    Rig g;
    g.cr1(0x00);
    g.w(kMsb3, 0x12);      // any MSB address reaches the one buffer
    g.w(kLatch1, 0x34);
    CHECK(g.ptm.latch(0) == 0x1234, "the MSB buffer is shared by all three timers");
    CHECK(g.ptm.counter(0) == 0x1234, "a latch write initializes when CRx4 = 0");
    CHECK(g.r(kMsb1) == 0x12, "counter read returns the MSB");
    CHECK(g.r(kLatch3) == 0x34, "...and any LSB address returns the buffered LSB");
    CHECK(g.r(kCr13) == 0, "address 0 reads as no operation");
}

void continuous16() {
    SECTION("mc6840: continuous 16-bit on E");
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x82);      // timer 1: output on, E clock, continuous, released
    g.latch(kMsb1, 4);     // N = 4: a time-out every 5 clocks
    g.edges.clear();
    g.run(4);
    CHECK(g.counter(kMsb1) == 0 && !g.ptm.flag(0), "N clocks bring the counter to 0");
    g.run(1);
    CHECK(g.ptm.flag(0), "the flag sets at N+1 clocks");
    CHECK(g.counter(kMsb1) == 4, "the counter recycles from the latch");
    CHECK(g.ptm.output(0), "O1 went high at the first time-out");
    g.run(5);
    CHECK(!g.ptm.output(0), "O1 toggles at every time-out");
    CHECK(g.edges.size() == 2 && g.edges[0] == std::make_pair(0, true) &&
              g.edges[1] == std::make_pair(0, false),
          "onOutput saw both edges, in order");
    g.run(5 * 1000 + 3);
    CHECK(g.counter(kMsb1) == 1, "a long run lands on the right count");
}

void dual8() {
    SECTION("mc6840: dual 8-bit");
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x86);      // output on, dual 8-bit, E clock
    g.latch(kMsb1, 0x0304);  // M = 3, L = 4: the data sheet's Figure 10
    g.edges.clear();
    g.run(15);
    CHECK(!g.ptm.output(0), "low for M(L+1)+1 = 16 clocks (15 in)");
    g.run(1);
    CHECK(g.ptm.output(0), "high after 16 clocks");
    CHECK(!g.ptm.flag(0), "no time-out yet");
    g.run(3);
    CHECK(g.ptm.output(0) && !g.ptm.flag(0), "high for L = 4 clocks (3 in)");
    g.run(1);
    CHECK(g.ptm.flag(0), "the time-out comes at (L+1)(M+1) = 20 clocks");
    CHECK(!g.ptm.output(0), "the output falls at the time-out");
    CHECK(g.counter(kMsb1) == 0x0304, "the counter recycles to M:L");
    g.run(7);
    CHECK(g.counter(kMsb1) == 0x0202, "the LSB counts L..0 and each underflow takes one from the MSB");
    CHECK(g.edges.size() == 2, "two edges in the first period");
}

void single_shot() {
    SECTION("mc6840: single-shot 16-bit");
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0xA2);      // output on, single-shot, E clock
    g.latch(kMsb1, 4);
    CHECK(!g.ptm.output(0), "low at initialization");
    g.run(1);
    CHECK(g.ptm.output(0), "high after the first clock");
    g.run(4);
    CHECK(!g.ptm.output(0) && g.ptm.flag(0), "low at the time-out, flag set");
    g.run(50);
    CHECK(!g.ptm.output(0), "and stays low");
    g.w(kLatch1, 4);       // re-initialize
    g.run(1);
    CHECK(g.ptm.output(0), "a new initialization gives a new pulse");
}

void flag_rules() {
    SECTION("mc6840: flags, IRQ and the RS-RT rule");
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x02);      // timer 1 on E, IRQ disabled
    g.latch(kMsb1, 9);
    g.run(10);
    CHECK(g.r(kCr2) == 0x01, "status bit 0 is I1");
    CHECK(!g.ptm.irq(), "no IRQ without CR16");
    g.w(kCr13, 0x42);      // IRQ enabled
    CHECK(g.ptm.irq(), "CR16 lets the set flag drive IRQ");
    CHECK(g.r(kCr2) == 0x81, "status bit 7 is the composite flag");

    // A counter read without a status read first does not clear the flag.
    Rig h;
    h.w(kCr2, 0x01);
    h.w(kCr13, 0x42);
    h.latch(kMsb1, 9);
    h.run(10);
    h.counter(kMsb1);
    CHECK(h.ptm.flag(0), "a counter read alone leaves the flag set");
    h.r(kCr2);
    h.counter(kMsb1);
    CHECK(!h.ptm.flag(0) && !h.ptm.irq(), "status read, then counter read, clears it");

    // A flag that sets between the status read and the counter read survives.
    Rig k;
    k.w(kCr2, 0x01);
    k.w(kCr13, 0x42);
    k.latch(kMsb1, 9);
    k.r(kCr2);             // sees nothing
    k.run(10);
    k.counter(kMsb1);
    CHECK(k.ptm.flag(0), "a flag the status read did not see is not cleared");

    // A latch write clears it when it initializes.
    k.latch(kMsb1, 9);
    CHECK(!k.ptm.flag(0), "a latch write (CRx4 = 0) clears the flag");
}

void one_event() {
    SECTION("mc6840: one deadline, only when it matters");
    Rig g;
    g.ptm.onOutput = nullptr;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x02);      // E clock, IRQ off, output off
    g.latch(kMsb1, 99);
    CHECK(g.ptm.nextEvent(g.clk) == 0, "nothing to wake for with IRQ and output off");
    g.w(kCr13, 0x42);
    CHECK(g.ptm.nextEvent(g.clk) == g.clk.now() + 100, "IRQ on: wake at the time-out");
    g.run(100);
    CHECK(g.ptm.irq() && g.ptm.nextEvent(g.clk) == 0, "flag up: no further wake");
}

void prescaler() {
    SECTION("mc6840: timer 3's divide-by-8");
    Rig g;
    g.cr3(0x03);           // E clock, CR30 = 1
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x00);      // release
    g.latch(kMsb3, 9);
    g.run(79);
    CHECK(!(g.r(kCr2) & 0x04), "not yet at 79 E cycles");
    g.run(1);
    CHECK(g.r(kCr2) & 0x04, "a time-out at (N+1)*8 E cycles");
}

void external() {
    SECTION("mc6840: external clock");
    Rig g;
    g.w(kCr2, 0x81);       // timer 2: output on, external
    g.w(kCr13, 0x80);      // timer 1: output on, external, released
    g.latch(kMsb1, 2);
    g.run(10000);
    CHECK(g.counter(kMsb1) == 2, "E cycles do not clock an external timer");
    g.ptm.clockEdge(0, g.clk);
    g.ptm.clockEdge(0, g.clk);
    CHECK(g.counter(kMsb1) == 0, "each Cx edge is one clock");
    g.ptm.clockEdge(0, g.clk);
    CHECK(g.ptm.flag(0) && g.ptm.output(0), "the third edge times out and O1 rises");

    // An external timer 3 with CR30 counts every eighth edge.
    g.cr3(0x01);
    g.w(kCr2, 0x81);
    g.latch(kMsb3, 0);
    for (int i = 0; i < 7; ++i) g.ptm.clockEdge(2, g.clk);
    CHECK(!g.ptm.flag(2), "seven edges through the prescaler: nothing");
    g.ptm.clockEdge(2, g.clk);
    CHECK(g.ptm.flag(2), "the eighth is a clock");

    // Held, edges are ignored.
    g.w(kCr2, 0x81);
    g.w(kCr13, 0x81);
    g.ptm.clockEdge(0, g.clk);
    CHECK(g.counter(kMsb1) == 2, "a held timer ignores its clock");
}

void flex_probe() {
    SECTION("mc6840: FLEX9's boot probe arithmetic");
    // CR2 = 01, CR1 = 00, latch FFFF, then two reads 240 edges apart (120/s for 2 s).
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0x00);
    g.latch(kMsb1, 0xFFFF);
    uint16_t a = g.counter(kMsb1);
    for (int i = 0; i < 240; ++i) g.ptm.clockEdge(0, g.clk);
    uint16_t b = g.counter(kMsb1);
    CHECK((uint16_t)(a - b) == 240, "the difference is the number of line pulses");
}

void not_modeled() {
    SECTION("mc6840: comparison modes are reported");
    Rig g;
    g.w(kCr2, 0x09);
    g.w(kCr13, 0x08);
    auto log = g.ptm.drainLog();
    CHECK(log.size() == 2, "each timer that selects a comparison mode is logged once");
    g.w(kCr13, 0x08);
    CHECK(g.ptm.drainLog().empty(), "...and only once");
}

void snapshot() {
    SECTION("mc6840: snapshot and restore");
    Rig g;
    g.w(kCr2, 0x01);
    g.w(kCr13, 0xC2);
    g.latch(kMsb1, 0x0100);
    g.run(300);
    StateWriter w;
    g.ptm.serialize(w);
    uint16_t cnt = g.ptm.counter(0);

    Ptm6840     p;
    p.onOutput = [](int, bool) {};
    StateReader rd(w.data());
    p.deserialize(rd);
    CHECK(p.counter(0) == cnt && p.latch(0) == 0x0100 && p.control(0) == 0xC2,
          "counter, latch and control survive");
    CHECK(p.flag(0) == g.ptm.flag(0) && p.irq() == g.ptm.irq(), "the flag and IRQ survive");
    CHECK(p.nextEvent(g.clk) == g.ptm.nextEvent(g.clk), "the restored chip wants the same deadline");
}

} // namespace

void test_mc6840() {
    reset_state();
    held_by_cr10();
    decode_buffers();
    continuous16();
    dual8();
    single_shot();
    flag_rules();
    one_event();
    prescaler();
    external();
    flex_probe();
    not_modeled();
    snapshot();
}
