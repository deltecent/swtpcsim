#include "chips/mc6840.h"

#include "core/clock.h"
#include "core/statefile.h"

#include <algorithm>
#include <limits>

namespace swtpc {
namespace {

constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();

constexpr uint8_t kHold    = 0x01;  // CR10: every timer held preset
constexpr uint8_t kCr1Sel  = 0x01;  // CR20: address 0 writes CR1, not CR3
constexpr uint8_t kNoWInit = 0x10;  // CRx4: a latch write does not initialize
constexpr uint8_t kCompare = 0x08;  // CRx3: the comparison modes
constexpr uint8_t kIrqEn   = 0x40;  // CRx6
constexpr uint8_t kOutEn   = 0x80;  // CRx7

} // namespace

// ---------------------------------------------------------------------------
// Counting. The counter's state is `cnt`: one 16-bit value, or MSB:LSB in dual
// 8-bit mode, where the LSB counts L..0 and each LSB underflow decrements the MSB.
// ---------------------------------------------------------------------------

// Clocks from the current state to the next time-out.
uint64_t Ptm6840::toTimeout(int i) const {
    const Timer& t = t_[i];
    if (!dual(i)) return (uint64_t)t.cnt + 1;
    uint64_t L = t.latch & 0xFF;
    return (uint64_t)(t.cnt & 0xFF) + 1 + (uint64_t)(t.cnt >> 8) * (L + 1);
}

uint64_t Ptm6840::period(int i) const {
    const Timer& t = t_[i];
    if (!dual(i)) return (uint64_t)t.latch + 1;
    return ((uint64_t)(t.latch & 0xFF) + 1) * ((uint64_t)(t.latch >> 8) + 1);
}

// Clocks until the output level can next change (Tables 5 and 6), kNever if it cannot.
uint64_t Ptm6840::toChange(int i) const {
    const Timer& t = t_[i];
    if (single(i)) {
        if (t.fired) return kNever;
        if (toggles(i) && !t.clocked) return 1;
    }
    uint64_t r = toTimeout(i);
    if (toggles(i)) return r;
    uint64_t L = t.latch & 0xFF;
    return r > L ? r - L : r;  // high for the last L clocks of the period
}

// The output pin.
//  - Continuous 16-bit (and dual with L = 0): low at initialization, toggles at
//    each time-out.
//  - Continuous dual 8-bit: high for the last L clocks of each period.
//  - Single-shot: one pulse, then low. 16-bit: low for the first clock, high until
//    the time-out. Dual: the continuous waveform's first period. N = 0 (or M = L = 0)
//    gives no pulse at all.
bool Ptm6840::output(int i) const {
    const Timer& t = t_[i];
    if (!(t.ctrl & kOutEn) || held()) return false;
    if (single(i)) {
        if (t.fired) return false;
        if (toggles(i)) return t.clocked && (t.latch >> (dual(i) ? 8 : 0)) != 0;
    } else if (toggles(i)) {
        return t.out16;
    }
    uint64_t L = t.latch & 0xFF;
    return (t.cnt >> 8) == 0 && (uint64_t)(t.cnt & 0xFF) < L;
}

// n clocks with no time-out in them.
void Ptm6840::count(int i, uint64_t n) {
    Timer& t = t_[i];
    if (n == 0) return;
    t.clocked = true;
    if (!dual(i)) {
        t.cnt = (uint16_t)(t.cnt - n);
        return;
    }
    uint64_t L     = (t.latch & 0xFF) + 1;
    uint64_t total = (uint64_t)(t.cnt & 0xFF) + (uint64_t)(t.cnt >> 8) * L - n;
    t.cnt          = (uint16_t)(((total / L) << 8) | (total % L));
}

// n clocks. With someone listening to the output, it steps from edge to edge so
// each one reaches onOutput in order. Otherwise whole periods are skipped at once.
void Ptm6840::advance(int i, uint64_t n) {
    Timer& t = t_[i];
    while (n > 0) {
        bool     watch = watching(i);
        uint64_t r     = toTimeout(i);
        uint64_t step  = watch ? std::min(n, toChange(i)) : n;
        if (step < r) {
            count(i, step);
            n -= step;
            if (watch) notify(i);
            continue;
        }
        // The time-out: set the flag and recycle from the latch.
        n -= r;
        t.cnt     = t.latch;
        t.flag    = true;
        t.fired   = true;
        t.clocked = true;
        t.out16   = !t.out16;
        if (!watch || toChange(i) == kNever) {
            uint64_t p    = period(i);
            uint64_t more = n / p;
            n %= p;
            if (more & 1) t.out16 = !t.out16;
        }
        if (watch) notify(i);
    }
    notify(i);
}

void Ptm6840::notify(int i) {
    bool level = output(i);
    if (level == t_[i].lastOut) return;
    t_[i].lastOut = level;
    if (onOutput) onOutput(i, level);
}

// Counter initialization: the latch into the counter, the flag cleared.
void Ptm6840::init(int i, uint64_t now) {
    Timer& t  = t_[i];
    t.cnt     = t.latch;
    t.flag    = false;
    t.out16   = false;
    t.fired   = false;
    t.clocked = false;
    t.pre     = 0;
    t.tRef    = now;
}

// Bring an E-clocked timer up to `now`.
void Ptm6840::sync(int i, uint64_t now) {
    Timer& t = t_[i];
    if (held() || !onE(i)) {
        t.tRef = now;
        return;
    }
    uint64_t d = div(i);
    uint64_t n = (now - t.tRef) / d;
    t.tRef += n * d;
    advance(i, n);
}

void Ptm6840::poll(const Clock& clk) {
    for (int i = 0; i < kTimers; ++i) sync(i, clk.now());
}

uint64_t Ptm6840::nextEvent(const Clock&) const {
    uint64_t best = 0;
    if (held()) return 0;
    for (int i = 0; i < kTimers; ++i) {
        const Timer& t = t_[i];
        if (!onE(i)) continue;
        uint64_t k = kNever;
        if ((t.ctrl & kIrqEn) && !t.flag) k = toTimeout(i);
        if (watching(i)) k = std::min(k, toChange(i));
        if (k == kNever) continue;
        uint64_t at = t.tRef + k * div(i);
        if (best == 0 || at < best) best = at;
    }
    return best;
}

bool Ptm6840::irq() const {
    for (const Timer& t : t_)
        if (t.flag && (t.ctrl & kIrqEn)) return true;
    return false;
}

void Ptm6840::clockEdge(int i, const Clock& clk) {
    if (held() || onE(i)) return;
    if (i == 2 && (t_[2].ctrl & 0x01)) {
        t_[2].pre = (uint8_t)((t_[2].pre + 1) & 7);
        if (t_[2].pre != 0) return;
    }
    t_[i].tRef = clk.now();
    advance(i, 1);
}

// ---------------------------------------------------------------------------
// Registers (Table 1).
// ---------------------------------------------------------------------------
uint8_t Ptm6840::read(int reg, const Clock& clk) {
    reg &= 7;
    if (reg == 0) return 0;  // no operation
    if (reg == 1) {
        poll(clk);
        uint8_t s = 0;
        for (int i = 0; i < kTimers; ++i)
            if (t_[i].flag) s |= (uint8_t)(1 << i);
        rsSeen_ = s;
        if (irq()) s |= 0x80;
        return s;
    }
    if (reg & 1) return lsbBuf_;
    // A counter's MSB, which also puts its LSB in the LSB buffer. After a status
    // read that saw this timer's flag set, it clears the flag.
    int i = (reg >> 1) - 1;
    sync(i, clk.now());
    lsbBuf_ = (uint8_t)(t_[i].cnt & 0xFF);
    if (rsSeen_ & (1 << i)) {
        rsSeen_ &= (uint8_t)~(1 << i);
        t_[i].flag = false;
    }
    return (uint8_t)(t_[i].cnt >> 8);
}

void Ptm6840::write(int reg, uint8_t data, const Clock& clk) {
    reg &= 7;
    uint64_t now = clk.now();
    if (reg == 0) {
        writeControl((t_[1].ctrl & kCr1Sel) ? 0 : 2, data, now);
        return;
    }
    if (reg == 1) {
        writeControl(1, data, now);
        return;
    }
    if (!(reg & 1)) {
        msbBuf_ = data;
        return;
    }
    // A latch: the MSB buffer and this byte. It initializes the counter unless CRx4
    // says only the gate and reset do.
    int i = (reg >> 1) - 1;
    sync(i, now);
    t_[i].latch = (uint16_t)((msbBuf_ << 8) | data);
    if (!(t_[i].ctrl & kNoWInit) || held()) init(i, now);
    notify(i);
}

void Ptm6840::writeControl(int i, uint8_t v, uint64_t now) {
    // Bring every timer up to date under the old settings first: CR10 moves them all.
    for (int j = 0; j < kTimers; ++j) sync(j, now);
    bool wasHeld = held();
    bool wasE    = onE(i);
    t_[i].ctrl   = v;
    if (i == 0 && !wasHeld && held())
        for (int j = 0; j < kTimers; ++j) init(j, now);
    if (held() != wasHeld || onE(i) != wasE)
        for (Timer& t : t_) t.tRef = now;
    if ((v & kCompare) && !(warned_ & (1 << i))) {
        warned_ |= (uint8_t)(1 << i);
        log_.push_back("6840 timer " + std::to_string(i + 1) +
                       ": the comparison modes are not modeled; it counts as continuous");
    }
    for (int j = 0; j < kTimers; ++j) notify(j);
}

void Ptm6840::reset(const Clock& clk) {
    for (int i = 0; i < kTimers; ++i) {
        t_[i].latch = 0xFFFF;
        t_[i].ctrl  = 0;
        init(i, clk.now());
    }
    t_[0].ctrl = kHold;
    msbBuf_ = lsbBuf_ = 0;
    rsSeen_           = 0;
    for (int i = 0; i < kTimers; ++i) notify(i);
}

std::vector<std::string> Ptm6840::drainLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE. tRef is absolute emulated time, which travels with the Clock.
// The board re-arms its deadline from nextEvent() after restore.
// ---------------------------------------------------------------------------
void Ptm6840::serialize(StateWriter& w) const {
    for (const Timer& t : t_) {
        w.u16(t.latch);
        w.u8(t.ctrl);
        w.u16(t.cnt);
        w.boolean(t.flag);
        w.boolean(t.out16);
        w.boolean(t.fired);
        w.boolean(t.clocked);
        w.u8(t.pre);
        w.u64(t.tRef);
    }
    w.u8(msbBuf_);
    w.u8(lsbBuf_);
    w.u8(rsSeen_);
    w.u8(warned_);
}

void Ptm6840::deserialize(StateReader& r) {
    for (Timer& t : t_) {
        t.latch   = r.u16();
        t.ctrl    = r.u8();
        t.cnt     = r.u16();
        t.flag    = r.boolean();
        t.out16   = r.boolean();
        t.fired   = r.boolean();
        t.clocked = r.boolean();
        t.pre     = r.u8();
        t.tRef    = r.u64();
    }
    msbBuf_ = r.u8();
    lsbBuf_ = r.u8();
    rsSeen_ = r.u8();
    warned_ = r.u8();
    // The board restores whatever hangs off the outputs, so this tells nobody.
    for (int i = 0; i < kTimers; ++i) t_[i].lastOut = output(i);
}

} // namespace swtpc
