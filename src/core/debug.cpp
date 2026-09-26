#include "core/debug.h"

#include <csignal>

#include "core/machine.h"
#include "cpu/cpu.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <vector>

namespace swtpc {

// The ONE thing a signal handler is allowed to touch. Not a std::string, not the
// machine, not a stream -- a lock-free flag, and nothing else.
static std::atomic<bool> g_stopRequest{false};

void Debugger::requestStop() { g_stopRequest.store(true); }
void Debugger::clearStopRequest() { g_stopRequest.store(false); }
bool Debugger::stopRequested() { return g_stopRequest.load(); }

// See SigintGuard in debug.h for the whole reasoning, including why the second ^C
// has to kill. `g_prevSigint` is read only by the handler and written only when a
// guard is constructed or destroyed, which never happens while one is in flight.
static void (*g_prevSigint)(int) = nullptr;

static void onSigint(int sig) {
    if (g_stopRequest.exchange(true)) {
        // The previous one was never consumed -- this operator is not being heard. Die the
        // way they meant: the default disposition, not whatever was installed before us.
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

// IF IT WAS IGNORED, LEAVE IT IGNORED -- the same POSIX idiom, and for the same reason,
// as armSignalHandlers() in platform/posix/terminal_posix.cpp. A process started in the
// BACKGROUND or under `nohup` inherits SIGINT already set to SIG_IGN, precisely so a ^C
// meant for the foreground job cannot reach it; SIG_IGN survives exec. Installing over
// that would make `nohup altairsim … --mcp &` answerable to a keystroke aimed at
// something else -- and, with the second-^C rule above, killable by one. (Found exactly
// that way: a test script launched the server as a background job, so the guard's "previous
// handler" was SIG_IGN and the kill path restored *ignore* -- the process could not be
// stopped by any number of ^Cs.)
SigintGuard::SigintGuard() {
    prev_ = std::signal(SIGINT, onSigint);
    if (prev_ == SIG_IGN) {          // detached: put it back and stay out of the way
        std::signal(SIGINT, SIG_IGN);
        installed_ = false;
        return;
    }
    installed_   = true;
    g_prevSigint = prev_;
}

SigintGuard::~SigintGuard() {
    if (!installed_) return;
    std::signal(SIGINT, prev_);
    g_prevSigint = nullptr;
}

const char* breakKindName(BreakKind k) {
    switch (k) {
    case BreakKind::Pc:       return "pc";
    case BreakKind::MemRead:  return "mem r";
    case BreakKind::MemWrite: return "mem w";
    case BreakKind::TapeStop: return "tape stop";
    }
    return "?";
}

const DeviceEvent* deviceEventForKind(BreakKind k) {
    for (const DeviceEvent& de : kDeviceEvents)
        if (de.bk == k) return &de;
    return nullptr;
}

const char* breakActionName(BreakAction a) {
    switch (a) {
    case BreakAction::Stop:     return "stop";
    case BreakAction::TraceOn:  return "trace on";
    case BreakAction::TraceOff: return "trace off";
    }
    return "?";
}

std::string Breakpoint::describe() const {
    // A device-event breakpoint has no address to print -- it is a named hardware event,
    // not a place on the bus. Render it "<kind> <action>" lowercased from the one table,
    // so a new member names itself here the moment its row lands (no drift with the parser).
    if (const DeviceEvent* de = deviceEventForKind(kind)) {
        std::string s;
        for (const char* p = de->kind; *p; ++p) s += (char)std::tolower((unsigned char)*p);
        s += ' ';
        for (const char* p = de->action; *p; ++p) s += (char)std::tolower((unsigned char)*p);
        if (action != BreakAction::Stop) s += std::string(" ") + breakActionName(action);
        return s;
    }

    char buf[80];
    if (lo == hi)
        std::snprintf(buf, sizeof buf, "%-6s %04X", breakKindName(kind), (unsigned)lo);
    else
        std::snprintf(buf, sizeof buf, "%-6s %04X-%04X", breakKindName(kind), (unsigned)lo,
                      (unsigned)hi);
    std::string s = buf;
    // IF and LOADS are the same stored Expr; condWhen tells them apart. LOADS reads as
    // what it tests -- the value the cycle produced -- so it earns its own word here.
    if (cond) s += (condWhen == CondWhen::After ? " loads " : " if ") + cond->text();
    // Mirrors the order it was typed in: BREAK 100 IF A==3 TRACE ON. Stop is the
    // default and saying so on every ordinary breakpoint would be noise on every
    // line of the listing.
    if (action != BreakAction::Stop) s += std::string(" ") + breakActionName(action);
    return s;
}

int Debugger::add(BreakKind k, uint32_t lo, uint32_t hi, std::shared_ptr<const Expr> cond,
                  BreakAction action, CondWhen condWhen) {
    Breakpoint b;
    b.id = nextId_++;
    b.kind = k;
    b.lo = lo;
    b.hi = hi;
    b.cond = std::move(cond);
    b.action = action;
    b.condWhen = condWhen;
    bps_.push_back(b);
    return b.id;
}

bool Debugger::remove(int id, std::string& err) {
    for (size_t i = 0; i < bps_.size(); ++i) {
        if (bps_[i].id == id) {
            bps_.erase(bps_.begin() + (long)i);
            if (bps_.empty()) nextId_ = 1;
            return true;
        }
    }
    err = "no breakpoint " + std::to_string(id);
    return false;
}

void Debugger::clear() { bps_.clear(); nextId_ = 1; }

// A board -> small handle, for the HISTORY "who" columns. The hot path: a one-entry
// cache (the same board answered the last cycle), then a short scan of the boards seen
// so far this run, then -- only on a board's first appearance -- the id string is
// interned into the stable name table. So a warmed recorder does no allocation and, in
// the common case, one pointer compare. See the members' comment in debug.h.
int Debugger::internBoard(Board* b) {
    if (!b) return -1;
    if (b == internMru_) return internMruH_;
    for (const auto& pr : internPtrs_)
        if (pr.first == b) {
            internMru_ = b;
            internMruH_ = pr.second;
            return pr.second;
        }
    // First time this board has driven or answered this run. Give its id a stable
    // handle -- reusing one from an earlier run if the same id is back -- and remember
    // the pointer so the rest of this run answers from the scan above.
    int h = -1;
    for (size_t i = 0; i < boardHandles_.size(); ++i)
        if (boardHandles_[i] == b->id) {
            h = (int)i;
            break;
        }
    if (h < 0) {
        h = (int)boardHandles_.size();
        boardHandles_.push_back(b->id);
    }
    internPtrs_.emplace_back(b, h);
    internMru_ = b;
    internMruH_ = h;
    return h;
}

// ---------------------------------------------------------------------------
// The cycle breakpoints, as a bus observer.
//
// ARMED ONLY WHILE RUNNING, and that is deliberate. The monitor's own DUMP and
// DEPOSIT are real bus cycles -- that is the point of them -- so an always-armed
// BREAK MEM W would "trigger" on the operator's own DEPOSIT, with no program
// running to stop. Breaking is something that happens to a RUNNING machine; the
// observer exists only for as long as one is.
// ---------------------------------------------------------------------------
bool Debugger::armObserver() {
    // ALWAYS ARMED while running, now -- not only when a cycle breakpoint is set.
    // HISTORY is a flight recorder that has to be running BEFORE the stop it explains,
    // and TRACE can be turned on for a run that has no breakpoints at all. The three
    // jobs share one observer so a cycle costs one std::function call, not three.
    // (The monitor's own DUMP/DEPOSIT are still safe: the observer exists only for as
    // long as a run does, so a front-panel poke while stopped records nothing.)
    cycleHit_ = 0;
    // The pointer->handle map is only good for one run (a Board* does not outlive the
    // machine that held it). Drop it and the cache; the id NAME table (boardHandles_)
    // survives, so a record still in the ring from a previous run keeps its name.
    internPtrs_.clear();
    internMru_ = nullptr;
    internMruH_ = -1;
    observer_ = m_.bus.observe([this](const BusCycle& c) {
        CycleRec rec;
        rec.type = c.type;
        rec.addr = c.addr;
        rec.data = c.data;
        rec.contended = m_.bus.lastContended();
        rec.t = m_.clock.now();
        // WHO answered -- interned to a handle here, at record time. The origin is not
        // recorded: every cycle comes from the CPU and renders as a literal "cpu".
        rec.responder = (int16_t)internBoard(m_.bus.lastResponder());

        // HISTORY: overwrite-oldest ring.
        if (ring_.size() < kHistoryCap) {
            ring_.push_back(rec);
        } else {
            ring_[ringHead_] = rec;
            ringHead_ = (ringHead_ + 1) % kHistoryCap;
        }

        // TRACE: one line per cycle the mask admits.
        if (tracing() && traceShows(rec)) *traceSink_ << formatCycle(rec, boardHandles_) << "\n";
    });
    return true;
}

// TRACE's filter. An empty mask shows everything; otherwise a cycle is shown if any
// of its categories is selected. Contention is carried on the record.
bool Debugger::traceShows(const CycleRec& r) const {
    if (traceMask_ == 0) return true;
    unsigned cat = 0;
    if (r.contended) cat |= Contended;
    return (cat & traceMask_) != 0;
}

std::string Debugger::formatCycle(const CycleRec& r, const std::vector<std::string>& handles) {
    const char* what = "?";
    switch (r.type) {
    case Cycle::MemRead:  what = "MR";  break;
    case Cycle::MemWrite: what = "MW";  break;
    }

    char buf[80];
    std::snprintf(buf, sizeof buf, "%10llu  %-4s %04X = %02X", (unsigned long long)r.t, what,
                  (unsigned)r.addr, r.data);

    // WHO drove it -> WHO answered. Every cycle originates at the CPU, so the origin is
    // the literal "cpu"; the responder is a handle into the name table, or -1 for the
    // floating bus -- nobody. An out-of-range handle (a table that lost the id) shows
    // "?" rather than reading past the end.
    auto name = [&handles](int16_t h, const char* dflt) -> std::string {
        if (h < 0) return dflt;
        if ((size_t)h < handles.size()) return handles[(size_t)h];
        return "?";
    };
    // "cpu" padded to a fixed width so the responder column lines up row to row; the
    // responder is last (bar an optional [CONTENTION] flag) and is NOT padded, so an
    // ordinary line carries no trailing whitespace.
    char who[48];
    std::snprintf(who, sizeof who, "   %-8s -> %s", "cpu",
                  name(r.responder, "--").c_str());

    std::string s = buf;
    s += who;
    if (r.contended) s += "  [CONTENTION]";
    return s;
}

std::vector<Debugger::CycleRec> Debugger::history(size_t n) const {
    size_t have = ring_.size();
    if (n > have) n = have;
    bool full = (have == kHistoryCap);
    size_t start = full ? ringHead_ : 0;   // oldest
    size_t skip = have - n;                // keep the LAST n
    std::vector<CycleRec> out;
    out.reserve(n);
    for (size_t i = skip; i < have; ++i) out.push_back(ring_[(start + i) % have]);
    return out;
}

void Debugger::clearHistory() {
    ring_.clear();
    ringHead_ = 0;
}

std::vector<Debugger::InsnRec> Debugger::insnHistory(size_t n) const {
    size_t have = insnRing_.size();
    if (n > have) n = have;
    bool full = (have == kInsnHistoryCap);
    size_t start = full ? insnRingHead_ : 0;   // oldest
    size_t skip = have - n;                     // keep the LAST n
    std::vector<InsnRec> out;
    out.reserve(n);
    for (size_t i = skip; i < have; ++i) out.push_back(insnRing_[(start + i) % have]);
    return out;
}

void Debugger::clearInsnHistory() {
    insnRing_.clear();
    insnRingHead_ = 0;
}

void Debugger::disarmObserver() {
    if (observer_) {
        m_.bus.unobserve(observer_);
        observer_ = 0;
    }
    m_.bus.clearPreAccessVeto();
}

// Match one cycle against the CYCLE-kind breakpoints, doing the hit-counting and
// trace bookkeeping in ONE place -- called from the pre-access veto, before the
// access, so the machine can stop with the PC on the instruction and nothing done.
// Sets cycleHit_ for a Stop; the caller throws-and-unwinds on it.
void Debugger::matchCycleBreak(const BusCycle& c) {
    for (Breakpoint& b : bps_) {
        if (!b.enabled || b.kind == BreakKind::Pc) continue;

        bool match = false;
        switch (b.kind) {
        case BreakKind::MemRead:  match = c.type == Cycle::MemRead;  break;
        case BreakKind::MemWrite: match = c.type == Cycle::MemWrite; break;
        case BreakKind::Pc:       break;
        case BreakKind::TapeStop: break;  // device event, matched in the tape-stop path, not here
        }
        if (!match) continue;

        if (c.addr < b.lo || c.addr > b.hi) continue;

        // A CONDITIONAL cycle breakpoint (BREAK ... IF|LOADS) cannot be decided here: this
        // is mid-instruction, where a register read has no boundary-consistent answer, and
        // LOADS needs the value the cycle is about to produce. Defer it to the instruction
        // boundary, which evaluates the condition and counts the hit only if it acts.
        if (b.cond) {
            if (std::find(pendingCond_.begin(), pendingCond_.end(), b.id) == pendingCond_.end())
                pendingCond_.push_back(b.id);
            continue;
        }

        ++b.hits;

        // A tracepoint acts and the machine runs on -- so it does NOT set
        // cycleHit_, and a Stop breakpoint on the same cycle still stops.
        if (b.action != BreakAction::Stop) {
            traceActive_ = (b.action == BreakAction::TraceOn);
            continue;
        }
        if (!cycleHit_) cycleHit_ = b.id;   // the FIRST one to fire wins the report
    }
}

// ---------------------------------------------------------------------------
// The run loop. This is the debugger, and it asks only generic questions.
// ---------------------------------------------------------------------------
RunResult Debugger::run(uint64_t maxSteps, bool clearPending) {
    RunResult r;

    CpuCore* cpu = m_.cpu();
    BusMaster* master = m_.master();
    if (!cpu || !master) {
        // A BACKPLANE WITH NO PROCESSOR IN IT IS A REAL MACHINE, and it is the one
        // milestone 1a ran. So this is not an internal error -- it is a fact about
        // the machine, reported as one.
        r.why = StopReason::NoCpu;
        return r;
    }

    // ONE-SHOT RESUME (see skipArmed_): a previous run stopped BEFORE a cycle
    // breakpoint, leaving the PC on the instruction that tripped. If the PC is still
    // there, keep the one-shot armed so this run steps THROUGH that instruction once
    // before the veto starts trapping again. If the operator moved the PC in between
    // (a jump, a register poke), the one-shot is stale -- drop it so it cannot swallow
    // an unrelated hit at the new address.
    if (skipArmed_ && cpu->pc() != resumeCyclePc_) skipArmed_ = false;

    if (clearPending) clearStopRequest();
    armObserver();
    m_.running = true;

    // DEVICE-EVENT BREAKPOINTS (BREAK TAPE STOP, and its future siblings). Are any armed?
    // If none are, we never poll the boards for them and a machine without one pays
    // nothing. If one IS armed,
    // drain every board's edge latch ONCE here to sync it to the here-and-now, so a device
    // already sitting at its mark from an earlier load does not fire the instant this run
    // begins -- only a fresh arrival during THIS run does (core/board.h takeAutoStop).
    bool watchDeviceEvent = false;
    for (const Breakpoint& b : bps_)
        if (b.enabled && deviceEventForKind(b.kind)) { watchDeviceEvent = true; break; }
    if (watchDeviceEvent)
        for (const auto& bd : m_.boards()) bd->takeAutoStop();

    // PRE-ACCESS VETO for the CYCLE breakpoints. Installed only when at least one
    // MEM/IO breakpoint is enabled, so a machine that carries only PC breakpoints (or
    // none) never pays for the four cycle paths' null test to become a real call. The
    // veto fires from inside a CPU cycle, BEFORE any board is touched: it matches the
    // cycle, and on a Stop it throws CycleBreakBefore so the instruction unwinds with
    // nothing executed and the PC still on it.
    bool watchCycle = false;
    for (const Breakpoint& b : bps_)
        if (b.enabled && (b.kind == BreakKind::MemRead || b.kind == BreakKind::MemWrite)) {
            watchCycle = true;
            break;
        }
    if (watchCycle)
        m_.bus.setPreAccessVeto([this](const BusCycle& c) -> bool {
            // The resumed instruction runs with the veto inert -- for the WHOLE
            // instruction, not just its first matching cycle, so an instruction with
            // several matching cycles (a broad BREAK MEM R over its own fetches) still
            // steps through instead of re-trapping on cycle two. The run loop clears
            // skipArmed_ the moment that instruction retires.
            if (skipArmed_) return false;
            matchCycleBreak(c);
            return cycleHit_ != 0;
        });

    // Reflected registers for BREAK <addr> IF <expr>. The RegDef list is snapshotted
    // ONCE -- it is stable across a run (its get() closures read live state), so a
    // conditional breakpoint costs no per-step allocation -- and looked up by name,
    // so it never learns what a 6800 is and a future 6809 inherits it (DESIGN.md 3.0.3).
    std::vector<RegDef> regs = cpu->registers();
    Expr::Resolver resolveReg = [&regs](const std::string& name, uint32_t& out) -> bool {
        for (const RegDef& rd : regs) {
            if (rd.name.size() != name.size()) continue;
            bool eq = true;
            for (size_t i = 0; i < name.size(); ++i)
                if (std::toupper((unsigned char)rd.name[i]) !=
                    std::toupper((unsigned char)name[i])) {
                    eq = false;
                    break;
                }
            if (eq) {
                out = rd.get();
                return true;
            }
        }
        return false;
    };

    for (;;) {
        // The bus cannot see the PC, so hand it this instruction's address before the
        // cycles run -- it is what an unclaimed-port warning names (DESIGN.md 4.6.1).
        // Captured here, at the boundary, because by the time an OUT cycle fires the
        // CPU's own PC has already stepped past the opcode and its operand.
        m_.bus.setInstrPc(cpu->pc());

        // CPU HISTORY: snapshot the machine as it stands right now -- the same state STEP
        // would print for the instruction about to run -- into the instruction ring. The
        // sibling of the bus flight recorder in armObserver(); always on while running.
        // A ring slot's regs vector keeps its capacity across wraps, so a warmed ring
        // records without allocating. Reuses the once-snapshotted `regs` for the getters.
        // Hoisted out of the block below so the CycleBreakBefore catch can read it
        // back: it is the pristine register snapshot to restore the CPU from.
        InsnRec* slot;
        {
            if (insnRing_.size() < kInsnHistoryCap) {
                insnRing_.emplace_back();
                slot = &insnRing_.back();
            } else {
                slot = &insnRing_[insnRingHead_];
                insnRingHead_ = (insnRingHead_ + 1) % kInsnHistoryCap;
            }
            slot->pc = cpu->pc();
            slot->regs.resize(regs.size());
            for (size_t i = 0; i < regs.size(); ++i) slot->regs[i] = regs[i].get();
            slot->nbytes = 3;
            for (uint8_t k = 0; k < 3; ++k)
                slot->bytes[k] = m_.bus.peek((uint16_t)(slot->pc + k));
        }

        // A fresh instruction: forget any conditional cycle matches recorded for the last
        // one. matchCycleBreak fills this from inside step() below (via the pre-access
        // veto), and the boundary code past step() drains it -- so it only ever holds THIS
        // instruction's matches.
        pendingCond_.clear();

        StepResult s;
        try {
            s = master->step(m_.bus);
        } catch (const CycleBreakBefore&) {
            // A pre-access cycle breakpoint fired inside this instruction. The bus
            // threw before touching any board, so no port was read and no byte
            // written -- but the core had already mutated registers on its way to the
            // vetoed cycle (PC stepped past the opcode, an operand fetched, and for a
            // multi-cycle op an EARLIER cycle may have committed). Restore the whole
            // architectural state from the boundary snapshot: PC back onto the
            // instruction, every register pristine. Order-independent -- pair/half
            // aliases restore to the same snapshot value.
            for (size_t i = 0; i < regs.size(); ++i) regs[i].set(slot->regs[i]);

            r.why = StopReason::Breakpoint;
            r.bp = cycleHit_;

            // Arm the one-shot so the next RUN/STEP can get PAST this line: restoring
            // the PC means a bare resume would re-issue the identical cycle and trap
            // forever. See skipArmed_.
            skipArmed_ = true;
            resumeCyclePc_ = cpu->pc();
            break;
        }

        // The instruction retired. If we were resuming through a one-shot, that debt
        // is now paid -- re-arm the veto so the NEXT matching cycle traps normally
        // (including a later pass over this same instruction in a loop).
        if (skipArmed_) skipArmed_ = false;

        ++r.steps;
        r.cycles += s.cycles;

        // EMULATED TIME ADVANCES HERE AND NOWHERE ELSE (DESIGN.md 7.5). Exactly
        // the cycles the CPU said it took -- so the UART's idea of when a
        // character has finished going out is derived from the same instruction
        // stream the guest is timing it with, and the two cannot drift.
        m_.clock.advance(s.cycles);

        // SET BUS UNCLAIMED=HALT: the guest reached an I/O port no board decodes.
        // The bus armed this during the cycle; we stop at the boundary, exactly as a
        // cycle breakpoint does, and for the same reason (DESIGN.md 4.6.1).
        if (m_.bus.takeUnclaimedHalt()) {
            r.why = StopReason::Unclaimed;
            r.addr = m_.bus.haltAddr();
            r.write = m_.bus.haltWasWrite();
            break;
        }

        // BREAK TAPE STOP: a cassette deck reached its auto-stop mark since the last
        // boundary. There is no bus cycle for "the tape ran out", so unlike a MEM/IO
        // breakpoint we POLL the boards here, at the boundary, exactly as the unclaimed
        // halt above does. Every board is drained each pass (read-and-clear keeps their
        // edge latches in step); the event is DECK-NEUTRAL, so any deck reaching its mark
        // fires a BREAK TAPE STOP. Then act just like the PC path below: count the hit,
        // flip trace for a tracepoint, stop otherwise.
        if (watchDeviceEvent) {
            bool tapeStopped = false;
            for (const auto& bd : m_.boards())
                if (bd->takeAutoStop()) tapeStopped = true;
            if (tapeStopped) {
                bool stop = false;
                for (Breakpoint& b : bps_) {
                    if (!b.enabled || b.kind != BreakKind::TapeStop) continue;
                    ++b.hits;
                    if (b.action != BreakAction::Stop) {
                        traceActive_ = (b.action == BreakAction::TraceOn);
                        continue;
                    }
                    r.why = StopReason::TapeStop;
                    r.bp = b.id;
                    stop = true;
                }
                if (stop) break;
            }
        }

        // CONDITIONAL cycle breakpoints (BREAK MEM/IO ... IF|LOADS <expr>). Unlike an
        // unconditional cycle breakpoint -- which stops BEFORE the access, via the veto's
        // throw -- a conditional one let the instruction RETIRE (matchCycleBreak recorded
        // it in pendingCond_ rather than vetoing) and is judged HERE, at the boundary, the
        // one place the registers have a consistent answer. IF judges the inputs: the
        // snapshot this loop took BEFORE stepping (slot->regs). LOADS judges the outputs:
        // the live registers, now the read has landed. See Breakpoint::condWhen.
        if (!pendingCond_.empty()) {
            // A resolver over the PRE-instruction snapshot, for the IF (Before) gate -- the
            // same names as resolveReg, but reading the boundary snapshot rather than the
            // live registers, so "A" is the accumulator as the instruction FOUND it.
            Expr::Resolver resolveBefore =
                [&regs, slot](const std::string& name, uint32_t& outv) -> bool {
                for (size_t i = 0; i < regs.size(); ++i) {
                    if (regs[i].name.size() != name.size()) continue;
                    bool eq = true;
                    for (size_t j = 0; j < name.size(); ++j)
                        if (std::toupper((unsigned char)regs[i].name[j]) !=
                            std::toupper((unsigned char)name[j])) {
                            eq = false;
                            break;
                        }
                    if (eq) {
                        outv = slot->regs[i];
                        return true;
                    }
                }
                return false;
            };
            bool condStop = false;
            for (int id : pendingCond_) {
                for (Breakpoint& b : bps_) {
                    if (b.id != id || !b.enabled || !b.cond) continue;
                    const Expr::Resolver& res =
                        (b.condWhen == CondWhen::After) ? resolveReg : resolveBefore;
                    if (!b.cond->eval(res)) break;   // condition false -- it did not act
                    ++b.hits;
                    // A conditional cycle TRACEPOINT flips trace and runs on, exactly as
                    // the unconditional one does in matchCycleBreak; only a Stop stops.
                    if (b.action != BreakAction::Stop) {
                        traceActive_ = (b.action == BreakAction::TraceOn);
                        break;
                    }
                    r.why = StopReason::Breakpoint;
                    r.bp = b.id;
                    condStop = true;
                    break;
                }
                if (condStop) break;
            }
            if (condStop) break;
        }

        // BREAK <addr>: PC equals X after a step. One comparison, and it knows
        // nothing about what CPU it is asking.
        uint16_t pc = cpu->pc();
        bool hit = false;
        for (Breakpoint& b : bps_) {
            if (!b.enabled || b.kind != BreakKind::Pc) continue;
            if (pc < b.lo || pc > b.hi) continue;
            // A conditional breakpoint that does not hold is not a stop -- and it does
            // not count as a hit either. `hits` means "times it ACTED".
            if (b.cond && !b.cond->eval(resolveReg)) continue;
            ++b.hits;

            // A tracepoint flips TRACE and we keep going. It must NOT break out of
            // this loop: an ordinary breakpoint at the same PC still has to stop, and
            // a tracepoint that swallowed it would be a debugger hiding a breakpoint
            // from you. PC lands here BEFORE the instruction at this address runs, so
            // BREAK 100 TRACE ON traces the instruction at 100, and BREAK 200 TRACE
            // OFF does not trace the one at 200 -- the region is [100,200).
            if (b.action != BreakAction::Stop) {
                traceActive_ = (b.action == BreakAction::TraceOn);
                continue;
            }

            r.why = StopReason::Breakpoint;
            r.bp = b.id;
            hit = true;
            break;
        }
        if (hit) break;

        // STEP-OVER's one-shot target: PC reached the return address of a JSR/BSR
        // the operator asked NEXT to run through. Same one comparison a PC
        // breakpoint is, but off the books -- see setStepTarget. A real breakpoint
        // above wins if the callee happens to hit one first, which is what you want.
        if (stepTarget_ >= 0 && pc == (uint16_t)stepTarget_) {
            r.why = StopReason::StepTarget;
            break;
        }

        // WAI with nobody to wake it is the end of the program. WAI with a live
        // interrupt source is NOT -- the CPU is parked, time still passes, and the
        // board that will interrupt it is clocked by the very cycles we are
        // still counting.
        //
        // AND NEITHER IS A WAI WITH A DEADLINE STILL ON THE BOOKS, which is the half
        // this used to get wrong. The old code asked "is anyone pulling IRQ RIGHT
        // NOW?" and called that "can anything ever wake it?". They are not the same
        // question, and a 6850 console tells them apart: enable its transmit interrupt,
        // send a character, and halt -- which is an entirely ordinary thing for a driver
        // to do. At the instant of the WAI nothing is pulling IRQ, because the
        // character is still going out. The interrupt is two thousand cycles away
        // and absolutely certain to arrive. The old test declared that program
        // finished, and it would have been right about the wire and wrong about the
        // machine.
        //
        // The clock knows better: if anything at all is still scheduled, this
        // machine has a future. And it terminates -- a queue with nothing in it is
        // the honest definition of a machine that will never do anything again.
        if (s.status == RunStatus::Halted && !m_.bus.intPending() && m_.clock.queued() == 0) {
            r.why = StopReason::Halted;
            break;
        }

        if (g_stopRequest.load()) {
            r.why = StopReason::StopRequested;
            break;
        }

        if (maxSteps && r.steps >= maxSteps) {
            r.why = StopReason::Steps;
            break;
        }
    }

    m_.running = false;
    disarmObserver();
    r.pc = cpu->pc();
    return r;
}

} // namespace swtpc
