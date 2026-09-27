#include "boards/swtpc-mpid.h"

#include "core/statefile.h"
#include "host/endpoint.h"

#include <cctype>
#include <cstdio>
#include <utility>

namespace swtpc {
namespace {

MpidBoard::EndpointResolver g_resolver;

constexpr int kA = Pia6820::kSectionA;
constexpr int kB = Pia6820::kSectionB;

constexpr unsigned kPtmOffset = 0x10;  // the 6840 is the next port up from the PIA
constexpr uint8_t  kCa1Bit    = 0x80;  // PA7, IC7's top bit, also drives CA1

bool isLpt(const std::string& unit) {
    std::string lo;
    for (char c : unit) lo += (char)std::tolower((unsigned char)c);
    return lo == "lpt";
}

} // namespace

void MpidBoard::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

MpidBoard::MpidBoard() : lpt_(std::make_unique<NullStream>()) {
    ptm_.onOutput = [this](int timer, bool level) { output(timer, level); };
}

MpidBoard::~MpidBoard() {
    // The queue holds lambdas with `this` in them, and a board can be pulled out of a
    // running machine.
    if (clock_) {
        clock_->cancel(lineWake_);
        clock_->cancel(ptmWake_);
    }
}

// Before a clock is attached (a board being configured), time stands at zero.
const Clock& MpidBoard::clk() const {
    static const Clock idle;
    return clock_ ? *clock_ : idle;
}

// ---------------------------------------------------------------------------
// Decode: the PIA in the first 16 addresses, the 6840 in the next 16.
// ---------------------------------------------------------------------------
bool MpidBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return c.addr >= at_ && c.addr <= (uint16_t)(at_ + 0x1F);
}

// Every access first brings the 6840 up to now, so an O1 edge due before it has already
// reached IC7 and PA. Afterwards the 6840's deadline is re-armed: a status or counter read
// may have cleared a flag, and a write may have changed a mode or a latch.
uint8_t MpidBoard::read(const BusCycle& c) {
    unsigned off = (unsigned)(c.addr - at_);
    ptm_.poll(clk());
    uint8_t v;
    if (off >= kPtmOffset) {
        v = ptm_.read((int)(off & 7), clk());
    } else {
        int sec = (int)((off >> 1) & 1);
        v       = pia_.read(sec, (off & 1) ? 0 : 1);
    }
    rearmPtm();
    intChanged();
    return v;
}

void MpidBoard::write(const BusCycle& c) {
    unsigned off = (unsigned)(c.addr - at_);
    ptm_.poll(clk());
    if (off >= kPtmOffset) {
        ptm_.write((int)(off & 7), c.data, clk());
    } else {
        int sec = (int)((off >> 1) & 1);
        pia_.write(sec, (off & 1) ? 0 : 1, c.data);
        uint8_t out;
        if (pia_.takeOutput(sec, out) && sec == kB) {
            // The printer takes the byte, and its ACK comes back on CB1.
            lpt_->writeByte(out);
            pia_.strobeC1(kB);
        }
    }
    rearmPtm();
    intChanged();
}

bool MpidBoard::assertsInt() const { return ptm_.irq() || pia_.irq(kA) || pia_.irq(kB); }

// ---------------------------------------------------------------------------
// The wires between the chips.
// ---------------------------------------------------------------------------
uint8_t MpidBoard::portA() const {
    return (uint8_t)((ptm_.output(0) ? 0x01 : 0x00) | (uint8_t)(ic7_ << 1));
}

// O1 clocks IC7 on its falling edge, and PA follows O1 and the count. O3 is wired to C2.
void MpidBoard::output(int timer, bool level) {
    if (timer == 0) {
        uint8_t before = portA();
        if (!level) ++ic7_;
        uint8_t after = portA();
        pia_.setInput(kA, after);
        bool was = (before & kCa1Bit) != 0, now = (after & kCa1Bit) != 0;
        if (was != now && now == pia_.c1RisingEdge(kA)) pia_.strobeC1(kA);
    } else if (timer == 2 && !level) {
        ptm_.clockEdge(1, clk());
    }
}

// ---------------------------------------------------------------------------
// The line clock: 2 x line_hz pulses a second, one deadline each.
// ---------------------------------------------------------------------------
void MpidBoard::stepPulse() {
    uint64_t d   = 2 * (uint64_t)lineHz_;
    uint64_t num = lastFrac_ + (uint64_t)clk().hz();
    pulseAt_     = lastPulse_ + num / d;
    pulseFrac_   = num % d;
}

void MpidBoard::armLine() {
    if (!clock_) return;
    clock_->cancel(lineWake_);
    stepPulse();
    // A board added to a running machine has no pulses behind it to catch up on.
    while (pulseAt_ <= clock_->now()) {
        lastPulse_ = pulseAt_;
        lastFrac_  = pulseFrac_;
        stepPulse();
    }
    lineWake_ = clock_->at(pulseAt_, [this] { pulse(); });
}

// C1 always takes the pulse; C3 takes it through the INT jumper (the manual's setting).
void MpidBoard::pulse() {
    lineWake_ = Clock::kNone;
    ptm_.clockEdge(0, clk());
    ptm_.clockEdge(2, clk());
    lastPulse_ = pulseAt_;
    lastFrac_  = pulseFrac_;
    armLine();
    intChanged();
}

void MpidBoard::rearmPtm() {
    if (!clock_) return;
    ptm_.poll(*clock_);
    uint64_t next = ptm_.nextEvent(*clock_);
    // A guest polling the status register would otherwise re-queue the same deadline on
    // every read.
    if (next != 0 && clock_->pending(ptmWake_) && next == ptmAt_) return;
    clock_->cancel(ptmWake_);
    ptmWake_ = Clock::kNone;
    ptmAt_   = next;
    if (next)
        ptmWake_ = clock_->at(next, [this] {
            ptmWake_ = Clock::kNone;
            rearmPtm();
            intChanged();
        });
}

// ---------------------------------------------------------------------------
// Lifecycle. The bus RESET resets the PIA and the 6840 and clears IC7. The line keeps
// its phase: the mains does not stop for a reset.
// ---------------------------------------------------------------------------
void MpidBoard::reset(Reset) {
    pia_.reset();
    ptm_.reset(clk());
    ic7_ = 0;  // after the 6840's reset, whose O1 may have just fallen
    pia_.setInput(kA, portA());
    lpt_->flush();
    armLine();
    rearmPtm();
    intChanged();
}

// Power restarts time, so the line clock restarts with it.
void MpidBoard::power() {
    lastPulse_ = 0;
    lastFrac_  = 0;
    reset(Reset::PowerOn);
}

void MpidBoard::pump() {
    lpt_->pump();
    lpt_->flush();
}

void MpidBoard::clockAttached() {
    armLine();
    rearmPtm();
}

void MpidBoard::configChanged() {
    decodeChanged();
    intChanged();
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> MpidBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name   = "base";
        x.help   = "Port 8 of the I/O block: the PIA at base..base+F, the 6840 at "
                   "base+10..base+1F. The block (C000 or E000) and the 1K segment are jumpers";
        x.kind   = Kind::Int;
        x.radix  = 16;
        x.min    = 0xC080;
        x.max    = 0xFC80;
        x.values = "C080, C480, ... DC80 | E080, E480, ... FC80";
        x.get    = [this] { return Value::ofInt(at_); };
        x.set    = [this](const Value& v, std::string& err) {
            long long b = v.i();
            if ((b & 0x3FF) != 0x080 || (b >> 13) < 6 || (b >> 13) > 7) {
                err = "the MP-ID base is port 8 of an I/O block: $80 into a 1K segment of "
                      "$C000-$DFFF or $E000-$FFFF (E080 is the standard setting)";
                return false;
            }
            at_ = (uint16_t)b;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name   = "line_hz";
        x.help   = "The power-line frequency. The line pulse clocks the 6840 at twice it: "
                   "120 pulses a second on 60 Hz, 100 on 50 Hz";
        x.kind   = Kind::Int;
        x.min    = 50;
        x.max    = 60;
        x.values = "50 | 60";
        x.get    = [this] { return Value::ofInt(lineHz_); };
        x.set    = [this](const Value& v, std::string& err) {
            long long hz = v.i();
            if (hz != 50 && hz != 60) {
                err = "line_hz is 50 or 60";
                return false;
            }
            lineHz_ = (int)hz;
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<Property> MpidBoard::unitProperties(const std::string& unit) {
    if (!isLpt(unit)) return {};
    std::vector<Property> p;
    Property              x;
    x.name = "connect";
    x.help = "The endpoint the printer port writes to (CONNECT sets this)";
    x.kind = Kind::Str;
    x.get  = [this] { return Value::ofStr(lptSpec_); };
    x.set  = [this](const Value& v, std::string& err) { return connect("lpt", v.s(), err); };
    p.push_back(std::move(x));
    return p;
}

std::vector<UnitDef> MpidBoard::units() const { return {{"lpt", UnitKind::Serial, lptSpec_}}; }

std::vector<MapEntry> MpidBoard::memMap() const {
    return {
        {(uint32_t)at_, (uint32_t)(at_ + 0x0F), "read/write",
         "6820 PIA (x4): side A reads the line-clock count (IC7), side B is the printer port"},
        {(uint32_t)(at_ + kPtmOffset), (uint32_t)(at_ + 0x1F), "read/write",
         "MC6840 timer (x2): C1 and C3 on the line pulse, O3 into C2"},
    };
}

std::vector<std::string> MpidBoard::statusLines() const {
    static const char* const kClock[3] = {"line pulse", "O3", "line pulse"};
    std::vector<std::string> out;
    char                     buf[128];
    std::snprintf(buf, sizeof buf, "line: %d Hz, %d pulses a second into C1 and C3", lineHz_,
                  2 * lineHz_);
    out.push_back(buf);
    for (int i = 0; i < Ptm6840::kTimers; ++i) {
        uint8_t cr = ptm_.control(i);
        std::snprintf(buf, sizeof buf,
                      "timer %d: CR%d=%02X latch %04X counter %04X, %s clock, flag %s%s", i + 1,
                      i + 1, cr, ptm_.latch(i), ptm_.counter(i),
                      (cr & 0x02) ? "E" : kClock[i], ptm_.flag(i) ? "set" : "clear",
                      (ptm_.control(0) & 0x01) ? " (held by CR10)" : "");
        out.push_back(buf);
    }
    std::snprintf(buf, sizeof buf, "IC7 count %u (PA reads $%02X)", (unsigned)ic7_, portA());
    out.push_back(buf);
    out.push_back("printer (lpt): " + lptSpec_);
    return out;
}

std::vector<std::string> MpidBoard::drainLog() {
    std::vector<std::string> out;
    for (std::string& s : ptm_.drainLog()) out.push_back(id + ":" + s);
    return out;
}

// ---------------------------------------------------------------------------
// The unit `lpt`. An in:/out: PATH is rebased against the machine file's directory; the
// spec as given is kept, so CONFIG SAVE + reload does not rebase it twice.
// ---------------------------------------------------------------------------
bool MpidBoard::connect(const std::string& unit, const std::string& endpoint, std::string& err) {
    if (!isLpt(unit)) {
        err = "mpid has one unit, 'lpt' (the printer port)";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(endpoint, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    lpt_     = std::move(s);
    lptSpec_ = endpoint;
    return true;
}

bool MpidBoard::disconnect(const std::string& unit, std::string& err) {
    if (!isLpt(unit)) {
        err = "mpid has one unit, 'lpt' (the printer port)";
        return false;
    }
    lpt_     = std::make_unique<NullStream>();
    lptSpec_ = "null";
    return true;
}

bool MpidBoard::connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                              std::string& err) {
    if (!isLpt(unit)) {
        err = "mpid has one unit, 'lpt' (the printer port)";
        return false;
    }
    if (!s) s = std::make_unique<NullStream>();
    lptSpec_ = s->describe();
    lpt_     = std::move(s);
    return true;
}

ByteStream* MpidBoard::unitStream(const std::string& unit) {
    return isLpt(unit) ? lpt_.get() : nullptr;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The pending deadlines are not written -- they are derived, and
// re-armed from the restored state into the restored clock.
// ---------------------------------------------------------------------------
void MpidBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u16(at_);
    w.u8((uint8_t)lineHz_);
    pia_.serialize(w);
    ptm_.serialize(w);
    w.u8(ic7_);
    w.u64(lastPulse_);
    w.u64(lastFrac_);
}

void MpidBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    at_     = r.u16();
    lineHz_ = r.u8();
    pia_.deserialize(r);
    ptm_.deserialize(r);
    ic7_       = r.u8();
    lastPulse_ = r.u64();
    lastFrac_  = r.u64();
    lineWake_  = Clock::kNone;  // handles from the life RESTORE undid; the clock dropped them
    ptmWake_   = Clock::kNone;
    armLine();
    rearmPtm();
    intChanged();
}

} // namespace swtpc
