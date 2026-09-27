#pragma once
//
// The SWTPC MP-ID interface driver board -- reference/MP-ID Interface Driver Board.md.
//
// THE S/09's I/O DRIVER, WITH A PIA AND A 6840 ON IT. The real board also buffers and
// decodes the 30-pin I/O bus and generates the baud clocks; neither is modeled, because
// the SS-30 boards here decode themselves and take `baud` directly. What software sees is
// two ports of the I/O window (standard jumpers, $E000 block):
//
//     base+$00..$0F   the 6820 PIA  (RS0 = A0, RS1 = A1; mirrored four times)
//                       +0 PRA/DDRA   +1 CRA   +2 PRB/DDRB   +3 CRB
//     base+$10..$1F   the MC6840 PTM (RS0-RS2 = A0-A2; mirrored twice)
//
// THE TIMER WIRING (the schematic):
//  - The LINE PULSE fires on both halves of the AC wave: 2 x line_hz pulses a second. It
//    clocks C1, and C3 through the INT jumper.
//  - O3 is wired to C2, so timer 2 counts timer 3's output. The gates are grounded.
//  - IC7, a 74LS393, counts O1's falling edges. PA0 is O1 itself and PA1-PA7 are the
//    count, so PA reads the number of timer-1 time-outs mod 256 -- which is what FLEX9's
//    TIME.CMD reads, beside the timer-1 counter at base+$12. PA7 also drives CA1.
//  - The 6840's IRQ and both PIA IRQs are on the bus IRQ. The bus RESET resets both chips
//    and clears IC7.
//
// Side B is the parallel printer port, the unit `lpt`: a byte the guest writes to PRB goes
// out to whatever `lpt` is connected to, and the printer's ACK comes straight back on CB1.
// CB2's DATA READY strobe is not modeled; the write itself is the strobe.
//
// THE LINE CLOCK IS ONE DEADLINE PER PULSE, 100 or 120 a second -- cheap. The 6840 asks
// for its own deadline (Ptm6840::nextEvent) only when a timer on the E clock has an
// interrupt or a watched output to deliver.

#include "chips/mc6820.h"
#include "chips/mc6840.h"
#include "core/board.h"
#include "core/clock.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace swtpc {

class MpidBoard : public Board {
public:
    MpidBoard();
    ~MpidBoard() override;

    std::string type() const override { return "mpid"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;
    bool    decodeIsPageUniform() const override { return false; }

    // ---- interrupts: the 6840 and both PIA sides share the bus IRQ ----
    bool assertsInt() const override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void pump() override;
    void clockAttached() override;
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property>    properties() override;
    std::vector<Property>    unitProperties(const std::string& unit) override;
    std::vector<UnitDef>     units() const override;
    std::vector<MapEntry>    memMap() const override;
    std::vector<std::string> statusLines() const override;
    std::vector<std::string> drainLog() override;

    // ---- the printer port, unit `lpt` ----
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For the tests: the 6840, IC7's count, and the cycle of the next line pulse.
    const Ptm6840& ptm() const { return ptm_; }
    uint8_t  ic7() const { return ic7_; }
    uint64_t nextPulse() const { return pulseAt_; }

    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

private:
    uint16_t at_     = 0xE080;  // the PIA; the 6840 is at at_ + $10
    int      lineHz_ = 60;

    Pia6820 pia_;
    Ptm6840 ptm_;
    uint8_t ic7_ = 0;           // the 74LS393's eight bits

    // The line clock. A pulse period is hz / (2 x line_hz) cycles, rarely a whole number,
    // so the remainder is carried: the last pulse and its remainder are the state, and
    // the next pulse (with the remainder it will leave) is worked out when it is armed --
    // after every board's power(), so a CPU board that publishes the rate late is heard.
    uint64_t lastPulse_ = 0;
    uint64_t lastFrac_  = 0;
    uint64_t pulseAt_   = 0;
    uint64_t pulseFrac_ = 0;

    std::unique_ptr<ByteStream> lpt_;
    std::string                 lptSpec_ = "null";

    Clock::Handle lineWake_ = Clock::kNone;
    Clock::Handle ptmWake_  = Clock::kNone;
    uint64_t      ptmAt_    = 0;

    const Clock& clk() const;
    uint8_t      portA() const;          // what IC7 and O1 put on PA0-PA7
    void         output(int timer, bool level);
    void         stepPulse();            // pulseAt_/pulseFrac_: one period after lastPulse_
    void         armLine();
    void         pulse();                // the line-pulse deadline
    void         rearmPtm();             // poll the 6840 and arm its next deadline
};

} // namespace swtpc
