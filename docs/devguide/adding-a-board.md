# Writing a board

We are going to build a board. A real one — it plugs into the backplane, it decodes a bus
cycle, it holds state, it has a setting you can put in a machine file, and when we are done
the monitor will know about it, `CONFIG SAVE` will write it out, and an AI assistant driving
the machine over MCP will be able to configure it. None of that last part will cost us a
line of code.

The board is **eight lamps and a latch**. A store to its address lights them. That is the
whole thing.

The finished source is in the tree at **`examples/boards/lamp/lamp.h`**, and
`tests/test_lamp.cpp` drives it on a real bus — so it compiles and it is tested, which is
not something you can say about most tutorial code. Read along, or write it yourself.

## Why an address, and why only the write

The 6800 has no IN/OUT space. It reaches every device the same way it reaches RAM: by
**address**, with the ordinary load and store instructions. So our board does not own a
"port" — it owns an address, and it lives on the **SS-30 I/O bus**, the eight-slot window at
`$8000`–`$801F` where the console and the disk controller already sit (slot *N* is based at
`$8000 + 4·N`). Slot 4, `$8010`, is empty in the stock `swtpc` machine. Ask it:

```
swtpcsim> WHO 8010
8010 read  nobody -- floats to FF
8010 write nobody -- floats to FF (a write here is simply gone)
```

Nobody, either direction — so the lamp can have it. But watch which direction it takes. A
**store** lights the lamps; a **load** has nothing to answer — there is no "what are the
lamps?" register on the real thing, just eight pins driving eight LEDs. So the board claims
the *write* at `$8010` and leaves the *read* alone, and a load from `$8010` floats to `0xFF`
like any unclaimed address.

That is not a shortcut. It is how 6800 I/O actually works, and the 6850 ACIA in the next slot
proves it: its **status** register is a read at `$8004` and its **control** register is a
write at the *same* `$8004`. One address, two registers, told apart by direction. The bus can
do that because it routes each cycle by its **type** — `Cycle::MemRead` and `Cycle::MemWrite`
are different cycles, decoded separately (`src/core/bus.h`), for the same reason the real
backplane carries R/W̅ alongside the address lines.

> **The bus routes by cycle type, not just by address.**

Keep that in mind while writing `decodes()` — a board that answered the *read* at `$8010`
would be claiming a byte it has no answer for.

## 1. The board

`examples/boards/lamp/lamp.h`. A board is a class that inherits `Board`
(`src/core/board.h`) and answers some questions.

### Its name

```cpp
std::string type() const override { return "lamp"; }
```

This is the word a machine file uses: `type = "lamp"`. It is the **chip or the common
name**, never a catalog number — nobody ever asked for an MP-A board, they asked for a 6800.

### What it decodes

```cpp
bool decodes(const BusCycle& c) const override {
    if (!enabled_) return false;                  // a board switched off drives nothing
    if (c.type != Cycle::MemWrite) return false;  // a store lights them; a load is not ours
    return c.addr == addr_;
}
```

That middle line is the whole lesson of this chapter. Delete it and the board answers the
*load* at `$8010` too — a read that should float to `0xFF` now returns the latch, and any
program that probes the empty slot gets a lie instead of the floating bus.

One more line earns its place, and it is easy to leave out:

```cpp
bool decodeIsPageUniform() const override { return false; }  // one address, not the page
```

The bus caches decode a page at a time — 256 bytes — because most boards answer a whole page
uniformly and it would be wasteful to ask 256 times. Our board answers *one* address inside
page `$80`, a page it shares with the console and the disk controller, so it must tell the bus
to ask per address. Forget this and the cache smears one answer across the page and boards
collide. (The `mps` and `dc4` boards say the same thing, for the same reason.)

Two rules about `decodes()`, and they are not negotiable:

- **It must be pure and combinational.** It answers *"if this cycle happened, would I drive
  the bus?"* and it does nothing else. No side effects, no counters, no latching.
- **The bus caches the answer.** It keeps one cached answer per page (and, for a board like
  ours that says its decode is not page-uniform, per address), so a decode is a
  table lookup rather than a walk over every board in the machine. Which is why a `decodes()`
  with a side effect in it is a bug that will not show up for a month: it does not get called
  when you think it does.

If a board's decode ever *changes*, it must say so — `decodeChanged()`. (You will see below
that we get that for free.)

### What it does with the cycle

```cpp
void write(const BusCycle& c) override { latch_ = c.data; }
```

We claimed the cycle, so the byte is ours.

We never override `read()`. We never say yes to a read, so we are never asked one — and a
load from `$8010` floats to `0xFF`, which is exactly what an output-only board does.

### Power and reset

```cpp
void reset(Reset) override { latch_ = 0; }
void power() override      { latch_ = 0; }
```

These are **two different events** and a board is entitled to treat them differently.
`Reset::Bus` is the RESET* line — the reset the machine asserts when you press it. `Reset::PowerOn`
is the power coming up, and it is the only thing that loses RAM. For our lamps they mean the same thing:
the lights go out. For a memory board they emphatically do not.

### Its state — SNAPSHOT and RESTORE

A board writes its **runtime state** so `SNAPSHOT` can save it and `RESTORE` can put it
back (`DESIGN.md` §13). Chain to the base — it handles `enabled_` — then read and write
your own fields in the same order:

```cpp
void serialize(StateWriter& w) const override {
    Board::serialize(w);
    w.u8(latch_);
}
void deserialize(StateReader& r) override {
    Board::deserialize(r);
    latch_ = r.u8();
}
```

`StateWriter`/`StateReader` (`core/statefile.h`) are explicit and little-endian — `u8`,
`u16`, `u32`, `u64`, `boolean`, `str`, `blob`, and `raw` for a fixed array — so a snapshot
reads back byte-for-byte on every target. Three rules decide what goes in:

- **Write runtime state, not configuration.** A snapshot is RESTOREd into a machine built
  from the same machine file, so your straps (the port, a baud rate, a jumper) are already
  correct — writing them would be a second copy that could disagree. Write the registers,
  latches, RAM and in-flight buffers that a running machine accumulates.
- **Do not write a host handle.** A `ByteStream`, a disk image, the `Display` — those are
  re-opened from the (unchanged) config. But bytes that are **not on the host yet** — an
  uncommitted write buffer, a tape's head position — *are* your state and *do* travel.
- **Never write a `Clock::Handle`.** The event queue does not survive a snapshot. If your
  board sets a deadline, **re-arm it in `deserialize()`** from the state you just read — call
  the same `refresh()`/`arm…()` you already call when a jumper moves. Store the deadline as
  an absolute cycle if you need the timing back; it stays valid because the clock's time
  travels with it.

### Its settings — and this is the part that pays

```cpp
std::vector<Property> properties() override {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "addr";
        x.help  = "the SS-30 address this board latches. Write-only -- a load here is not ours";
        x.kind  = Kind::Int;
        x.radix = 16;               // ON THE WIRE -> HEX. An address is a thing the CPU sees.
        x.min   = 0x8000;
        x.max   = 0x801F;
        x.get   = [this] { return Value::ofInt(addr_); };
        x.set   = [this](const Value& v, std::string&) {
            addr_ = (uint16_t)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "lamps";
        x.help  = "what the guest last wrote -- the eight LEDs";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.get   = [this] { return Value::ofInt(latch_); };
        // NO SETTER.
        p.push_back(std::move(x));
    }
    return p;
}
```

**This vector is the entire configuration layer.** There is no schema file, no parser, no
registration call, and nowhere else to declare anything. `SET`, `SHOW`, the TOML loader,
`CONFIG SAVE`, the MCP tool schemas, tab completion, and the User Manual's generated board
reference are all written **once**, against this, and know nothing about any particular board.

Two details worth stealing:

- **`radix = 16`**, because an address is a thing the processor can see. On the wire → hex; never
  on the wire → decimal. Get this wrong and `addr = 8010` in a machine file quietly means eight
  thousand and ten instead of the address `$8010`. **It declares the class, not a display preference** — the one `radix`
  both reads bare digits and prints them, an operator can force any base of their own
  (`0x`, `0o`, `0b`, `#`) whatever you chose, and `SET CONSOLE base=octal` does not reach it:
  that switches how the *monitor* prints addresses and bytes, while `SHOW` prints every
  property in its own radix. So pick it by which side of the wire the number lives on, and
  never because one base reads better.
- **`lamps` has no setter**, and that absence *is* the signal. `SHOW` prints `(read-only)`,
  `CONFIG SAVE` leaves it out of the file, and the manual's reference marks it. A setter that
  merely *refused* would stop a `SET` and fool all three at once — which is a mistake that was
  in this codebase, on the memory board, until this manual went looking.
- **We never call `decodeChanged()`** in the `port` setter. The property layer calls it for us
  after any successful set, precisely so that a board author cannot forget.

### What it tells the operator

```cpp
std::vector<MapEntry> memMap() const override {
    return {{addr_, addr_, "write", "lamp latch -- D0..D7 (write-only)"}};
}
```

This is what `BOARDS`, `SHOW BUS MAP` and `WHO` print. **It is documentation, not decode** —
the bus never consults it. A board whose `memMap()` disagreed with its `decodes()` would work
perfectly and lie to you, which is worse than a board that does not work.

## 2. Put it in the registry

Two lines and an include, in `src/boards/registry.cpp`:

```cpp
#include "../../examples/boards/lamp/lamp.h"

// ...in boardTypes():
{"lamp", "Eight-LED output latch (the Developer Guide's example)",
         "A write-only latch: a store lights eight LEDs. The Developer Guide builds this"},

// ...in makeBoard():
if (type == "lamp") return std::make_unique<LampBoard>();
```

The row is the type name, a one-line summary for the `SHOW BOARDS` catalog (keep it short
enough to fit the column -- a test checks every row fits 78 columns), and the full description
that `SHOW BOARD <type>` prints.

That is all. `registry.h` promises *"adding a board type is one line here and nothing anywhere
else"*, and it means it.

(Our board is a header, so it needs no entry in `CMakeLists.txt`. A real one — with a `.cpp`
— adds its source to the `swtpc_core` library alongside the others.)

## 3. Build it, and watch what you get for free

```
$ cmake --build build -j
```

The board is now in the catalogue. `SHOW BOARDS` lists every type with its one-line summary;
name one to see its settings, their help text and their legal values -- and nobody wrote a
line of code to put it there:

```
swtpcsim> SHOW BOARDS
  TYPE      DESCRIPTION
  --------  ------------------------------------------------------------------
  ...
  lamp      Eight-LED output latch (the Developer Guide's example)
  ...

  SHOW BOARD <type> for a board's properties (add UNITS for just the units)

swtpcsim> SHOW BOARD lamp
  lamp  A write-only latch: a store lights eight LEDs. The Developer Guide
        builds this

  PROPERTY  HELP
  --------  ------------------------------------------------------------------
  addr      the SS-30 address this board latches. Write-only -- a load here is
            not ours
            values: 0x8000 .. 0x801F
  lamps     what the guest last wrote -- the eight LEDs
```

Fit one, and ask the machine the same question we asked at the start:

```
swtpcsim> BOARDS ADD lamp lamp0
lamp0: lamp added

swtpcsim> WHO 8010
8010 read  nobody -- floats to FF
8010 write lamp0
```

It said `nobody` on the write an hour ago. Now light the lamps — `DEPOSIT` runs a store cycle
and says nothing, because a store says nothing; the byte shows up in `SHOW`:

```
swtpcsim> DEPOSIT 8010 55

swtpcsim> SHOW lamp0
lamp0  (lamp)

  property         value            legal
  addr             0x8010           0x8000..0x801F
  lamps            0x55             (read-only)
```

`DEPOSIT 8010 55` ran a **real store cycle on a real bus** — the same path a CPU's own store
takes, because there is only one. The board decoded it, latched it, and `SHOW`
read it back out of the reflection layer. `lamps` is marked read-only, and it worked that out
from the missing setter.

And the *read* at that address is still nobody's — `WHO 8010` said so up above, and a load
there floats to `0xFF`. One address, two directions: the write is the lamp's and the read is
nobody's, with no contention. **Because the bus routes by cycle type.**

## 4. Put it in a machine

```toml
[machine]
name = "lamps"
base = "default"

[[board]]
type = "lamp"
id   = "lamp0"
addr = 8010        # radix 16 -- no 0x needed
```

Nobody taught the TOML loader what a lamp is. It asked the board for its properties, found one
called `addr`, and set it. A board added next year is configurable, scriptable, drivable by
an assistant, and documented **the day it lands**.

## 5. Test it

`tests/test_lamp.cpp` is the model. It does several things, and the middle one is the important
one:

```cpp
// A REAL STORE CYCLE on the bus -- not a method call on the board.
m.bus.memWrite(0x8010, 0x55);
CHECK(lamps(lamp) == 0x55, "the store latched the byte, read back through reflection");

// The board is WRITE-ONLY, and the proof is that the LOAD FLOATS.
CHECK(m.bus.memRead(0x8010) == 0xFF, "a load from 8010 is not the lamp's -- the bus floats");
```

…and then it pins down the claim this whole chapter rests on — that the bus tells the two
directions apart:

```cpp
BusCycle store; store.type = Cycle::MemWrite; store.addr = 0x8010;
BusCycle load;  load.type  = Cycle::MemRead;   load.addr  = 0x8010;

CHECK(m.bus.respondersTo(store).size() == 1, "a store at 8010: the lamp, and ONLY the lamp");
CHECK(m.bus.respondersTo(load).empty(),      "a load at 8010: nobody -- the read was never claimed");
CHECK(m.bus.drain().empty(),                 "...and the bus logs NOTHING. It is not contention.");
```

**Assert the thing your design depends on, not the thing that is easy to assert.** If the bus
ever stopped decoding by direction, this chapter would be teaching a falsehood — and that
test is what would say so.

> **One trap, and it caught this test.** `m.bus.attach(board)` wires a board to the backplane;
> it does not hand it to the **machine**. Lifecycle events — `reset()`, `power()`, `pump()` —
> are dispatched by `Machine` over the boards it *owns*. A board that is only on the bus will
> answer cycles all day and never hear the RESET line, which is why the test calls `power()`
> and `reset()` on the lamp by hand. A board that comes from a machine file goes in through
> `Machine::add()` and gets both.

## 6. What the lamp skipped — wiring a board that ships

The lamp is complete, tested, and configurable, and everything above is the whole story for a
board that only latches a byte. A board that talks to the **outside world**, or that a user will
find in the manual, needs a few connections the lamp never made. None of them is hard; each is
easy to *forget*, and the forgetting fails in a way that does not point back here.

### A board that talks to an endpoint wires its resolver in two places

A board with a real line — a serial port, a printer, a socket — does not open that line itself.
It hands a *spec* like `socket:2323` or `file:printout.txt` to a resolver and gets back a
`ByteStream`. **The endpoint grammar lives in exactly one file, `src/host/endpoint.cpp`
(`resolveEndpoint`), and a board must not know it** — that separation is what lets `CONNECT`,
tab completion and the MCP schema all speak the same vocabulary without any board learning what
a socket is.

A board reaches the resolver through a static `setResolver()` (the SIO family is the model). The trap is that this is wired at the **composition root**, and there are
**two** of them:

- `src/main.cpp` — the shipping program.
- `tests/main.cpp` — the test harness.

> **Wire the resolver in `tests/main.cpp` too, or your board test connects to nothing.** Both
> files carry the same block of `YourBoard::setResolver(resolveEndpoint)` lines. A board added
> to only `main.cpp` works in the running program and then every test that `CONNECT`s it gets a
> null resolver and fails somewhere unhelpful. Copy the line into both.

### Remember the path as written, not as resolved

If your endpoint is a path (`file:`, a disk image), resolve it through the board's
`resolvePath()` so a relative path means the right thing (relative to the machine file when it
came from one, relative to the shell when typed — the rule is in the serial-I/O chapter and the
config docs). But **store the spec as the user wrote it** for `describe()` and round-trip.

> **Where you LOOK is `resolvePath()`; what you REMEMBER is the path as written.** Save the
> resolved absolute path instead and `CONFIG SAVE` writes *that* back, so the next load rebases
> an already-absolute path and the one after that rebases again. The hard-sector controller
> states the rule in a comment at its `openMedia` site; follow it.

### Adding a *new* endpoint scheme touches its help in two spots

If you are not just consuming endpoints but adding one (a new `something:` scheme in
`endpoint.cpp`), it must appear in **`endpointHelp()`** *and* get a one-line explanation in the
`CONNECT` command help in `src/cli/commands.cpp`. `test_cli.cpp` asserts that every name
`endpointHelp()` offers is a word `CONNECT` explains — a bare `null` or `scripted` tells a user
nothing, and the explanation is a hand-copy that rotted once (it promised `socket:` "was coming" long
after it shipped). The test is what keeps the two honest.

### Regenerate the reference, ship a machine, and mind the two unguarded docs

Three loose ends after the board itself compiles:

- **The generated board reference.** After editing `registry.cpp`, `commands.cpp`, or a machine
  file, regenerate `docs/manual/ref/*.md` — `cmake --build build --target docs-reference` — and
  commit the result. A ctest byte-diffs the committed files and goes red until you do. **Edit
  the emitter or the source, never the `.md`.**
- **A sample machine is a TOML file**, not C++: drop it in `machines/` with `base = "default"`
  under `[machine]` and a `name`. `cmake/embed_machines.cmake` embeds it on the next build.
- **Two documents no test guards, so they drift silently.** The prose board chapter
  (`docs/manual/boards.md`) and the changelog's `Unreleased` section are *not* test-enforced —
  the manual guard only checks self-containment and `ORDER` membership, nothing checks that the
  prose lists every registered board. The authoritative list is `registry.cpp`'s `boardTypes()`;
  the generated `ref/boards.md` is guarded, but the hand-written chapter is not, and it once
  drifted nine boards behind before anyone noticed.

> **When you add a board, update the prose chapter and add an `Unreleased` changelog line by
> hand. No build will remind you.** The board also ships with its own `docs/boards/*.md` (from
> `docs/boards/_TEMPLATE.md`) and a row in `docs/sources.md` for anything it embeds.

## What to do next

Our board answers a cycle. A more interesting one **asks for something**:

- **Interrupts.** Override `assertsInt()` to report whether your board is pulling the IRQ line,
  and **call `intChanged()` from every place your pending flag could move.** A needless call
  costs a virtual call. A missing one hangs the guest forever. It shows up in `SHOW BUS IRQ`
  for free.
- **Time.** A board with a deadline uses the `Clock` it was handed. A UART absolutely needs
  one: transmit-buffer-empty is a deadline, not a flag.
- **The outside world.** Anything that talks to a socket, a file or a keyboard does it in
  `pump()` — **never inside a bus cycle**. That seam is what keeps `read()` and `write()` pure
  computation over state, and it is what a deterministic replay would be built on. The
  **MP-S serial board** (`src/boards/swtpc-mps.{h,cpp}`) is a good small example that does this
  for real: a 6850 ACIA whose `tty` unit is a `ByteStream`, so `CONNECT mps0:tty file:out.txt`
  captures the console and the board never learns what a file is. It is a good next read after
  this lamp.
- **Sub-units.** A board with a *list* of things — regions on a memory board, drives on a
  controller — declares `subUnitTables()` and gets `[[board.region]]` / `[[board.drive]]` in
  TOML for free.

And whatever you build: **it ships with its `.md`.** A board and its documentation are one
deliverable, and the **Limitations** and **Quirks** sections are the load-bearing ones —
they are what forces the honest question *"what did I not actually implement?"*, which is
exactly the question a simulator author most wants to avoid. `docs/boards/_TEMPLATE.md` is
the form.
