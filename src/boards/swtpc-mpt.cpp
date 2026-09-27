#include "boards/swtpc-mpt.h"
#include "boards/ss30.h"

#include "core/statefile.h"

#include <cstdio>
#include <utility>

namespace swtpc {
namespace {

constexpr int kA = Pia6820::kSectionA;
constexpr int kB = Pia6820::kSectionB;

constexpr uint8_t  kResetLine = 0x80;     // PB7 -> MK5009 RESET 0
constexpr uint8_t  kSelect    = 0x0F;     // PB0-PB3 -> MK5009 2^0..2^3
constexpr uint64_t kHalfUsPerSec = 2000000;  // the edge arithmetic counts half-microseconds

// a * b / c without the a * b overflowing: a cycle count times a clock rate is past 2^64
// long before the machine has run a day.
uint64_t mulDiv(uint64_t a, uint64_t b, uint64_t c) { return (a / c) * b + (a % c) * b / c; }

const char* rateName(unsigned code) {
    static const char* const kNames[16] = {"1 us",   "10 us", "100 us", "1 ms",
                                           "10 ms",  "100 ms", "1 s",   "10 s",
                                           "100 s",  "1 min", "1 hour", "10 min",
                                           nullptr,  nullptr, "20 ms",  nullptr};
    return kNames[code & 0x0F];
}

} // namespace

// The MK5009's taps, off a 1 MHz crystal: 10^0 through 10^8 plus 2x10^4, 6x10^7, 6x10^8 and
// 36x10^8 (the data sheet's OUTPUT GATING). The select code is not in tap order -- A is the
// hour and B ten minutes -- and three codes select the external input, which the MP-T
// grounds (the manual's "no output").
uint64_t MptBoard::periodUs(unsigned code) {
    static const uint64_t kPeriod[16] = {
        1ULL,          10ULL,          100ULL,        1000ULL,       // 0-3
        10000ULL,      100000ULL,      1000000ULL,    10000000ULL,   // 4-7
        100000000ULL,  60000000ULL,    3600000000ULL, 600000000ULL,  // 8-B
        0,             0,              20000ULL,      0,             // C-F
    };
    return kPeriod[code & 0x0F];
}

MptBoard::~MptBoard() {
    // The queue holds a lambda with `this` in it, and a board can be pulled out of a
    // running machine.
    if (clock_) clock_->cancel(wake_);
}

// ---------------------------------------------------------------------------
// Decode. The PIA fills the slot: RS0 = A0, RS1 = A1.
// ---------------------------------------------------------------------------
bool MptBoard::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    return c.addr >= at_ && c.addr <= (uint16_t)(at_ + 3);
}

// base+0/+2 are the data (or DDR) addresses of sides A/B, base+1/+3 their control. The
// chip's own numbering is (section, 0 = control / 1 = data).
uint8_t MptBoard::read(const BusCycle& c) {
    unsigned off = (unsigned)(c.addr - at_) & 3;
    int      sec = (int)(off >> 1);
    uint8_t  v   = pia_.read(sec, (off & 1) ? 0 : 1);
    if (sec == kB) rearm();   // a PRB read clears the CB1 flag: the next edge counts again
    intChanged();
    return v;
}

void MptBoard::write(const BusCycle& c) {
    unsigned off = (unsigned)(c.addr - at_) & 3;
    int      sec = (int)(off >> 1);
    pia_.write(sec, (off & 1) ? 0 : 1, c.data);
    if (sec == kB) {
        uint8_t discard;
        pia_.takeOutput(kB, discard);  // side B's output goes to the MK5009, read by bLines()
        linesChanged();
        rearm();
    }
    intChanged();
}

bool MptBoard::assertsInt() const { return pia_.irq(kA) || pia_.irq(kB); }

// ---------------------------------------------------------------------------
// The MK5009.
// ---------------------------------------------------------------------------
uint8_t MptBoard::bLines() const {
    uint8_t ddr = pia_.direction(kB);
    return (uint8_t)((pia_.outputRegister(kB) & ddr) | (uint8_t)~ddr);
}

void MptBoard::linesChanged() {
    bool held = (bLines() & kResetLine) != 0;
    if (held_ && !held) t0_ = clock_ ? clock_->now() : 0;  // RESET 0 fell: the chain starts
    held_ = held;
}

// The output is low from reset, rises at P/2 and falls at P. Counting in half-periods, edge
// m (m >= 1) is at t0 + m*P/2: odd m rise, even m fall. Everything is done in
// half-microseconds so that P/2 is exact even at 1 us.
uint64_t MptBoard::nextEdge() const {
    if (!clock_ || held_) return 0;
    uint64_t p = periodUs(bLines() & kSelect);
    if (p == 0) return 0;
    if (pia_.inputFull(kB)) return 0;  // the flag is already up; another edge changes nothing

    uint64_t hz  = (uint64_t)clock_->hz();
    uint64_t now = clock_->now();
    uint64_t par = pia_.c1RisingEdge(kB) ? 1 : 0;

    auto at = [&](uint64_t m) { return t0_ + mulDiv(m * p, hz, kHalfUsPerSec); };

    uint64_t m = mulDiv(now - t0_, kHalfUsPerSec, hz) / p;
    if (m % 2 != par) ++m;
    if (m == 0) m = 2;
    while (at(m) <= now) m += 2;
    return at(m);
}

void MptBoard::rearm() {
    if (!clock_) return;
    uint64_t next = nextEdge();
    // A guest polling CRB would otherwise cancel and re-queue the same deadline on every
    // read. Leave it alone when nothing moved.
    if (next != 0 && clock_->pending(wake_) && next == wakeAt_) return;
    clock_->cancel(wake_);
    wake_   = Clock::kNone;
    wakeAt_ = next;
    if (next) wake_ = clock_->at(next, [this] { edge(); });
}

void MptBoard::edge() {
    wake_ = Clock::kNone;
    pia_.strobeC1(kB);
    intChanged();
}

// ---------------------------------------------------------------------------
// Lifecycle. RESET clears the PIA (the manual: a 6820 is reset by the bus RESET, so it
// never interrupts from the monitor), which lets PB float high: the chain is held.
// ---------------------------------------------------------------------------
void MptBoard::reset(Reset) {
    pia_.reset();
    held_ = true;
    rearm();
    intChanged();
}

void MptBoard::configChanged() {
    decodeChanged();
    intChanged();
}

// ---------------------------------------------------------------------------
// Reflection.
// ---------------------------------------------------------------------------
std::vector<Property> MptBoard::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name   = "base";
        x.help   = "SS-30 slot base (window + slot*4: $8000 on a 6800 motherboard, $E000 on "
                   "a 6809 one); PIA side A at base/base+1, side B (the timer) at base+2/base+3";
        x.kind   = Kind::Int;
        x.radix  = 16;
        x.min    = 0x8000;
        x.max    = 0xE01C;
        x.values = "8000-801C | E000-E01C, a multiple of 4";
        x.get    = [this] { return Value::ofInt(at_); };
        x.set    = [this](const Value& v, std::string& err) {
            long long b = v.i();
            if (!ss30Slot(b)) {
                err = "the MP-T base is an SS-30 slot: a multiple of 4 in the $8000-$801C "
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

std::vector<MapEntry> MptBoard::memMap() const {
    return {
        {(uint32_t)at_, (uint32_t)(at_ + 1), "read/write",
         "6820 PIA side A -- the input port: data/DDR, control"},
        {(uint32_t)(at_ + 2), (uint32_t)(at_ + 3), "read/write",
         "6820 PIA side B -- the MK5009 timer: rate select + reset (PB0-3, PB7), control (CB1)"},
    };
}

std::vector<std::string> MptBoard::statusLines() const {
    unsigned    code = bLines() & kSelect;
    const char* name = rateName(code);
    char        buf[96];
    if (held_)
        std::snprintf(buf, sizeof buf, "timer: held in reset (PB7 high)");
    else if (!name)
        std::snprintf(buf, sizeof buf, "timer: running, code %X -- no output", code);
    else
        std::snprintf(buf, sizeof buf, "timer: running, code %X -- every %s", code, name);
    std::vector<std::string> out{buf};
    out.push_back(std::string("CB1 flag: ") + (pia_.inputFull(kB) ? "set" : "clear") +
                  (pia_.irq(kB) ? " (pulling IRQ)" : ""));
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE. The base is config but two bytes; the PIA carries the lines the
// guest drove, t0_ and held_ the chain. The pending edge is not written -- it is derived,
// and re-armed from the restored state into the restored clock.
// ---------------------------------------------------------------------------
void MptBoard::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u16(at_);
    pia_.serialize(w);
    w.u64(t0_);
    w.boolean(held_);
}

void MptBoard::deserialize(StateReader& r) {
    Board::deserialize(r);
    at_ = r.u16();
    pia_.deserialize(r);
    t0_   = r.u64();
    held_ = r.boolean();
    wake_ = Clock::kNone;  // a handle from the life RESTORE undid; the clock has dropped it
    rearm();
    intChanged();
}

} // namespace swtpc
