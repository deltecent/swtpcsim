#include "test.h"

#include "boards/s100-memory.h"
#include "core/debug.h"
#include "core/expr.h"
#include "core/machine.h"
#include "cpu/cpu.h"

#include <sstream>

using namespace swtpc;

namespace {

// The debugger is CPU-agnostic -- it works over the bus cycle stream and the core's
// reflected registers, and never learns what a 6800 is. The 6800 is memory-mapped:
// there is no port I/O and no IN/OUT, so the port-I/O breakpoint (BREAK IO) and the
// 8080's RST-vector interrupt model have no analogue here, and this suite covers what
// a memory-mapped machine actually produces -- PC and BREAK MEM breakpoints, plus the
// generic history/trace/tracepoint machinery.

struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    CpuCore* cpu = nullptr;

    Rig() {
        std::string err;
        // THE WIRE IS CHECKED ON EVERY INSTRUCTION. intPending() reads a cached
        // wire-OR count; this re-derives it from every board's assertsInt() and
        // aborts on the first disagreement. A board that moved its interrupt pin
        // and forgot to say so hangs the guest forever, and "the emulator locks up
        // sometimes" is a bug worth a week. It is not left to trust.
        m.bus.setVerify(true);

        Board* b = m.add("memory", "mem0", err);
        mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();
        m.add("6800", "cpu0", err);
        cpu = m.cpu();
        // POWER-ON arms the 6800's deferred restart fetch: the first step reloads PC
        // from the reset vector [FFFE/FFFF]. RAM is zero-filled here, so that vector
        // is 0000 -- the first instruction runs at 0000, exactly where the code is
        // loaded and where each section sets the PC.
        cpu->reset(Reset::PowerOn);
    }
    void load(std::initializer_list<uint8_t> code, uint16_t at = 0) {
        for (uint8_t byte : code) m.bus.memWrite(at++, byte);
    }
};

// Build a breakpoint condition against a machine's real reflected registers -- the
// same path the monitor takes, so the test exercises the CPU-agnostic seam and not
// a hand-rolled resolver.
std::shared_ptr<const Expr> cond(CpuCore* cpu, const std::string& src) {
    auto regs = cpu->registers();
    auto known = [regs](const std::string& n) {
        for (const RegDef& rd : regs)
            if (rd.name == n) return true;
        return false;
    };
    std::string err;
    return Expr::parse(src, known, err);
}

} // namespace

void test_debug() {
    SECTION("the debugger -- it lives in Machine, and knows nothing about a 6800");

    Rig g;

    // A tight loop: INCA ; JMP 0. It never ends on its own, which is exactly what
    // a breakpoint is for.
    g.load({0x4C, 0x7E, 0x00, 0x00});
    g.cpu->setPc(0);

    RunResult r = g.m.debug.run(5);
    CHECK(r.why == StopReason::Steps, "run(5) runs five instructions and says why it stopped");
    CHECK(r.steps == 5, "five");
    // INCA, JMP, INCA, JMP, INCA -- three at 2 cycles and two at 3.
    CHECK(r.cycles == 3 * 2 + 2 * 3, "and counts their cycles honestly: 12");

    SECTION("BREAK <addr> -- one comparison against a reflected register");

    g.cpu->setPc(0);
    int bp = g.m.debug.add(BreakKind::Pc, 0x0001, 0x0001);
    r = g.m.debug.run(0);   // 0 == until something stops us
    CHECK(r.why == StopReason::Breakpoint, "it stops");
    CHECK(r.bp == bp, "at the breakpoint we set");
    CHECK(r.pc == 0x0001, "with the PC exactly there");
    CHECK(g.m.debug.breakpoints()[0].hits == 1, "and the hit is counted");

    std::string err;
    CHECK(g.m.debug.remove(bp, err), "and it can be removed");
    CHECK(!g.m.debug.remove(99, err), "removing one that isn't there fails, and says so");
    CHECK(!err.empty(), "with a message");

    SECTION("BREAK MEM -- bus observers, not CPU features");

    // These watch the CYCLE STREAM every board already sees. So they are not the
    // CPU's business, they work on any processor, and they would catch a DMA
    // transfer just as readily -- which is the whole reason they are built this way.
    Rig w;
    w.load({0x86, 0x41,          // LDAA #41
            0xB7, 0x20, 0x00,    // STAA 2000
            0x3E});              // WAI
    w.cpu->setPc(0);
    int wb = w.m.debug.add(BreakKind::MemWrite, 0x2000, 0x2000);
    RunResult wr = w.m.debug.run(0);
    CHECK(wr.why == StopReason::Breakpoint && wr.bp == wb, "a write to 2000 stops the machine");
    CHECK(wr.pc == 0x0002, "with the PC ON the STAA -- stopped BEFORE it, not at the next boundary");
    CHECK(w.m.bus.memRead(0x2000) == 0x00, "and the write never happened -- the byte is not there yet");

    // A READ breakpoint on the same address does NOT fire on that write. The cycle
    // type is part of the match, or every BREAK MEM R would trip on its own store.
    Rig q;
    q.load({0x86, 0x41, 0xB7, 0x20, 0x00, 0x3E});
    q.cpu->setPc(0);
    q.m.debug.add(BreakKind::MemRead, 0x2000, 0x2000);
    RunResult qr = q.m.debug.run(0);
    CHECK(qr.why == StopReason::Halted, "a read breakpoint ignores a write, and the program WAITs");

    // A register reader over the reflected register set -- the CPU-agnostic seam the
    // debugger itself uses, so the test never learns what a 6800 is.
    auto regOf = [](CpuCore* c, const std::string& name) -> uint32_t {
        for (const RegDef& rd : c->registers())
            if (rd.name == name) return rd.get();
        return 0xFFFFFFFF;  // no such register -- an assertion below will fail loudly
    };

    SECTION("a cycle breakpoint unwinds the WHOLE instruction, not just its own cycle");

    // BREAK MEM R on the OPERAND byte of a 3-byte op. The opcode fetch does not match
    // (wrong address); the operand read does -- and the machine stops with the PC on
    // the OPCODE, the whole instruction rolled back, X never loaded.
    Rig mid;
    mid.load({0xCE, 0x12, 0x34, 0x3E});  // LDX #1234 ; WAI
    mid.cpu->setPc(0);
    mid.m.debug.add(BreakKind::MemRead, 0x0001, 0x0001);
    RunResult mrr = mid.m.debug.run(0);
    CHECK(mrr.why == StopReason::Breakpoint, "a read of the operand byte stops the machine");
    CHECK(mrr.pc == 0x0000, "with the PC on the OPCODE, not the operand -- the instruction unwound");
    CHECK(regOf(mid.cpu, "X") == 0x0000, "and X was never loaded");

    SECTION("RESUME past a cycle breakpoint -- the one-shot lets you step through it");

    // Sitting ON the instruction means a bare RUN would re-issue the identical cycle
    // and trap forever. The one-shot lets exactly that instruction through, once, so a
    // loop that returns to it traps again on the NEXT pass -- and never double-counts.
    Rig res;
    res.m.bus.memWrite(0x2000, 0x77);  // a byte the LDAA will pick up
    res.load({0xB6, 0x20, 0x00,        // LDAA 2000
              0x7E, 0x00, 0x00});      // JMP 0
    res.cpu->setPc(0);
    res.m.debug.add(BreakKind::MemRead, 0x2000, 0x2000);
    RunResult rr = res.m.debug.run(0);
    CHECK(rr.why == StopReason::Breakpoint && rr.pc == 0x0000, "first: stops before the LDAA");
    CHECK(res.m.debug.breakpoints()[0].hits == 1, "one hit so far");

    rr = res.m.debug.run(0);   // resume -- must get PAST the LDAA this time
    CHECK(rr.why == StopReason::Breakpoint && rr.pc == 0x0000,
          "resume runs the LDAA, loops, and traps on the NEXT pass -- not stuck on this one");
    CHECK(regOf(res.cpu, "A") == 0x77, "the LDAA executed on resume: A took the byte at 2000");
    CHECK(res.m.debug.breakpoints()[0].hits == 2, "the resumed cycle was not recounted -- the next pass is hit 2");

    SECTION("an unarmed run (only a PC breakpoint) never touches the cycle path");

    // No cycle breakpoint means no veto is installed, so the read executes untouched --
    // proof the machinery is inert unless a MEM breakpoint asked for it.
    Rig un;
    un.m.bus.memWrite(0x2000, 0x77);
    un.load({0xB6, 0x20, 0x00,    // LDAA 2000
             0x3E});              // WAI
    un.cpu->setPc(0);
    un.m.debug.add(BreakKind::Pc, 0x0003, 0x0003);
    RunResult ur = un.m.debug.run(0);
    CHECK(ur.why == StopReason::Breakpoint && ur.pc == 0x0003, "it stops at the PC breakpoint");
    CHECK(regOf(un.cpu, "A") == 0x77, "and the read really ran -- no veto interfered with the cycle");

    SECTION("Stop and TraceOn on the SAME cycle: it stops, and the vetoed cycle is not traced");

    Rig trc;
    trc.load({0xB6, 0x20, 0x00, 0x3E});  // LDAA 2000 ; WAI
    trc.cpu->setPc(0);
    std::ostringstream ts;
    trc.m.debug.traceTo(&ts, 0);  // sink everything...
    trc.m.debug.traceOff();       // ...but start off; the tracepoint will turn it on
    trc.m.debug.add(BreakKind::MemRead, 0x2000, 0x2000, nullptr, BreakAction::TraceOn);
    int sb = trc.m.debug.add(BreakKind::MemRead, 0x2000, 0x2000);  // a Stop on the same cycle
    RunResult trcr = trc.m.debug.run(0);
    CHECK(trcr.why == StopReason::Breakpoint && trcr.bp == sb, "the Stop wins the report");
    CHECK(ts.str().empty(), "and the cycle was vetoed before it could be recorded -- nothing traced");

    SECTION("a breakpoint is armed only while the machine RUNS");

    // The monitor's own DUMP and DEPOSIT are REAL bus cycles -- that is the point
    // of them, and it is what made the bus testable with no CPU. So an always-armed
    // BREAK MEM W would "fire" on the operator's own DEPOSIT, with no program
    // running to stop and nothing sensible to report. Breaking is something that
    // happens to a RUNNING machine.
    Rig d;
    d.m.debug.add(BreakKind::MemWrite, 0x3000, 0x3000);
    d.m.bus.memWrite(0x3000, 0x99);   // a front-panel DEPOSIT, in effect
    CHECK(d.m.debug.breakpoints()[0].hits == 0, "a DEPOSIT while stopped does not trip it");
    CHECK(d.m.bus.memRead(0x3000) == 0x99, "and the DEPOSIT worked, obviously");

    SECTION("no CPU is a REAL machine, and the debugger says so rather than crashing");

    Machine bare;
    std::string e2;
    bare.add("memory", "mem0", e2);
    RunResult br = bare.debug.run(1);
    CHECK(br.why == StopReason::NoCpu, "GO on a backplane with no processor is a FACT, not a crash");
    CHECK(bare.cpu() == nullptr, "there is no core to ask");
    CHECK(bare.isa().empty(), "so the machine has no instruction set, and DISASM must be told one");

    SECTION("BREAK <addr> IF <expr> -- a condition over the reflected registers");

    // INCA ; JMP 0. At 0001 (after the INCA) A has just been bumped, so a condition
    // on A picks out one pass of the loop -- and the debugger never learns what a
    // 6800 is: it reads A by NAME.
    Rig c;
    c.load({0x4C, 0x7E, 0x00, 0x00});
    c.cpu->setPc(0);
    int cb = c.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c.cpu, "A==3"));
    RunResult cr = c.m.debug.run(0);
    CHECK(cr.why == StopReason::Breakpoint && cr.bp == cb, "it stops");
    CHECK(cr.pc == 0x0001, "at the address");
    for (const RegDef& rd : c.cpu->registers())
        if (rd.name == "A") CHECK(rd.get() == 3, "with the condition actually holding: A==3");

    // A CONDITION THAT NEVER HOLDS IS NOT A STOP -- and it does not count as a hit,
    // because `hits` means "times it stopped you", not "times the PC passed by".
    Rig c2;
    c2.load({0x4C, 0x7E, 0x00, 0x00});
    c2.cpu->setPc(0);
    c2.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c2.cpu, "A==0"));  // A is never 0 at 0001 within the budget
    RunResult cr2 = c2.m.debug.run(50);
    CHECK(cr2.why == StopReason::Steps, "a never-true condition never stops the run");
    CHECK(c2.m.debug.breakpoints()[0].hits == 0, "and never counts a hit");

    // A COMPOUND CONDITION. INCA wraps FF->00 and sets the zero flag on the wrap.
    // Stop the first time A is zero AND Z is set.
    Rig c3;
    c3.load({0x4C, 0x7E, 0x00, 0x00});
    c3.cpu->setPc(0);
    for (const RegDef& rd : c3.cpu->registers())
        if (rd.name == "A") rd.set(0xFE);   // so the next INCA gives FF, then 00
    int c3b = c3.m.debug.add(BreakKind::Pc, 0x0001, 0x0001, cond(c3.cpu, "A==0 && Z==1"));
    RunResult cr3 = c3.m.debug.run(0);
    CHECK(cr3.why == StopReason::Breakpoint && cr3.bp == c3b, "A==0 && Z==1 stops on the wrap");
    for (const RegDef& rd : c3.cpu->registers())
        if (rd.name == "A") CHECK(rd.get() == 0, "and A is indeed zero there");

    // describe() carries the condition, which is what BREAK lists and every stop
    // prints.
    CHECK(c.m.debug.breakpoints()[0].describe().find("if A==3") != std::string::npos,
          "the condition shows in describe()");

    SECTION("HISTORY -- a flight recorder of bus cycles, fed by the same observer");

    Rig hh;
    hh.load({0x86, 0x41,           // LDAA #41
             0xB7, 0x20, 0x00,     // STAA 2000
             0x3E});               // WAI
    hh.cpu->setPc(0);
    hh.m.debug.run(2);   // just the LDAA and the STAA, so the store is the last data cycle

    auto recent = hh.m.debug.history(4);
    CHECK(!recent.empty(), "there is history after a run");
    // The last data cycle of this run is the STAA's write to 2000. It is in there,
    // and it is a WRITE, and the byte is right -- the recorder saw the same cycle the
    // breakpoint would.
    bool sawWrite = false;
    Debugger::CycleRec theWrite;
    for (const auto& rec : recent)
        if (rec.type == Cycle::MemWrite && rec.addr == 0x2000 && rec.data == 0x41) {
            sawWrite = true;
            theWrite = rec;
        }
    CHECK(sawWrite, "the STAA's write to 2000 is on the tape");

    // WHO drove it, WHO answered. Every cycle originates at the CPU (rendered "cpu"),
    // and the memory board answered -- its handle resolves through the debugger's name
    // table to the board's id.
    const auto& handles = hh.m.debug.boardHandles();
    CHECK(theWrite.responder >= 0 && (size_t)theWrite.responder < handles.size(),
          "the responder is an interned handle into the name table");
    CHECK(handles[(size_t)theWrite.responder] == "mem0",
          "and it resolves to the board that answered -- mem0");

    // The rendered line names both, and it is the SAME renderer TRACE uses.
    std::string line = Debugger::formatCycle(theWrite, handles);
    CHECK(line.find("cpu") != std::string::npos, "the line shows who drove it -- cpu");
    CHECK(line.find("-> mem0") != std::string::npos, "and who answered it -- mem0");

    // Oldest-first, and bounded by what actually ran.
    auto few = hh.m.debug.history(2);
    CHECK(few.size() == 2, "history(n) returns n when it has them");
    CHECK(hh.m.debug.history(100000).size() < 100000, "and never more than it holds");

    SECTION("CPU HISTORY -- the instruction flight recorder, the sibling of the bus ring");

    Rig ih;
    ih.load({0x86, 0x41,           // LDAA #41
             0xB7, 0x20, 0x00,     // STAA 2000
             0x3E});               // WAI
    ih.cpu->setPc(0);
    ih.m.debug.run(0);

    // Three instructions retired -- LDAA, STAA, WAI -- recorded oldest first. Each carries
    // the PC it ran at and the opcode byte that ran there, so a later overwrite of the code
    // cannot change what the tape says executed.
    auto insns = ih.m.debug.insnHistory(8);
    CHECK(insns.size() == 3, "exactly the three instructions that ran");
    CHECK(insns.front().pc == 0x0000 && insns.front().bytes[0] == 0x86, "LDAA #41 at 0000");
    CHECK(insns.back().pc == 0x0005 && insns.back().bytes[0] == 0x3E, "WAI at 0005");

    // The register snapshot is the state BEFORE each instruction: A is still 0 at the LDAA
    // and 41 by the time the STAA runs, because the LDAA has landed. Read A by its own name
    // out of the core's register order, the way the monitor's line does.
    auto regs = ih.cpu->registers();
    size_t ai = 0;
    bool haveA = false;
    for (size_t i = 0; i < regs.size(); ++i)
        if (regs[i].name == "A") {
            ai = i;
            haveA = true;
        }
    CHECK(haveA, "the 6800 exposes an A register to read back");
    CHECK(insns[0].regs[ai] == 0x00, "A is 0 at the LDAA -- the state BEFORE it runs");
    CHECK(insns[1].regs[ai] == 0x41, "and 41 by the STAA -- the LDAA has landed");

    // Oldest-first, bounded by what ran, and clearable -- exactly like the bus ring.
    CHECK(ih.m.debug.insnHistory(2).size() == 2, "insnHistory(n) returns n when it has them");
    CHECK(ih.m.debug.insnHistory(100000).size() < 100000, "and never more than it holds");
    ih.m.debug.clearInsnHistory();
    CHECK(ih.m.debug.insnHistory(4).empty(), "clearInsnHistory wipes the tape");

    SECTION("TRACE -- every cycle to a sink");

    Rig tt;
    tt.load({0x86, 0x41,           // LDAA #41
             0xB7, 0x20, 0x00,     // STAA 2000  (a memory write)
             0x3E});               // WAI
    tt.cpu->setPc(0);

    // No mask: everything shows, the memory write and the fetches alike.
    std::ostringstream all;
    tt.m.debug.traceTo(&all, 0);
    tt.m.debug.run(0);
    tt.m.debug.traceOff();
    CHECK(all.str().find("MW") != std::string::npos, "the memory write is traced");
    CHECK(all.str().find("MR") != std::string::npos, "and the fetches");

    SECTION("TRACEPOINTS -- a breakpoint whose ACTION is TRACE, and does not stop");

    // The headline: trace a REGION of a program instead of all of it.
    //
    //   0000  LDAA #0        before  -- not traced
    //   0002  INCA           before  -- not traced
    //   0003  INCA           TRACE ON lands here
    //   0004  STAA 2000      traced
    //   0007  WAI            TRACE OFF lands here -- not traced
    Rig tp;
    tp.load({0x86, 0x00, 0x4C, 0x4C, 0xB7, 0x20, 0x00, 0x3E});
    tp.cpu->setPc(0);

    std::ostringstream region;
    tp.m.debug.traceTo(&region, 0);
    tp.m.debug.traceOff();  // CONFIGURED but not running -- what a tracepoint needs
    CHECK(tp.m.debug.traceConfigured(), "TRACE OFF keeps the sink");
    CHECK(!tp.m.debug.tracing(), "but is not tracing");

    tp.m.debug.add(BreakKind::Pc, 3, 3, nullptr, BreakAction::TraceOn);
    tp.m.debug.add(BreakKind::Pc, 7, 7, nullptr, BreakAction::TraceOff);
    RunResult tr = tp.m.debug.run(0);

    // It ran to the WAI: a tracepoint acts and the machine carries on. If this
    // reports Breakpoint, a tracepoint stopped the machine and the feature is a
    // breakpoint wearing a costume.
    CHECK(tr.why == StopReason::Halted, "a tracepoint does NOT stop the machine");
    CHECK(region.str().find("0003 = 4C") != std::string::npos, "the region's first fetch traced");
    CHECK(region.str().find("2000 = 02") != std::string::npos, "and the store inside it");
    CHECK(region.str().find("0000 = 86") == std::string::npos, "nothing from before TRACE ON");
    CHECK(region.str().find("0007 = 3E") == std::string::npos, "nor the WAI that turned it off");
    CHECK(!tp.m.debug.tracing(), "and it left the trace off, where the region ended");
    CHECK(tp.m.debug.breakpoints()[0].hits == 1, "hits counts a tracepoint firing");
    CHECK(tp.m.debug.breakpoints()[1].hits == 1, "both of them");

    // A CYCLE tracepoint -- safe where IF is not, because it reads no registers. And
    // the cycle that TRIGGERED it is traced: the observer matches before it emits, so
    // BREAK MEM W 2000 TRACE ON puts the write to 2000 at the TOP of the trace rather
    // than one cycle above it, which would be a trace that omits its own reason.
    Rig tc;
    tc.load({0x86, 0x41, 0xB7, 0x20, 0x00, 0x4C, 0x3E});
    tc.cpu->setPc(0);
    std::ostringstream fromWrite;
    tc.m.debug.traceTo(&fromWrite, 0);
    tc.m.debug.traceOff();
    tc.m.debug.add(BreakKind::MemWrite, 0x2000, 0x2000, nullptr, BreakAction::TraceOn);
    CHECK(tc.m.debug.run(0).why == StopReason::Halted, "a cycle tracepoint does not stop either");
    std::string firstLine = fromWrite.str().substr(0, fromWrite.str().find('\n'));
    CHECK(firstLine.find("MW") != std::string::npos && firstLine.find("2000 = 41") != std::string::npos,
          "the write that turned tracing on is the FIRST line of the trace");

    // A tracepoint must not SHADOW an ordinary breakpoint at the same place. If the
    // match loop broke out on the tracepoint, this run would sail past bp 2.
    Rig tb;
    tb.load({0x86, 0x41, 0x4C, 0x4C, 0x3E});
    tb.cpu->setPc(0);
    std::ostringstream both;
    tb.m.debug.traceTo(&both, 0);
    tb.m.debug.traceOff();
    tb.m.debug.add(BreakKind::Pc, 3, 3, nullptr, BreakAction::TraceOn);
    int stopper = tb.m.debug.add(BreakKind::Pc, 3, 3);
    RunResult tbr = tb.m.debug.run(0);
    CHECK(tbr.why == StopReason::Breakpoint, "an ordinary breakpoint at a tracepoint's PC stops");
    CHECK(tbr.bp == stopper, "and it is the STOP one that is reported");
    CHECK(tb.m.debug.tracing(), "while the tracepoint still did its job");

    // describe() carries the action -- that is what BREAK's listing shows.
    CHECK(tb.m.debug.breakpoints()[0].describe().find("trace on") != std::string::npos,
          "describe() says trace on");
    CHECK(tb.m.debug.breakpoints()[1].describe().find("trace") == std::string::npos,
          "and an ordinary breakpoint does not mention tracing at all");
}
