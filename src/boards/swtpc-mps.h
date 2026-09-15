#pragma once
//
// The SWTPC MP-S serial interface -- reference/MP-S Serial Interface.md.
//
// THE SWTPC 6800's CONSOLE PORT. The MP-S is a single 6850 ACIA on an SS-30 I/O card
// that plugs into a slot of the MP-B motherboard; it is the board the console terminal
// normally attaches to, and SWTBUG/MIKBUG's terminal routines are hardwired to it.
//
// MEMORY-MAPPED, NOT PORT-MAPPED. The 6800 has no IN/OUT space, so the ACIA answers at
// ADDRESSES. The SS-30 I/O window is $8000-$801F: eight slots of four addresses, slot N
// based at $8000 + 4*N. The console lives in slot 1, based at $8004:
//
//     8004   6850 ACIA -- Status (read) / Control (write)     [A0 = 0 -> RS = 0]
//     8005   6850 ACIA -- RxData (read) / TxData (write)      [A0 = 1 -> RS = 1]
//     8006   MIRROR of 8004 (only A0 reaches the 6850's RS)
//     8007   MIRROR of 8005
//
// The 6850 fills the whole four-address slot because ONLY A0 reaches its register
// select: base+0/base+2 are status/control, base+1/base+3 are data. That mirror is
// load-bearing -- SWTBUG's power-up probe requires [base] == [base+2] to recognize the
// board as an ACIA and master-reset it (see swtpc-mps.cpp decodes()). So this is the
// Altair 680b's onboard I/O (mits-680io.h) with the strap port removed and a
// configurable SS-30 slot base added.
//
// THE 6850 IS THE SAME CHIP AS EVERYWHERE (chips/mc6850.h), and the SAME serial section
// (chips/sio2port.h): one channel `tty`, base 0, so address `at`+0 -> section port 0
// (status/control) and `at`+1 -> section port 1 (data). Reusing the section is how the
// interrupt/connect/snapshot glue stays fixed in one place.

#include "chips/sio2port.h"
#include "core/board.h"

#include <cstdint>
#include <string>
#include <vector>

namespace swtpc {

class MpsBoard : public Board {
public:
    MpsBoard();

    std::string type() const override { return "mps"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle&) const override;
    uint8_t read(const BusCycle&) override;
    void    write(const BusCycle&) override;

    // Two addresses in a page, not the whole page -- so the bus must ask per address,
    // not cache one answer for the page. See Board::decodeIsPageUniform().
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts (the ACIA's IRQ -> the 6800's IRQ) ----
    bool    assertsInt() const override { return sio_.assertsInt(); }

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void pump() override { sio_.pump(); }
    void clockAttached() override { sio_.attachClock(clock_); }
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // ---- serial units: the 6850's `tty`, delegated to the section ----
    std::vector<UnitDef>  units() const override { return sio_.units(); }
    std::vector<Property> unitProperties(const std::string& unit) override {
        return sio_.unitProperties(unit);
    }
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override {
        return sio_.disconnect(unit, err);
    }
    // Install a PRE-BUILT stream (the MCP console's filtered scripted line, or a mirror over
    // it for --mirror). Forward to the section, exactly as disconnect()/unitStream() do --
    // without this the console cannot be rebound under --mcp and --mirror fails outright.
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override {
        return sio_.connectStream(unit, std::move(s), err);
    }
    ByteStream* unitStream(const std::string& unit) override { return sio_.unitStream(unit); }
    uint64_t    rxBytes() const override { return sio_.rxBytes(); }
    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // The endpoint resolver is the section's (shared with every SIO-bearing card).
    static void setResolver(EndpointResolver r) { Sio2Port::setResolver(std::move(r)); }

private:
    // The SS-30 slot base. Slot N is $8000 + 4*N; the console is slot 1 -> $8004. The
    // 6850 answers `at`+0 (control/status) and `at`+1 (Rx/Tx data); the upper two slot
    // addresses are not ours. Configurable via the `at` property so the card can move.
    uint16_t at_ = 0x8004;

    // The onboard 6850, its one channel named `tty`, at section-port 0 (base 0).
    Sio2Port sio_;

    std::vector<std::string> log_;
};

} // namespace swtpc
