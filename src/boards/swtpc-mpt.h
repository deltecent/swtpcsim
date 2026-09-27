#pragma once
//
// The SWTPC MP-T interrupt timer -- reference/MP-T Interrupt Timer.md.
//
// ONE 6820 PIA ON AN SS-30 SLOT, AND A CLOCK CHIP BEHIND ITS B SIDE. The four slot
// addresses are the PIA's four registers (RS0 = A0, RS1 = A1):
//
//     base+0   PRA / DDRA    the input port (side A)
//     base+1   CRA
//     base+2   PRB / DDRB    the timer's rate select and reset (side B)
//     base+3   CRB
//
// Side B drives an MK5009 counter/time base running off a 1 MHz crystal. PB0-PB3 pick
// one tap of its divider chain, PB7 is its RESET 0 input (1 = held at zero), and the
// chip's TIME OUT square wave comes back on CB1. So a program writes $FF to DDRB, sets
// CRB for a CB1 interrupt on the falling edge, writes $80 to PRB to hold the count,
// then the rate code (with PB7 clear) to start it. Every period after that, CB1 sets
// CRB bit 7 and -- with CRB bit 0 set -- pulls the 6800 IRQ. Reading PRB clears it.
//
//     code  period      code  period      code  period
//      0    1 us         6    1 s          B    10 min
//      1    10 us        7    10 s         E    20 ms
//      2    100 us       8    100 s        C, D, F  no output
//      3    1 ms         9    1 min
//      4    10 ms        A    1 hour
//      5    100 ms
//
// Side A is the board's other job: a buffered eight-bit input port with a CA1 strobe,
// the same as half an MP-L. It is the unit `in`: CONNECT it to an endpoint and each byte
// that arrives is latched and strobes CA1, one at a time -- the next waits until the
// guest reads PRA. The CA2 "data accepted" handshake back to the sender is not modeled;
// the stream paces itself. Both PIA IRQ outputs are jumpered to the bus IRQ -- the
// board's NMI option needs a bus NMI, which this simulator does not carry.
//
// THE TIME BASE IS ONE DEADLINE, NOT A TICK (DESIGN.md 7.5). Every tap of the MK5009
// comes off one synchronous divider chain that starts from zero when RESET 0 falls, so
// the whole waveform is known from t0_ alone: the output starts low, and its edges fall
// at t0 + k*P (k >= 1) and rise half a period earlier. The board arms ONE clock
// deadline, at the next active edge, and only while the CB1 flag is clear -- once the
// flag is up, further edges change nothing until the guest reads PRB. So a 1 us rate
// costs one event per interrupt the guest actually services, not a million a second.
// Any guest access to side B re-arms: a read may have cleared the flag, a write may
// have moved the rate, the reset or the edge.

#include "chips/mc6820.h"
#include "core/board.h"
#include "core/clock.h"
#include "host/stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace swtpc {

class MptBoard : public Board {
public:
    MptBoard();
    ~MptBoard() override;

    std::string type() const override { return "mpt"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // Four addresses in a page it shares with the other SS-30 boards -- the bus must ask
    // per address. See Board::decodeIsPageUniform().
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts: both PIA IRQ outputs are jumpered to the 6800 IRQ ----
    bool assertsInt() const override;

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override { reset(Reset::PowerOn); }
    void pump() override;
    void clockAttached() override { rearm(); }
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property>    properties() override;
    std::vector<Property>    unitProperties(const std::string& unit) override;
    std::vector<UnitDef>     units() const override;
    std::vector<MapEntry>    memMap() const override;
    std::vector<std::string> statusLines() const override;

    // ---- the side-A input port, unit `in` ----
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override;
    ByteStream* unitStream(const std::string& unit) override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The MK5009 period for a rate code, in microseconds; 0 for a code with no output.
    static uint64_t periodUs(unsigned code);

    // The endpoint resolver for `in`, installed by the composition root (DESIGN.md 7.7).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

private:
    // The SS-30 slot base. Slot 4 ($8010) is where SWTPC's INTCLK looks for the board,
    // and the one slot the stock `swtpc` machine leaves free.
    uint16_t at_ = 0x8010;

    Pia6820 pia_;

    // What is on the other end of side A. Never null -- a NullStream when idle. The spec
    // is the endpoint as the user gave it, for SHOW and CONFIG SAVE.
    std::unique_ptr<ByteStream> in_;
    std::string                 inSpec_ = "null";

    // The MK5009's state: the cycle its chain last left reset, and whether RESET 0 is
    // high now. A PB line the guest is not driving (DDR bit 0) floats high, so after
    // RESET the chain is held and the select reads F -- nothing runs until a program sets
    // side B up.
    uint64_t t0_   = 0;
    bool     held_ = true;

    Clock::Handle wake_   = Clock::kNone;
    uint64_t      wakeAt_ = 0;   // the cycle wake_ is set for

    uint8_t  bLines() const;       // PB0-PB7 as the MK5009 sees them
    uint64_t nextEdge() const;     // the cycle of the next active CB1 edge; 0 = none
    void     linesChanged();       // follow PB7 (reset) after a side-B write
    void     rearm();              // cancel the deadline and set the next one, if any
    void     edge();               // the deadline: CB1 fires
};

} // namespace swtpc
