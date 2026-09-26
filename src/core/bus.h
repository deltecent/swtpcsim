#pragma once
//
// The S-100 bus (DESIGN.md 4) -- the load-bearing spec.
//
// THE RULE THIS FILE EXISTS TO ENFORCE:
//   The bus carries signals and moves bytes. It does not invent behavior.
//
// It arbitrates no overlay, vectors no interrupt, knows no bank, and has never
// heard of ROM. Every one of those lives in a board. When you are tempted to
// add "if (board is a ROM)" here, you have found a bug in your board instead.

#include <bitset>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace swtpc {

class Board;
class Bus;

enum class Cycle { MemRead, MemWrite };

struct BusCycle {
    Cycle type = Cycle::MemRead;
    uint16_t addr = 0;  // memory address; for I/O the port is addr & 0xFF
    // Valid on writes. On a READ it is zero while the cycle is in flight -- nobody
    // has driven the bus yet when decodes() and read() are asked -- but it is
    // BACK-FILLED with the byte that came back (a board's, or the floating bus's
    // 0xFF) before the observers see it. See Bus::settle().
    uint8_t data = 0;

    bool isWrite() const { return type == Cycle::MemWrite; }
};

// Thrown out of a cycle function BEFORE the device is touched, to unwind the
// in-flight instruction when a cycle breakpoint should stop the machine WITH THE
// PC STILL ON THE INSTRUCTION (see Bus::setPreAccessVeto and the debugger's run
// loop). It carries nothing: the veto predicate has already recorded which
// breakpoint fired. This is a deliberate, localized departure from the codebase's
// no-sentinels idiom -- it is the only way to abandon a bus access from deep inside
// a core's decode switch without every core learning what a breakpoint is -- and it
// is confined to the bus<->debugger seam and caught one frame up in Debugger::run.
struct CycleBreakBefore {};

// NOTE there is deliberately NO `origin` field (Cpu/Monitor). A real
// backplane cycle carries no such tag -- which is exactly WHY a monitor
// DEPOSIT is indistinguishable from a CPU write, and why a real ROM ignores
// both. See DESIGN.md 10.2: the operator writes ROM through Machine::burn(),
// which is a PROM burner and not a bus operation at all.

enum class Reset {
    PowerOn,  // POC* -- Altair bus pin 76. Board-specific; the board just needs
              //   to know it happened. Guest code cannot see it.
    Bus       // RESET* -- the front-panel button.
};
// NEITHER RESET CLEARS MEMORY. Only removing power does (DESIGN.md 6). A RAM
// chip has no POC* pin.

enum class Contention { Silent, Warn, Error };

// The floating-bus diagnostic (DESIGN.md 4.6.1). A guest that reaches an address no
// board decodes reads 0xFF for ever and hangs with no explanation -- this names the
// address and the PC when it happens. On the 6800 everything is memory-mapped, so an
// undecoded MEMORY access -- an unpopulated SS-30 slot, or a runaway jump into empty
// space -- is the hang this catches. Default Silent -- opt-in, so no existing machine
// gains a line and the hot path pays one enum compare when off. Warn logs and runs
// on; Halt logs AND stops the guest at the boundary.
enum class Unclaimed { Silent, Warn, Halt };

// ---------------------------------------------------------------------------
// BOARDS RESPOND TO BUS CYCLES. A BUS MASTER ORIGINATES THEM. (DESIGN.md 3)
//
// Two concepts, and a CPU card is both. This lives with the bus and not with the
// CPU on purpose: mastering the bus is a property of the backplane, not of any one
// processor. The CPU card is the master -- it drives the cycles every other board
// responds to -- through the very same interface any master would use.
// ---------------------------------------------------------------------------
enum class RunStatus {
    Ok,      // an instruction retired
    Halted,  // WAI. The CPU is still powered and still watching the IRQ line.
};

// An explicit result, NEVER a sentinel cycle count (DESIGN.md 3.1, 16). The
// prototype's `return 0 means something went wrong` is exactly how the RLC/RRC
// bug hid: a legal instruction that happened to take zero cycles was
// indistinguishable from a failure, so nobody looked.
struct StepResult {
    uint32_t cycles = 0;
    RunStatus status = RunStatus::Ok;
};

class BusMaster {
public:
    virtual ~BusMaster() = default;
    virtual StepResult step(Bus&) = 0;
};

// A row of SHOW BUS MAP.
struct MapEntry {
    uint32_t lo = 0, hi = 0;  // inclusive
    std::string what;         // "ram", "rom", "read/write"
    std::string note;         // "bank 3", "empty socket"
};

class Bus {
public:
    void attach(Board* b);
    void detach(Board* b);
    const std::vector<Board*>& boards() const { return boards_; }

    // ---- The decode is CACHED, because on real hardware it is WIRED ----
    //
    // A card's address decoder is combinational logic -- a PAL, a row of gates --
    // wired to the address lines and to the status lines sMEMR/sINP/sOUT. It does
    // not "answer a question" per cycle. It settles, and it only CHANGES when
    // something latches: a bank strap, a card pulled from the backplane.
    //
    // Re-deriving it on every cycle was costing more than everything else in the
    // simulator combined, AND it was asking cards questions they are not even
    // wired for: an I/O-only 2SIO was asked to decode every memory read, and its
    // first act was to throw the question away (mits-2sio.cpp:296). A real 2SIO
    // has no connection to the memory read strobe. It is not in that conversation.
    //
    // So the bus asks the SAME question -- decodes(), of every board, in slot
    // order -- and asks it ONCE, storing the answer. The board
    // still owns the entire decision. The bus still invents nothing. It just
    // stopped asking sixty-five thousand times a second.
    //
    // A board whose decode changes MUST say so (Board::decodeChanged()). If one
    // forgets, the tables go stale and the machine lies quietly -- so that is not
    // left to trust: setVerify(true) re-derives the decode the slow way on
    // every single cycle and screams if it disagrees with the table. The test
    // suite and the CPU validation gate both run with it on.
    void invalidateDecode() { dirty_ = true; }

    // Paranoid mode: re-derive the decode AND the interrupt wire the slow way, and
    // check them against what we cached -- on every cycle and every instruction.
    // Slower than the original code. That is fine: it is a proof, not a path.
    void setVerify(bool on) { verify_ = on; }
    bool verify() const { return verify_; }

    // The two cycles. Each asks every board whether it decodes; exactly one should
    // answer, and it moves the byte. A decode, no decisions.
    uint8_t memRead(uint16_t addr);
    void memWrite(uint16_t addr, uint8_t data);

    // ---- the IRQ line -- A WIRE, NOT A POLL ----
    //
    // A real bus does not poll a board for interrupt status. The board sets a signal
    // high or low on the bus, the CPU reads that signal off the bus, and the board
    // clears it again when its own design says to (Patrick, 2026-07-12).
    //
    // That was a description of the hardware. It was also a description of a bug, and
    // it was not written as one. This used to
    // walk the backplane and ask every card `assertsInt()` -- ONCE PER INSTRUCTION,
    // sixty million times a second, to compute a boolean that changes about a
    // thousand times a second on a busy machine. It cost more per instruction than
    // the entire rest of the bus once the decode was cached.
    //
    // Now a board PULLS the pin (Board::intChanged()) and the bus keeps the
    // wire-OR as a running count. Reading the wire is an integer test. The bus still
    // does exactly what it did before -- carry the OR of every board asserting it,
    // pick no winner, hand out no vector (DESIGN.md 4.4) -- it just stopped
    // conducting a survey to find out what was already on the wire.
    //
    // The 6800 does not acknowledge an interrupt on the bus, and no card jams a
    // vector: when the I mask is clear and this wire is pulled, the CPU reads its
    // handler address straight from the fixed IRQ vector at FFF8 (DESIGN.md 4.4).
    // The bus does not know what a vector is -- it carries a level, nothing more.
    bool intPending() const;

    // A board's pin moved. Called by Board::intChanged(), and by nothing else.
    void intWireChanged(bool pulling) { intCount_ += pulling ? 1 : -1; }

    // ---- The cycle stream (DESIGN.md 3.0.3, 4.2.2) ----
    //
    // Every board already sees every cycle -- that is what a backplane IS. An
    // observer watches the SAME stream from outside the backplane, and that is
    // the whole implementation of BREAK MEM, TRACE and HISTORY.
    //
    // So those are NOT CPU features and must never live in a core: they are
    // questions about bus cycles, they are answered here, and a 6809 or a Z80
    // inherits every one of them on the day it lands without writing a line.
    using Observer = std::function<void(const BusCycle&)>;
    int observe(Observer fn);   // returns a handle
    void unobserve(int handle);

    // ---- The pre-access veto (BREAK MEM stops BEFORE the cycle) ----
    //
    // An Observer sees a cycle AFTER the device has driven it -- perfect for
    // HISTORY and TRACE, which record what happened. But a MEM breakpoint on the CPU
    // wants the machine to stop with the PC on the instruction and NOTHING executed:
    // no byte read, no byte written. That cannot be a post-access observer -- the
    // access already happened by then.
    //
    // So the debugger installs ONE veto, consulted at the TOP of every cycle,
    // before any board is asked. If it returns true the cycle function throws
    // CycleBreakBefore and the instruction unwinds untouched; the debugger catches
    // it and restores the CPU. Exactly one slot, like the machine's other single
    // wires -- the debugger is the only caller. A machine with no cycle breakpoint
    // never installs it and the four cycle paths pay one null-function test.
    using PreAccessVeto = std::function<bool(const BusCycle&)>;
    void setPreAccessVeto(PreAccessVeto fn) { preVeto_ = std::move(fn); }
    void clearPreAccessVeto() { preVeto_ = nullptr; }

    // Look without running a cycle -- for DISASM, TRACE and the debugger's
    // display, none of which are allowed to have side effects. Runs the SAME
    // decode as a real read; it just never strobes anybody. Floats to 0xFF when
    // nobody can answer, which is what the bus would have done anyway.
    uint8_t peek(uint16_t addr) const;

    // `n` bytes of peek() from `addr` up (wrapping at 64K), in ONE call -- the same
    // bytes n peek()s would return. CPU HISTORY reads the opcode and its operands
    // before every instruction, and three separate trips through peek() were a
    // sixth of the run loop's time.
    void peekBytes(uint16_t addr, uint8_t* out, int n) const;

    // Reverse lookup: who answers here, and why. Backs WHO and the contention
    // detector. Returns every board that ACTUALLY decodes this address.
    std::vector<Board*> respondersTo(const BusCycle& c) const;

    void setContentionPolicy(Contention p) { policy_ = p; }
    Contention contentionPolicy() const { return policy_; }

    // The floating-bus diagnostic (DESIGN.md 4.6.1, and enum Unclaimed above).
    void setUnclaimedPolicy(Unclaimed p) { unclaimedPolicy_ = p; }
    Unclaimed unclaimedPolicy() const { return unclaimedPolicy_; }

    // The CPU's current instruction address, so an unclaimed-port warning can name
    // the PC. The bus has no other way to know it -- published once per instruction
    // by the run loop (Debugger::run) BEFORE the instruction's cycles are driven.
    void setInstrPc(uint16_t pc) { instrPc_ = pc; }

    // The address the run loop last published -- read by the debug facility's
    // PC-prefix provider (dbg::setPcProvider), so every diagnostic line can name
    // the instruction that produced it.
    uint16_t instrPc() const { return instrPc_; }

    // Re-arm the once-per-(address,direction) de-dup. Called at the start of each
    // operator RUN/GO so an absent address is re-reported on a later run -- otherwise a
    // guest polling an absent UART thousands of times would bury the console.
    void resetUnclaimedWarnings() {
        warnedRead_.reset();
        warnedWrite_.reset();
        unclaimedHalt_ = false;
    }

    // Read-and-clear the pending Halt request: Unclaimed::Halt set it when a guest
    // reached an address no board decodes. The run loop checks this at the instruction
    // boundary, exactly as it checks a cycle breakpoint.
    bool takeUnclaimedHalt() {
        bool h = unclaimedHalt_;
        unclaimedHalt_ = false;
        return h;
    }
    // Which access tripped the last Halt -- for the stop message.
    uint16_t haltAddr() const { return haltAddr_; }
    bool haltWasWrite() const { return haltWrite_; }

    // Every message the bus has emitted (contention, discarded writes). The
    // monitor prints these; MCP returns them as structured data.
    const std::vector<std::string>& drain() const { return log_; }
    void clearLog() { log_.clear(); }

    // The last cycle answered nobody. DEPOSIT uses this to say "byte discarded"
    // out loud instead of silently doing nothing -- silence here is a bug that
    // takes hours to find.
    bool lastUnclaimed() const { return unclaimed_; }

    // More than one board answered the last cycle. Contention is a BUS fact (unlike
    // origin, which is not -- see BusCycle) and already computed on every cycle; the
    // observer reads it so TRACE's CONTENTION mask and HISTORY can flag the cycle.
    bool lastContended() const { return contended_; }

    // WHO answered the last cycle -- the first board in slot order that drove it, or
    // null when nobody did (the floating bus). This is NOT a new question: the cycle
    // already resolved the winning decoder (Slot::who, or Decode::first) to move the
    // byte, and this just keeps the pointer it already had instead of throwing it
    // away. The observer reads it to record HISTORY's "who responded" column -- so it
    // costs one pointer store per cycle and never a re-scan (contrast respondersTo(),
    // which allocates). On contention it is the first driver; lastContended() flags
    // the rest. Valid only immediately after a cycle, like lastContended().
    Board* lastResponder() const { return responder_; }

private:
    // THE SAME QUESTION AS decoders(), WITHOUT THE VECTOR.
    //
    // decoders() returns its answer by value, so asking it costs a heap
    // allocation -- and the cycle functions ask it on EVERY guest read and EVERY
    // guest write. That malloc/free pair was measured at two thirds of the cost of
    // a memory access (72ns -> 26ns when removed), which is to say: most of the
    // time this simulator spent was spent allocating a vector to hold the number 1.
    //
    // Nothing about the MODEL changes here. Every board is still asked, in slot
    // order, and the board still owns the whole decision. We just stopped putting
    // the answer on the heap.
    //
    // One decoder is the overwhelming case, so that is the case with no allocation
    // at all. Contention is rare, already a fault, and about to print a line of
    // text -- it can afford decoders().
    struct Decode {
        Board* first = nullptr;  // the first board in slot order that drives
        int n = 0;               // how many drive. >1 is contention.
    };
    Decode scan(const BusCycle& c) const;

    std::vector<Board*> decoders(const BusCycle& c) const;
    void reportContention(const BusCycle& c, const std::vector<Board*>& who);

    // An address no board decoded, under a non-Silent Unclaimed policy. Formats
    // one line into log_ (de-duped per address+direction) and, under Halt, arms
    // the boundary stop.
    void reportUnclaimed(const BusCycle& c);

    // ---- The cached decode ----
    //
    // One entry per 256-byte page, per cycle class. Two tables, because a card is
    // wired to the read strobe and the write strobe separately and a ROM region
    // famously does not decode a WRITE at all -- so "who answers here" has a
    // different answer for a read than for a write, and that fell out of the model
    // rather than being bolted onto it.
    struct Slot {
        Board* who = nullptr;   // the single board that drives. null: nobody -> floats 0xFF
        bool slow = false;      // more than one driver. Contention: take the exact path.
    };

    // MEMORY DECODE IS PAGE-GRANULAR (256 bytes), and that is a CONTRACT on
    // Board::decodes(), written down in board.h. It is not a guess: real S-100
    // memory decoding is done from the high address lines at 1K granularity at the
    // very finest, and our own MemoryBoard is built on a 256-entry page map
    // already. I/O needs no such contract -- all 256 ports are stored exactly.
    Slot memRead_[256], memWrite_[256];

    Slot resolve(Cycle t, uint16_t addr) const;
    void rebuild();
    void verifySlot(const BusCycle& c, const Slot& s) const;
    void verifyInt() const;

    // The IRQ line as a WIRE-OR: how many enabled boards are pulling it down right now.
    // Maintained by intWireChanged(); never recomputed on the hot path.
    int intCount_ = 0;

    // The exact path. THIS IS THE DEFINITION OF CORRECTNESS; the tables above are
    // a cache OF it, and the verifier checks them AGAINST it.
    uint8_t memReadExact(uint16_t addr);
    void memWriteExact(uint16_t addr, uint8_t data);

    bool dirty_ = true;
    bool verify_ = false;

    // Second pass, once per cycle: hand the completed cycle to every observer.
    // The bus is not notifying anyone -- the cycle was on the backplane the whole
    // time and they could all see it. The caller has already back-filled `data`
    // with what got driven (see the read paths), so the cycle handed here is the
    // finished one.
    void settle(const BusCycle& c);

    std::vector<Board*> boards_;
    std::vector<std::string> log_;
    Contention policy_ = Contention::Warn;
    bool unclaimed_ = false;
    bool contended_ = false;
    Board* responder_ = nullptr;  // who drove the last cycle -- see lastResponder()

    // The unclaimed-I/O diagnostic (DESIGN.md 4.6.1). Default Silent -- opt-in.
    Unclaimed unclaimedPolicy_ = Unclaimed::Silent;
    uint16_t instrPc_ = 0;                // the running instruction's PC, for the message
    std::bitset<65536> warnedRead_;       // addresses already warned this run: read ...
    std::bitset<65536> warnedWrite_;      // ... and write, kept apart -- the message names it
    bool unclaimedHalt_ = false;          // Halt tripped; the run loop stops at the boundary
    uint16_t haltAddr_ = 0;               // which address, and ...
    bool haltWrite_ = false;              // ... which direction, for the stop message

    std::vector<std::pair<int, Observer>> observers_;
    int nextObserver_ = 1;

    // The pre-access veto -- null unless the debugger has installed one for a run
    // that carries a cycle breakpoint. See setPreAccessVeto.
    PreAccessVeto preVeto_;
};

} // namespace swtpc
