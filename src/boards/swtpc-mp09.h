#pragma once
//
// The SWTPC MP-09 processor board: a Motorola MC6809, the DAT, and the S-BUG ROM
// socket (reference/MP-09 6809 CPU Board.md).
//
// THE FIRST BOARD WHERE THE CPU'S ADDRESS IS NOT THE BUS'S ADDRESS. The DAT is a
// 16 x 4 RAM (a 74S189) that sits between the 6809's A12-A15 and the backplane's:
// each logical 4K segment is looked up and replaced with a physical one. So the
// core cannot be handed the backplane -- the byte it asks for at C000 may live at
// 3000. Instead the board gives the core a PRIVATE inner Bus with one board on it,
// the DAT port below, and the port forwards every cycle to the backplane at the
// translated address. The core is unchanged, the backplane is unchanged, and the
// translation lives in a board, which is where DESIGN.md 4 says it has to live.
// Everything that watches the backplane -- BREAK MEM, TRACE, HISTORY -- sees real
// PHYSICAL cycles, which is what a logic analyser on the bus would see.
//
// THE DAT, AS THE BOARD WIRES IT:
//   - Write-only, at logical FFF0-FFFF: a write to FFFn loads entry n. The chip's
//     outputs are inverting, so software stores the COMPLEMENT of the physical
//     segment (S-BUG writes $F0 for logical F to reach physical F). Only the low
//     four bits are stored; the high four would go to the extended-address chip
//     (IC8), which the standard board does not carry. The bus stays 16-bit.
//   - Reads of FFF0-FFFF are not the DAT; they are the vectors in the ROM.
//   - Logical FF00-FFFF BYPASSES the DAT. ⚠ This is an INFERENCE, not a reading of
//     the schematic: the DAT powers up holding junk, and S-BUG's reset vector and
//     START both sit at FF00 -- START is the code that loads the DAT, so its page
//     has to be reachable before it has run. If a boot misbehaves around Fxxx,
//     suspect this first (reference/MP-09 6809 CPU Board.md says why).
//
// THE ROM IS DECODED ON THE PHYSICAL ADDRESS: IC4's select comes off the bus A14/A15
// through IC7, after the DAT. So this card is ALSO a plain Board on the backplane,
// answering reads of physical F800-FFFF with S-BUG -- the same way a memory board
// answers its ROM region. IC1-IC3 (E000/E800/F000) are unpopulated sockets whose DIP
// switches are OFF in a standard system, and are not modeled.
//
// The core's IRQ input is the bus IRQ wire, mirrored onto the inner bus before each
// instruction (the 6809 samples it at the instruction boundary). FIRQ is left
// UNCONNECTED, as on the plain `6809` card: the schematic draws the line to the bus
// edge but no SS-50 pin carries it, and no board in the set drives it.

#include "core/board.h"
#include "core/bus.h"
#include "cpu/cpu.h"
#include "cpu/cpu6809.h"

#include <array>
#include <memory>
#include <string>

namespace swtpc {

class Mp09Board : public Board, public BusMaster, public CpuCard {
public:
    Mp09Board();
    ~Mp09Board() override;

    std::string type() const override { return "mp09"; }

    // ---- BusMaster: the core drives the INNER bus; the port carries it out ----
    StepResult step(Bus& bus) override;

    // ---- CpuCard ----
    CpuCore* activeCore() override { return core_.get(); }
    void      reportAchievedHz(long long hz) override { achievedHz_ = hz; }
    long long achievedHz() const override { return achievedHz_; }
    uint16_t  toBus(uint16_t logical) const override;

    // ---- Board: the backplane face is IC4, the S-BUG ROM at physical F800-FFFF ----
    bool decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override { return rom_[c.addr - kRomBase]; }
    bool peek(uint16_t addr, uint8_t& out) const override;
    std::vector<MapEntry> memMap() const override;

    void reset(Reset r) override { core_->reset(r); }  // RESET leaves the DAT alone
    void power() override;
    void clockAttached() override { publishPolicy(); }

    std::vector<Property> properties() override;
    std::vector<UnitDef> units() const override;

    // A ROM that would not reload at POWER says so here, rather than going quiet.
    std::vector<std::string> drainLog() override {
        std::vector<std::string> out;
        out.swap(log_);
        return out;
    }

    // SNAPSHOT/RESTORE (DESIGN.md 13): the core and the DAT. The ROM is an image,
    // reloaded from `rom`, like a memory board's.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    long long clockHz() const { return clockHz_; }

    // The DAT entry as the chip holds it (the complement, low four bits) -- for tests
    // and SHOW; the guest cannot read it back.
    uint8_t datEntry(int seg) const { return dat_[seg & 0xF]; }

    static constexpr uint16_t kRomBase = 0xF800;
    static constexpr size_t kRomSize = 0x800;

private:
    class DatPort;
    friend class DatPort;

    uint16_t translate(uint16_t logical) const;
    void datWrite(uint16_t logical, uint8_t data) { dat_[logical & 0xF] = data & 0x0F; }
    bool loadRom(const std::string& spec, std::string& err);
    void publishPolicy();

    std::unique_ptr<Cpu6809> core_;
    std::unique_ptr<DatPort> port_;
    std::unique_ptr<Bus> inner_;   // the core's side of the DAT; one board on it
    Bus* outer_ = nullptr;         // the backplane, handed in by step()

    std::array<uint8_t, 16> dat_{};
    std::array<uint8_t, kRomSize> rom_{};
    bool romLoaded_ = false;       // false: an empty socket, and IC4 decodes nothing
    std::string romSpec_ = "builtin:sbug";

    std::vector<std::string> log_;

    long long clockHz_ = 0;        // 0 = flat out, the default
    bool idle_ = true;             // stand down on an empty poll loop
    long long achievedHz_ = 0;
};

} // namespace swtpc
