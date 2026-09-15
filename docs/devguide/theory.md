# Theory of operation

This chapter is about what is actually going on inside the machine. It assumes you have the
source, and it names files: `src/core/bus.h`, `src/core/board.h`, `src/core/value.h`,
`src/core/machine.h`, and [`DESIGN.md`](../../DESIGN.md), which is the document all of this
was argued out in.

The next chapter builds a board — a lamp latch on port `FF`. Everything here is what that
board is standing on.

## The whole architecture, in one sentence

> **Boards respond to bus cycles. The CPU originates them.**

That is not a slogan. It is the shape of the code, and everything else in this chapter is a
consequence of it.

The two concepts are separate types, and they live together in `src/core/bus.h` rather than
with the CPU:

```cpp
class BusMaster {
public:
    virtual StepResult step(Bus&) = 0;
};
```

A CPU board is **both**: `Cpu6800Board : public Board, public BusMaster`. It is a board you can
pull out with your hand (`BOARDS REMOVE cpu0` works, and a machine with no processor in it is
a real machine), and it is also the thing that drives cycles onto the backplane.

**Making the CPU a `BusMaster` — not a bus special case — is what keeps the model clean.** It is
how the monitor *finds* and *steps* the processor generically, and it is the door a DMA card
would walk through unchanged: a `Board` that *becomes* a `BusMaster` when it is granted the bus,
driving the very same cycles through the very same interface the CPU uses. **DMA would never be a
special path bolted onto the bus.** (No 6800 machine here masters the bus from anything but the
CPU; the framework's S-100 `pHOLD`/`pHLDA` DMA arbitration was removed — DESIGN.md §4.5 — but the
door it used is still open.)

It is also why `BREAK MEM W 0100` catches a write no matter what drove it. A cycle is a cycle;
nothing on the backplane records who originated it. There is deliberately **no `origin` field**
on `BusCycle`, and `src/core/bus.h` says why: a real backplane cycle carries no such tag, which
is exactly why an operator's `DEPOSIT` is indistinguishable from a CPU write, and why a real ROM
ignores both.

## The bus carries signals. It does not invent behavior.

The second rule, and the one the bus header exists to enforce:

> The bus arbitrates no overlay, vectors no interrupt, knows no bank, and has never heard of
> ROM.

It picks no winner between two cards. It does not hand the CPU an interrupt vector. It does not
route reads to one board and writes to another. Every one of those lives in a board.

When you are tempted to add `if (board is a ROM)` to `Bus`, **you have found a bug in your
board instead.** There is exactly one thing the bus does that no board does, and §4.6.1 of
DESIGN.md pins it down: it supplies `0xFF` when nobody answered. That is the entire bus/board
overlap, and it stays that size.

> **This is the SS-50 bus, and the 6800 has no I/O space.** The `BusCycle` above carries a
> cycle type, a 16-bit address, and a data byte — and the type is only ever `MemRead` or
> `MemWrite`, because the 6800 reaches every device through memory, with the same loads and
> stores it uses for RAM. There is no IN/OUT, no port table, no separate I/O strobe. A device
> is just an address that a board answers, and the SS-30 I/O bus is a window of such addresses
> (`$8000`–`$801F`) hanging off the SS-50 motherboard. **DESIGN.md §4.0** argues out where
> that line is drawn.

## A bus cycle, end to end

```cpp
enum class Cycle { MemRead, MemWrite };

struct BusCycle {
    Cycle type = Cycle::MemRead;
    uint16_t addr = 0;   // the address on the bus
    uint8_t data = 0;

    bool isWrite() const { return type == Cycle::MemWrite; }
};
```

Two cycle types — a read and a write, both to memory, because on the 6800 there is no other
kind.

`data` is valid on a write. **On a read it is zero while the cycle is in flight** — nobody has
driven the bus yet when `decodes()` and `read()` are asked, so there is nothing honest to put
there — and it is **back-filled** with the byte that came back (a board's, or the floating bus's
`0xFF`) before the observers see it. `Bus::settle()`.

The bus runs each cycle in two passes:

| Pass | What it does |
|---|---|
| 1 | Ask who `decodes()` this cycle. Exactly one should answer. Nobody → `0xFF`. |
| 2 | `settle()`: hand the completed cycle to every observer watching from outside the backplane (the debugger, the tracer). |

Running a decode and letting the watchers see the result. **No decisions.**

## The decode is cached, because on real hardware it is wired

A board's address decoder is combinational logic — a PAL, a row of gates — wired to the address
lines and to the R/W̅ line that says which way the byte is going. It does not *answer a question*
once per cycle. It **settles**, and it only changes when something latches: a bank strap, an
enable/disable, a board pulled from the backplane.

So the bus asks the same questions of the same boards in slot order, and asks them **once**,
storing the answer in two tables:

```cpp
struct Slot {
    Board* who = nullptr;   // the single board that drives. null: nobody -> floats 0xFF
    bool slow = false;      // more than one driver. Contention: take the exact path.
};

Slot memRead_[256], memWrite_[256];
```

One entry per **256-byte page**, per direction. A decode is now a table lookup, not a walk over
every board in the machine.

The board still owns the entire decision. The bus still invents nothing. It just stopped asking
sixty-five thousand times a second — and stopped asking boards questions they are not wired for.
A board that only decodes a write — a plain output latch — used to be asked to decode every read
too, and its first act was to throw the question away. **It has no connection to the read
direction. It is not in that conversation.**

Three consequences follow, and all three are things you can get wrong.

### `decodes()` must be pure

```cpp
virtual bool decodes(const BusCycle&) const { return false; }
```

**Same board state, same cycle → same answer, and no side effects.** `decodes()` is
combinational. The bus may call it more than once per cycle — inside its own decode and again
when it re-derives the cache — and then it *caches the result*.

A `decodes()` with a side effect in it is a bug that will not surface for a month. It will fire
the right number of times on the day you write it and the wrong number of times forever after,
because the number of times the bus asks is an implementation detail of a cache. **Latch nothing
here** — a `decodes()` has no business changing anything about the board.

### If your decode changes, say so

```cpp
void decodeChanged();   // "my decode just changed" -- sets a dirty flag on the bus
```

A bank strap moved. A chip came out of a socket. The board went
`enabled = false`. **Call `decodeChanged()`.** Forget, and the tables go stale and the machine
lies quietly, which is the worst failure mode there is.

You mostly get this for free: `setProperty()` — the one path by which any property is *ever*
set, from `SET`, from TOML, from `BOARDS ADD`, from MCP — calls `configChanged()` on the board
after every successful set, and the default `configChanged()` calls `decodeChanged()` and
`intChanged()`. You call it by hand for changes that do not come through a property: a guest
store that moves a bank strap, a boot ROM switching itself out.

And it is not left to trust. `Bus::setVerify(true)` re-derives the decode the slow way on every
single cycle and screams the moment it disagrees with the table. The unit suites run with it on
permanently. **It is a proof, not a path** — it is slower than the code it replaced, and that is
fine.

### The bus routes by CYCLE TYPE, not just by address

Two tables, not one. That is not an optimization detail — it is the model:

**A board is wired to the address lines and to R/W̅.** A ROM famously does not decode a
write *at all*; it does not reject the write, or ignore it, or log it — it never answers the
cycle. So "who answers here" has a **different answer for a read than for a write**, and that
falls out of the model rather than being bolted onto it.

The consequence you will use immediately: **one address can mean two different things by
direction, with no contention whatsoever.** That is not a trick. It is what the 6850 ACIA does
on the console board: a *read* of `$8004` is its status register and a *write* of `$8004` is
its control register — one address, two registers, told apart by R/W̅. `src/boards/swtpc-mps.cpp`
decodes both directions of the slot and its `read()`/`write()` hand back the right register for
each.

The other half of the same coin is a board that answers only *one* direction. An output latch —
the next chapter's lamp board — decodes the **write** at its address and leaves the read alone:

```cpp
bool decodes(const BusCycle& c) const override {
    return enabled_ && c.type == Cycle::MemWrite && c.addr == addr_;
}
```

A load from that address **is not the latch's** — it goes unclaimed and floats to `0xFF`, which
is precisely what a real output-only port does. So the read at that address is free space, and
that is where the next chapter's lamp board lives.

### `decodeIsPageUniform()` — when a card decodes a low address line

```cpp
virtual bool decodeIsPageUniform() const { return true; }
```

Memory decode is cached one entry per 256-byte page, and that is a **contract on `decodes()`**.
Nearly every board can keep it: memory decoding comes off the *high* address lines, and a
board selected at 1K or 4K granularity answers a whole page or none of it.

A board that decodes only *part* of a page says `false` to `decodeIsPageUniform()`. The MP-S
serial board is the real one — it answers at `8004`-`8007`, four addresses inside page `80`, and
nothing else — so some addresses of that page are its and most are not. Say `false` and the bus
probes every address of any page whose answer is not uniform, serving it by the exact, uncached
two-pass path. **You lose nothing but the cache, and only on the pages you actually touch.**

### The cycle stream — the debugger observes from outside

**Every cycle is on the backplane. That is what a backplane IS** — the address bus is not
addressed *to* anyone, it is simply present. The debugger watches that same stream from
**outside** the backplane through `Bus::observe()`:

```cpp
using Observer = std::function<void(const BusCycle&)>;
int observe(Observer fn);   // returns a handle; unobserve(handle) removes it
```

An observer is called **once per cycle, after it completes**, with `data` back-filled. That one
hook is the entire implementation of `BREAK MEM`, `TRACE` and `HISTORY` — they are questions
about bus cycles, answered at the bus, so they cost the cores nothing and a future 6809 inherits
them for free. The bus is not *notifying* anyone: the cycle was on the backplane the whole time,
and an observer is just a probe clipped onto the wires.

(A `BREAK MEM` must stop the machine *before* the access, with the PC still on the instruction —
which an after-the-fact observer cannot do — so that one case rides a separate hook, the
pre-access veto, `Bus::setPreAccessVeto`. `TRACE` and `HISTORY` only record, so they ride the
observer.)

### `peek()` — look without touching

```cpp
virtual bool peek(uint16_t addr, uint8_t& out) const { return false; }
```

**A `read()` may CONSUME.** A read from a 6850's data register takes the byte and the guest never
sees it again. So `DISASM`, `TRACE` and the debugger's register display are built on `peek()`,
never on a read — a disassembler that ate the console's input the first time someone
disassembled a page with a 6850 mapped into it would be a debugger you could not trust.

`peek()` runs the same decode, so a board that is disabled or banked away is invisible to it
exactly as it is to a real read. It just never strobes anybody.

A board that cannot answer without side effects returns `false`, **and that is an honest answer,
not a failure**: the byte on a real bus is only defined *during* a cycle. The caller shows `FF`,
which is what the bus would have floated to.

## The floating bus — one rule, three consequences

> **If no board drives the bus, it floats high. Every read of an unmapped address or port
> returns `0xFF`. Writes go nowhere.**

One rule. Two things that would otherwise each need a special case fall straight out of it:

| Situation | What happens | Why it matters |
|---|---|---|
| Unpopulated memory | reads `0xFF` | Period software **sizes memory by reading**. Return `0x00` and every machine looks like it has 64K, and software builds itself wrong. |
| An empty SS-30 slot | reads `0xFF` | A guest probing for a device that is not there gets the answer real hardware gives it. |

Model the floating bus honestly once and both are free. Fake either one and you will
fake the other differently.

**Therefore no board may ever manufacture `0xFF`,** and in particular no board may seed its own
store with it. A RAM chip does not power up holding `FF`; it powers up holding whatever it feels
like, which is what `fill = random` is for. Seed a board's store with `FF` and `DUMP` shows `FF`
for a board whose RAM is fine, `FF` for a board whose RAM was never filled, and `FF` for a board
that **isn't in the machine**. One symptom, three causes. The moment a board can produce `FF`,
the only signal the bus has stops being a signal. `tests/test_boundary.cpp` enforces it.

## Interrupts

One wire's worth of interface, and it obeys the same rules as `decodes()`.

```cpp
virtual bool assertsInt() const { return false; }   // pulling the IRQ line?
void         intChanged();                           // "my pin may have moved"
```

The 6800 has one maskable interrupt line, **IRQ**, plus a non-maskable **NMI** and the software
**SWI**. All three vector through fixed locations at the top of memory (IRQ `$FFF8`, SWI
`$FFFA`, NMI `$FFFC`). A board that wants to interrupt pulls the IRQ line and holds it; the CPU
reads its handler address out of `$FFF8` and takes it when the `I` mask is clear. The bus
carries a level and never invents a vector — there is no acknowledge cycle and no card that
drives the data bus during one. (That was the S-100 lineage; see the heritage note in
DESIGN.md §4.4.)

### An interrupt is a LEVEL, not an event

A UART with a character waiting and its receive interrupt enabled says `true`, **and keeps
saying `true` until the guest reads the character.** There is no queue, so a board cannot
"lose" an interrupt — there was never a queue to lose it from.

`assertsInt()` is **combinational and pure**, exactly like `decodes()`. It reports the settled
state of a pin, computed from the state of the chip and nothing else. It does not advance a
receiver, take a byte off a line, or do any work the guest has not paid for.

**It used to do all three**, and there was a whole section of DESIGN.md defending it. The 6850
has to notice a character has finished arriving, which happens on the chip's own clock with no
help from the CPU — and being asked `assertsInt()` was the only thing that ever woke the card
up. **The poll was serving as the card's clock.** That work moved to where it belongs: a
`Clock` deadline the card sets for itself, and `pump()`. Read §4.4.1 of DESIGN.md before you
are tempted to do work inside it.

### The bus does not poll. It keeps the wire.

`Bus::intPending()` used to walk the backplane and ask every card `assertsInt()`, **once per
instruction** — sixty million times a second, to compute a boolean that changes maybe a thousand
times a second. It was the single largest per-instruction cost left in the simulator once the
decode was cached, and it *grew with every card you added*.

Now the board **pulls the pin** and the bus keeps a running wire-OR (`intCount_`). Reading the
IRQ line is one integer test, flat in the number of cards.

The two hard-won rules turn out to be one rule:

| | combinational, pure | "it moved" | what the bus keeps |
|---|---|---|---|
| address decode | `decodes()` | `decodeChanged()` | a page table |
| interrupt | `assertsInt()` | `intChanged()` | a wire-OR count |

> **A board's outputs are pure functions of its state. When its state changes, it says so. The
> bus caches the rest.**

**Call `intChanged()` after anything that could move the pin**: a register written, a character
taken off the line, a deadline coming due. A spurious call costs a virtual call. **A missing one
hangs the guest forever**, waiting for an interrupt that already happened — and it presents as
*"the emulator locks up sometimes"*, which is worth a week of anyone's life.

So it is not left to trust either. `Bus::setVerify(true)` re-derives the wire from every board's
`assertsInt()` on every instruction, and aborts the moment a board disagrees with the cache. A
stale interrupt wire hangs the guest exactly this way, and is otherwise near-impossible to see.

## Memory and ROM

A memory card is **a list of regions**, not an address range: RAM here, a ROM socket there, an
empty socket between them. One card can occupy two separate ranges, because a real one does.
`SHOW BUS MAP` is per-*range*, not per-board, for that reason.

**An empty socket decodes nothing.** It does not read as zero — it floats to `FF`, like anything
else nobody drives.

**A ROM region never answers a write.** Its `decodes()` returns `false` for a `MemWrite`, so a
guest store to ROM lands nowhere — the write half of the floating bus — and the bus needed no
rule about ROM to make it happen. Reads and writes decode separately (the two `Slot` tables),
which is what makes that fall out for free.

> **Heritage note — `PHANTOM*`.** The S-100 machines the framework came from had a `PHANTOM*`
> line (pin 67): a boot ROM pulled it, memory boards strapped to honour it took *themselves* off
> the bus, and the overlay was **emergent** — the ROM the only board still answering because the
> RAM switched itself off, the bus picking no winner. No 6800 machine here shadows memory (the
> SWTPC and Altair-680 maps place ROM and RAM in disjoint ranges), so `PHANTOM*`, the
> `assertsPhantom` hook and the `honors_phantom` strap were all removed. The lesson it taught
> survives: an overlay is two ordinary board behaviours, never a bus special case picking a
> winner.

### `rawRead`/`rawWrite`/`rawSize` — the PROM burner

```cpp
virtual size_t  rawSize() const { return 0; }
virtual uint8_t rawRead(size_t) const { return 0xFF; }
virtual bool    rawWrite(size_t, uint8_t) { return false; }
```

Straight into the card's backing store, **bypassing decode entirely**. Offsets are board-local,
and the store may be far larger than 64K — a banked card's bank 3 simply *is* offset `0x30000`.

**This is the PROM burner, and that is not a metaphor.** It is how the operator writes a ROM the
guest cannot, because **burning a PROM is not a bus operation on real hardware either. You pull
the chip.**

Model it as a bus write instead and the bus would have to know *who originated a cycle* — which
a real backplane cannot know, and which no board should ever have to ask.

## Reflection is the keystone

```cpp
virtual std::vector<Property> properties() = 0;

struct Property {
    std::string name;                   // "baud", "fill", "clock_hz"
    std::string help;                   // one line, shown by SHOW
    Kind kind = Kind::Int;              // Int | Bool | Str | Enum

    std::vector<std::string> choices;   // Kind::Enum -- also feeds tab completion
    long long min = 0, max = 0;         // Kind::Int; min==max means unbounded
    int radix = 10;                     // 16 for addresses, so SHOW reads right
    std::string unit;                   // "Hz", "bytes" -- display only

    std::function<Value()> get;
    std::function<bool(const Value&, std::string& err)> set;
};
```

`SET`, `SHOW`, the TOML loader, `CONFIG SAVE`, the MCP tool schemas, tab completion **and the
manual's generated board reference** are all written **once**, against this. They know nothing
board-specific.

**There is no second schema anywhere.** A board's TOML keys *are* its properties. A board added
next year is configurable, scriptable, agent-drivable and documented **the day it lands**, and
none of those six consumers changes a line.

The cost of that is that a property has to carry enough metadata to validate, render and
describe itself. That is the whole trick, and it is why `radix` is there: `PORT=10` is port
`0x10` and `BAUD=9600` is nine thousand six hundred, because the property said so.

Three things to get right when you write one.

**A property with no setter is read-only.** Live pin state, a derived value, a lamp. Leave `set`
empty — **and that absence is the only signal any consumer has.** A setter that always refuses
would stop a `SET`, and simultaneously fool `SHOW`, `CONFIG SAVE`, the MCP schema and the
generated docs, all four of which read the *presence* of the function. Do not write one.

**There is no config-time-only property.** Every property can be set, always. You can only type
at the prompt when the machine is stopped — that is what the STOP key does, returning you to the
monitor, and there is no moment at which a `SET` races a running CPU. And on real hardware the
gate would be a fiction anyway: a board being worked on sits on an **extender**, out where you
can reach it, and its jumpers get moved with the power on.

**`unitProperties()` is for a real sub-thing with its own settings.**

```cpp
virtual std::vector<Property> unitProperties(const std::string& unit) { return {}; }
```

A board with two independent 6850s carries **two channels** with their own crystals' worth of
jumpers — independent baud rates, independent transforms — so they are units, and `SET <board>:a
BAUD=9600` reaches one of them. Folding them into `properties()` as `a_baud`/`b_baud` would work
for two channels and fall apart on the first board with eight ports. Most boards return `{}` here,
and that is fine.

A unit is a **name and a kind** (`UnitDef`, `UnitKind::{Disk, Rom, Serial, Tape, Cpu}`), never
an index — so `MOUNT dj:drive0`, and mounting a disk image onto a serial port is an *error with
a sentence*, not undefined behaviour that half-works. `units()` is the only list; `SHOW`,
`MOUNT`, `CONNECT`, the MCP schemas and tab completion all read it, so they cannot disagree
about what exists.

## Reset is two different events

```cpp
enum class Reset {
    PowerOn,  // POC* -- power-on clear. Nothing in software can assert it; only the power supply.
    Bus       // RESET* -- the RESET line. Warm.
};

virtual void reset(Reset) {}
virtual void power() {}
```

Mixing these up is the classic source of *"works from power-on but not from the reset button."*

**Neither reset clears memory. Only removing power does.** That is not a nuance, it is the rule,
and the memory array is the proof: **a RAM chip has no POC\* pin.** Its contents are
indeterminate at power-up because *the chips just powered up*, not because a signal arrived. A
`Reset::Bus` leaves memory intact and leaves media mounted and streams connected.

`power()` is the only thing that loses RAM and re-reads ROM images.

**What each signal does is a fact about your board, and it comes from the manual.** It is
tempting to scrub the card clean on `RESET*` because it feels safe, and it is exactly backwards:
**a card that resets more of itself than the reset line physically reaches is inventing a machine
nobody built, and the invention is destructive.**

A 6850 board proves it. The **MC6850 has no RESET pin** — 24 pins, and RESET is not among them —
so `RESET*` reaches that board's address decoding and *nothing else*. This tree used to reset the
ACIA anyway, throwing away the guest's word format and interrupt enables and eating a byte out
of the receive register, on a board where a real bus reset would have preserved all of it.

The memory board is the model to copy: **it clears its bank latch on either reset and touches not
one byte of RAM.**

Your board's `.md` must say concretely what each of the two does to it. That is a gate, not a
nicety (DESIGN.md §14).

## Lifecycle, time, and the host

### `pump()` — the one door to the outside world

```cpp
virtual void pump() {}
```

Give the host a turn: accept a socket connection, drain a keyboard. It is called **once per time
slice by the run loop, and NEVER from inside a bus cycle.**

**That seam is what keeps a board pure.** `read()` and `write()` are pure computation over the
card's state; anything that has to *talk to the outside world* happens here, at a known point in
emulated time. That is what would let a recorded session replay identically instead of depending
on when the host scheduler happened to deliver a packet.

Boards must **never** touch a socket, a file handle or `termios` directly. Everything they need
from the host comes through the services in `src/host/` and `src/platform/`.

### `Clock` — emulated time

Time is measured in **cycles**, never milliseconds, and it advances **only when the CPU
retires an instruction, by exactly the count the CPU reported** (`StepResult::cycles`).
`Machine::clock` is the one clock; a board is handed it by `attachClock()` when it goes into the
backplane.

Nothing else in the simulator may call `std::chrono::now()`. That is what makes the guest's sense
of time and a card's sense of time *the same sense of time* — a UART's idea of when a character
has finished going out is derived from the very instruction stream the guest is timing it against,
so the two cannot drift.

A card with nothing time-dependent on it never looks at the clock, and most don't. A UART
absolutely does: **TDRE is a deadline, not a flag.** Two facilities, and they answer different
questions:

- **`Clock::at()`/`after()`** — a *deadline*: something emulated time already knows is coming. A
  character finishing transmission. A sector arriving under the head.
- **`pump()`** — a *keystroke*, which is not in emulated time at all and which nothing could have
  scheduled.

The 6850 needs both, in the same function: `pump()` takes the byte off the line, and if the line
has not yet had time to deliver it, sets a deadline for when it will.

**The crystal is on the CPU card**, so `clock_hz` is that board's property. There is no machine
clock and there must never be one again — a machine-level copy would be a second place to say one
thing, and the day the two disagreed the machine would run at whichever was written last.

### `drainLog()` — what the card wants said out loud

```cpp
virtual std::vector<std::string> drainLog() { return {}; }
```

A bank select it could not decode. A ROM that failed to load. A sector whose checksum did not
match. Drained by the monitor after every command and after every run, and cleared by the
draining; MCP returns the same strings as structured data.

It is on `Board`, and it is virtual, because it used to be a `dynamic_cast<MemoryBoard*>` in
`Machine::drainBoardLog()` — which meant a disk controller with something to say about a bad
sector **had no way to say it**. A general facility with one card's name compiled into it is a
bug, every time.

## A chip is not a board

`src/chips/` holds the parts that get soldered onto more than one board: `mc6850.h` (the MP-S
console and the 680b onboard/Universal I/O), `uart1602.h` (the COM2502 — the 680b KCACR cassette),
`wd17xx.h` (the DC-4's FD1791).

**Each chip is modelled from its DATA SHEET. Each board is modelled from its MANUAL.** A chip
built instead from the one BIOS that happens to drive it implements exactly the subset that BIOS
touches and quietly gets the rest wrong — and it looks finished while doing it.

**A chip knows nothing about the bus.** It has a clock, some pins, and (if it moves bytes) a
`ByteStream`. What it talks to is an **interface, never a file**: `Wd1791` has a `FloppyDrive`
— STEP, DIRC, TR00, WPRT, READY, IP, with a drive on the far end. It owns no image, no geometry
and no host file, and it has **no drive-select and no side-select pin**, because the FD179x
hasn't got either. Which drive those pins reach is a **latch on the board**. That absence is the
chip/board seam stated in silicon.

**And the seam is not "the chip does the work and the board forwards to it."** A board whose
status word is **inverted** and one whose is not do not share a shared UART class with a `bool
invert` on it — that is *precisely* the bug `src/chips/` exists to prevent, so such boards share
**no code**, on purpose. What permits sharing is being **the same part**, not filling the same role.

**Where the seam falls is a fact about the chip, not a house style.** The chip is what the data
sheet describes, at its pins, in true sense. Everything between those pins and the bus —
the inverting buffers, which bit each signal lands on, the interrupt-enable flip-flops that live
in a *different IC*, the port decode — is the **board's**, and it stays on the board.

## Where this leaves you

You now know what a board is: a thing that is asked two pure questions (`decodes`, `assertsInt`),
that is *told* two things (`read`, `write`), that describes itself (`properties`), and that gets a
turn to talk to the host at one known point in emulated time (`pump`). The debugger watches the
same cycles from outside the backplane (`Bus::observe`), never through the board. Everything else
on the vtable is a detail of one of those.

You also know the traps: a decode with a side effect in it, a decode that changed without saying
so, a missing `intChanged()`, a card that resets more of itself than the real one does, and a
board that manufactures its own `0xFF`.

**That is enough to write a board. The next chapter writes one** — a lamp latch that decodes the
*store* at an empty SS-30 address, leaving the load there to float, which is the whole lesson
made concrete.
