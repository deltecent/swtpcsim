#pragma once
//
// mc6840.h -- a Motorola MC6840 PTM (Programmable Timer Module): three 16-bit
// down-counters, each with a latch, a control register, a clock input Cx, and an
// output Ox, plus a shared status register and IRQ.
//
// A CHIP IS NOT A BOARD (see chips/mc6850.h): it has no addresses of its own. The
// owning board decodes its window and passes RS2..RS0 as `reg` (0..7). The board
// also owns the wires: it feeds falling edges of an external clock in with
// clockEdge(), and it hears every change of an output through onOutput (the MP-ID
// chains O3 into C2 and counts O1 with a 74LS393).
//
// Modeled from the data sheet (reference/MC6840 Programmable Timer Module.md):
//  - Table 1 register selection, the shared MSB buffer and LSB buffer.
//  - Continuous and single-shot modes, 16-bit and dual 8-bit, with the output
//    waveforms of Tables 5 and 6. The counter recycles from the latch at every
//    time-out, which sets the timer's flag.
//  - Counter initialization on RESET, on CR10 = 1, and on a latch write when
//    CRx4 = 0.
//  - The status register, the composite flag, and the RS-RT rule: a flag clears
//    when its counter is read after a status read that saw it set.
//  - CR30's divide-by-8 prescaler on timer 3.
//
// NOT modeled:
//  - The gate inputs. Every timer is treated as if its gate were held low, as the
//    MP-ID wires them. So a G falling edge never initializes a counter.
//  - The frequency and pulse-width comparison modes (CRx3 = 1). They measure the
//    gate. Selecting one logs "not modeled", and the timer counts as continuous.
//  - The three-E-cycle synchronization of an external clock edge.
//
// Time is event-driven (DESIGN.md 7.5). A timer on the E clock is brought up to
// date from the cycle count whenever it is looked at. nextEvent() says when the
// board must call poll() so that a flag with its IRQ enabled, or an output edge
// someone listens to, happens on time. A timer on an external clock moves only
// when clockEdge() is called.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace swtpc {

class Clock;
class StateWriter;
class StateReader;

class Ptm6840 {
public:
    static constexpr int kTimers = 3;

    // reg = RS2..RS0. Reads bring the timers up to `clk.now()`.
    uint8_t read(int reg, const Clock& clk);
    void    write(int reg, uint8_t data, const Clock& clk);

    // RESET: every latch FFFF, every control bit 0 except CR10 = 1, every flag clear.
    void reset(const Clock& clk);

    // A recognized falling edge on Cx (timer 0..2). Ignored while the timer is on the
    // E clock or held by CR10.
    void clockEdge(int timer, const Clock& clk);

    // Bring every timer up to `clk.now()`. Output edges and flags happen in order.
    void poll(const Clock& clk);

    // The absolute cycle at which poll() must next run, or 0 when nothing needs it.
    uint64_t nextEvent(const Clock& clk) const;

    // The IRQ pin: status bit 7, the composite flag.
    bool irq() const;

    // Every change of an output, as it happens: (timer 0..2, new level).
    std::function<void(int, bool)> onOutput;

    // Read-only views, for the board's status lines and the tests. They are the
    // state as of the last access or poll(): call poll() first for `now`.
    bool     output(int timer) const;
    uint8_t  control(int timer) const { return t_[timer].ctrl; }
    uint16_t latch(int timer) const { return t_[timer].latch; }
    uint16_t counter(int timer) const { return t_[timer].cnt; }
    bool     flag(int timer) const { return t_[timer].flag; }

    // "not modeled" notes. Cleared by draining.
    std::vector<std::string> drainLog();

    void serialize(StateWriter& w) const;
    void deserialize(StateReader& r);

private:
    struct Timer {
        uint16_t latch   = 0xFFFF;
        uint8_t  ctrl    = 0;
        uint16_t cnt     = 0xFFFF;
        bool     flag    = false;
        bool     out16   = false;  // the 16-bit output's toggle state
        bool     fired   = false;  // a time-out since initialization (single-shot)
        bool     clocked = false;  // a clock since initialization (single-shot)
        uint8_t  pre     = 0;      // timer 3's ÷8 prescaler, for an external clock
        uint64_t tRef    = 0;      // E clock: the cycle the counter state is true at
        bool     lastOut = false;  // what onOutput was last told
    };

    bool     held() const { return (t_[0].ctrl & 0x01) != 0; }
    bool     onE(int i) const { return (t_[i].ctrl & 0x02) != 0; }
    bool     dual(int i) const { return (t_[i].ctrl & 0x04) != 0; }
    bool     single(int i) const { return (t_[i].ctrl & 0x28) == 0x20; }
    bool     toggles(int i) const { return !dual(i) || (t_[i].latch & 0xFF) == 0; }
    uint64_t div(int i) const { return (i == 2 && (t_[2].ctrl & 0x01)) ? 8 : 1; }
    uint64_t toTimeout(int i) const;
    uint64_t toChange(int i) const;
    uint64_t period(int i) const;
    bool     watching(int i) const { return onOutput && (t_[i].ctrl & 0x80); }

    void init(int i, uint64_t now);
    void sync(int i, uint64_t now);
    void advance(int i, uint64_t n);
    void count(int i, uint64_t n);
    void notify(int i);
    void writeControl(int i, uint8_t v, uint64_t now);

    Timer   t_[kTimers];
    uint8_t msbBuf_ = 0;
    uint8_t lsbBuf_ = 0;
    uint8_t rsSeen_ = 0;  // flags the last status read saw set (the RS-RT rule)
    uint8_t warned_ = 0;  // timers already logged as "not modeled"
    std::vector<std::string> log_;
};

} // namespace swtpc
