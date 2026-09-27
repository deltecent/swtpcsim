#include "boards/swtpc-mps.h"
#include "boards/ss30.h"

#include "core/statefile.h"
#include "host/endpoint.h"

#include <utility>

namespace swtpc {

MpsBoard::MpsBoard()
    // The section is handed the card's intChanged() so it can drive the 6800's IRQ pin --
    // it is not a Board and cannot reach it. `this` is valid here: the base subobject is
    // built. One channel, `tty`, at offset 0; the card puts the section's base at 0 so
    // that address `base`+0 maps to section-port 0 and `base`+1 to port 1.
    : sio_({{"tty", 0}}, [this] { intChanged(); }) {
    sio_.setBase(0);
    // The section rebases a declarative `[mps0.unit.tty] connect = "in:tape.tap"`
    // (applied through the chip's `connect` property) against the machine file's dir;
    // the card's connect() rebases the CONNECT command separately.
    sio_.setRebase([this](const std::string& p) { return resolvePath(p); });
}

// ---------------------------------------------------------------------------
// Decode. The 6850 fills the WHOLE four-address slot: only A0 reaches its register
// select (RS), so base+0/base+2 are control/status and base+1/base+3 are Rx/Tx data --
// the upper pair MIRRORS the lower. This is not a nicety: SWTBUG's power-up probes the
// console board by writing a PIA init pattern and then requiring [base] == [base+2] to
// decide the board is an ACIA and master-reset it (SWTBUG.ASM: PIAINI, then `LDAA 0,X /
// CMPA 2,X / BNE CONTRL`). If base+2 did not mirror base, the compare fails, the ACIA is
// never reset, and the console emits garbage. The register within the slot is A0.
// ---------------------------------------------------------------------------
bool MpsBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;

    if (c.addr >= at_ && c.addr <= (uint16_t)(at_ + 3))
        return sio_.decodesPort((uint8_t)((c.addr - at_) & 1));

    return false;
}

uint8_t MpsBoard::read(const BusCycle& c) {
    if (c.type == Cycle::MemRead && c.addr >= at_ && c.addr <= (uint16_t)(at_ + 3))
        return sio_.read((uint8_t)((c.addr - at_) & 1));
    return 0xFF;
}

void MpsBoard::write(const BusCycle& c) {
    if (c.type == Cycle::MemWrite && c.addr >= at_ && c.addr <= (uint16_t)(at_ + 3))
        sio_.write((uint8_t)((c.addr - at_) & 1), c.data);
}

// ---------------------------------------------------------------------------
// Lifecycle.
// ---------------------------------------------------------------------------
void MpsBoard::reset(Reset r) { sio_.reset(r); }

void MpsBoard::power() { reset(Reset::PowerOn); }

void MpsBoard::configChanged() {
    // `base` can move our two addresses, so re-derive the wiring; an interrupt strap may
    // have moved; and a SIO strap (baud/interrupt/connect) may have moved a deadline.
    decodeChanged();
    intChanged();
    sio_.refresh();
}

bool MpsBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    // The Sio2Port has no config dir; the board is the only thing that knows one, so a
    // machine-file in:/out: PATH is rebased here before the resolver opens it.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    if (!sio_.connect(unit, spec, err)) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> MpsBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "SS-30 slot base (window + slot*4: $8000 on a 6800 motherboard, $E000 on "
                  "a 6809 one); control/status at base, Rx/Tx at base+1";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0x8000;
        x.max   = 0xE01C;
        x.values = "8000-801C | E000-E01C, a multiple of 4";
        x.get   = [this] { return Value::ofInt(at_); };
        x.set   = [this](const Value& v, std::string& err) {
            long long b = v.i();
            if (!ss30Slot(b)) {
                err = "the MP-S base is an SS-30 slot: a multiple of 4 in the $8000-$801C "
                      "or the $E000-$E01C I/O window";
                return false;
            }
            at_ = (uint16_t)b;
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> MpsBoard::memMap() const {
    return {
        {(uint32_t)at_, (uint32_t)(at_ + 3), "read/write",
         "6850 ACIA 'tty' -- A0 selects: base/base+2 status/control, base+1/base+3 Rx/Tx data"},
    };
}

std::vector<std::string> MpsBoard::drainLog() {
    std::vector<std::string> out;
    for (auto& s : sio_.drainLog()) out.push_back(id + ":" + s);
    for (auto& s : log_) out.push_back(std::move(s));
    log_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The base is config (re-applied from the machine file), but it is
// two bytes and travels harmlessly; the 6850's live state travels via the section
// (DESIGN.md 13).
// ---------------------------------------------------------------------------
void MpsBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u16(at_);
    sio_.serialize(w);
}

void MpsBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    at_ = r.u16();
    sio_.deserialize(r);
}

} // namespace swtpc
