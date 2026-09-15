#include "core/bus.h"

#include "core/board.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace swtpc {

void Bus::attach(Board* b) {
    boards_.push_back(b);
    b->attachBus(this);
    // It may ALREADY be pulling the IRQ line -- a card does not stop asking to be
    // serviced because you moved it to another slot, and a card built on the bench
    // arrives with its pins in whatever state it left them in. (intWire() already
    // accounts for enabled: a disabled card drives nothing, and says so when it is
    // disabled, not when it is asked.)
    if (b->intWire()) ++intCount_;
    // If the card offers debug flags, give it a diagnostic channel named by its id
    // (core/debuglog.h). Idempotent and not undone by detach(), so moving the card
    // between backplanes keeps the operator's DEBUG= selection. `id` is set by now.
    b->ensureDebugChannel();
    invalidateDecode();  // a card went into a slot: the wiring changed
}

void Bus::detach(Board* b) {
    boards_.erase(std::remove(boards_.begin(), boards_.end(), b), boards_.end());
    if (b->intWire()) --intCount_;  // and it takes its interrupt out with it
    b->attachBus(nullptr);
    invalidateDecode();  // ...and the wiring changed again when it came out
}

std::vector<Board*> Bus::decoders(const BusCycle& c) const {
    std::vector<Board*> out;
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) out.push_back(b);
    return out;
}

Bus::Decode Bus::scan(const BusCycle& c) const {
    Decode d;
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) {
            if (!d.first) d.first = b;
            ++d.n;
        }
    return d;
}

std::vector<Board*> Bus::respondersTo(const BusCycle& c) const {
    return decoders(c);
}

uint8_t Bus::peek(uint16_t addr) const {
    // peek is on the instruction flight recorder's hot path -- debug.cpp snapshots
    // three opcode bytes with it for EVERY executed instruction -- so it takes the
    // same cached decode the live read path (memRead) uses instead of re-scanning
    // the backplane. memRead_[page] names the single board that answers a MemRead in
    // that 256-byte page; on a non-slow page that IS the only decoder (slow == n>1),
    // so peeking it is identical to the exact first-responder scan, minus the scan.
    if (!dirty_) {
        const Slot& s = memRead_[addr >> 8];
        if (verify_) verifySlot(BusCycle{Cycle::MemRead, addr, 0}, s);
        if (!s.slow) {
            uint8_t v = 0xFF;
            if (s.who) s.who->peek(addr, v);  // false leaves 0xFF: the floating bus
            return v;
        }
        // slow page (contention, or a sub-page decoder): fall through
    }

    // No cache to trust -- dirty (nothing has rebuilt it yet this run) or a slow page.
    // The exact first-responder scan, still without building a decoders() vector.
    BusCycle c{Cycle::MemRead, addr, 0};
    for (Board* b : boards_)
        if (b->enabled() && b->decodes(c)) {
            uint8_t v = 0xFF;
            if (b->peek(addr, v)) return v;
        }
    return 0xFF;  // nobody could answer without side effects. Neither can we.
}

static const char* cycleName(Cycle t) {
    switch (t) {
    case Cycle::MemRead: return "read";
    case Cycle::MemWrite: return "write";
    }
    return "?";
}

void Bus::reportContention(const BusCycle& c, const std::vector<Board*>& who) {
    if (policy_ == Contention::Silent) return;

    char buf[160];
    std::snprintf(buf, sizeof buf, "CONTENTION: 0x%04X (%s) driven by", c.addr,
                  cycleName(c.type));

    std::string m = buf;
    for (Board* b : who) m += " " + b->id;
    // Two boards both actually driving is a real electrical fault, and a real
    // backplane would hand you exactly this bug. We report it; we do not pick a
    // winner. Picking a winner is how a simulator lies to you.
    m += "  -- both boards drive. The bus does not arbitrate (DESIGN.md 4.6).";
    log_.push_back(m);
}

// The floating-bus diagnostic (DESIGN.md 4.6.1). A guest that reaches an address no
// board decodes reads 0xFF for ever and hangs -- this is the line that says why.
// Called from the memory paths, when nobody answered. The PC is whatever the run loop
// last published (setInstrPc); on a bare interactive access with no run in flight it
// is simply the last instruction the CPU retired.
void Bus::reportUnclaimed(const BusCycle& c) {
    // The caller has already checked the policy is not Silent, but keep the guard so
    // the function is safe to call unconditionally too.
    if (unclaimedPolicy_ == Unclaimed::Silent) return;

    const bool write = c.isWrite();
    std::bitset<65536>& seen = write ? warnedWrite_ : warnedRead_;
    if (seen.test(c.addr)) return;  // once per address+direction per run -- see resetUnclaimedWarnings()
    seen.set(c.addr);

    char buf[160];
    if (write)
        // e.g. warning: write C000 <- 01 at PC=0113: no board decodes address 0xC000. the byte is gone.
        std::snprintf(buf, sizeof buf,
                      "warning: write %04X <- %02X at PC=%04X: no board decodes address 0x%04X. "
                      "the byte is gone.",
                      c.addr, c.data, instrPc_, c.addr);
    else
        // e.g. warning: read C000 -> FF at PC=0113: no board decodes address 0xC000. it floated to 0xFF.
        std::snprintf(buf, sizeof buf,
                      "warning: read %04X -> %02X at PC=%04X: no board decodes address 0x%04X. "
                      "it floated to 0xFF.",
                      c.addr, c.data, instrPc_, c.addr);
    log_.push_back(buf);

    // Halt names the same access to the run loop, which stops at the boundary.
    if (unclaimedPolicy_ == Unclaimed::Halt) {
        unclaimedHalt_ = true;
        haltAddr_ = c.addr;
        haltWrite_ = write;
    }
}

// THE CYCLE HANDED TO settle() IS THE FINISHED ONE, AND `data` IS ALWAYS VALID HERE.
//
// BusCycle::data is documented as "valid on writes", and during the cycle that is
// true -- on a READ nobody has driven the bus yet when decodes() and read() are
// asked. But settle() runs AFTER the cycle completes, and by then
// a byte HAS been driven: by the board that answered, or by the floating bus, which
// drives 0xFF just as surely. So every read path back-fills `data` with what came
// back before calling this.
//
// That is not a convenience for a watcher. It is what the wire is doing. A TRACE
// does not care which direction the byte was going -- the value on D0..D7 is the
// value on D0..D7 -- which is why the observers get the same corrected cycle.
void Bus::settle(const BusCycle& c) {
    // Anyone watching from OUTSIDE the backplane -- the debugger, the tracer. Every
    // board already SEES every cycle (that is what a backplane is); an observer
    // watches the SAME stream. That is why BREAK MEM needed no new machinery and
    // cost no CPU support: a bus cycle is a bus cycle no matter who originated it,
    // so they work against a DEPOSIT as surely as against the processor.
    for (const auto& o : observers_) o.second(c);
}

int Bus::observe(Observer fn) {
    int h = nextObserver_++;
    observers_.emplace_back(h, std::move(fn));
    return h;
}

void Bus::unobserve(int handle) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [&](const auto& o) { return o.first == handle; }),
                     observers_.end());
}

// READ THE IRQ LINE. Is anybody pulling it down?
//
// Not a question we ask the boards -- they TOLD us, when it changed. That is what
// a wire is, and it is the whole difference between this and a survey.
bool Bus::intPending() const {
    if (verify_) verifyInt();
    return intCount_ > 0;
}

// The proof, and the exact counterpart of verifySlot(). Re-derive the wire from
// every board's combinational assertsInt() and compare it to what each board says
// it is driving, and to the running count.
//
// A board that changed its interrupt state and forgot to call intChanged() dies
// HERE, loudly, at the next instruction -- rather than hanging the guest in a WAI
// forever, waiting for an interrupt that already happened and was never carried.
// That is a bug worth days, and it would present as "the emulator locks up
// sometimes", which is worth several more.
void Bus::verifyInt() const {
    int live = 0;
    for (Board* b : boards_) {
        bool actual = b->enabled() && b->assertsInt();
        if (b->intWire()) ++live;
        if (actual == b->intWire()) continue;

        std::fprintf(stderr,
                     "\nIRQ WIRE IS STALE -- board '%s'\n"
                     "  the wire says: %s\n"
                     "  the board says: %s\n"
                     "It changed its interrupt state and did not call intChanged().\n",
                     b->id.c_str(), b->intWire() ? "pulling" : "not pulling",
                     actual ? "pulling" : "not pulling");
        std::abort();
    }

    // ...and the count itself, which attach()/detach() maintain by hand. Two
    // boards whose errors cancel would sail past the loop above; a bad count would
    // not, and this is the only place either could be caught.
    if (live != intCount_) {
        std::fprintf(stderr,
                     "\nIRQ WIRE-OR COUNT IS WRONG\n"
                     "  the bus thinks %d board(s) are pulling the IRQ line\n"
                     "  %d actually are\n"
                     "A board went into or out of the backplane without the count following.\n",
                     intCount_, live);
        std::abort();
    }
}

// ---------------------------------------------------------------------------
// The cycle. This is the whole bus.
//
//   pass 1  decode  -- ask every board whether it drives; move the byte
//   pass 2  settle  -- hand the finished cycle to every observer watching from
//                      outside the backplane (the debugger, the tracer)
//
// Pass 2 is not a callback and it is not the bus telling anyone anything: the
// cycle was on the backplane, in front of everything, the entire time -- an
// observer just watches the same wires. It backs BREAK MEM, TRACE and HISTORY.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// THE EXACT PATH. This is the definition of correctness, and it is the code that
// was here before -- every board asked, nothing cached. The tables below are a
// CACHE OF THIS, and the verifier checks them AGAINST it.
//
// A read is answered by the FIRST board in slot order that drives it. A write is
// latched by EVERY board that drives it -- two cards both latching the same write
// is a real fault and we reproduce it rather than pick a winner.
//
// The n > 1 arm re-asks decoders(). That calls decodes() a second time, which is
// free of consequence because decodes() is COMBINATIONAL AND PURE by contract
// (board.h) -- and it happens only when the machine is already electrically
// broken and we are on our way to printing a paragraph about it.
// ---------------------------------------------------------------------------

uint8_t Bus::memReadExact(uint16_t addr) {
    BusCycle c{Cycle::MemRead, addr, 0};

    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));

    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    responder_ = d.first;
    uint8_t v = d.first ? d.first->read(c)
                        : 0xFF;  // floating bus (DESIGN.md 4.6.1)
    c.data = v;                  // what got driven -- see settle()
    if (unclaimedPolicy_ != Unclaimed::Silent && d.n == 0) reportUnclaimed(c);
    settle(c);
    return v;
}

void Bus::memWriteExact(uint16_t addr, uint8_t data) {
    BusCycle c{Cycle::MemWrite, addr, data};

    Decode d = scan(c);
    if (d.n > 1) reportContention(c, decoders(c));

    unclaimed_ = (d.n == 0);
    contended_ = (d.n > 1);
    responder_ = d.first;
    // Nobody latched it. The byte is simply gone -- the write half of the
    // floating bus. This is what a guest write to ROM does, and the bus needed
    // no rule about ROM to make it happen.
    if (d.n == 1) d.first->write(c);
    else if (d.n > 1) for (Board* b : decoders(c)) b->write(c);
    if (unclaimedPolicy_ != Unclaimed::Silent && d.n == 0) reportUnclaimed(c);
    settle(c);
}


// ---------------------------------------------------------------------------
// THE CACHE. Built by asking the boards -- the same two questions, in slot order.
// ---------------------------------------------------------------------------

Bus::Slot Bus::resolve(Cycle t, uint16_t addr) const {
    BusCycle c{t, addr, 0};
    Decode d = scan(c);  // ask every board whether it drives, exactly as a live cycle does

    Slot s;
    s.who  = d.first;
    s.slow = (d.n > 1);  // contention: send it down the exact path, which reports it
    return s;
}

void Bus::rebuild() {
    dirty_ = false;

    // IS ANY CARD IN THIS MACHINE DECODING A LOW ADDRESS LINE?
    //
    // Usually not. But an SS-30 board answers just a few addresses inside its page:
    // the MP-S serial console decodes only its control/status and data bytes at the
    // slot base -- two answers inside one 256-byte page. We do not ASSUME our way
    // past that and we do not pick a finer page size and hope. We ask
    // (decodeIsPageUniform()), and if any card says no we stop guessing and PROBE.
    bool uniform = true;
    for (Board* b : boards_)
        if (!b->decodeIsPageUniform()) uniform = false;

    for (int p = 0; p < 256; ++p) {
        uint16_t a   = (uint16_t)(p << 8);
        memRead_[p]  = resolve(Cycle::MemRead, a);
        memWrite_[p] = resolve(Cycle::MemWrite, a);

        if (uniform) continue;

        // Probe all 256 addresses of the page. If they do not agree, the page has
        // no single answer, so it gets NO cached answer: it is served by the exact
        // two-pass path forever, which is the code that was always here.
        //
        // This costs a few milliseconds, it happens only on a rebuild (an operator
        // action, not a guest one), and only in a machine that actually contains
        // such a card. An SS-30 board costs us ONE page out of 256.
        for (int lo = 1; lo < 256; ++lo) {
            uint16_t x = (uint16_t)(a | lo);
            Slot r = resolve(Cycle::MemRead, x);
            Slot w = resolve(Cycle::MemWrite, x);
            if (r.who != memRead_[p].who || r.slow != memRead_[p].slow)
                memRead_[p].slow = true;
            if (w.who != memWrite_[p].who || w.slow != memWrite_[p].slow)
                memWrite_[p].slow = true;
        }
    }
}

// The proof. Re-derive the decode the slow way and compare it to what we cached.
// A board that changed its decode without saying so dies HERE, loudly, at the
// first cycle that touches it -- instead of somewhere downstream, quietly, in a
// month. Costs more than the original code; it is not a path, it is an assertion.
void Bus::verifySlot(const BusCycle& in, const Slot& s) const {
    BusCycle c = in;
    Decode d = scan(c);

    bool ok = (d.first == s.who) && ((d.n > 1) == s.slow);
    if (ok) return;

    std::fprintf(stderr,
                 "\nBUS DECODE CACHE IS STALE at %s 0x%04X\n"
                 "  cached: who=%s slow=%d\n"
                 "  actual: who=%s n=%d\n"
                 "A board changed its decode and did not call decodeChanged().\n",
                 cycleName(c.type), c.addr, s.who ? s.who->id.c_str() : "-",
                 (int)s.slow, d.first ? d.first->id.c_str() : "-", d.n);
    std::abort();
}

// ---------------------------------------------------------------------------
// THE CYCLE. A table lookup, and the board that the table names.
// ---------------------------------------------------------------------------

uint8_t Bus::memRead(uint16_t addr) {
    // A cycle breakpoint that stops BEFORE the access unwinds the instruction here,
    // before any board is touched (see setPreAccessVeto / CycleBreakBefore). One
    // null-function test when no such breakpoint is armed.
    if (preVeto_ && preVeto_(BusCycle{Cycle::MemRead, addr, 0})) throw CycleBreakBefore{};
    if (dirty_) rebuild();
    const Slot& s = memRead_[addr >> 8];

    // A slow page has no cached answer to check -- that is what makes it slow.
    if (s.slow) return memReadExact(addr);  // contention, or a non-uniform page.
    if (verify_) verifySlot(BusCycle{Cycle::MemRead, addr, 0}, s);

    BusCycle c{Cycle::MemRead, addr, 0};
    unclaimed_ = (s.who == nullptr);
    responder_ = s.who;
    uint8_t v = s.who ? s.who->read(c) : 0xFF;  // floating bus (DESIGN.md 4.6.1)
    c.data = v;                                 // what got driven -- see settle()
    if (unclaimedPolicy_ != Unclaimed::Silent && s.who == nullptr) reportUnclaimed(c);
    settle(c);
    return v;
}

void Bus::memWrite(uint16_t addr, uint8_t data) {
    if (preVeto_ && preVeto_(BusCycle{Cycle::MemWrite, addr, data})) throw CycleBreakBefore{};
    if (dirty_) rebuild();
    const Slot& s = memWrite_[addr >> 8];

    if (s.slow) { memWriteExact(addr, data); return; }
    if (verify_) verifySlot(BusCycle{Cycle::MemWrite, addr, data}, s);

    BusCycle c{Cycle::MemWrite, addr, data};
    unclaimed_ = (s.who == nullptr);
    responder_ = s.who;
    // Nobody latched it. The byte is simply gone -- the write half of the floating
    // bus, and what a guest write to ROM does. No rule about ROM was needed.
    if (s.who) s.who->write(c);
    if (unclaimedPolicy_ != Unclaimed::Silent && s.who == nullptr) reportUnclaimed(c);
    settle(c);
}

} // namespace swtpc
