#include "boards/swtpc-mp09.h"

#include "core/hex.h"
#include "core/roms.h"
#include "core/statefile.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>

namespace swtpc {

// ---------------------------------------------------------------------------
// THE DAT PORT -- the only board on the core's inner bus.
//
// It answers every cycle the core makes, and turns each into a backplane cycle at
// the translated address. It is the card's address path drawn as a board: the DAT
// chip, the '157 that feeds it, and the bus drivers -- and nothing else. It holds no
// memory of its own; a byte the core reads came off the backplane.
// ---------------------------------------------------------------------------
class Mp09Board::DatPort : public Board {
public:
    explicit DatPort(Mp09Board& card) : card_(card) {}

    std::string type() const override { return "mp09-dat"; }
    std::vector<Property> properties() override { return {}; }

    bool decodes(const BusCycle&) const override { return true; }

    uint8_t read(const BusCycle& c) override {
        return card_.outer_->memRead(card_.translate(c.addr));
    }

    // A write to FFF0-FFFF loads the DAT. The cycle still goes out on the bus -- the
    // board's drivers are enabled for it like any other -- and it goes FIRST, so a
    // cycle breakpoint that stops before the access leaves the DAT as it was.
    void write(const BusCycle& c) override {
        card_.outer_->memWrite(card_.translate(c.addr), c.data);
        if (c.addr >= 0xFFF0) card_.datWrite(c.addr, c.data);
    }

    // The backplane's IRQ wire, as the core's pin sees it. Set before each instruction.
    void setIrq(bool on) {
        if (irq_ == on) return;
        irq_ = on;
        intChanged();
    }
    bool assertsInt() const override { return irq_; }

private:
    Mp09Board& card_;
    bool irq_ = false;
};

// ---------------------------------------------------------------------------

Mp09Board::Mp09Board()
    : core_(std::make_unique<Cpu6809>()),
      port_(std::make_unique<DatPort>(*this)),
      inner_(std::make_unique<Bus>()) {
    port_->id = "dat";
    inner_->attach(port_.get());
    std::string err;
    loadRom(romSpec_, err);  // the built-in is compiled in; it cannot fail to open
}

Mp09Board::~Mp09Board() {
    inner_->detach(port_.get());
}

StepResult Mp09Board::step(Bus& bus) {
    outer_ = &bus;
    inner_->setVerify(bus.verify());
    port_->setIrq(bus.intPending());
    return core_->step(*inner_);
}

// Logical FF00-FFFF passes straight through (the inferred bypass -- see the header);
// everything else takes its physical A12-A15 from the DAT, whose outputs invert what
// was stored.
uint16_t Mp09Board::translate(uint16_t logical) const {
    if ((logical & 0xFF00) == 0xFF00) return logical;
    uint16_t seg = (uint16_t)(~dat_[logical >> 12] & 0x0F);
    return (uint16_t)((seg << 12) | (logical & 0x0FFF));
}

uint16_t Mp09Board::toBus(uint16_t logical) const { return translate(logical); }

// IC4 on the backplane: reads of physical F800-FFFF, when there is a chip in it. A
// ROM does not answer a write.
bool Mp09Board::decodes(const BusCycle& c) const {
    return romLoaded_ && !c.isWrite() && c.addr >= kRomBase;
}

bool Mp09Board::peek(uint16_t addr, uint8_t& out) const {
    if (!romLoaded_ || addr < kRomBase) return false;
    out = rom_[addr - kRomBase];
    return true;
}

std::vector<MapEntry> Mp09Board::memMap() const {
    if (!romLoaded_) return {};
    MapEntry e;
    e.lo = kRomBase;
    e.hi = 0xFFFF;
    e.what = "rom";
    e.note = romSpec_;
    return {e};
}

// Power APPLIED: the ROM is re-read, and the DAT -- plain static RAM -- comes up
// holding whatever it likes. A fixed seed, so a power-on is repeatable; nothing that
// runs correctly may depend on these bits, which is the point of them not being zero.
void Mp09Board::power() {
    if (!romSpec_.empty()) {
        std::string err;
        if (!loadRom(romSpec_, err)) log_.push_back("power: " + err);
    }
    std::mt19937 rng(0x6809u);
    for (auto& e : dat_) e = (uint8_t)(rng() & 0x0F);
    publishPolicy();
    core_->reset(Reset::PowerOn);
}

// The socket's contents. A `builtin:` name or a file (relative to the machine file),
// in S-record, Intel HEX or flat binary -- the same loaders as a memory board's ROM.
// Whatever it holds has to lie within F800-FFFF: IC4 is a 2K part.
bool Mp09Board::loadRom(const std::string& spec, std::string& err) {
    if (spec.empty()) {
        romLoaded_ = false;
        rom_.fill(0xFF);
        decodeChanged();
        return true;
    }

    Image img;
    if (spec.rfind("builtin:", 0) == 0) {
        const BuiltinRom* r = findRom(spec.substr(8));
        if (!r) {
            err = "no built-in ROM named '" + spec.substr(8) + "'. SHOW ROMS lists them.";
            return false;
        }
        if (!decodeRom(*r, kRomBase, img, err)) return false;
    } else {
        std::string file = resolvePath(spec);
        std::ifstream f(file, std::ios::binary);
        if (!f) {
            err = "cannot open '" + file + "'" + pathNote(spec);
            return false;
        }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        if (looksLikeHex(raw)) {
            if (!loadHex(raw, img, err)) { err = spec + ": " + err; return false; }
        } else if (looksLikeSrec(raw)) {
            if (!loadSrec(raw, img, err)) { err = spec + ": " + err; return false; }
        } else {
            loadBin(raw, kRomBase, img);
        }
    }

    if (img.empty()) {
        err = spec + ": no bytes";
        return false;
    }
    if (img.lo() < kRomBase || img.hi() > 0xFFFF) {
        char b[128];
        std::snprintf(b, sizeof b, ": the image spans %04X-%04X; IC4 is F800-FFFF",
                      (unsigned)img.lo(), (unsigned)img.hi());
        err = spec + b;
        return false;
    }

    rom_.fill(0xFF);  // an unprogrammed EPROM cell
    for (const auto& [a, v] : img.bytes) rom_[a - kRomBase] = v;
    romLoaded_ = true;
    decodeChanged();
    return true;
}

void Mp09Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    core_->serialize(w);
    for (uint8_t e : dat_) w.u8(e);
}

void Mp09Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    core_->deserialize(r);
    for (auto& e : dat_) e = r.u8() & 0x0F;
}

std::vector<Property> Mp09Board::properties() {
    std::vector<Property> p;

    {
        Property x;
        x.name = "clock_hz";
        x.help = "The CPU clock: a quarter of the crystal -- 1000000 for the 4 MHz crystal, "
                 "2000000 for 8 MHz. 0 runs flat out -- as fast as the host can.";
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
        x.name = "rom";
        x.help = "The monitor in IC4, at physical F800-FFFF: builtin:<name> or a file "
                 "(relative to THIS FILE). Empty leaves the socket empty.";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(romSpec_); };
        x.set  = [this](const Value& v, std::string& err) {
            if (!loadRom(v.s(), err)) return false;
            romSpec_ = v.s();
            return true;
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name = "dat";
        x.help = "LIVE: the physical 4K segment each logical segment 0-F maps to, as the "
                 "DAT holds it. Read-only -- the guest loads it by writing FFF0-FFFF.";
        x.kind = Kind::Str;
        x.get  = [this] {
            std::string s;
            for (int i = 0; i < 16; ++i) s += "0123456789ABCDEF"[~dat_[i] & 0x0F];
            return Value::ofStr(s);
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name  = "achieved_hz";
        x.help  = "LIVE: cycles per real second the run loop last reached -- the clock "
                  "you got, beside the one you asked for. Read-only; 0 until it has run.";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.unit  = "Hz";
        x.get   = [this] { return Value::ofInt(achievedHz_); };
        p.push_back(std::move(x));
    }

    return p;
}

void Mp09Board::publishPolicy() {
    if (!clock_) return;
    clock_->setHz(clockHz_);
    clock_->setIdle(idle_);
}

std::vector<UnitDef> Mp09Board::units() const {
    return {{"6809", UnitKind::Cpu, "active"}};
}

} // namespace swtpc
