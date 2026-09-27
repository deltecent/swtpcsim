#pragma once
//
// A plain 6809 CPU board: a Motorola MC6809 and its crystal, and nothing else.
//
// The 6800 card (mits-680cpu.h) with the core swapped. It is a Board AND a
// BusMaster, it decodes NOTHING, the crystal and the idle policy live on the card,
// and the core is a UNIT.
//
// It is NOT the SWTPC MP-09. That card also carries the DAT address-translation
// RAM and the S-BUG ROM socket, and it is modeled from its own manual when the
// swtpc09 machine is built. This card is what `BOARDS ADD 6809` gives you: a
// processor on the bus, for a bench or a machine file that supplies the rest.
//
// The core's IRQ input is the bus IRQ wire. Its FIRQ input is left UNCONNECTED:
// the SS-50 bus manual in reference/ names only IRQ and NMI, and a board that
// invented a wire for FIRQ would give the bus a line it may never have had. The
// MP-09 manual decides that.

#include "core/board.h"
#include "cpu/cpu.h"
#include "cpu/cpu6809.h"

#include <memory>

namespace swtpc {

class Cpu6809Board : public Board, public BusMaster, public CpuCard {
public:
    Cpu6809Board() : core_(std::make_unique<Cpu6809>()) {}

    std::string type() const override { return "6809"; }

    // ---- BusMaster: this card drives the bus ----
    StepResult step(Bus& bus) override { return core_->step(bus); }

    // ---- CpuCard ----
    CpuCore* activeCore() override { return core_.get(); }
    void      reportAchievedHz(long long hz) override { achievedHz_ = hz; }
    long long achievedHz() const override { return achievedHz_; }

    // ---- Board ---- (decodes() stays false, which is the truth for this card)
    void reset(Reset r) override { core_->reset(r); }
    void power() override {
        publishPolicy();
        core_->reset(Reset::PowerOn);
    }

    // A NEW CLOCK HAS NEVER HEARD OF THIS CARD'S CRYSTAL, so tell it (board.h, #34).
    void clockAttached() override { publishPolicy(); }

    std::vector<Property> properties() override;

    long long clockHz() const { return clockHz_; }

    std::vector<UnitDef> units() const override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The card's state is the core's; the crystal
    // and idle straps are config, re-published on attach.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    void publishPolicy();

    std::unique_ptr<Cpu6809> core_;

    // The same defaults as the 6800 card, for the same reasons (mits-680cpu.h):
    // flat out unless you set a crystal, and idle at a prompt.
    long long clockHz_ = 0;   // 0 = flat out, the default
    bool idle_ = true;        // stand down on an empty poll loop
    long long achievedHz_ = 0;
};

} // namespace swtpc
