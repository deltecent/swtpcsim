#include "boards/cpu6809card.h"

#include "core/statefile.h"

namespace swtpc {

void Cpu6809Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    core_->serialize(w);
}

void Cpu6809Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    core_->deserialize(r);
}

// A near-verbatim copy of Cpu6800Board::properties() (mits-680cpu.cpp) -- the crystal,
// the idle nap, and the read-only achieved-crystal companion, identical in every
// respect but the core they pace. There is no shared base because two dozen-line
// cards do not need one.
std::vector<Property> Cpu6809Board::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name = "clock_hz";
        x.help = "Crystal on the board. 0 runs flat out -- as fast as the host can.";
        x.kind = Kind::Int;
        x.radix = 10;
        x.unit = "Hz";
        x.min = 0;
        x.max = 100000000;
        x.get = [this] { return Value::ofInt(clockHz_); };
        x.set = [this](const Value& v, std::string&) {
            clockHz_ = v.i();
            publishPolicy();
            return true;
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name = "idle";
        x.help = "Stand down when the guest is only polling an empty keyboard. On by "
                 "default -- the guest cannot tell, and a prompt stops burning a core.";
        x.kind = Kind::Bool;
        x.get  = [this] { return Value::ofBool(idle_); };
        x.set  = [this](const Value& v, std::string&) {
            idle_ = v.b();
            publishPolicy();
            return true;
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name  = "achieved_hz";
        x.help  = "LIVE: cycles per real second the run loop last reached -- the crystal "
                  "you got, beside the one you asked for. Read-only; 0 until it has run.";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "Hz";
        x.get   = [this] { return Value::ofInt(achievedHz_); };
        p.push_back(std::move(x));
    }

    return p;
}

void Cpu6809Board::publishPolicy() {
    if (!clock_) return;
    clock_->setHz(clockHz_);
    clock_->setIdle(idle_);
}

std::vector<UnitDef> Cpu6809Board::units() const {
    return {{"6809", UnitKind::Cpu, "active"}};
}

} // namespace swtpc
