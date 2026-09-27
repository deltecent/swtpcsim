# `swtpcsim` — Design Document

**Status:** implemented and running. A Motorola 6800 CPU driving the SS-50/SS-30 bus, and period
software that boots — SWTBUG and FLEX 2.0/3.0 off a DC-4 floppy, CP68 off a FLEX-format disk, the
Altair 680b's MON680 over a 6850 console, and Kansas City Standard cassette. What is *not* built is
named where it is described.
**Started:** 2026-07-11; a living document, revised with the code.

> **Heritage note.** `swtpcsim` began as `altairsim`, a simulator of the MITS Altair 8800 and the
> S-100 bus. It keeps that project's C++20 framework — the CPU-is-a-board model, the reflective
> board properties, the host services, the monitor and debugger — but the machines it ships are
> Motorola 6800 systems: the SWTPC 6800 on the SS-50/SS-30 bus, and the Altair 680b. The S-100-only mechanisms (`PHANTOM*`, `pHOLD`/`pHLDA`
> DMA arbitration, the vectored-interrupt lines, the 8080 status byte) have been **removed from the
> code**, and the chapters below retargeted to the 6800. Where a section still names an S-100 signal
> or an 8080 example, it is a deliberate **heritage note** — the framework's lineage and the reasoning
> that still applies — not a description of shipped hardware.

---

## 0. Ground rules

### 0.1 Where hardware facts come from

**Period manuals and first-hand artifacts only.** Register maps, bit layouts, and timings are sourced from original vendor documentation (MITS and other manufacturers' manuals), from period software's own equate blocks and disassembled PROMs, or from hardware in hand.

**We do not read other emulators' source code to learn how hardware works.** That includes SIMH / AltairZ80. Second-hand facts inherit second-hand mistakes, and the whole value of this project rests on the hardware model being *right*, not on it matching somebody else's model.

> **If a spec is missing, ask Patrick — he will source the manual.** Do not guess, do not reconstruct from memory, and do not go read another simulator. A wrong bit layout that "seems to work" is the most expensive kind of bug in a project like this, because the software will paper over it until one day it doesn't.

When two sources disagree, say so in the board's `.md` and say which one won and why.

Consequences already baked into this design:
- **AltairZ80's port-0xFE "SIMH pseudo device" is not implemented**, and *its* `R.COM`/`W.COM` will not run here. See §12. *(This aged well: as of 2026-07-13 port 0xFE is the **88-VI/RTC's real control register** — 376 octal, straight out of the MITS manual. AltairZ80 put its pseudo-device on top of a port that belongs to an actual MITS card, and we would have had to evict it.)* **We ship our own `R.COM` and `W.COM`** against our own Host Bridge card at 0xB0 (§12.1) — same names, because the muscle memory is worth keeping; different code, different card, different protocol, nothing derived.
- Boards with no manual in the tree (88-HDSK, 88-PIO/4PIO) are **blocked on documentation**, not on code. See §17. None of them block milestone 1. (The **88-ACR** and the **88-VI/RTC** were on this list; both manuals are now in the tree and both cards are built.)

### 0.2 Doc discipline

Every board ships with a `docs/boards/<board>.md` on the standard template (§14). No board is merged without one. The **Limitations** and **Quirks** sections are the load-bearing ones.

### 0.3 "Board" is the word, except when the sentence is about the artifact

A board and a card are **the same object** — `src/core/board.h` says so outright, and that identity is not up for revision. This rule is about which of the two synonyms we write down, and it exists because the reader has to type one of them.

**Default to "board."** It is the base class, the `src/boards/` directory, the `[[board]]` table in every machine file, and the `BOARDS` command. A reader who types `BOARDS` and reads "card" in the reply has to carry two words for one thing, forever, for no gain.

**Write "card" only when the sentence is about the physical historical artifact** — the thing with jumpers and edge fingers that somebody bought in 1975. *"A JUMPER on the real card"*, *"the real card has no motor control"*, *"two cards, one chip, and not one line of polarity shared between them"*: these are better with "card", because "card" is what the period manuals and the people who owned these machines said. Swapping those to "board" would cost precision to gain tidiness.

The test is simple: **if the sentence would still be true of the C++ object, write "board"; if it is only true of the PCB, "card" is right.** The failure this rule was written against is using both for one object in one breath — a config error that named the CPU CARD in one line and the CPU's `[[board]]` in the next.

**§7.8 keeps its name.** "A chip is not a card" is an argument about physical parts and PCBs — exactly where "card" belongs — and it is cited by name from `theory.md`, three chip headers and two board headers. It is not an exception to this rule; it is an instance of it.

### 0.4 Glossary — what category a word names

The words below name *different kinds of thing*, and the confusion this section settles is the
category error, not the spelling: a **tape** is a mounted medium, not a serial endpoint, even
though both move bytes and both feel like "a connection." Read it once and the rest of the
document stops surprising you. The authority for each is the code named in its line — this table
tracks it, it does not define it.

- **board** (= **card**) — a thing in a slot on the backplane: the C++ `Board` (`src/core/board.h`),
  the `[[board]]` in a machine file, the row in `BOARDS`. §0.3 governs which of the two synonyms to
  write. Everything else in this glossary hangs off a board.
- **unit** — a **named, typed socket on one board** that holds a medium or a connection:
  `MOUNT dj:drive0`, `CONNECT dj:tty`. **A unit is a NAME, not an index**, and one board can carry
  several of different kinds — the Tarbell has a boot PROM *and* drives *and* a serial port (§9).
  `Board::units()` is the list; `SHOW <id>` reads it.
- **kind** — the *category* of a unit, `enum class UnitKind` (`src/core/board.h`): `Disk`, `Rom`,
  `Serial`, `Tape`, `Cpu`. The kind decides the verb — `MOUNT` for a `Disk`/`Rom`/`Tape` image,
  `CONNECT` for a `Serial` endpoint, *nothing* for a `Cpu` (a core is soldered on, §3.0.1) — and it
  is **checked**: `MOUNT`ing a disk image onto a serial unit is an error with a sentence, not a
  silent misfile (§9). (`kind` is overloaded on purpose in two unrelated places — `Value::Kind` is
  the type of a *property*, and `BreakKind` is the type of a *breakpoint* — but on a unit it always
  means `UnitKind`.)
- **medium** — the host thing a mountable unit holds: a `DiskImage` (§7.3) or a `TapeImage`
  (§7.3.1), buffered and written back. Named by a host path in `MOUNT`.
- **tape** — a **mounted sequential medium** (`UnitKind::Tape`, a `TapeImage`), e.g. a cassette in
  an 88-ACR. **It is NOT an endpoint.** Bytes flow through it, which is why it reads like a
  connection, but you `MOUNT` a `.TAP`/`.WAV` file onto the deck's unit; you do not `CONNECT` it to
  a `resolveEndpoint` scheme. This is the exact confusion the section was written for. (`BREAK TAPE
  STOP`, §3.0.3, is a *device event* on such a deck — the deck reaching its auto-stop mark.)
- **endpoint** — the **other end of a `Serial` unit's wire**, resolved by `resolveEndpoint`: `console`,
  `socket:…`, `serial:/dev/…` or `serial:COM3`, `in:`/`out:` paper tape, `null`, `loopback` (§9,
  §7.1). You reach one with `CONNECT`. An endpoint is a *destination*, never a medium — that is the
  tape/endpoint line above, from the other side.
- **line** — a **serial channel** on a card: its unit plus its *coding* (baud, data_bits, parity —
  the frame, §10). A line is **8-bit clean** by contract; the character transforms (UPPER, STRIP7,
  CRLF…) are the **console's**, never the line's (they were on the line once and it was reversed as
  a corruption bug, §7.2). "Line" also appears in its plain-English senses (a status line, a line of
  a config file) — the serial-channel sense is the one this glossary claims.
- **device** — deliberately **not a modeled category**. There is no `Device` class, and there is
  no "boot device" the monitor knows about (§10.0.3) — that is the whole point of §9. The word is
  used loosely for "a thing the guest talks to" (a board, or a unit's far end), and in the one
  formal place it appears — a **device-event** breakpoint, `BREAK <kind> <action>` (§3.0.3) — it
  means exactly "an event a board raises that is not a bus cycle." When precision matters, prefer
  board, unit, or endpoint; "device" is the fuzzy word and this line is its only license.

---

## 1. Purpose, goals, non-goals

`swtpcsim` is a C++ simulator of Motorola 6800 machines — the SWTPC 6800 on the SS-50/SS-30 bus, and the MITS Altair 680b — running on Intel Mac, Apple Silicon Mac, Linux, and Windows, built with CMake.

It is a **hardware development bench** that happens to run period software. The SS-50/SS-30 bus is a first-class modeled object, not an implementation detail, because the point is to develop **new hardware** as well as software. The interface is a command monitor plus an MCP server so Claude can drive the machine efficiently.

**Goals**
- Emulate the SWTPC 6800, the Altair 680b, and the SS-50/SS-30 bus as closely as practical.
- Make writing a *new* SS-50/SS-30 board straightforward, and make a new board behave the way a real card would on a real backplane.
- Multiple instances of any board, each independently configured (e.g. two MP-S serial boards at once).
- Boards that move characters can use the console, a TCP socket, or a real host serial port — interchangeably.
- An efficient, structured interface for Claude (MCP), not screen-scraping of a text CLI.

**Non-goals (v1)**
- A **GUI**. No window, no LEDs you can look at, no switches you can click. The machine is driven entirely through the monitor and the MCP server. (The framework's Altair origin modeled a front-panel *board* — sense switches at port `0xFF`, address/data lamps latched off the bus — because that is what a panel soldered to an S-100 backplane *is*; the 6800 machines here ship no such board, and a future SS-50 panel would be one more board, needing no new bus concept.)
- Plugin ABI / dynamically loaded boards. All boards are compiled in and self-register.
- Cycle-exact modeling *within* a bus cycle (sub-cycle phase/signal states).

---

## 2. Portability and build

- C++20, no compiler extensions. MSVC / clang / gcc.
- CMake ≥ 3.20 with presets (`CMakePresets.json`) for macOS (universal `arm64;x86_64`), Linux, Windows.
- **Dependencies: there are none, and that turned out to be achievable.** An earlier draft of this section planned to pull a TOML parser, a JSON library and **replxx** in via `FetchContent` with pinned versions. **None of that happened, and there is no `FetchContent` in the build at all.** The TOML parser (`src/config/toml.cpp`), the JSON encoder (`src/util/json.cpp`) and the line editor (`src/cli/lineedit.cpp`) are all in-tree and hand-written, so a fresh clone builds with a C++20 compiler and CMake and nothing downloaded. **The one exception is SDL3** (video, §7.4), and it is *detected* rather than required (`SWTPCSIM_ENABLE_SDL`, default ON = "use it if it is here"): the simulator builds and every acceptance test passes with it absent, so CI runs headless. Nothing else is a dependency of the core, hard or soft.
- Strict on warnings. `-fno-exceptions` is not required, but the hot bus path must be exception-free.
- **Endianness and alignment:** snapshots must be byte-exact across all four targets. Serialize explicitly; never `memcpy` a struct.

### 2.1 No `#ifdef`s. OS differences live in separate files.

**Hard architectural rule, enforced by CI.** SIMH is riddled with conditional compilation and the result is code you cannot read without mentally executing the preprocessor. We are not doing that.

> **`src/platform/` EXISTS as of 2026-07-12** — `serial.h` + `socket.h` + `terminal.h` (pure declarations, zero conditionals, **no OS type in any signature**: no `int fd`, no `HANDLE`, no `termios`), with `posix/` built and proved against two FTDI cables, a null modem and a pty, and `win32/` **built and field-proven** — serial over two FTDI ports on a null modem, socket against the real Winsock stack, terminal on a real console (`docs/porting-notes.md` records each, check by check; Windows has been a required CI leg since #44). CMake picks the directory; that `if(WIN32)` in `CMakeLists.txt` is the only place in the project that asks what OS it is on.
>
> **The rule earned its keep immediately.** The POSIX socket file wanted `MSG_NOSIGNAL` (Linux) / `SO_NOSIGPIPE` (macOS) to stop a hung-up client from killing the process with SIGPIPE — a genuine macOS/Linux divergence, which by this section's own rules would need its own *file*. `signal(SIGPIPE, SIG_IGN)` is plain POSIX, works on both, and needs no branch at all. **The right answer to a platform conditional is usually to stop needing it.**
>
> **THE LINT IS ON, as of 2026-07-12** (`cmake/lint_platform.cmake`, a build dependency of `swtpc_core` — not a test, because this section says *fails the build*). The terminal was the last thing in the tree with an OS underneath it; it is now `src/platform/terminal.h`, and `lineedit.cpp`'s `#if defined(_WIN32)` — the only conditional compilation in the project — is gone with it.
>
> **The lint greps for OS *headers*, not just OS *macros*, and that is the half that mattered.** Moving the terminal turned up two offenders and only one had a conditional: `src/host/console.cpp` simply `#include`d `<termios.h>` in the open, no `#ifdef` anywhere near it. A macro-only lint — the one this section originally specified — would have called that file **clean**, and it would have compiled here forever and failed on Windows the day someone tried. The `#ifdef` is the symptom. **Reaching for the OS outside the platform layer is the disease**, and the lint is aimed at the disease.

OS differences are expressed as **an interface in a header with no conditionals at all**, plus **one implementation file per OS**, selected by CMake at configure time:

```
src/platform/
├── platform.h          # the contract. Pure declarations. Zero #ifdefs.
├── terminal.h  serial.h  socket.h  fs.h  time.h  console.h
├── posix/
│   ├── terminal_posix.cpp     # termios raw mode, restore-on-signal
│   ├── serial_posix.cpp       # open / tcsetattr / cfsetspeed
│   ├── socket_posix.cpp       # BSD sockets, poll
│   ├── fs_posix.cpp
│   ├── time_posix.cpp         # clock_gettime, nanosleep
│   ├── macos/ serial_macos.cpp   # only where macOS genuinely differs from Linux
│   └── linux/ serial_linux.cpp   #   (serial enumeration, custom baud rates)
└── win32/
    ├── terminal_win32.cpp     # SetConsoleMode, ENABLE_VIRTUAL_TERMINAL_*
    ├── serial_win32.cpp       # CreateFile, SetCommState/DCB, OVERLAPPED
    ├── socket_win32.cpp       # Winsock, WSAStartup
    ├── fs_win32.cpp
    └── time_win32.cpp         # QueryPerformanceCounter, timeBeginPeriod
```

CMake selects the right directory. Nothing else in the tree ever asks what OS it is on.

Rules that make this hold:
- **Opaque handles.** Where the underlying type differs (`int fd` vs `HANDLE` vs `SOCKET`), the interface exposes an opaque handle or a pimpl, never a raw platform type. This is what usually forces people back into `#ifdef`; head it off in the interface design.
- **macOS/Linux divergence inside POSIX** gets a `posix/macos/` or `posix/linux/` override *file*, never an `#ifdef __APPLE__` inside a shared file.
- **The only permitted conditionals are feature toggles, not OS toggles** — e.g. `SWTPCSIM_ENABLE_SDL`, which lives in CMake.

**Enforcement:** a CI lint greps for `_WIN32`, `__APPLE__`, `__linux__`, `__unix__`, `_MSC_VER` anywhere outside `src/platform/*/` and **fails the build**. Without the lint this rule decays within a month: the first time someone needs one small thing on Windows an `#ifdef` appears "just this once," and you are SIMH again.

Payoff: a board author writes against `ByteStream`, `DiskImage`, `Display`, `EventQueue` and **never sees an OS detail on any platform** — but only if the OS layer is genuinely sealed.

---

## 3. The CPU is a board

In an SWTPC 6800 the processor *is* a card: the MC6800 CPU board on the SS-50 bus, the way the framework's original Altair **88-CPU** (8080A) was a card on the S-100 bus. So the CPU lives in `boards/` with everything else and gets `properties()` (`SET cpu0 clock_hz=1000000`, `SHOW cpu0`), `reset(PowerOn|Bus)`, `serialize()`, a slot, and a line in `BOARDS` — no special cases.

But there is a distinction to draw:

> **Boards *respond* to bus cycles. The CPU *originates* them.**

Two concepts; a CPU card is both:

```cpp
class BusMaster {                        // drives cycles onto the bus
    virtual StepResult step(Bus&) = 0;   // one instruction: cycles, and WHY it stopped
};

class Cpu6800Board : public Board, public BusMaster, public CpuCard { ... };  // the 6800 CPU card
```

*(`step()` returns a `StepResult`, never a bare cycle count — §3.1. A count alone
cannot distinguish "this instruction legitimately took N cycles" from "something
went wrong", and §16 records what that cost the prototype.)*

**Built.** `src/isa/isa6800.cpp`, `src/cpu/cpu6800.cpp`,
`src/boards/mits-680cpu.cpp`, and the debugger in `src/core/debug.cpp`. The card
decodes nothing — `BOARDS` shows `cpu0` with no memory (and there is no I/O space
to show), and that is the truth about a processor card, not a gap in the table. It
still has a *unit* (`1 cpu: 6800`), because the processor on the card is a unit like
any other.

**The 6809 is built the same way.** `src/isa/isa6809.cpp` (disassembler and full
assembler, both pages of prefixed opcodes and every indexed post-byte),
`src/cpu/cpu6809.cpp`, and `src/boards/cpu6809card.cpp` — a plain `6809` board: the
6800 card with the core swapped, and nothing else. It is deliberately **not** the
MP-09, which is its own board (below). Its FIRQ input is **unconnected**, because the
SS-50 bus manual in `reference/` names only IRQ and NMI, and a board that invented a
wire for FIRQ would give the bus a line it may never have had (§0.1). The ISA registry
moved out of `isa6800.cpp` into `src/isa/isa.cpp`, since it no longer belongs to one
instruction set.

**The SWTPC MP-09 (`mp09`, `src/boards/swtpc-mp09.cpp`) is the first card whose CPU address
is not the bus address.** Its DAT — a 16 × 4 RAM, write-only at `FFF0`–`FFFF` — replaces
the 6809's A12–A15 with a physical segment, so the byte the core asks for at `C000` may
live at `3000`. The core is not told. The card gives it a **private inner `Bus`** with one
board on it, the DAT port, which forwards every cycle to the backplane at the translated
address; the core, the backplane and its no-invention rule (§4) are all unchanged, and
BREAK MEM, TRACE and HISTORY see real **physical** cycles. The card is also a plain board on
the backplane: IC4, the S-BUG ROM, is decoded on the physical address (`F800`–`FFFF`).
Logical `FF00`–`FFFF` passes the DAT untranslated — an **inference** from S-BUG, whose reset
code lives there and loads the DAT (`reference/MP-09 6809 CPU Board.md` says why, and that
it is the first suspect if a boot misbehaves around `Fxxx`). The debugger's views keyed to
the PC ask the card where a CPU address lands (`CpuCard::toBus`, §10.2).

**The split still pays off, even though no 6800 card here masters the bus.** Making the CPU a `BusMaster` rather than a bus special case is what lets the monitor *find* and *step* the processor generically (`Machine::master()`/`masters()`, a `dynamic_cast<BusMaster*>`), and it is the door a future cycle-stealing card walks through unchanged: a DMA card is simply a `Board` that *becomes* a `BusMaster` when granted the bus, never a bolted-on path in the bus. The framework's S-100 machines used exactly that door for `pHOLD`/`pHLDA` DMA; none of the 6800 machines here do, so the transient-master half was removed — see §4.5 for the heritage note.

**The chip is not the card:**
- `src/cpu/` — `Cpu6800` and `Cpu6809`: pure instruction cores behind one `CpuCore` interface. No bus, no board, no config. Independently testable, which is what makes the diagnostic gate easy to run (§3.2).
- `src/boards/mits-680cpu.cpp` — the **6800 CPU card**: hosts a `CpuCore`, plugs into the bus, owns the clock property, pulls nothing but reads the IRQ/NMI lines the bus carries, and honors both resets. (It does **not** serialize — nothing does; see §4.)

A card's cores are **units** (§3.0.1) — a plain CPU card has exactly one, and a hypothetical dual-processor card has two with one active. Swapping the *card* is `BOARDS REMOVE` / `BOARDS ADD`, exactly as you'd swap the physical thing.

**The clock is the CPU board's property**, not the machine's: `SET cpu0 clock_hz=2000000`. It belongs to the card because that is where the crystal is, and because a backplane with no CPU card in it — which is what milestone 1a runs — has no clock rate to speak of.

### 3.0.1 A card may carry more than one processor, and they are units

**Settled 2026-07-11 by Patrick.** A card that carries more than one core — the framework's own case was an 8080 *and* an 8085 switching when a program does an `OUT` — is a real thing. So **cores are units** (§9), of kind `Cpu`, exactly one of them active. The 6800 machines here ship one core per card, but the model is unchanged:

```
swtpcsim> SHOW cpu0
cpu0  (6800)

  unit     kind    holds
    6800    cpu     active
```

**This needs no new bus concept whatsoever.** A multi-core card decodes the write that switches it, sets its own latch, and reports a different active core — structurally identical to bank switching on a memory card (§4.3): *the board keeps its own state, and the bus arbitrates nothing.* `Machine` asks the backplane which board is the bus master and which core is live; two cards claiming it is contention and we say so; **no CPU card at all is a real machine you can build** — the monitor is then the only bus master, which is how memory is inspected before any processor is fitted.

### 3.0.2 The disassembler belongs to the instruction set — not to the CPU, and certainly not to the card

**Patrick, 2026-07-11:** the SWTPC and Altair-680 CPU boards are different cards, but both run a 6800, so they must share the same 6800 CPU glue and the same disassembly.

So "CPU" is three things wearing one name, and they are separated:

| Layer | What it is | Lives in |
|---|---|---|
| **Instruction set** | a **stateless** disassembler: bytes in, text + length out. No registers, no state, no board. Named `"6800"` or `"6809"` — a registry key, exactly like `"memory"` is for `makeBoard()`. | `src/isa/` — `disassemblerFor("6800")` |
| **Core** | registers + execute. A plain object; **not** a `Board`. | `src/cpu/` — `Cpu6800` |
| **Card** | the thing you pull out with your hand: one or more cores, plus the serial port / boot PROM that happen to be on *this* card. | `src/boards/` |

Two 6800 cards that differ only in an onboard peripheral share the instruction set and the core **completely**, and differ only in the card — which is the only place they differ in reality. The thing they have in common is not the chip and not the board: it is *the way bytes decode*, and that is why it needs a name of its own to be shared by.

**A stateless disassembler runs with no CPU in the machine.** `DISASM E000 CPU=6800` works against the SWTBUG ROM whether or not a processor is fitted — the same argument that made the bus testable before the CPU existed (§15), and it means the 6800 decode tables get exercised independently of anything that executes them.

**But naming the CPU is not the normal case, and must not be** (Patrick). The active core reports which instruction set it speaks, and `DISASM` asks the machine:

```
which = the explicit CPU= argument
      | the active core's own answer      <- THE NORMAL CASE: you never type it
      | error: "no CPU in the machine -- say CPU=6800"
```

The fallout: on a card that switches cores, when the guest writes the register that switches it, **`DISASM` follows automatically** — "which instruction set" and "which core is active" are the same question, and it is already being asked. (The framework proved this on a dual-8080/8085 S-100 card; the 6800 machines here ship one core per card, but the wiring is the same.)

*(The word `CPU` does double duty — `DISASM ... CPU=6800` names an instruction set, while `BOARDS ADD 6800 cpu0` names a card. That is deliberate: `CPU=` is the word everyone already uses, and inventing a second one to be precise about a distinction the user does not have to care about would cost more than it buys.)*

### 3.0.3 Registers are reflection, and the debugger is a bus observer

**Registers use the same trick as `properties()` (§5).** A core exposes `registers()` — name, width, get, set — and *not* a `Regs6800` struct the monitor knows about:

```cpp
struct RegDef { const char* name; int bits; /* get, set */ };
```

Then `REGS`, `SET REG A=3F`, breakpoint conditions (`BREAK 100 IF A==0`) and the MCP schema **work for the 6809 with no monitor change** — which it did: the 6809 core landed with `D`, `DP`, `U`, `Y` and the `E F H I N Z V C` lamps and `REGS`, `SET REG`, `BREAK … IF` and MCP needed nothing. (`SNAPSHOT` would too, if it existed; §13.) It is the same bet that already paid for `SET`/`SHOW`/TOML/MCP: one schema, no second copy to drift.

#### 3.0.3.1 The status line: the core describes it, the monitor renders it

**Every stop prints one line** (Patrick, 2026-07-13) — because more than one line is what you read when what you wanted was to *glance*. On the 6800:

```
H0I1N0Z0V0C0 A=02 B=10 X=8004 SP=A03D PC=E201  ASRA
```

That layout is **not** generic — which registers are lamps, what each is *called*, and in what order are real differences between one processor and the next. But it is also **not** a formatter handed to the core: that would have put a `printf` where the reflection layer is, and every new ISA would owe the monitor a second copy of its register file. So `RegDef` grew a display contract instead, and the split is:

| | |
|---|---|
| **`name`** | **identity.** What you TYPE — `SET REG`, `BREAK … IF`, MCP. |
| **`label`** | **presentation.** What the status line CALLS it. |
| **`show`** | `Flag` (a lamp: label + one digit, no spaces, clustered at the front), `Field` (`LABEL=hex`, in the order the core listed it), or `Off` (reachable by name, but not on the line). |

The 6800 uses that directly: the six condition-code bits `H I N Z V C` are `Flag`s, clustered at the front in the order they sit in the CCR; `A`, `B`, `X`, `SP` are `Field`s. **PC is last, and nothing may come between `PC=` and the instruction**: they are the pair your eye reads together.

> **Heritage note.** The one-line format is DDT's, inherited from the framework's 8080 world, and the split above was forced by how idiosyncratically the 8080 wanted to render: the sign flag was *named* `S` but *labelled* `M` (minus), parity `P` printed as `E` (even), and register *pairs* (`BC`/`DE`/`HL`) were `RegDef`s in their own right over the same bytes as the halves, so `SET REG HL=BEEF` then `SET REG L=01` gave `H=BE01`. Name-is-identity, label-is-only-paint is what kept those non-ambiguous to the machine — and it is why the contract earns its weight even on the plainer 6800.

`bits` no longer means "flag": the renderer keys on `show`, never on width. A register narrower than a nibble prints as a number rather than a hex digit.

**Breakpoints and tracing are NOT CPU features.** If a core owned them, every core would reimplement them and they would differ in subtle ways. They don't belong there:

- **`BREAK MEM R|W`, `TRACE`, `HISTORY`** are questions about **bus cycles**, and every cycle is present on the backplane; the debugger observes it from outside (`Bus::observe()`, §4.2.2). **CPU-agnostic, and the machinery already exists.**
- **`BREAK <addr>`** is the only CPU-flavoured one, and it is just *"PC equals X after a step"* — one comparison against a register the reflection layer already exposes.
- **`BREAK <kind> <action>`** is a third plane: a **device-event** breakpoint, first member `BREAK TAPE STOP` (halt when a cassette deck reaches its auto-stop mark, so a load can be caught without knowing the loader's end address). There is no bus cycle for *"the tape ran out"*, so it is neither a cycle observer nor a PC compare — the run loop **polls** each board's `takeAutoStop()` edge latch at the instruction boundary and stops there, exactly as `SET BUS UNCLAIMED=HALT` samples its latch (§4.6.1). One table (`kDeviceEvents` in `core/debug.h`) drives the parser, `describe()` and the poll together, so a future member (`PRINTER PAGE`, `LINE CARRIER`, `DISK SEEK`) is a single row. It is still CPU-agnostic — the CPU is never asked anything — so it inherits onto any core for free, the same as the other two.

So the debugger lives in `Machine`, drives `cpu->step(bus)`, and asks only generic questions. **The 6809 card inherited the entire debugger for free.**

What it did *not* inherit for free were two questions the monitor used to answer from 6800 knowledge, and they became core hooks rather than `if (isa == "6809")` in the monitor: **`vectors()`** (the fixed vectors at the top of memory — three for the 6800 plus reset, six plus reset for the 6809 — which `SHOW BUS IRQ` prints) and **`waitingOn()`** (what a parked core is waiting for — `WAI` on the 6800, `CWAI` or `SYNC` on the 6809 — which the halt message names). The disassembler likewise gained **`maxLen()`**, so the `DISASM` byte column is wide enough for a 5-byte 6809 instruction without padding every 6800 line.

### 3.1 Core semantics

- **Dispatch:** 256-entry table of function pointers or a `switch`. *Not* a bit-pattern if/elif chain (the Python prototype's linear scan).
- **cycles:** counted per instruction — the 6800's are fixed per opcode (a relative branch is 4 whether it is taken or not; the longest, `SWI`, is 12). Unlike the Python prototype, the count is **load-bearing** — it drives `Board::tick()`, baud rate, disk rotation, and the clock throttle.
- **Flags:** the 6800's condition-code register is `H I N Z V C` — half-carry, interrupt mask, negative, zero, two's-complement overflow, carry. The traps are the half-carry (bit 3, which `DAA` reads after a BCD add) and the `V`/`C` distinction on `SUB`/`CMP`; get those wrong and signed comparisons and packed-BCD arithmetic drift silently.
- **Interrupts** (absent from the Python prototype — its interrupt bits set a flag nothing read): the `IRQ` (maskable) and `NMI` lines the bus carries, plus `SWI` and `WAI`, each taking its handler from a **fixed vector at the top of memory** (§4.4). There is no acknowledge cycle and no instruction fetched off the bus.
- **`step()` returns an explicit `StepResult { uint32 cycles; Status status; }` — never a sentinel value.** See §16 for why (the RLC/RRC bug).

### 3.2 Validation is a gate, not a nice-to-have

The CPU is not "done" until it passes its opcode-and-flag suites and then boots **real period software**. For the 6800 that is `tests/test_cpu6800.cpp` and `tests/test_isa6800.cpp` — every opcode, addressing mode, and condition-code effect — backed by the acceptance tests that boot **SWTBUG**, **MON680**, and **FLEX** on a whole machine through the real CLI (`tests/acceptance/`). That last gate is the one that matters most: an OS exercises corners no hand-written unit test thinks to.

**A validation harness may not emulate the thing it is validating.** The framework established that rule on its 8080 core, and the 6800 inherits it. The 8080 gate ran the period **TST8080 / 8080PRE / CPUTEST / 8080EXM** CP/M `.COM` suites with *no* CP/M and *no* console card — via a BDOS stub written in **real 8080 machine code**, reached through the real `JMP` at `0005`, writing to a real port on a real board. Trapping `PC == 0005` in C++ would have been less code and was rejected: it would fake the `CALL`, the `RET`, the stack and the `OUT` inside the one program whose job is to check that we implement them correctly.

**The 6809 has passed both halves of that gate.** `tests/test_isa6809.cpp` decodes every opcode on all three pages and round-trips each one through the assembler. `tests/test_cpu6809.cpp` executes each instruction group with its flag rules and cycle counts, every indexed post-byte form, the interrupt stacking (`E` set for the full frame, clear for `FIRQ`), `CWAI`/`SYNC`, and NMI disarmed until `S` is loaded. The expected values come from the Motorola programming manual. On a whole machine, `tests/acceptance/flex9.exp` boots the `swtpc09` machine through the real CLI: S-BUG loads the MP-09's DAT from its power-up junk and signs on, its `U` command boots **FLEX9 2.8:3** off the DC-4, and `CAT` reads the directory back.

*(A dedicated 6800 or 6809 exerciser of the 8080EXM kind is not in the tree yet.)*

---

## 4. The bus — the load-bearing spec

The bus carries signals and moves bytes. It invents no behavior, and boards never see each other.

```cpp
enum class Cycle { MemRead, MemWrite };

struct BusCycle {
    Cycle    type;
    uint16_t addr;      // the memory address
    uint8_t  data;      // valid on writes; BACK-FILLED on reads before the observers see it
};

class Board {
public:
    virtual ~Board() = default;
    virtual const char* type() const = 0;

    // Decode: does this board respond to this cycle?
    // Returns false when the board is disabled or banked away — see §4.2.
    // Decode is STATE, not just address math.
    virtual bool     decodes(const BusCycle&) const = 0;
    virtual uint8_t  read (const BusCycle&)       = 0;
    virtual void     write(const BusCycle&)       = 0;

    // NOTE there is deliberately NO tick(dt). An earlier draft had one, and a
    // per-cycle tick on every card is the polling loop 7.5 exists to avoid: a
    // card that wants to do work at a time schedules a DEADLINE on the Clock,
    // and a card that must notice host input is pump()ed. On a quiet line a
    // board schedules nothing at all. See 7.5.

    // Interrupts. A board PULLS the IRQ line and holds it; the bus reads the wire and
    // does NOT invent a vector — see §4.4 and §4.4.1.
    //
    // COMBINATIONAL AND PURE, exactly like decodes(). The board announces a change
    // with intChanged(); the bus caches the wire-OR. (This was non-const, and there
    // was a section defending that. Reversed 2026-07-12 — see §4.4.1.)
    virtual bool     assertsInt() const { return false; }   // pulling the IRQ line?
    void             intChanged();                          // "my pin moved"

    // Configuration reflection — see §5.
    virtual std::vector<Property> properties() = 0;   // by value, and NOT const:
                                                     // a card may compute its list. See 5.

    // Reset — see §6.
    virtual void     reset(Reset kind) = 0;

    // State (snapshot / restore) -- BUILT for SNAPSHOT/RESTORE (13). Every board,
    // the CPU core and the Clock implement serialize()/deserialize() over
    // StateWriter/StateReader (core/statefile.h); Machine::snapshot()/restore()
    // drive them, and the two monitor commands drive that. A board writes its
    // RUNTIME STATE and re-arms its own Clock deadlines on the way back in; config,
    // host handles and derived caches do not travel (13.1). RECORD/REPLAY, which add
    // a T-stamped event log on top, are NOT built yet and resolve at the prompt.
    virtual void serialize(StateWriter&) const;
    virtual void deserialize(StateReader&);
};
```

### 4.0 This is the SS-50/SS-30 bus — and the framework's lineage is the Altair 8800's

The bus modeled here is the **SWTPC SS-50/SS-30 backplane**: a flat 64 KB address space with the CPU and memory on the SS-50 motherboard, and I/O boards decoded in the SS-30 window at `$8000–$801F` (eight four-byte slots). Everything is **memory-mapped** — there is no separate I/O port space, and no 8080 status byte broadcast on every cycle. A cycle is a memory read or a memory write, and nothing else (§4.1).

> **Heritage note.** The framework came from `altairsim`, whose bus was the **1975 Altair 8800 / S-100** bus — a 100-pin backplane built around a front panel that halted the processor, seized the address and data buses using the 8080 status lines, and single-stepped memory. None of that hardware ships here: there is no front panel, no S-100 status byte, no `PHANTOM*` overlay line (§4.2), and no `pHOLD`/`pHLDA` DMA arbitration (§4.5). What carried over is the *shape* — a `BusCycle` a board answers, a decode a board owns, an interrupt line a board pulls — because that shape is bus-agnostic and describes the 6800's SS-50 backplane as faithfully as it did the 8080's S-100 one. Where a section below still reaches for an S-100 example to make a point, it is kept as contrast, not as a description of shipped hardware.

### 4.1 Registration and decode

A board declares its memory address ranges at construction (from its TOML config). The bus builds decode tables. `decodes()` is the second-level check for boards whose decode is conditional — enabled/disabled, bank-selected, or drive-selected.

### 4.2 PHANTOM* — a heritage note, and why the bus still arbitrates nothing

**No shipping board shadows memory, so `PHANTOM*` is gone from the code** — the `phantom`/`honors_phantom` straps, `Board::assertsPhantom`, and the `BusCycle::phantom` pass were all removed. It is recorded here because the *principle* it demonstrated is the one this whole chapter turns on.

On the S-100 machines the framework came from, a boot ROM could overlay the RAM beneath it by pulling `PHANTOM*` (pin 67); memory boards strapped to honor it disabled their own drivers while it was low. **The bus arbitrated nothing** — the overlay was *emergent*, the ROM simply being the only board still answering because the RAM had switched itself off. That was the whole lesson: an overlay is two ordinary board behaviors (one board asserts, another honors), never a bus special case that picks a winner and routes reads to one card and writes to another.

The 6800 machines here don't need it — the SWTPC and Altair-680 memory maps place ROM and RAM in disjoint ranges, with no shadowing — so the mechanism was cut rather than carried as dead weight. But the rule it taught stands and is enforced everywhere below: **a board answers only questions about itself; the bus carries signals and moves bytes, and decides nothing.** (Writing a ROM is still not a bus operation — the operator reaches behind the bus into the board's store, `LOAD … ROM`, §10.2; a real backplane cannot tell who originated a cycle, and no board should ever have to ask.)

### 4.2.1 Boards enable and disable — decode is state

**Decode is state, not address arithmetic.** A memory board is not merely "at 0000–7FFF"; it is at 0000–7FFF *when it is enabled*, and on *the live bank*.

A board must be able to turn itself on and off, and there are three honest ways it happens — all ordinary board behavior:

| Trigger | Mechanism |
|---|---|
| **The guest writes the board's control address** | An ordinary memory write the board decodes, mutating its own decode state. **Not a bus special case** — same shape as bank select (§4.3). |
| **Reset** | `reset(PowerOn)` restores the power-up state; `reset(Bus)` may or may not, and the board's `.md` must say **concretely** which (§6). Get this wrong and you get the classic "boots from power-on but not from the reset button." |
| **The operator** | *Planned, not built:* `SET mem1 ENABLED=OFF` as a runtime `properties()` value like any other, for debugging. The first two mechanisms are real; this third one is not. |

> **Heritage note.** The mechanism was built for the S-100 pattern of a **boot ROM that appears at low memory on reset, runs, and switches itself out** so the RAM underneath becomes visible — the guest writing the ROM board's own port as nearly its last act. The 6800 machines here don't do that: SWTBUG lives at `$E000`/`$FC00` and stays there. But the *field* is general — a board's decode is state that reset restores — so it stays.

`enabled` was to be a **standard runtime property on every memory board**, with
`SHOW BUS MAP` showing it, because a board that is present but decoding nothing is otherwise
invisible and maddening.

> **NEITHER HALF SHIPPED.** `Board::enabled_` exists as a plain C++ field with a non-virtual
> setter (`src/core/board.h`), reachable from no `Property` — so `SET mem0 enabled=off` is refused,
> and `MemoryBoard::properties()` declares `fill, seed, pages` and no `enabled`. The reset path is
> real and exercised; only the operator's switch and the display of it are missing. The argument
> for them still stands exactly as written.

What `SHOW BUS MAP` actually prints is the range, the board, what it is, and the unmapped
remainder — no `state` column and no `notes` column:

```
swtpcsim> SHOW BUS MAP
MEMORY
  0000-7FFF  mem0     ram  
  8004-8007  mps0     read/write 6850 ACIA 'tty' -- A0 selects: base/base+2 status/control, base+1/base+3 Rx/Tx data
  8014-8014  dc40     drive select D0-D1 drive 0-3, D6 side (write-only, true-sense)
  8018-8018  dc40     command/status WD179x
  8019-8019  dc40     track WD179x
  801A-801A  dc40     sector WD179x
  801B-801B  dc40     data WD179x
  A000-BFFF  mem0     ram  
  E000-E3FF  mem0     rom  builtin:swtbug
  FC00-FFFF  mem0     rom  builtin:swtbug
  unmapped: 8100-9FFF,C000-DFFF,E400-FBFF  (floats to FF)
```

The last line earns its keep: a hole in memory that reads `FF` forever is the single most
confusing thing a backplane can do to you, and it is invisible unless something says so.

Note that one board occupies **several disjoint ranges** — `mem0` carries RAM at `0000-7FFF` and
`A000-BFFF` and ROM at `E000-E3FF` and `FC00-FFFF`, because a real card carries several populated
regions with empty sockets between them (`docs/boards/s100-memory.md`). The map is per-*range*,
not per-board.

### 4.2.2 The cycle stream — the debugger is a bus observer

**The address bus is not addressed to anyone.** Every cycle is simply present on the backplane, and the debugger watches that same stream from **outside** the backplane. `Bus::observe()` hands a callback the finished cycle — type, address, and the byte that was driven, back-filled on reads (`Bus::settle()`) — and that callback is the entire implementation of `BREAK MEM`, `TRACE` and `HISTORY` (§3.0.3).

They are therefore **not CPU features**: they cost the cores nothing, they are questions about bus cycles answered at the bus, and a 6809 or any future core inherits every one of them on the day it lands without writing a line. A cycle is a cycle, whatever drove it.

An observer is **armed only while the machine is running.** The monitor's own `DUMP` and `DEPOSIT` are real bus cycles — that is the point of them — so an always-armed `BREAK MEM W` would "fire" on the operator's own `DEPOSIT`, with no program running to stop and nothing sensible to report.

A `BREAK MEM` must stop the machine *before* the access, with the PC still on the instruction — but an observer sees a cycle only *after* the device has driven it. So that one case is a separate hook, the **pre-access veto** (`Bus::setPreAccessVeto`, `src/core/bus.h`), consulted at the top of every cycle; `TRACE` and `HISTORY`, which only record what happened, ride the observer.

### 4.3 Banking — heritage evidence that boards must own their decode

No shipping 6800 machine here banks memory, so there is no `bankmem` board — but the argument it settled is why this whole chapter puts decode in the board and not in the bus, and it is the sharpest case there is.

On the S-100 machines the framework came from, a banked card registered a bank-select port; a guest write mutated the board's **own** state to choose which 64K plane was live — an ordinary write the board decoded, never a bus special case, the same shape as §4.2.1's enable/disable. **What a bank select *did* was the board's business, and the bus stated no rule about it.** Four real cards proved a generic "bank number" would be a lie: the **Vector Graphic 64K** and the **Cromemco 64KZ** sat on the *same* port `0x40` with **incompatible** encodings — one selected one-of-eight, the other was an 8-bit mask that held several banks live at once — while the **North Star HRAM** used an on/off-plus-one-hot toggle that was not a bank number at all. A single "write a byte, get a bank" abstraction cannot express those together, so the board must own its decode or the model silently corrupts whichever card the code was not written for.

**The board owns its decode, or the model is a lie** — and on the 6800 the same rule shows up without banking at all: the memory board answers only its own populated ranges, the DC-4 gates its registers on a drive-select latch, and the MP-S picks one register out of its window with address bit A0. Two boards that decode the same cycle is contention (§4.6), caught and named exactly as a real backplane would have handed it to you.

### 4.4 The interrupt model

The 6800 has two hardware interrupt inputs — **IRQ** (maskable, gated by the `I` bit in the CCR) and **NMI** (a non-maskable edge on a dedicated pin) — plus the software **SWI**. Every one of them takes its handler address from a **fixed vector at the top of memory**: IRQ `$FFF8`, SWI `$FFFA`, NMI `$FFFC`, RESET `$FFFE`. There is no acknowledge cycle on the bus, no priority encoder, and no card that jams a vector onto the data bus. When the `I` mask is clear and the IRQ line is pulled, the CPU finishes the current instruction, stacks its state, reads its handler address straight out of `$FFF8`, and goes there.

So the bus does exactly one thing with an interrupt: it carries **IRQ as the wire-OR of every board pulling it**, and hands the CPU a level — nothing more. A board's interrupt output is wired straight to that line; on the SS-30 serial boards the ACIA's IRQ pin *is* the line, with no strap in between. A board that wants to interrupt pulls the wire and holds it; the CPU decides whether to take it from its own `I` mask, and clears it by servicing the chip. The bus never picks a winner and never invents a vector — there is nothing to arbitrate on a single shared wire.

> **Heritage note.** The framework this fork grew from carried the S-100 *vectored*-interrupt model: a board pulled `pINT` (pin 73), the CPU ran an **`IntAck`** bus cycle, and either a card jammed an `RST n` onto the data bus or the floating bus read `0xFF` = `RST 7`; an **88-VI** card watched eight prioritized `VI0–VI7` lines and drove the winner. None of that is 6800 hardware — the 6800 vectors through fixed memory, it does not fetch an interrupt instruction off the bus — and it was removed along with the S-100 boards that used it. What survives is the one idea that was always the point, and it is 6800-true unchanged: **the board pulls the wire; the bus does not go and ask.**

#### 4.4.1 The board **pulls** the IRQ line. The bus does not go and ask.

**Corrected 2026-07-12 by Patrick, and this reverses what this section used to say:**

> "Does the bus poll each board for interrupt status, or does a board set interrupt flags that the bus simply checks? **In a real system, the bus doesn't poll a board for interrupt status. The board sets high/low signals on the bus that the CPU reads from the bus. The board then clears the int signal based on its design.**"

He was describing the hardware. He was also describing a bug.

`Bus::intPending()` used to walk the backplane and ask every card `assertsInt()`, **once per instruction** — sixty million times a second, to compute a boolean that changes perhaps a thousand times a second on a busy machine. Once the decode was cached (§4.7) it was the single largest per-instruction cost left in the simulator, and it grew with every card you added: **6.6 ns with two boards, 15.6 ns with six.**

**The interface now is the exact analogue of the decode**, and the two hard-won rules turn out to be one rule:

| | combinational, pure | "it moved" | the bus |
|---|---|---|---|
| address decode | `decodes()` | `decodeChanged()` | caches a page table |
| interrupt | `assertsInt() const` | `intChanged()` | caches a wire-OR count |

> **A board's outputs are pure functions of its state. When its state changes, it says so. The bus caches the rest.**

Reading the IRQ line is now an integer test — **1.8 ns, flat in the number of cards in the machine.**

##### What the poll was secretly doing, and where it went

This section previously argued — at length, and correctly, as far as it went — that `assertsInt()` **could not be `const`**, because a board may have to *do something* to answer honestly. A 6850 has to notice that a character has finished arriving, which happens **on the chip's own clock**, with no help from the CPU and no bus cycle involved. The bug that established it is real and is still worth reading:

> **An interrupt-driven driver never reads the status port. Not reading it is the entire point of being interrupt-driven.** So `RDRF` was never set, IRQ never rose, the interrupt never fired, and the operator could type forever with nothing happening.

All of that is true. The mistake was the conclusion. **The card's free-running work is real; making a query do it was not.** The receiver was being advanced inside `assertsInt()` for one reason only: *being asked was the only thing that ever woke the card up.* The poll was serving as the card's clock.

So the work moved to where it belongs, and `assertsInt()` became `const` and pure:

- a **deadline** the card sets for itself (`Clock::at`, §7.5) — for anything emulated time can predict, such as the transmit register draining;
- **`pump()`** — for anything it cannot, such as a human touching a key.

**And that made the model *more* capable, not less.** With the poll, the card could only ever act at a moment when someone was already asking it something. A guest that enables the transmit interrupt, sends a character and parks in `WAI` — an entirely ordinary driver — was **declared finished by the run loop**, two thousand cycles before the interrupt it was waiting for. Nothing was pulling the IRQ line *yet*, and the old `WAI` check could not tell "nobody is interrupting" apart from "nothing ever will." A card that owns a deadline can. See `tests/test_sio2.cpp` — *"NOBODY IS ASKING: the card acts on its own deadline."*

##### The stale wire is not left to trust

A board that moves its interrupt pin and forgets to call `intChanged()` hangs the guest forever, waiting for an interrupt that already happened — and presents as *"the emulator locks up sometimes"*, which is worth a week of anyone's life. So it is checked, exactly as the decode cache is: **`Bus::setVerify(true)`** re-derives the whole wire from every board's `assertsInt()` on every instruction and aborts on the first disagreement. The unit suites run with it on permanently.

### 4.5 DMA — a heritage note; `BusMaster` stays, the arbitration is gone

`BusMaster` — the interface that *drives* cycles onto the bus, as opposed to a `Board` that *responds* to them — is a first-class concept and it stays: the CPU card is one (§3), and `Machine::master()`/`masters()` find it with a `dynamic_cast<BusMaster*>`, which is also how the monitor lists the machine's processors.

What was removed is everything that let a **second, transient** master take the bus. On the S-100 machines the framework came from, a DMA card asserted `pHOLD`, the bus granted `pHLDA` at the next instruction boundary, and the card drove its own cycles until it released — a disk controller stealing cycles from the processor. No 6800 machine here has a non-CPU bus master, so `Board::requestsBus()`/`busMaster()`, the grant, and the debugger's per-boundary `serviceDma()` were all cut. The idea was sound and additive — DMA was never a special path in the bus, only the same `BusMaster` interface the CPU already used — so if a cycle-stealing 6800 card ever ships, it returns the way it left: one `Board` that *becomes* a `BusMaster` when granted the bus, with no bus change. Every cycle on the backplane today originates at the CPU, which is why the debugger renders the driver of a cycle as a literal `cpu` (§3.0.3) rather than storing an origin the bus does not carry.

### 4.6 Contention

When two boards `decodes()` the same cycle, log a diagnostic naming both board `id`s and the address, and return the wire-AND of the drivers (real tri-state / open-collector contention pulls low). This is a **feature** — it is the bug you'd chase on a real backplane, surfaced immediately.

`SET BUS CONTENTION=WARN|ERROR|SILENT`.

The test is who **actually** `decodes()` the cycle, not who is registered near the address — and reads and writes decode separately, so a ROM region that never answers a write does not collide with whatever takes that write. Contention is two boards both driving the *same* cycle; the diagnostic names both, exactly as a real backplane would have handed it to you.

### 4.6.1 The floating bus — one rule, two consequences

> **If no board drives the bus, it floats high. Every read of an unmapped address returns `0xFF`. Writes go nowhere.**

This is a single rule, and two things the design would otherwise need special cases for fall straight out of it:

| Situation | What happens | Why it matters |
|---|---|---|
| **Unpopulated memory** | Reads `0xFF` | Period software **sizes memory by reading**. Return `0x00` instead and every machine looks like it has 64K, and the OS builds itself wrong. A zero-filled hole also disassembles as a field of `NOP`s, which is a uniquely confusing thing to stare at. |
| **Unpopulated I/O slot** | Reads `0xFF` | I/O is memory-mapped (SS-30, `$8000–$801F`), so a device is just an address. A guest probing for a board that isn't fitted gets the same answer real hardware gives it. |

Model the floating bus honestly once and both are free. Fake either one and you will fake the other differently.

#### `0xFF` belongs to the bus, and to nothing else

> **Providing `0xFF` when no board answered is the ONLY thing the bus does that a board does not.** That is the entire bus/board overlap, and it stays that size. — *Patrick, 2026-07-11*

So **no board may ever manufacture `0xFF`**, and in particular **no board may seed its own store with it.** A RAM chip does not power up holding `0xFF`; it powers up holding whatever it feels like, which is what `fill = random` is for, and *what a card's chips contain is that card's business* (`docs/boards/s100-memory.md`). The bus does not initialize anyone's memory and has never heard of `fill`.

The failure this prevents is a quiet one, and it was in this code until Patrick caught it. Seed a board's store with `0xFF` — it is the obvious "uninitialized" filler — and `DUMP` shows `FF` for a card whose RAM is fine, `FF` for a card whose RAM was never filled, and `FF` for a card that **isn't in the machine**. One symptom, three causes, and you chase the wrong one. The moment a board can produce `0xFF`, the single signal the bus has stops being a signal.

`tests/test_boundary.cpp` enforces it. Shared *helpers* that several boards call are fine — that is code reuse, not the bus growing opinions.

**But silence is how an unimplemented device becomes an unexplained hang.** The behavior above is correct and must stay; the *diagnostic* is separate. `SET BUS UNCLAIMED=WARN|HALT|SILENT` (default `SILENT`) names the address and the PC:

```
warning: write C000 <- 01 at PC=0113: no board decodes address 0xC000. the byte is gone.
```

`WARN` logs the line and runs on; `HALT` logs it and stops the guest at that instruction boundary — the hang caught in the act, so `SHOW REG` and a `DUMP` are looking at the machine as it wedged. On the memory-mapped 6800 every device is an address, so it is an **undecoded memory access** — an unpopulated SS-30 slot, or a runaway jump into empty space — that this catches, de-duplicated to once per address and direction per run, so a poll loop on an absent UART reports the address once rather than burying the console. The default is `SILENT`: the diagnostic is opt-in, so no machine that was quiet becomes noisy, and the hot path pays a single compare when it is off. Reach for it — `SET BUS UNCLAIMED=HALT` — when a machine you assembled yourself hangs with nothing on the screen.

### 4.7 Fast path

Plain unbanked RAM resolves to a direct pointer through a page table; the full `BusCycle` dispatch runs only for pages with a non-trivial decoder. Document this so nobody "optimizes" it away later.

---

## 5. Board properties — one generic interface, board-specific contents

`SET sio2a BAUD=9600` must work without the monitor knowing what a baud rate is. So a board **describes its own configuration**, and the monitor, TOML loader, MCP server and `CONFIG SAVE` all drive that one description. (Tab completion was to be a fifth consumer; it is designed but not built — §10.4.)

```cpp
struct Property {
    std::string  name;         // "baud"
    Type         type;         // Int | Enum | Bool | String | Hex | Path
    std::string  description;  // "Serial line rate"
    std::string  units;        // "bps"
    Constraint   constraint;   // range, or enum value list
    Getter       get;
    Setter       set;          // returns an error string on reject
};
```

Consequences, all of which must be stated in the doc:
- `SET <id> <k>=<v>` and `SHOW <id>` are **fully generic**. `SHOW` prints every property with value, units and legal range.
- **MCP tool schemas are generated from `properties()`** — Claude gets typed, constrained, self-documenting board config instead of guessing at free text.
- **The TOML loader and `CONFIG SAVE` are the same code path.** A board's config keys *are* its properties, so round-tripping is automatic and cannot drift.
- **Tab completion would be generated from `properties()`** too — designed, **not built**; there is no Tab handling in the line editor (§10.4).
- **A LIST of things is a sub-unit, and it round-trips the same way** — regions on a memory card, drives on a controller. `subUnitTables()` + `addSubUnit()` read them; **`subUnits()` writes them back** (built 2026-07-12). The board renders its own text, because only the board knows that an address is hex and zero-padded (`at = 0x0400`) while a size is decimal with a suffix (`size = "48K"`) — `Value::text(16)` produces neither, and a writer that guessed would be a second, worse copy of what the board already knows. The claim "`subUnits()` is `addSubUnit()`'s inverse" is therefore one a test can simply *execute*: render, feed it straight back in, compare.

  **This closed the last board-specific line in the config layer.** `CONFIG SAVE` used to reach for a `dynamic_cast<MemoryBoard*>` to write `[[board.region]]`, which meant any *other* board with a sub-unit table — a disk controller with four `[[board.drive]]` entries — would **load and silently not save**. You would configure the machine, save it, and get a controller with no drives. `src/config/toml.cpp` now includes no board header at all, and it should never include one again.

- **…and a sub-unit's KEYS are declared, like everything else** — `subUnitProperties(table)` (built 2026-07-14). **This was the one place the claim above was false, and it was the worst possible place: the keys that carry the disk, the ROM and the write-protect tab.** `unit`, `mount`, `readonly`, `media`, `type`, `at`, `size` were known to nothing but a chain of string compares inside each board — so they appeared in no generated reference, no MCP schema and no `SHOW`, and each board hand-wrote its own validation and its own `else { err = "no such key"; }`. That is a **second schema**, written four times, agreeing with the documentation by luck. It was found the way such things always are: Patrick asked me to file a read-only disk mount as a *missing feature*. It was not missing. It had worked for weeks. It was **undiscoverable**, and the reason it was undiscoverable is that nothing in the program could enumerate it.

  A sub-unit key is an ordinary `Property` with **no `get` and no `set`** — the half that is a *description* (kind, choices, range, radix, help) without the half that is an *accessor*, because the drive it describes **does not exist yet**. That is exactly the half a validator, a schema and a documentation generator need, which is why there is no second struct. `Board::loadSubUnit()` is the **one door** — it checks the table, the keys and the values against that declaration and only then lets the board build — and `addSubUnit()` is now **protected**, so the TOML loader, `REGION ADD` and the tests cannot go around it. What is left inside a board is *construction*, plus the one thing a schema cannot express: that `type` is **required**, that a drive needs a `unit`.
- **THERE IS NO CONFIG-TIME-ONLY PROPERTY** (Patrick, 2026-07-12). Every property can be set, always. There was a `runtime` flag here that rejected a SET while the machine ran; it is gone, for two reasons and the second is the real one:
  - **You can only type at the prompt when the machine is STOPPED** — by ATTN, by a breakpoint, by a HLT. That is the front panel's STOP switch. There is no moment at which a `SET` races a running CPU.
  - **On real hardware the rule would be a fiction anyway.** A card being worked on sits on an **extender**, out where you can reach it, and its jumpers get moved with the power on. That is ordinary practice on real boards, not an abuse of them (Patrick, 2026-07-12).

  It was also never enforced: nothing in the simulator ever set the flag the gate was conditioned on, so it had never once fired. A rule the code only pretends to enforce is worse than no rule.

### 5.4 A card can bring a VERB with it (built 2026-07-12)

**`REWIND` should exist when there is a cassette in the machine, and not otherwise.** Putting it in the static command table would mean a verb that is always spelled and never usable on most machines — and the monitor would have to know what a tape *is*, which is exactly the knowledge §7.7 keeps out of it.

```cpp
virtual std::vector<CommandDef> commands() const { return {}; }   // a static table; empty for most
virtual bool runCommand(const std::string& name, const std::vector<std::string>& args,
                        std::ostream& out, std::string& err);
```

`CommandDef` moved from `cli/commands.h` down to **`core/command.h`**, and that move is the whole layering argument: a board may declare a verb, and **a board must never include the CLI**. The dependency runs `cli → core`, one way. The *table* of built-in commands stays up in `cli/`, where it is the monitor's business and nothing in `core` has an opinion about it.

**THE STATIC MENU ALWAYS WINS.** The monitor prefix-resolves against the built-in table first, in its existing priority order, by code that has never heard of boards. Only when *nothing* built-in matches does it ask the cards. So **no card can shorten, shadow or destabilize a built-in abbreviation by being plugged in**: `D` is DUMP and `RE` is REGS on every machine ever booted, whatever is in the slots. `REWIND` is reachable at **`REW`** precisely because the built-ins already own `R` (RUN), `RE` (REGS) and `RES` (RESET) — and `REW` is the first prefix none of them claims.

That ordering has a price, and it is paid in the right place. A card *can* declare a verb **nobody can ever type** — one whose every prefix a built-in claims first. **A user cannot create that; only a board author can**, so it is caught as a **merge gate** in `tests/test_cli.cpp` (over every type in the registry), not at runtime where the error message would be about a word somebody typed rather than about the card that is wrong. This is the same shape as the older invariant it sits next to — *no command name may be a strict prefix of another* — which is also a test and not a runtime check.

**A board verb's first argument names the board** (`<id>` or `<id>:<unit>`), read exactly as MOUNT and CONNECT read it. It has to: two 88-ACRs both declare `REWIND`, and the verb alone cannot say which tape to wind. The monitor enforces that convention once, so no board reimplements it.

---

## 6. RESET semantics

The Altair bus has two distinct reset lines, and boards must honor both. Conflating them is the classic source of "works from power-on but not from the reset button" bugs.

```cpp
enum class Reset {
    PowerOn,   // POC* (pin 76) — power-on clear. What it does is BOARD-SPECIFIC;
               // the board just needs to know it happened. Nothing in software can
               // assert it — only the power supply does.
    Bus,       // RESET* — the front-panel reset button. Warm. CPU PC<-0, boards reset
               // their logic, but memory contents SURVIVE and mounted media / connected
               // streams stay attached.
};
```

**Neither reset clears memory. Only removing power does.** This is not a nuance, it is the rule, and the memory array is the proof: a RAM chip has no POC\* pin. Its contents are indeterminate at power-up because *the chips just powered up*, not because a signal arrived. POC\* and power coming up coincide on real hardware — which is why they share the `POWER` command — but they are different things, and a board that clears its store in its `Reset::PowerOn` handler is modeling a machine nobody built. Model the fill as belonging to **power**, and let both resets leave the store alone.

Get this backwards and it shows up as *"my program vanished when I hit reset"* — which reads like a memory-model bug rather than a reset bug, and costs you a day.

### 6.1 A bus reset does what the board does — and if the board does nothing, it does nothing.

The vocabulary, because two of these get called "reset" and they are not the same signal:

- **Bus reset** — `RESET*`, the front-panel RESET switch. `Reset::Bus`.
- **Power-on-clear** — POC\*, the delayed clear that comes up with the power supply. `Reset::PowerOn`. Nothing in software can assert it.
- **Software reset** — whatever the *guest* writes to a chip to reset it. Not a bus signal at all, and not this section's business — except to say it is **modeled exactly**, because guest software can see, time and depend on every part of it. The 6850's master reset is the worked example: writing `11` into the divide field *latches*, holds the chip down, and inhibits TDRE until a second control write releases it, which is why every 6850 driver does two `OUT`s and why the card is dead after only the first. Round that corner and period software mysteriously half-works.

**The rule for the two bus signals: a board does on a reset exactly what the real board does — and a board that does nothing does nothing.** It is tempting to have every card scrub itself clean on `RESET*` because it feels safe, and it is exactly backwards: a card that resets more of itself than the hardware's reset line physically reaches is inventing a machine nobody built, and the invention is *destructive*. The 88-2SIO is the case that proves it. The MC6850 **has no RESET pin** — 24 pins, and RESET is not among them — so `RESET*` reaches that card's address decoding and nothing else. This tree used to reset both ACIAs on `Bus` anyway, which threw away the guest's word format and interrupt enables *and ate a byte out of the receive register*, on a card where a real bus reset would have preserved all of it.

So: **what a board does on each of the two signals is a fact about the board, it comes from the manual and the data sheet like every other fact (§0.1), and the board's `.md` must state it concretely.**

**What we do *not* model is the exact timing or the internal sequencing of power-on-clear.** A real 6850 comes up held in an internal reset that is only released by the guest's first master reset; we don't reproduce that, because nothing can observe it that does not also program the chip. `Reset::PowerOn` simply leaves every card in a known good state **immediately**, so that a machine is usable the moment it is switched on. That is the one place a bus reset is allowed to be pragmatic rather than literal.

**What POC\* does is board-specific**, and each board's `.md` must say **concretely** what each reset does to it. Examples:
- **`memory`**: both resets clear the bank-select latch to 0 and touch nothing else. `POWER` re-fills RAM regions per `fill` and re-reads ROM regions from their files. See `docs/boards/s100-memory.md`.
- **A boot ROM that disables itself** (§4.2.1): `PowerOn` **re-enables it** — otherwise the machine boots exactly once and never again. Whether `Bus` (the front-panel reset button) also re-enables it is a **board-specific strap, and the board's `.md` must say which.** This is the single most likely place to produce the classic "works from power-on, dead from the reset button" bug, because a warm reset that leaves the ROM switched out drops the CPU onto RAM at 0000 and it executes garbage.
- **88-2SIO**: `Bus` does **nothing at all** to the two 6850s — the chip has no reset pin, so `RESET*` reaches the card's decode logic and stops there. `PowerOn` puts each chip in a known good state (`Mc6850::powerOn` — clears RDRF, zeroes the control register, asserts RTS) and **keeps the `ByteStream` connected**. Neither is the 6850's *master* reset, which is the guest's, arrives as a control byte, and does not clear the control register. See §6.1.
- **88-DCDD**: deselects all drives, unloads the head, invalidates the sector counter on both — but **keeps images mounted**, and does *not* seek to track 0 on a warm reset (real drives don't).
- A DMA board must release the bus.
- CPU: PC←0, interrupts disabled; Z80 also `I`/`R`←0 and IM 0.

CLI:
```
RESET        RESET* / pRESET — "press the reset button". Warm; memory survives.
RESET CPU    CPU only; boards untouched (a debugging convenience, not a real signal).
POWER        Power-cycle: RAM contents are lost, ROM images are re-read, and POC* is
             pulsed. The only thing that clears memory.
```

`RESET` must be safe at any point — the bus asserts it at the next instruction boundary, never mid-cycle.

---

## 7. Host services layer

Boards must **never** touch a socket, a file handle, or `termios` directly. Everything a board needs from the host goes through a small set of generic, platform-abstracted services implemented once in `platform/`. This is what keeps the four targets honest and keeps replay deterministic — if a board can read the host clock or a socket on its own, replay is dead.

### 7.1 `ByteStream` — the generic serial endpoint

**Built, 2026-07-11** (`src/host/stream.h`). **Sockets, real serial ports and modem control landed 2026-07-12.** Implemented: `NullStream`, `LoopbackStream`, `ScriptedStream`, `Console`, **`TcpListenStream`, `TcpConnectStream` (`src/host/tcp.cpp`), `HostSerialStream` (`src/host/hostserial.cpp`)**. **`FileStream` landed too** (`src/host/file.h`) — a **paper-tape reader and punch**, wired as `in:PATH` (a byte source), `out:PATH` (a byte sink), or the combined `in:PATH,out:PATH` (two files, two positions, on one bidirectional line). A reader may be paced — `in:tape.tap?cps=300` is the 88-HSR (issue #152) — via the same wall-clock mechanism `TapeStream` uses. The punch overwrites forward without truncating. Still to come: `ReplayStream` — which is blocked on RECORD/REPLAY, and those are unbuilt (§10.1).

Every board that moves characters (88-SIO, 88-2SIO, 88-ACR, 88-LPC, paper tape, PMMI) talks only to this. `CONNECT` binds an implementation to a unit.

#### The line goes BOTH WAYS (2026-07-12)

`LineStatus` was carrier/CTS/DSR and **input-only**, so RTS was decoded out of the 6850's control register and dropped on the floor — there was nowhere for it to go. Now:

```cpp
struct LineStatus  { bool carrier, cts, dsr, ring; };   // INPUTS  -- the far end drives these
struct LineControl { bool rts, dtr, brk; };              // OUTPUTS -- the CARD drives these
struct LineParams  { long long baud; int dataBits, stopBits; LineParity parity; };

virtual LineStatus status() const;
virtual void setControl(const LineControl&);
virtual bool setParams(const LineParams&, std::string& err);   // false -> the card says so out loud
```

**`true` is ASSERTED, everywhere, in both structs.** The pin-level inversions are real — the 6850's are `/DCD` and `/CTS`, active low — but they are a fact about *that chip's pins* and they stay inside the chip that has them. A stream that exported one chip's polarity would make the 88-SIO wrong for free.

**The stream reports LEVELS; the chip latches EDGES.** The same division the shared IRQ line draws (§4.4): the wire carries a level, and the receiving device decides what it means. The stream says *"carrier is down"* and says it for as long as it is down; the **6850** is what latches that, interrupts on it, holds it after the pin returns, and clears it only on status-then-data. Put the latching in the stream and every stream re-implements it slightly differently.

**The strap lives on the board** — `SET sio0:a dcd=wired`, default `ground`.

#### There is ONE baud rate, and it is the card's (Patrick, 2026-07-12)

> *"Do we need emulated character timing with a real serial port attached? The real serial port is the limiting factor."*

The plan called for two — a card baud and an independent endpoint baud. **That is struck.** There is exactly one line rate in a serial card and it is the UART's clock, a jumper; the frame format is whatever the guest wrote into the control register, because those bits *are* what goes on the wire. So **`CONNECT … serial:/dev/tty…` programs the host port from the chip**, and re-programs it whenever the guest rewrites the control register. A card strapped for 300 driving a terminal set to 9600 does not give you a fast link on real hardware — it gives you garbage, and a second baud rate could only ever configure the garbage.

The emulated character timing **stays**: it is the *same* duration the real port takes, not an extra one, and it must stay because the guest can **measure** it (the Mike Douglas BIOS times TDRE to infer the line speed).

#### …and there is NO `baud = 0`. It is not the analogue of `clock_hz = 0` (2026-07-14)

`clock_hz = 0` is free-running and is the default (§8), so `baud = 0` looks like the obvious next tidy-up. **It is not the same kind of number, and the request was declined on evidence.**

- **`clock_hz` is invisible to the guest.** It changes only the mapping from emulated cycles onto wall-clock seconds. Nothing inside the machine can observe it — which is exactly what makes flat out a safe default.
- **`baud` is a duration in cycles** — *inside* emulated time — so the guest **can and does** observe it. TDRE timing is one way (above). The **inter-character gap on receive** is the other, and it is the one that bites: the 6850 has **no handshaking**, so **the baud rate is the only flow control the card has**. In this simulator it is precisely what turns a host *paste* — ten bytes appearing in the pty at once — into a **paced serial stream**.

At `baud = 0` a character occupies the line for **zero cycles**, so the next byte is already in RDRF at the same cycle the guest read the last one, and **the guest's foreground never runs at all**. MITS PS2 is the proof: its ISR hands characters to its foreground through a **one-byte mailbox** and a semaphore, and that handoff requires the foreground to run *between* characters. Give it a zero character time and nine of the ten characters of a typed line are overwritten in the mailbox, and the monitor spins on the semaphore for ever. `baud = 0` is not a 2SIO running flat out; it is an **infinitely fast serial line**, which never existed (§0.1).

And the speed it was supposed to buy is mostly already there: **the baud rate never makes the run loop *sleep***. The throttle is `!clock.free()` (§8), so on a `clock_hz = 0` machine the TDRE wait is not a wall-clock delay at all — it is emulated cycles, executed at host speed. It is not free (a *very* low baud means proportionally more spin instructions for the host to grind through), but it is not the throttle it looks like, and it is not what a free-running default would have removed.

The property refuses `0` (`min = 50`), `tests/test_sio2.cpp` locks it in, and this is why.

And there is **no `flow = rtscts` endpoint setting**, for the same reason: hardware flow control in `termios` hands RTS/CTS to the *OS driver*, and the 6850 owns those pins. Two owners for one pin is a bug that shows up under load. **XON/XOFF was never a board option at all** — it is always software's job, never the card's (Patrick, 2026-07-12). It is bytes in the data stream, which makes it the guest's business, and a card that filtered them would be eating the guest's data.

> **An unconnected line is not an error, and there is no null pointer in the stream path.** A disconnected unit is bound to a `NullStream`, because that is what an unconnected 6850 on a real card *is*: it sits there with TDRE set forever, and software that writes to it works fine and talks to nobody. So no board contains a branch for "what if nothing is plugged in" — there was never a case to handle.

> **A `ByteStream` is NOT a serial line.** It is a *buffered, flow-controlled* source — a pipe, a socket, an OS keyboard queue. It will hold a byte until you take it. A board that models it as a free-running wire (and therefore synthesizes overruns from it) manufactures data loss that the host transport does not have. This cost real debugging.

```cpp
class ByteStream {
public:
    virtual size_t read (uint8_t* buf, size_t n) = 0;  // non-blocking; 0 if empty
    virtual size_t write(const uint8_t* buf, size_t n) = 0;
    virtual bool   readable() const = 0;               // -> drives RDRF
    virtual bool   writable() const = 0;               // -> drives TDRE
    virtual void   flush() = 0;
    virtual Status status() const = 0;                 // carrier/DSR/CTS
};
```

Implementations: `ConsoleStream`, `TcpListenStream` (`socket:2323` — accept, one client, survive disconnect/reconnect), `TcpConnectStream` (`socket:host:port`), `HostSerialStream` (termios vs `SetCommState`/DCB), `FileStream` (paper tape — `in:` reader, optionally paced, and/or `out:` punch), `NullStream`, `LoopbackStream` (testing), `ReplayStream` (recorded bytes at recorded cycle stamps).

> **A CLIENT CONNECTING *IS* CARRIER APPEARING**, and everything else falls out of it: a telnet client closes its window and DCD drops (and the 6850 latches that, and interrupts); the guest drops DTR and we hang up on the client; the TCP send buffer fills and CTS falls, so TDRE stays clear and the **guest waits rather than losing a byte**. That is what every terminal server ever built did, and it is what will let a PMMI work over a socket without the board learning what TCP is.
>
> **The connect banner is the terminal server's, never the board's.** A listening `telnet:` greets each caller with one line (`Connected to swtpcsim … (uio0:serial) on port 2323`), sent only down the socket; the guest never sees it, because a real terminal server's hello never crossed the RS-232 side either. It is **on for `telnet:`, off for `socket:`** (`?banner` / `?banner=off` flip either): `telnet:` is the endpoint for a person, while a raw `socket:` is the pipe another machine dials, and there a banner would arrive as data in the far guest's input. The stream cannot name its own line, since the board resolved it and a board never tells a stream what it is plugged into. So a stream that owes a caller the banner raises `ByteStream::greetingsDue()`, and `Machine::pump()` then walks the backplane's serial units and calls `greet("uio0:serial")` on each. That covers every connect path (monitor, MCP, machine file, `SET`) without touching a board, and costs one compare per slice when nobody is owed one.
>
> **No threads.** §7.5 permits blocking host I/O behind a `ByteStream`, and it turned out not to be needed: a socket and a serial port can both be asked, without waiting, whether they have anything. A thread would buy nothing and would cost the determinism RECORD/REPLAY is built on — the bytes would arrive at whatever T-state the host scheduler felt like. They arrive in `pump()`, at a known point in emulated time.
>
> **RECORD/REPLAY must capture line transitions, not just bytes.** A recording that logs only characters diverges the first time a carrier drop drives an interrupt. `ReplayStream` will replay `(tState, byte)` **and** `(tState, LineStatus)`. Impossible to retrofit; the interface is shaped for it now.

**Discipline: the board asks `readable()`/`writable()`; it never blocks.** All actual I/O is drained/filled by the event loop once per time slice, so `tick()` is pure computation.

### 7.2 `Console` — the host keyboard and screen

**Built, 2026-07-11** (`src/host/console.cpp`, `src/host/filter.cpp`). Implemented: `upper`, `strip7in`, `strip7out`, `crlf`, `echo`, `bell`, `bsdel`, and `attn`; **`log` landed 2026-08-02**. Not yet: `tabs`, `ansi`, `rows`/`cols`, `pace`.

A `ByteStream` like any other, so a board connecting to it needs no special code. But it is the only stream with a human on the far end, so it owns a configurable **transform chain**, applied inbound from the keyboard and outbound to the screen. Properties are declared through the same `Property` layer as boards, so `SET`/`SHOW`/MCP work on it for free.

> **The transforms are the CONSOLE's, and the console's alone** (Patrick, 2026-07-13) — `SET CONSOLE UPPER=ON`, `[console]` in a config file. Every other endpoint — socket, serial port, tape, file, loopback — is **8-bit clean, always**.
>
> They were briefly moved onto the **line** instead, as a `FilterStream` inside each UART, on the strength of the paragraph below ("a real terminal on a real host serial port wants the same uppercase folding"). **That was wrong, and it was wrong in a way that corrupts data.** A card's connector goes to a modem, a socket or a real `/dev/tty.usbserial`, and the next thing down it is **XMODEM — 8-bit binary**. A `strip7out` on that line masks bit 7 of every byte of the transfer and does it *silently*. Set it once for MITS BASIC, forget, and every binary file that ever leaves the machine is quietly corrupt. The 88-ACR reached this conclusion first and refused the chain outright ("a tape is binary, not text"); every other line deserves the same protection.
>
> **A LINE HAS LINE CODING, NOT FILTERS.** `baud`, `data_bits`, `stop_bits`, `parity` are real, they are **hardware** — the 88-SIO's NDB/NSB/NPB/POE jumpers, the 6850's control register — and they belong to the card. They set how long a character occupies the wire, and on a **real serial port they are programmed into the real port** (`ByteStream::setParams`). A frame is not a filter: a card strapped for 7 data bits sends seven because that is what the hardware does, and it does not mask the guest's byte to do it.
>
> **The rule, in one sentence: the only thing that may alter a byte is the console, because the only thing with a human on the end of it is the console.**

| Property | Meaning |
|---|---|
| `upper` | Fold keyboard input to uppercase. Essential — much period software only accepts caps. |
| `strip7in` / `strip7out` | Mask the high bit, independently per direction. |
| `crlf` | CR→CRLF on output; Enter→CR vs LF on input. |
| `bsdel` | Map host Backspace to BS (0x08) or DEL (0x7F). A perennial CP/M annoyance. |
| `tabs` | Expand outbound tabs at N columns. |
| `echo` | Local echo (for half-duplex hardware). |
| `ansi` | Run outbound bytes through the VT100/ANSI screen model, or pass raw. |
| `rows`, `cols` | Screen size — **and the answer to an `ESC[6n` DSR query**, which is how a guest discovers terminal size. (Lifted from the Python prototype, but as a property rather than a hardcoded sniff.) |
| `pace` | Throttle output to the configured baud, so a 110-baud Teletype *looks* like one. Off by default. |
| `attn` | The escape key that drops from console back to the monitor (e.g. `Ctrl-E`). **Non-negotiable** — without it a guest that swallows all input traps you with no way out. |
| `log` | Tee the session to a host file. |
| `bell` | Ring the host bell on 0x07, or ignore. |

**This list will grow.** That is precisely why it is a property table rather than hardcoded flags: adding one means adding a row, and the CLI, TOML and MCP pick it up automatically.

~~**Implement the transforms as a reusable filter chain on `ByteStream`, not as console-specific code** — a real terminal on a real host serial port wants the same uppercase folding, so `SET sio2b UPPER=ON` on a socket-connected line works for free.~~

**Struck, 2026-07-13, and left here as the record of a mistake worth not repeating.** It reads well and it is wrong: it optimises for the VT100 you *might* hang off a serial port and forgets the XMODEM you *will* run through it. The chain is still a reusable `FilterStream` (`host/filter.h`) — the console just owns the only one.

**Arbitration:** exactly one unit may hold the console at a time. `CONNECT sio2a:a console` steals it, warning who had it.

### 7.3 `DiskImage` — the generic mountable medium

Every disk/tape board (88-DCDD, 88-HDSK, Tarbell, Disk 1A, North Star, any future controller) sees only this. `MOUNT` binds an implementation to a unit.

**The interface is CHS, not LBA, and the format is per-track.** Both of those are forced by real disks, and an LBA interface with a single global geometry cannot express them:

```cpp
enum class Density { SD, DD };

class DiskImage {
public:
    explicit DiskImage(std::unique_ptr<MediaFile>);   // the host file lives BELOW this

    // The BOARD describes the medium: overall shape, then one or more TRACK RANGES.
    void init(int tracks, int heads, bool interleaved);
    void initFormat(int trackLo, int trackHi, int headLo, int headHi,
                    Density, int sectors, int sectorSize, int startSector);

    bool readSector (int t, int h, int s, uint8_t* buf, size_t* n);
    bool writeSector(int t, int h, int s, const uint8_t* buf, size_t* n);

    bool     readOnly() const;
    bool     readOnlyForced() const;   // the host would not let us write it
    void     sync();
    uint64_t size() const;
};
```

Why each piece is there — each corresponds to a disk that exists:

- **CHS, not LBA.** Every controller in the catalog addresses track/head/sector. An LBA interface would force each board to invent a flattening the hardware never had, and then invert it.
- **Format is declared over *track ranges*, not once for the disk.** Sector size and density genuinely vary *within* one image: a double-density soft-sector controller keeps **track 0 single-density** so the boot PROM can read it. One `Geometry` for the whole disk cannot say that; two `initFormat` calls can.
- **`startSector`.** The 88-DCDD numbers sectors from **0**; most soft-sector controllers number from **1**. This is exactly the off-by-one that silently corrupts a disk.
- **`interleaved`.** Whether a two-sided image stores `T0H0, T0H1, T1H0…` or all of head 0 followed by all of head 1 is a property of the *image*, and it varies by the tool that wrote it.

**Hard-sector vs soft-sector needs no flag** — it falls out of `sectorSize`:

| | `sectorSize` | What the image holds |
|---|---|---|
| **88-DCDD** (hard sector) | **137** | The *whole slot*: sync byte, track/sector header, 128-byte payload, checksum, stop byte, trailer. |
| Tarbell, Disk 1A, North Star… (soft sector) | **128** / 256 | **Payload only.** The header and checksum were in the inter-sector gaps on real media and never made it into the image. |

The board still owns what is *inside* the slot — for the DCDD, that the payload starts at offset 7 on a data track and 3 on a system track, and that a checksum sits at [4]. That is the controller's business.

> **Geometry probing belongs to the BOARD, not to this service.** An earlier draft of this section said the opposite — *"geometry probing lives here, once"* — and that was **wrong**. Geometry is a function of **controller × image size**, and the service does not know the controller. 337,568 bytes means a 77-track 8″ floppy *only because* it is a DCDD; 8,978,432 means a 2,048-track FDC+ *only because* it is a DCDD. The same byte count on a Tarbell means something else. So the **board** probes the size, picks among the formats *it* knows, and calls `init`/`initFormat`. The service does offsets and I/O and nothing else.

Worked example — the board configuring the medium:

```cpp
// 88-DCDD, 8 MB FDC+ image. The board knows it is hard-sector.
img.init(2048, 1, /*interleaved=*/false);
img.initFormat(0, 2047, 0, 0, Density::SD, 32, 137, 0);   // whole 137-byte slot

// A soft-sector DD controller whose boot track must stay single-density.
img.init(77, 1, false);
img.initFormat(0,  0, 0, 0, Density::SD, 26, 128, 1);     // track 0
img.initFormat(1, 76, 0, 0, Density::DD, 26, 256, 1);     // everything after
```

**Underneath it: `MediaFile` (`src/host/media.h`) — the host file, and the only thing in the program that opens one.** Buffered, dirty write-back, a write-protect flag, a sync. This is the layer a disk and a tape genuinely *share*.

**And `DiskImage` is ONE CLASS, not a base class** — every implementation this section used to name has dissolved. `ReadOnlyImage` was never a different *image*; it is a medium that says no. `MemoryDisk` was never a different image either; it is a `MemoryMedia`. And **`ImdImage`/`Td0Image` are never coming** (Patrick, 2026-07-12): raw disk images are the only kind this program will ever read, and an IMD file that has to be used here is one that gets converted to raw beforehand, outside it. Those container formats were the **entire** reason `readSector`/`writeSector` were virtual — a format that carries its own per-track sector map needs to override the arithmetic — so with them ruled out, the virtuals go. The image is sector-linear, always. A hook left in for a possibility the owner has ruled out is not extensibility; it is a hook nobody will ever pull, and the next reader has to work out why it is there.

**Write-protect mounts, it does not refuse** — the read-only flag goes on by itself, and the operator is told that it did (Patrick, 2026-07-12). A file the host will not let us write is a write-protected disk, which is an ordinary disk — so it mounts read-only. What must not happen is the *silent* version: the operator typed no `RO`, so `readOnlyForced()` is true and the board **says so** through `Board::drainLog()`. Discovering it at `sync()` instead — after CP/M has spent an afternoon writing to it and the flush fails with the work gone — is the failure this prevents.

`openMedia(path, readOnly, err)` is the **one seam**, installed by `setMediaResolver()` in `src/main.cpp` and `tests/main.cpp` — the exact shape of `resolveEndpoint()`/`Sio2Board::setResolver()`, and for the same reason: a board asks for a path and gets a medium, and a test swaps the filesystem out for RAM without the board noticing.

**The XMODEM pad.** Both 8″ DCDD images in the tree are 337,664 bytes, not the 337,568 that 77 × 32 × 137 predicts — XMODEM padded them to a 128-byte block boundary. A strict `size == exact` probe therefore rejects **both of the only 8″ disks we have**. Every format match is `exact <= size < exact + 128` (`sizeMatches()`, in `disk.h`, because the trap is in the file format and not in any one controller). The pad is never data, and a write never reaches it: `DiskImage` bounds every access against the declared geometry, and a **soft-sector** disk never grows under a write.

**Hard-sector disks carry no geometry, so they mount at any size — and FORMAT by growing.** A hard-sector image is fixed 137-byte slots addressed linearly `(track·spt + sector)·137`; its size is simply how many slots the guest has written, and `spt`/`sectorSize` are the *card's* constants, not the image's. So on a hard-sector controller the probe is **not a gate**: a size that matches a known format gets that format (so it is named and the XMODEM pad is tolerated), and a size that matches nothing — 0 bytes, an odd count, a disk about to be formatted — is not an error but an **unformatted** disk, mounted at the controller's full reach (its largest format) with `DiskImage::setExtendsOnWrite(true)`. The guest's own FORMAT program then grows the file into shape, one slot at a time, capped at that reach (the head-step clamp `d.fmt.tracks - 1` is the physical bound). Reading a slot that is not there fails, and the controller's sync-byte/checksum rejects it — exactly what unformatted media does — so nothing is invented. A blank file to start from comes from `MOUNT … CREATE` (or `create = true` in a machine file), which writes an empty file and mounts it.

**Soft-sector controllers now format too, one track at a time — see §7.3.2.** The old rule here was that a short soft-sector image was *truncated, not unformatted*, because a soft-sector FORMAT writes address marks a raw `.DSK` cannot hold. That has been deliberately reversed for the WD177x `Write Track` path: the marks and gaps never land in the `.DSK`, but they do not need to — the controller *parses* them out of the streamed track to learn the geometry, then writes only the payloads. So a blank soft-sector disk is now unformattable-until-FORMAT in exactly the way a hard-sector one is, with one difference recorded below.

*Modeled on `simh.mdsk/Altair8800/altair8800_dsk.c` (© 2025 Patrick A. Linstruth) — our own prior art, not another project's.*

### 7.3.1 `TapeImage` — the sequential medium, and why it is not a `DiskImage`

A cassette has exactly one thing a disk has not: **a position**. It is not that a tape is a worse disk — it is that the head is where it is, and the only way back to the start of the program is to **rewind**. That is the whole of the difference, and it is why `TapeImage` (`src/host/tape.h`) is its own class over the same `MediaFile`: `read`/`write`/`rewind`/`pos`/`atEnd`. The CLI gets a verb, `SHOW` gets a number, and the guest gets the bytes in the order they were recorded.

**And then the adapter that makes the 88-ACR nearly free: a tape *is* a `ByteStream`.** `TapeStream` is 20 lines, and with it the shared 1602 UART needs no cassette-specific code at all — the ACR hands it a `TapeStream` where the 88-SIO hands it a socket, and the only difference left is that the unit is `UnitKind::Tape` (MOUNT) rather than `UnitKind::Serial` (CONNECT). That is §7.1's promise being cashed: the board knows it has a serial line, and does not know what is on the end of it.

> **The prediction held — the card came in at ~250 lines and inherits its whole bus half from the 88-SIO, because it *is* an 88-SIO B. But "the only difference left is the unit kind" was one word short, and the missing word cost a silent data corruption.**
>
> A `TapeStream` also needs a **MODE**. A cassette has ONE head, so read and write share ONE position — they must; it is the same piece of tape. And a UART receives **eagerly**: it pulls a byte off its line the moment it has room, because that is how DAV and an interrupt-driven loader work. So a tape that was readable *and* writable at once had its first byte pulled away by the card **before the guest ever ran**, the head sat at 1, and **every recording began at byte one**. Playback worked perfectly the whole time, which is why no load test could ever have found it.
>
> The fix was the hardware's own and cost nothing: the 88-ACR has **no motor control** — a human pressed the buttons — and a recorder is in PLAY *or* in RECORD, never both. Making that exclusive makes the corruption **unrepresentable** rather than merely unlikely. `tests/test_media.cpp` had asserted the *opposite* (`CHECK(bs.writable())` on a readable stream); that is how it got in.

`readable()` is *there is more tape*, so the byte **waits for the card** — a tape that dropped a byte because the guest was slow would be manufacturing data loss the host does not have, which §7.1 forbids. A real recorder keeps rolling and *can* drop data; we do not model that, and the board's `.md` says so under Limitations.

### 7.3.2 Soft-sector FORMAT — geometry in real time

A hard-sector image carries no per-track geometry, so its whole geometry is a set of card constants declared once at mount (§7.3). A **soft-sector** image is different in kind: sector size, sector count and even density can vary from track to track, and none of it is in the `.DSK` — it lived in the address marks and gaps that a raw image throws away. So the geometry cannot be a mount-time constant. **The decision (2026-07): per-track geometry is established in real time, and the WD177x `Write Track` command is the only thing that establishes or mutates it.** `DiskImage::setTrackFormat(t, h, fmt)` is that one mutator; `Write Track` streams a whole raw revolution into the drive, the drive parses the marks back out to derive `TrackFormat{density, sectors, sectorSize, startSector}`, calls `setTrackFormat`, and writes the payloads. Density comes from the controller (the WD `DDEN` pin, `Wd17xx::dataRateBits`) — the chip is the single source of truth, so it **hands its own data rate to the drive's `Write Track` calls** and the drive keeps no density of its own; everything else comes from the stream. Because that rate is read per track, one double-density controller formats a **mixed** disk from the guest's per-track density bit — SD track 0 (250 kbit/s), DD tracks 1-76 (500 kbit/s). Reads and writes never change geometry — they validate against what a track *records*, and an unformatted / out-of-range track is **Record Not Found**, exactly as bare media reads back.

This is what makes a blank soft-sector disk formattable (`MOUNT … CREATE` → the guest's FORMAT lays down each track), an already-formatted disk usable with no FORMAT (the size probe establishes the *initial* geometry), and the double-density mixed disk — SD track 0, DD the rest — fall out of the same mechanism (the Tarbell #2022 formats one with `DFORMAT`, each track's density taken from its `OUT FC` density bit). It rests on one invariant: **FORMAT writes tracks 0→N in ascending order**, so when a track is (re)formatted to a larger geometry, the tracks after it in the running-sum offset layout (`rebuild()`) have not been written yet or are about to be overwritten — nothing valid is clobbered. Every real Tarbell format program formats ascending (`pd2/FORMAT.ASM`, `pd2/DFORMAT.ASM`). An out-of-order or cross-density reformat of a *populated* disk would need the tail shifted first; that is deferred. Growth is `setExtendsOnWrite(true)` with a **dynamic** `geometryBytes_` cap that rises as each track is formatted, the soft-sector twin of the hard-sector §7.3 rule.

This deliberately reverses the old wall — `Write Track` on a raw `.DSK` used to set **WRITE FAULT** on the grounds that the marks and gaps had nowhere to go (recorded at `chips/wd17xx.h` "the whole track"). They still have nowhere to go, and that is fine: the controller reads the geometry *out of* the stream and stores only payloads. The fault now means only what it should — an empty drive, or a controller that does not implement formatting. **The mechanics — the format FSM, the wait-synced `trackImageBytes(rate)` budget, the density model, the flat-`.DSK` limitation and the deferred work — live in `docs/devguide/soft-sector-floppy.md`; this section is the decision and the reasoning, not the how-to.** Controllers: the Tarbell #1011 (single density) and #2022 (mixed SD/DD, and it reads plain SD media too), and the **SD Systems VersaFloppy**, which formats **all ten** SD Systems formats via `Write Track` — five geometries (8″/5.25″ × SD/DD, plus 8″ DD-256) × single/double sided. Its 5.25″ media spins at 300 RPM, so the revolution budget generalized from `rate/48` to `rate / (8 × rev/s)` — the one shared-code change the VersaFloppy needed; everything else was board-local, as the checklist promises. *Modeled on `simh.mdsk/.../wd_17xx.c`, our own prior art.*

### 7.4 `Display` and `Audio` — SDL

For boards with graphics or sound (**VDM-1**, **Cromemco Dazzler**, music boards, and whatever you build next). Backed by SDL, and **optional**: compiled in only with `SWTPCSIM_ENABLE_SDL`. A headless build must still pass every acceptance test.

**Boards never call SDL.** They see only:

```cpp
class Display {
public:
    using Owner = const void*;   // the drawing board's `this` — which window (§ below)
    virtual Surface* acquire(Owner, int w, int h, PixelFormat, int targetWidthPx) = 0;
    virtual void     present(Owner, Surface*) = 0;
    virtual void     setPalette(Owner, std::span<const Color>) = 0;
};

class Audio {
public:
    virtual void   push(std::span<const int16_t> samples) = 0;   // clocked from EventQueue
    virtual size_t queued() const = 0;
};
```

The two boards this must serve are usefully different, and the API should be hand-checked against both:
- **VDM-1** is *memory-mapped*: a 1K text window the CPU writes into, plus a character-generator ROM; renders 16×64 characters. Its keyboard is a **separate parallel board** — so the SDL window's keystrokes must route back through a **`ByteStream`**, not a private path.
- **Dazzler** is *DMA-driven*: it steals bus cycles to read a bitmap out of main memory. It needs the `requestsBus()`/`busMaster()` path, and it is the concrete reason DMA is in the bus model at all.

Two constraints that are painful to retrofit:
1. **The SDL event loop does not own the main loop.** The simulator's clock and `EventQueue` own emulated time; the display is pumped once per time slice. Letting SDL drive would put the host frame rate in charge of emulated time and wreck both throttling and replay.
2. **On macOS, SDL requires the window and event pump on the main thread.** That is an OS constraint, not an SDL preference, and it dictates the threading model: main thread pumps SDL, emulation runs elsewhere, they communicate through queues. **Decide this now** — discovering it after the machine loop is written means restructuring the program.

**Keystrokes from an SDL window are an *input*** — they go through the recorded event queue like everything else, or replay breaks the first time a Dazzler game is involved.

**The `Joystick` service — built 2026-07-24.** A Dazzler game's *other* input is a joystick, and it is a host service of its own (`host/joystick.h`), the input analogue of `Display`: a board reads cached stick axes and buttons, and where those come from — a USB gamepad, the keyboard, or nothing — lives behind the seam, so the board (the **Cromemco D+7A**, which reads one or two JS-1 consoles) never touches SDL and a headless build reads every stick centered. It is polled once per slice from `pump()`, never inside a bus cycle, exactly as the `Display` is. It also sharpens the note above: a video window is a *keyboard* only when it ought to be. `[display] keyboard = none` makes a display-only window (a Dazzler) route its keystrokes to the joystick's keyboard fallback rather than the console — so a game's keys drive the stick instead of landing at the CP/M prompt — while `Ctrl-E` still stops the guest and hands back the monitor, like the close box. A Sol-20 window stays a console keyboard (`keyboard = console`, the default).

**Closing the window stops the guest; it does not quit the process** (built 2026-07-18). The window is an operator's control like ATTN, and it lands you in the same place: the run loop asks the display once per slice (`Display::takeQuitRequest()`, consuming exactly like `Console::takeAttn()`), stamps `StopReason::WindowClosed`, and gives back the prompt with the machine untouched. The *display* is asked; the display does not stop the machine — a board's `pump()` must never be able to halt the backplane it sits in, so the only thing that acts on the answer is the run loop, which is the only thing that can stop a machine at all.

**And the window is serviced at the prompt too, for liveness** (built 2026-07-25). The run loop is not the only place that must drain the window's event queue: a *stopped* machine sits in the monitor's line editor blocked on a console read, advancing no emulated time and pumping nothing — and a window nobody drains is one the compositor pings for liveness (`_NET_WM_PING`, the Cocoa watchdog), gets no answer from, and the OS then offers to Force-Quit. So the line editor's first-byte wait is a **timeout loop** (`platform::waitForInput`), and on each idle tick it runs an injected hook that calls `Display::pollEvents()` — draining SDL's whole event queue, which answers the liveness ping and tosses any keystrokes/gamepad events that arrived while stopped. This is **liveness only, not a second run loop**: it does not `pump()` the boards and it does not *render* (drawing stays the running machine's job, so `SET video=reverse` and the cursor blink still wait for `RUN`), and it touches neither the clock nor the backplane. The one thing it acts on beyond draining is a close box clicked *while stopped* — which, unlike one clicked mid-`RUN` (that stops the guest and keeps the window), means the operator is done with the window, so the hook calls `Display::closeWindow()` and it goes.

### 7.5 `Clock` — the single source of time

Nothing in the simulator may call `std::chrono::now()` except this. Time is measured in **cycles**, never milliseconds, and it advances only when the CPU retires an instruction — by exactly the count the CPU reported. That is what makes replay deterministic, and it is why the UART's idea of when a character has finished going out is derived from the very instruction stream the guest is timing it against; the two cannot drift.

The crystal is on the **CPU card** (§3, §8), so the card publishes its `clock_hz` here. A board converting a real-world rate (a baud rate, a disk RPM) into cycles asks the clock, and never has to go hunting through the backplane for whichever card holds the oscillator — or discover there isn't one.

```cpp
class Clock {
public:
    uint64_t now() const;              // cycles since POWER. Only power resets it.
    void     advance(uint64_t dt);     // the run loop, with StepResult::cycles

    using Handle = uint64_t;           // an integer, so a stale one is merely stale
    Handle at(uint64_t when, std::function<void()> fn);     // "call me AT T"
    Handle after(uint64_t dt, std::function<void()> fn);
    void   cancel(Handle h);           // cancelling a dead handle is legal
    bool   pending(Handle h) const;
    size_t queued() const;

    long long hz() const;              // published by the CPU card
    uint64_t cyclesPer(long long perSecond) const;   // the ONE place that division lives
    void     power();                  // time restarts; every deadline is gone
};
```

Two properties the boards depend on:

- **Order is total and deterministic.** Events fire in `(when, scheduling order)` — the handle is a monotone counter and breaks the tie. Two boards with deadlines on the same cycle fire in the same order in every replay, on every host. Without an explicit tiebreak this is a divergence that appears once a month and can never be reproduced.
- **Inside a callback, `now()` is when the event was *due*** — not where the instruction that carried time past it happened to end. An instruction is up to 12 cycles long; a board that re-arms with `now() + charTime` would otherwise drift a little further with every character it ever sent.

#### The `EventQueue` came back, and the board cited as proving it unnecessary is the board that proved it necessary

**Reversed 2026-07-12.** This section previously *deleted* the `EventQueue` and argued the case at length. The argument was:

> In this architecture a board is already **polled** for everything the bus can observe about it. `decodes()` is asked on every cycle; `assertsInt()` on every instruction boundary. So a board never needs to be *woken* — it needs to answer *"what time is it?"* when someone finally asks.

**That argument was circular, and the circle was hiding the bug.** The board did not need waking *because we were polling it sixty million times a second*. The poll was not evidence that the queue was unnecessary. **The poll *was* the queue**, run at enormous cost and called something else.

And the poll had to go, because it was never how the machine worked (§4.4): a bus does not interrogate a card for its interrupt status. A card **pulls pin 73 and holds it**. Take the poll away and the board is left holding a deadline it has no way to be present for:

> A 6850 with the transmit interrupt jumpered raises IRQ when its shift register drains. **Nobody touches it. No bus cycle happens.** And the guest is sitting in a `HLT` waiting for precisely that interrupt. If the only way the card can act is to *be asked*, and the only thing that would ask is the CPU that is halted waiting for it, **then nothing ever happens again.**

That is not hypothetical — `tests/test_sio2.cpp` runs exactly that machine, and it is the test that fails without a queue.

**What was right in the old argument, and still is:** TDRE *is* a deadline, not an event, and a board that can answer "what time is it?" when the guest finally reads the status port should do exactly that and **schedule nothing**. The 6850 still does, for the polled case — `nextEdge()` returns "never" on a quiet line with an idle transmitter, which is the commonest state in the machine, and the old model paid the full price of a poll for it. You come to the queue only for a state change that must be **visible to someone else the instant it happens**, and on this bus there is exactly one such thing: **a wire**.

#### And a periodic pump, because a deadline cannot predict a keystroke

Patrick asked whether boards want an event queue, a periodic timer, or both. **Both — and the second one already existed.**

They are not alternatives; they answer different questions. A **deadline** is something emulated time already knows is coming (a character finishing transmission, a disk sector arriving under the head). A **keystroke from the host** is not in emulated time at all — nothing could have scheduled it — so it arrives through `Board::pump()`, once per time slice, which is the one door the outside world comes through (§7.1).

The 6850 needs both, in the same function: `pump()` takes the byte off the line, and if the line has not yet had time to deliver it, sets a **deadline** for when it will.

**No threads.** Board logic stays in emulated time, single-threaded, or `RECORD`/`REPLAY` is dead (§13). Host I/O may thread *behind* the `ByteStream`, where it belongs — that is what the interface is for.

### 7.5.1 `Spindle` — the disk turns whether or not anyone is looking at it

**Built 2026-07-12** (`src/core/spindle.h`), ahead of the two floppy boards that need it, because they need the *same* arithmetic and it should exist once.

A floppy rotates on its own. So the sector under the head is **not state a controller advances — it is a reading taken off the clock**:

```
sector = (now / tPerSector) % sectorsPerTrack
```

**There is no hidden counter and no advance-on-read**, and that is the whole design. The tempting alternative — a counter the card bumps when the guest reads the sector-position port — makes the disk's rotation depend on *how often the guest polls*: a tight loop spins the platter faster than a slow one, the drive runs at the speed of the software watching it, and a recorded session stops replaying identically. Deriving it from `Clock` kills all three at once, and costs less code than the counter would have.

**Two cards need it, for different reasons, which is why it is neither card's:**

- the **88-DCDD** hands the sector number straight to the guest at `IN 0x09`;
- the **Tarbell** never exposes it — but its FD1771's `Read Address` (0xC4) must answer *"which sector is under the head right now"* so the buffered CP/M BIOS can begin a track read where the head already is. A static answer spins that BIOS forever.

It lives in `src/core/` (Patrick, 2026-07-12) because it is **pure time math over a `Clock` and knows nothing else** — not a board, not a `MediaFile`, not a sector's contents. It is not a chip (§7.8: nothing solders a spindle to a card), and it does not belong in `src/host/` beside `DiskImage`, which is bytes and offsets with no notion of time.

**It hands back a 0-based INDEX, and stops there.** A controller that numbers sectors from 1 (the Tarbell does; the DCDD does not) adds its own `startSector`. That off-by-one is the one that **silently corrupts a disk** (§7.3), so it stays in the board where it is visible, and is deliberately *not* buried in here where a reader would have to go looking.

Two invariants it enforces so no board has to:

- **`nextBoundary()` is strictly future**, at every instant of a revolution — by construction, not by a hand-written guard. A deadline armed for `now()` fires inside the drain loop that is running it, re-arms, and the machine never advances again. The UART's `nextEdge()` enforces the same rule by hand (§7.5); here it falls out of the arithmetic.
- **The sector is the unit, not the revolution.** Everything derives from `tPerSector`, so `sectorAt()` and `nextBoundary()` cannot round apart — a board that wakes on the deadline it was given always finds the sector it was promised, at that sector's first cycle. Deriving both independently from `tPerRev` would let them disagree by a cycle, occasionally, which is the worst kind of bug to own.

**The motor does not care what crystal the CPU has.** Rotation comes from `Clock::hz()`, so a 4 MHz machine executes twice as fast and still turns its disks 360 times a minute. Expressing rotation in raw cycles would make overclocking the CPU spin the floppy faster, which is nonsense.

### 7.6 `Log` / `Trace`

**The per-source half is built** (`src/core/debuglog.{h,cpp}`, `namespace swtpc::dbg`, 2026-08-02). The original design was one structured diagnostic sink with per-board *and* per-category masks (`IN`, `OUT`, `READ`, `WRITE`, `IRQ`, `DMA`, `CONTENTION`), mirroring the `DEBTAB` idea in `mits_dsk.c`, emitting text for the monitor and JSON for MCP from the same call site. `dbg` builds the per-source axis of that: a named **`Channel`** for each instrumented source — a board (named by its `id`, created on `Bus::attach` when the board's `debugFlags()` is non-empty), a chip (`mc6850` owns a static `6850` channel), a host layer (the socket code owns `socket`) — each carrying its own flag list, and **one global sink** (`Stderr` default, `Stdout`, or an appended `File`). A channel caches its enabled flags as a `uint32_t`, so an emit site is a single `if (ch.on(SEEK))` when the flag is off and the formatting behind it never runs. `dbg::line()` prefixes each line with the **PC of the driving instruction** — `2C38  dsk0: sector drive=0 track=0 sector=1` — read lazily through a provider `Machine` installs (`Bus::instrPc`, published once per instruction by the run loop), or `----` at the prompt where the machine is not running. The operator drives it from the monitor: `SET CONSOLE DEBUG=<sink>`, `SET <channel> DEBUG=/NODEBUG=<flags>` (additive/subtractive, `all`/`none`, atomic on an unknown flag), `SHOW DEBUG`, and Tab completion for channel names and flag values. None of it survives `CONFIG SAVE` — a diagnostic is a session, not a property of the machine.

**What is still not built is the *category* half and the JSON emitter.** `TraceCat` (`src/core/debug.h`) is `{InCycle, OutCycle, Irq, Dma, Contended}`, driven by `TRACE ... MASK=` — a separate bus-trace mechanism with no `READ`/`WRITE` memory category, **no per-board mask**, and no JSON for MCP. Folding the two together — per-board *and* per-category masks from one call site, text for the monitor and JSON for MCP — is the design that was specced; the `dbg` channels are the first half of it, and per-unit channels (`sio0:a`) plus instrumenting the remaining boards are the incremental next step.

### 7.7 The two consequences worth stating explicitly

- **A new board written against these services is automatically cross-platform and automatically replayable.** That is the point of the layer, and it is the acceptance test for the API.
- **`CONNECT` and `MOUNT` are generic**, not per-board commands. The monitor resolves an endpoint string to a `ByteStream` or a file to a `DiskImage` and hands it to whichever board declared a unit of that type — so a board written next year gets `MOUNT`/`CONNECT` for free without touching the monitor. Note the division of labor: the *monitor* opens the file; the *board* decides what its bytes mean (§7.3).
  - **This is why a network disk cost no board change.** `MediaFile` (§7.3) is *where the bytes are*, and `openHostMedia` is the one place that decides what a mount string becomes. Teaching it that `tnfs://host[:port]/path` means "slurp this image off a TNFS server, serve it from RAM, write the dirty range back on sync" is a new `MediaFile` implementation plus one branch in that resolver — no controller, no `DiskImage`, and no `MOUNT` grammar was touched, and every disk and tape board mounts over the network for free. The two protocol quirks that make it a *network* medium and not a file: a TNFS `READ` returns at most 512 data bytes so the slurp loops in chunks, and a lost UDP datagram is recovered by resending the *same* sequence number (the server idempotently replays its cached reply). Both live entirely in the medium, off the emulation path — the session talks to the server only at mount and at sync.

---

## 7.8 A chip is not a card (`src/chips/`)

**A board is a PCB with chips on it, and the code says so** (Patrick, 2026-07-12). `src/chips/` holds the parts that get soldered to more than one card: `mc6850.h` (the 6850 ACIA, on both halves of the 88-2SIO), `uart1602.h` (the COM2502 — the 88-SIO, and the 88-ACR when it lands; the same 40-pin part that others second-sourced as the AY-5-1013 and the TR1602), and `wd17xx.h` (the **FD1771**, on the Tarbell and on the controllers after it).

**What a chip talks to is an INTERFACE, never a file.** `Mc6850` has a `ByteStream`; `Wd1771` has a `FloppyDrive` — pins (STEP, DIRC, TR00, WPRT, READY, IP) with a drive on the far end. The FDC owns no image, no geometry and no host file, and it has **no drive-select and no side-select pin** — because the FD1771 hasn't got either. Which drive those pins reach is a *latch on the card*, so the board calls `attach()` and the chip is none the wiser. That absence is the chip/board seam stated in silicon.

**The FD1771 is also where §0.1 earned its keep.** `reference/` contains a WD177X data sheet, and it is a *different chip*: its stepping rates are 6/12/20/30 ms where the FD1771's are **6/6/10/20**, and its record type is one bit where the FD1771's is **two** (it has four data address marks). Building the Tarbell's controller from the sheet that was to hand would have produced something plausible, clean, and wrong — a controller that seeks at the wrong speed and mis-reports deleted records, while looking entirely finished. The right sheet was sourced first (`wd17xx.h` says which), and `tests/test_wd17xx.cpp` keeps a tripwire test on the step-rate table so that nobody "fixes" it back.

**The seam is not "the chip does the work and the board forwards to it."** The 88-SIO is the case that shows where it really falls. Its status word is *inverted* and the 88-2SIO's is not; a shared UART class with a `bool invert` on it is the exact bug this rule exists to prevent. So: **the chip is what the data sheet describes, at its pins, in true sense.** Everything between those pins and the S-100 bus — the inverting buffers, which bit each signal lands on, the board revision that moved them, the interrupt-enable flip-flops in a *different IC*, the port decode — is the CARD's, and it stays on the card. The COM2502 has no interrupt pin at all, so it cannot even answer "when could my interrupt move?"; it publishes its raw deadlines and the board, which owns the enables and the strap to pin 73, works that out. Two cards, one chip, and not one line of polarity shared between them.

**Each chip is modeled from its DATA SHEET, and each board from its MANUAL.** That line is the whole reason the directory exists. A chip built instead from the one BIOS that happens to drive it will implement exactly the subset that BIOS touches and quietly get the rest wrong — and it will look finished while doing it. §0.1 applies to a chip's registers exactly as it applies to a card's ports.

A chip knows nothing about S-100. It has a clock, some pins, and (if it moves bytes) a `ByteStream`. It never learns the endpoint *grammar* either: the monitor installs a resolver on the board, and the board hands the **function** down — which is §7.7's division of labor holding one level further in.

**This does not license sharing between cards that merely resemble each other.** The 88-SIO and the 88-2SIO still share no code, on purpose: they are *different chips with opposite status polarity*, and a common helper with a `bool` flipping the sense is precisely the trap that rule was written to prevent. What licenses sharing is being **the same part**, not filling the same role.

---

## 8. Timing and host idling

- Clock is the **CPU board's** `clock_hz` — not the machine's (§3). The crystal is on the card, and a backplane with no CPU card has no clock rate at all. **`0` = free-running, and it is the DEFAULT** (Patrick, 2026-07-13). The run loop simply does not sleep, so a cassette that took a real Altair 110 seconds comes off in about one. Emulated time is unchanged: `Clock::hz()` remains a 2 MHz **divisor** so no UART ever divides by zero, and a separate `Clock::free()` decides whether we wait. `clock_hz = 2000000` gives back the period machine *and* the period waiting. (Before this, `0` was documented as "runs flat out" and silently did nothing — `setHz(0)` coerced the rate back to 2 MHz and the run loop paced against it.)
- Throttle by comparing accumulated cycles against a monotonic host clock in ~1 ms slices and **sleeping** the remainder — never spin.
- **The CPU card WRITES to the Clock, and that makes re-attaching a card a publish** (issue #34, 2026-07-18). Every other card *reads* the clock and is happy to be handed a different one; this card pushes `clock_hz` and `idle` **into** it. A machine file is assembled into a scratch `Machine` so a bad file cannot damage a running one (§10), so the card announced 2 MHz to the *scratch* Clock and `Machine::replaceWith` then moved it onto the real backplane — whose Clock had never heard of it. `SHOW cpu0` read `2000000` off the card while the run loop free-ran, and **both were telling the truth about different objects**, which is the worst shape a bug can take. The invariant is *this card's clock knows what this card told it*, and it is kept where it can be broken: `Board::attachClock` calls a virtual `clockAttached()`, and the CPU cards republish there.
  - **It survived because every test set the crystal at the monitor.** `SET cpu0 clock_hz=...` runs on a card already on the real backplane, and `CONFIG LOAD` powers the machine again afterwards and republished by luck — so the one road nothing drove was the one every operator takes: put it in the file, start the simulator. A property that can be set two ways needs a test for **each** way, and the file is the way that matters.
- **Idle detection** — **BUILT, 2026-07-13** (Patrick), and it is the CPU card's `idle` property, on by default. It is the *second* sleeping policy and it is **orthogonal to the first**: `free()` asks "do we keep time?", `idle()` asks "do we stand down when there is nothing to do?". Both live on the Clock because that is where the run loop already goes to ask whether to sleep, and both are published by the card that carries the crystal (`boards/mits-88cpu.cpp`). `SET cpu0 idle=off` gets the spin back. **No hardware behaves differently — only the host sleeps, and the guest cannot tell.** Measured: 8 MB CP/M at `A0>` went from **100% of a core to ~3.5%**.
  - **The signals are three, and all must hold**: the guest **said** nothing (`Console::written()`), **received** nothing (`Console::consumed()`), and came to the keyboard and found it **empty** at least once every 32 instructions (`Console::hungry()`). Then, and only then, once it has been that way for an unbroken **20 ms**, the loop naps 4 ms per slice.
  - **"Any data read resets the counter" is the load-bearing clause, exactly as this section always said.** A guest receiving XMODEM *down the console line* is the counter-example that breaks every simpler rule: at 76,800 bps it waits 130 µs for each byte, polling an empty keyboard hundreds of times per slice, and it prints nothing for a whole 128-byte block. By "is it polling?" alone it **is** a prompt — and a first draft that judged it on the poll ratio alone napped straight through a fed console at 4.3% of a core, which would have dragged 7.7 kB/s down to 250 B/s. The difference between a parked machine and a working one is not how it polls; it is that **bytes are arriving**. So a byte crossing into the guest resets everything.
  - The 20 ms **warmup** is the belt to that braces: sub-millisecond gaps inside a transfer can never accumulate into a nap, while a human's gap before finding a key is effectively infinite. It costs a prompt nothing anyone can perceive (measured `DIR` round trip: 13 ms, nap and all).
  - It applies **only when a unit holds the console and stdin is a terminal** — the same gate as the throttle. Under `-s` or a pipe the keyboard is a script, and the run loop's existing end-of-input logic (`starved()`, §7.2) is what ends the run there. `hungry()` and `starved()` are **deliberately different counters**: empty, versus empty *and ended*. Merging them re-introduces the bug that killed a cassette load three slices in.

---

## 9. Devices, `MOUNT`, and `CONNECT`

- A board declares typed **units**, and **a unit is a NAME, not an index** (Patrick, 2026-07-11). A disk unit accepts `MOUNT id:unit <hostfile>` (`UNMOUNT` to release); a serial unit accepts `CONNECT id:unit <endpoint>` (`DISCONNECT`).

  **ONE CARD IS NOT ONE KIND OF THING.** A card may carry drives *and* ROM sockets *and* a serial port — the Tarbell carries a boot PROM and a floppy controller on one board (it is **not built**, see §4.2.1, but it is a real card and the constraint is real), and a controller with its own PROM, scratch RAM and a serial port was a completely ordinary 1977 product. Nothing in the bus model ever assumed otherwise: `decodes()` is asked about every cycle and `BusCycle::type` distinguishes memory from I/O, so one card answers both. `tests/test_units.cpp` builds exactly such a card and proves it.

  So units are named and typed — `MOUNT dj:drive0`, `MOUNT dj:rom0`, `CONNECT dj:tty` — and **the kind is checked**: mounting a disk image onto a serial port is an error with a sentence explaining it. The integer scheme could not be made safe, which is why it is gone: with a flat namespace, `MOUNT dj:4` on a serial unit can only *fail*, never *explain*, because the board has nothing left to distinguish 4-the-drive from 4-the-port. `SHOW <id>` lists the units, and it reads `Board::units()` — the same list MOUNT reads, so they cannot disagree.
- Endpoints: `console` | `socket:PORT` (listening) | `socket:HOST:PORT` (outbound) | `serial:/dev/tty.usbserial-X` or `serial:COM3` | `in:path` (reader) | `out:path` (punch) | `null`. **All of them are built** — `console`, `null`, `loopback`, `scripted`, `socket:`, `serial:` and `in:`/`out:`. (This line read "Built so far: `console`, `null`, `loopback`" long after that stopped being true, which is why the monitor's help text for CONNECT is now *generated* from the resolver's own `endpointHelp()` rather than written out here or in `commands.cpp`.) The resolver **names the legal forms when you ask for one it does not know**, rather than failing as though you had mistyped it.
- Exactly one unit may hold `console` at a time; the monitor arbitrates. **Connecting a second STEALS it and says who from** — two boards reading one keyboard would each get half the characters, which is not hypothetical: it is what happens the first time a machine has two 2SIOs and you forget.
- Disk images are buffered and written back. **The board** probes the image size against the formats *it* knows and declares the layout to `DiskImage` (§7.3); `media = ...` forces the choice when the size is ambiguous, and **the choices belong to the card**: an 88-DCDD takes `8in` and `fdc8mb`, an 88-MDS takes `minidisk`. Naming another card's medium is an error, not a probe — the two controllers are register-compatible, so nothing else would have caught it. `readonly` supported (the real board's write-protect).

---

## 10. Monitor CLI

Stable and greppable, in the tradition of the classic simulator command monitors.

```
CONFIGURATION
  CONFIG LOAD <file.toml>          CONFIG SAVE <file.toml>
                                   (bare LOAD/SAVE mean *memory* — see below)
  BOARDS                           the backplane: id, type, i/o, units, memory
                                   (BOARD too: it is a prefix of BOARDS, not an alias)
  BOARDS TYPES                     every board type compiled in, with its properties
  BOARDS ADD <type> <id> [k=v ...] BOARDS REMOVE <id>
  SHOW <id>                        every property: value, units, legal range
  SET <id> <k>=<v>                 generic; e.g. SET cpu0 CLOCK_HZ=1000000, SET mem0 fill=random
  SHOW ROMS                        every ROM compiled in: name, size, CRC32, description
                                   (use as mount = "builtin:<name>" — see §10.3.1)

INTROSPECTION
  SHOW BUS                         MAP + INTERRUPTS, one after the other. (It was
                                   specified as CPU/clock/cycles/pending IRQ state
                                   and is NOT that: the clock and the cycle count are
                                   the CPU CARD's, so they are SHOW cpu0.)
  SHOW BUS MAP                     memory decode map: range -> board, type, detail
  SHOW BUS IRQ                     the maskable IRQ wire (who is pulling it, and whether
                                   the CPU's I mask would let it be taken) plus the four
                                   vectors -- IRQ FFF8, SWI FFFA, NMI FFFC, RESET FFFE --
                                   as they currently stand in memory
  SHOW BUS CONTENTION              every address claimed by more than one board
  WHO <addr>                       reverse lookup: who responds here, and why

MEDIA AND CONNECTIONS
  MOUNT <id>:<u> <file> [RO]       UNMOUNT <id>:<u>
  CONNECT <id>:<u> <endpoint>      DISCONNECT <id>:<u>

CONSOLE  -- it CONFIGURES the console; it does not start the machine (RUN does).
  CONSOLE                          show it: properties, and WHICH UNIT HOLDS IT
  CONSOLE <k>=<v>                  set it.  SHOW/SET CONSOLE are the same, said long
  ATTN                             the key that takes the keyboard BACK from a running
                                   guest (default ^E). Tracked on CONSOLE INPUT ONLY:
                                   a unit on a socket or a serial port is not the
                                   console, and its data passes through UNALTERED.
  The transforms -- UPPER, STRIP7IN, STRIP7OUT, CRLF, BSDEL, ECHO, BELL -- are
  properties of the CONSOLE and of nothing else: SET CONSOLE UPPER=ON. They were
  once on the LINE (SET sio0:a UPPER=ON), and that was REVERSED 2026-07-13 as a
  data-corruption bug -- see 7.2. A card's line must stay 8-BIT CLEAN, because a
  line carries XMODEM and a filter on it corrupts a transfer silently. What a card
  has instead is line CODING (baud, data_bits, parity): a frame, never a mask.
  SET sio0:a UPPER=ON is an error today, and says so.
  TABS, ANSI, ROWS, COLS and PACE are NOT BUILT -- see 7.2. LOG tees the session
  to a host file (SET CONSOLE LOG=path; empty/off closes it). DEBUG aims the
  runtime diagnostic sink (SET CONSOLE DEBUG=stderr|stdout|path); the per-source
  channels are SET <name> DEBUG=<flags> / SHOW DEBUG -- see 7.6.

  WHICH UNIT IS THE CONSOLE? The one CONNECTed to it. Exactly one may hold it (there
  is one keyboard); connecting a second STEALS it and says who from. A config file
  that names two is REFUSED -- interactively the last cable you plug in is the one
  you meant, but in a file there is no "last": it is a typo.

  THE KEYBOARD IS BUFFERED BY THE HOST. Keys land in a buffer here and a card takes
  characters from it. That is what lets ATTN be watched whether or not anybody is
  reading -- including with no serial card in the machine at all -- and what lets
  MCP inject input that no board can tell from a human's.

MEMORY
  LOAD <file> [AT <addr>] [FORMAT=BIN|HEX] [ROM]
                                    HEX carries its own addresses; a flat binary does
                                    not, so it REQUIRES AT. The file's CONTENTS decide
                                    which it is; FORMAT= overrides and always wins. AT
                                    means PUT IT HERE for both: on a HEX file it moves
                                    the image so its FIRST DATA RECORD lands there,
                                    wrapping modulo 64K. ROM is the burner (10.2).
  SAVE <file> <range> [FORMAT=BIN|HEX]
                                    The NAME decides -- .HEX writes Intel HEX, anything
                                    else writes a flat binary -- because SAVE cannot
                                    sniff a file that does not exist yet. FORMAT=
                                    overrides.
  DUMP [<addr>|<range>] [WIDTH=16]  hex + ASCII. A bare <addr> runs to the END OF ITS
                                    PAGE (D 0001 -> 0001-00FF); bare DUMP continues
                                    from there. Page-aligned in and out, so the rows
                                    and the columns both stay put.
  DISASM <range>|<addr> [n] [CPU=6800]
                                    Mnemonics follow the ACTIVE CPU -- you never type
                                    CPU=. It is the override for a machine with no CPU
                                    card in it, or for looking at foreign code (§3.0.2).
  EDIT <addr> [ROM]                 Interactive DEPOSIT: the prompt shows an address and
                                    the byte there; a value writes it and drops to the next,
                                    a bare Enter leaves it and drops to the next, '.' stops.
                                    A REAL bus write (says so if nobody decodes it; ROM
                                    burns). Reads the new bytes from the monitor's own input.
  EXAMINE [<addr>]                  ONE byte: hex, ASCII, bits. Bare = EXAMINE NEXT.
                                    EXAMINE IS THE CPU: it jams the address into the
                                    PROGRAM COUNTER and the CPU drives the address lines.
                                    So EX LOADS THE PC (`EX E0D0` + RUN starts a ROM),
                                    the PC *is* the cursor, and with NO CPU CARD IT IS AN
                                    ERROR -- nothing is driving the bus. (Look at a
                                    CPU-less machine with DUMP: it runs no cycle, so it
                                    needs nobody to drive one.)
  DEPOSIT <addr> <bytes...>
  FILL <range> <byte>
  SEARCH <range> <bytes...>|"str"
  COMPARE <range> <addr>            memory to memory. There is no <file> form.
  MOVE <range> <dest>
  LOAD, DEPOSIT, FILL and MOVE take an optional ROM: program a ROM, by going behind the
  bus into whichever chip answers reads there (§10.2). The read side needs no such word
  -- a ROM answers reads like anything else. EVERY ADDRESS HERE IS A BUS ADDRESS.

EXECUTION
  RUN [addr] | STEP [n] | STOP      STOP is NOT BUILT (10.1): it needs a monitor that
                                    runs alongside the machine. ATTN is how you get out.
  RUN is the ONLY way to start the machine. `RUN <addr>` is EXAMINE + RUN: it loads
  the PC first.
    - A unit holds the console -> the GUEST GETS THE KEYBOARD (every key, including
      ^C, which the guest is entitled to read), and it runs at the CPU card's real clock.
    - Nothing holds it       -> there is nothing to hand over, so it just runs.
  That is not a mode the operator picks. It is a fact about the backplane, and the
  machine already knows it -- which is why GO was DELETED (Patrick, 2026-07-12):
  a "headless run" was never a second thing to be. Both paths stop on a breakpoint,
  on a HLT nothing can wake, and on ATTN, and both say which.

  ATTN (^E) IS THE STOP KEY, NOT ^C. Ctrl-C belongs to the guest. ATTN does not stop
  the machine -- it takes the keyboard back, and a bare RUN resumes where you were.
  RESET | RESET CPU | POWER
  There is NO `SET CPU`. The CPU is a CARD (§3): BOARDS ADD 6800 cpu0, and the clock
  is that board's property -- SET cpu0 clock_hz=1000000. A card carrying more than one
  core exposes them as UNITS and switches between them itself (§3.0.1).

DEBUG
  BREAK [<addr> [IF <expr>] | MEM R|W <addr> | IO R|W <port>] [TRACE ON|OFF] | NOBREAK
                                    The DIRECTION is required on both watch forms, and
                                    they take an address, not a range.
  REGS | SET REG <r>=<v>            Generic: registers are reflection (§3.0.3), so a
                                    6809 works the day it lands with no monitor change.

  EVERY STOP PRINTS WHY, AND THEN ONE LINE (§3.0.3.1). STEP traces in the same line,
  DDT-style: the machine as it stands, WITH the instruction it is about to run.

    ATTN -- the machine is still at E201. RUN resumes.
    H0I1N0Z0V0C0 A=02 B=10 X=8004 SP=A03D PC=E201  ASRA

  The reasons are ATTN (you took the keyboard back), a breakpoint (which one, and of
  what kind), HLT that nothing can interrupt, ^C, a SCRIPT'S INPUT ENDING, and no CPU
  in the machine. Six different things, and they get six different words -- the monitor
  used to GUESS at two of them from whether a console happened to be attached, and a
  guess is what you write when the reason was never carried in the first place.

  A CPU PARKED IN WAI DOES NOT RUN. It leaves WAI for an interrupt or a RESET, and
  loading the PC is neither -- so `RUN <addr>` on a parked machine says WAI again,
  correctly, and the fix is the RESET a human would throw. (This is what the honest stop
  reason caught first, in our OWN test: tests/acceptance/cli.exp had been "testing" ATTN
  against a machine that was parked the whole time, and matching the prompt that came
  back with the WAI.)
  TRACE ON|OFF [file] [MASK=IRQ,CONTENTION]   HISTORY [n]
  SYMBOLS LOAD <file> [REPLACE] | SYMBOLS CLEAR          SHOW SYMBOLS [<name>|<glob>]
                                    A Motorola as0/as9 .LST listing, so a name works
                                    wherever an address is typed (§10.3.2). HOST-SIDE like a
                                    breakpoint: survives RESET/POWER/CONFIG LOAD.
  SNAPSHOT <file> | RESTORE <file>  BUILT (13): the machine's STATE to a file, and back
                                    into a machine of the same shape. Board::serialize()
                                    on every board, the CPU core and the Clock (4, 13.1).
  RECORD <file> | REPLAY <file>     NOT BUILT (10.1, 13). They resolve and say so; they add
                                    a T-stamped event log on top of SNAPSHOT -- the next phase.
  SET BUS CONTENTION=WARN|ERROR|SILENT
  SET BUS UNCLAIMED=WARN|HALT|SILENT   floating-bus diagnostic, default SILENT (4.6.1). HALT
                                       stops the guest at an address no board decodes.
```

### 10.0.0 The command line, and the built-in machines

**Settled 2026-07-11 by Patrick.** This closes open finding **F4** (the command-line grammar was undefined).

```
swtpcsim [options] [<machine>]

  <machine>            a BUILT-IN name, or a FILE if it has a '/' in it or ends .toml.
                       Omitted: `./swtpcsim.toml` if the working directory has
                       one, else `default`.
  -m, --machine <n>    ALWAYS a built-in name -- never a file.
  -f, --file <path>    ALWAYS a file -- never a built-in name.
  -n, --none           empty backplane. No boards at all.
  -l, --list           list the built-in machines and exit.

  -s, --script <file>  run a command script, then exit with its status.  (was `-c`)
  -x, --exec <cmd>     run one monitor command (repeatable), then exit.
  -i, --interactive    after --script/--exec, stay in the monitor.
      --mcp            MCP server on stdio.
  -v, --version        -h, --help
```

`-s script.cmd` is the CI/regression entry point: a script that fails exits non-zero. `-x` is the same thing for one-liners, which is what makes a bug report reproducible in a single pasteable line.

**A built-in machine is a TOML file that lives in `.rodata`.** `machines/*.toml` are build-time inputs — CMake embeds them byte-for-byte, exactly as it does the ROM images, and `loadTomlText()` parses them at runtime with the *same* parser that reads a config off disk.

Two things follow, and both are requirements rather than conveniences:

- **The shipped binary is self-contained.** swtpcsim is delivered as **one executable plus documentation**. It must never go looking for a `machines/` directory, an install prefix, or anything relative to `argv[0]` — a simulator that cannot find its own default machine when copied to a USB stick is a simulator that does not start. `tests/test_machines.cpp` passes with the source tree deleted.
- **There is exactly one machine language.** Building the default machine in C++ (`m.add("memory", "mem0")`) would have been fewer lines and a quiet second dialect that nobody could copy, edit, or diff. Instead the machines we ship are written in the format users write, so they double as worked examples and *cannot drift from it* — if the config format changes under them, they stop loading and a test goes red.

**File or built-in is decided by SPELLING, never by probing the filesystem.** If the answer depended on the working directory, `swtpcsim swtpc` would mean one thing today and something else the day somebody saves a file called `swtpc` next to it. A command line whose meaning changes with its surroundings is a trap, and it is the kind that gets sprung at 2am. `-f ./swtpc` and `-m swtpc` never guess.

**The one filesystem probe is the EMPTY command line, and it does not weaken that.** With no machine argument at all, swtpcsim looks for `./swtpcsim.toml` and boots it, falling back to `swtpc`. There is no spelling to honor in that case — nothing was named — so the invariant above is untouched: it governs how a name you *typed* is resolved, and `swtpcsim altair680` means `altair680` in every directory on earth. What the probe buys is a project directory that boots its own machine when you type nothing, which is worth one well-known filename. It is the only file the simulator *finds* rather than is *given*.

The built-ins are the two Motorola 6800 machines this simulator ships:

| | |
|---|---|
| `swtpc` | The SWTPC 6800: `6800` CPU, the MP-S serial console (`mps`) at `$8004`, the DC-4 floppy (`dc4`), RAM, and the **SWTBUG** monitor ROM at `$E000`/`$FC00`. What you get with no arguments. |
| `altair680` | The MITS Altair 680b: `6800` CPU, its on-board 6850 console, and the **MON680** monitor ROM. |

Each machine boots its own monitor ROM through the reset vector — `startup = ["RESET", "RUN"]`, the operator's keystrokes, never fabricated hardware. `swtpcsim` and then the monitor's own prompt (`$` for SWTBUG, `.` for MON680) is the whole of it. For a bare machine to inspect by hand, `swtpcsim --none` gives an empty backplane and the monitor is then the only bus master.

### 10.0.1 The number base: on the wire → hex, never on the wire → decimal

**Settled 2026-07-11 by Patrick.** This closes open finding **F3**.

> **The base is a property of the OPERAND, not of the command line.**

| | |
|---|---|
| **HEX** — the machine sees it | addresses, ports, data bytes, register values, opcodes |
| **DECIMAL** — only you see it | step counts, dump widths, history depth, sizes, baud rates, unit numbers |

`DUMP 100` starts at `0100h`. `STEP 20` steps twenty times. `SET mps0:tty baud=9600` is nine thousand six hundred. The 6800 never holds a step count or a baud rate, so those are not hex; it holds an address on sixteen pins, so that is.

**A single global base was never actually on the table.** `baud=9600` cannot mean 38400, so the rule was always going to bend somewhere — and given that, it bends where it *means* something instead of where it happened to fall. This is also why the alternative ("everything in a command is hex") was rejected: it buys one sentence of simplicity and pays for it with `STEP 20` stepping 32 times, silently, forever.

**Overrides work everywhere, in both directions**, because a rule you cannot type your way out of is a trap: `0x20` / `$20` / `20h` force hex, `#32` forces decimal, `0b1010` is binary, `1_000` is spacing.

**A `K`/`M` suffix is always behind a decimal number** (Patrick) — `10K` is 10,240, never 16K, which is why in fifty years nobody has had to ask. The suffix therefore carries its own base and overrides the caller's default. `0x10K` demands hex *and* a suffix that is decimal by definition; there is no right answer, so **it is rejected rather than guessed at**.

**There is exactly one parser** — `parseNumber(text, out, err, base)` in `core/value.cpp` — and the caller passes the default base, because the caller is the only one who knows what kind of quantity it is. Properties carry theirs as `radix` (§10.4), which is the same rule reaching the same answer through the reflection layer. **No call site may pre-chew its input to get the base it wanted**; three of them used to (the CLI prepended `"0x"` to `at=`, the property layer prepended `"0x"` to any radix-16 value, and the region parser stripped its own `K`), and all three were quietly wrong in a different way.

Pinned by `tests/test_numbers.cpp`. The failures this catches are all silent: every one of these tokens parses fine under the wrong base, so nothing crashes — you just size a card at 16K when you wrote 10K, and find out much later.

### 10.0 There is no `BOOT` command. The config file runs commands instead.

An earlier draft listed `BOOT <id>[:u]`. **It is removed, because it has no honest meaning.**

A 6800 takes its first `PC` from the **reset vector** at `$FFFE`/`$FFFF`, which points into the monitor ROM (SWTBUG's is `$E0D0`). Something must get the machine there, and a simulator has only two ways to do it:

1. **Synthesize a bootstrap internally** and jam it into memory (what SIMH's `BOOT` does). **Forbidden by §0.1** — it is fabricated hardware, and it means the machine boots in a way no real machine ever booted.
2. **Do what the operator does: reset, and let the vector decide.** `RESET` arms the reset-vector fetch; `RUN` executes the first instruction, which loads `PC` from `$FFFE` and drops into the monitor. No address is typed — the vector decides where the CPU starts.

The reset vector at the top of memory **is** the 6800's power-on jump, in hardware — so there is no turnkey board or PROM-forcing trick to model, and nothing to fabricate.

So the monitor keeps only the honest verbs — **`RESET` and `RUN`; `GO` was deleted 2026-07-12, because there was never a second thing for `RUN` to be** — and to spare you typing them every session, **a machine config can carry a list of monitor commands to run once the backplane is built**:

```toml
[machine]
name = "swtpc"

startup = [                     # monitor commands, run in order after boards are created
  "RESET",                      # arm the reset-vector fetch
  "RUN",                        # load PC from $FFFE and run SWTBUG. Operator keystrokes, not fake hardware.
]

# NOTHING ELSE IS A [machine] KEY, and the one that used to be is the argument for
# why. `clock_hz` was here (the crystal is on the CPU CARD) and is a board property
# now -- a REFUSAL, not a silent migration: a config that looks like it set something
# and did not is worse than one that will not load.
[[board]]
type     = "6800"
id       = "cpu0"
clock_hz = 1_000_000
```

Three things fall out of this, and they are the reason it is the right shape:

- **The config language and the script language become one language.** A `startup` entry is an ordinary monitor command, so anything you can type, a config can do — and `swtpcsim -s script.cmd` and `CONFIG LOAD` stop being two different worlds.
- **`BOOT`'s special-casing disappears.** No verb needs to know what a "boot device" is, and a new disk controller written next year needs no monitor change to be bootable.
- **It is transparent.** `SHOW MACHINE` prints the startup commands; `CONFIG SAVE` round-trips them verbatim. Nothing happens that the user cannot see written down.

**"Anything you can type" has to be literally true, or it is a slogan.** A `startup` entry is a command line, and a command line **quotes its filenames** — the monitor's tokenizer requires it, because a period artifact's name often has a space in it. For a long time the array parser toggled on every `"` with no escape handling, so `MOUNT dc40 drive0 \"...\"` was silently cut at the backslash and the machine came up with an empty drive. The one thing `startup` exists for could not be expressed. `\"` and `\\` are now understood on both sides — and **any other escape is refused rather than quietly eaten**, so a Windows path written with single separators fails here instead of somewhere else, later, as a shorter and wrong path.

### 10.0.2 `base` — a config file may be a DELTA on another machine

A FLEX machine is *the `swtpc` with a disk in drive 0*. Before `base`, saying that took the whole backplane restated by hand — and **hand-copying a backplane is a defect class, not a chore**: a machine restated by hand can drop a board and boot into a console that is not there, and look fine doing it.

```toml
[machine]
name = "swtpc-flex"
base = "swtpc"            # cpu0 (6800), mps0 (MP-S console), dc40 (DC-4), mem0 (RAM + SWTBUG)

startup = ["RESET", "RUN"]

[[board]]                 # no `type` -> the card ALREADY in the machine with this id
id = "dc40"

  [[board.drive]]
  unit  = 0
  mount = "examples/flex/FLEX2-40.DSK"
```

**The base is named, never assumed** (Patrick, 2026-07-13). An implicit default was the other option and it is the wrong one: a machine **defined by what it does not have** would have to *remove* boards to describe a barer one, and **silence would stop meaning "nothing"**. A file with no `base` is a complete machine, exactly as before; one line at the top tells you what a delta starts from, and without that line the file *is* the backplane.

The four `[[board]]` forms — **add** (`type` + a new id), **replace** (`type` + an id from the base), **modify in place** (no `type`), and **remove** (`remove = true`) — are documented in `docs/config.md`. Two of them are load-bearing:

- **Replace exists because a list cannot be amended into a smaller one.** Regions are a *list*, so adding a 24K region to a base's 56K memory board would **overlap** it — two boards driving `0000–5FFF`, which is contention — not shrink it. Naming a card's `type` means *"this is the whole card now."*
- **A duplicate id within one file is still an error**, and replace is scoped around that check on purpose. A second `[[board]]` with a copy-pasted id is a **typo**; the same thing against a base is **intent**. Conflating them would discard the one diagnostic that catches the commonest mistake in a hand-written machine file.

**`CONFIG SAVE` never writes a `base`.** It writes the backplane it can see — every card, inherited or not — so a saved machine stands on its own. That is the only honest thing it can do, because a base may be a *file*, and a file can change under you.

**This is what makes `swtpc` a contract.** The machine `base = "swtpc"` starts from is a `6800` CPU, an MP-S console, a DC-4 floppy, RAM, and the SWTBUG ROM. Adding a card to it is no longer free — other files now depend on what is in it.

> **Caution, and it must be in the docs:** `CONFIG LOAD` on a machine file now *executes commands*. Loading a `.toml` from an untrusted source runs whatever is in its `startup` list. Keep `startup` to monitor commands only, and say so out loud.

### 10.1 `SET`/`SHOW` are generic

Implemented once against `Board::properties()`; they know nothing about baud rates, memory decode, or disk geometry. Adding a board adds its settings to the CLI for free. **Every property is settable** — see §5: you can only type at the prompt when the machine is already stopped, and a real card on an extender has its jumpers moved with the power on.

### 10.2 Memory access: through the bus, or behind it?

- **Default: through the bus.** `DUMP` and `DEPOSIT` see exactly what the CPU sees — live bank, ROM not decoding writes, contention reported. Addresses are **bus addresses**, 0x0000–0xFFFF. This is the only view that tells the truth about a misbehaving decode, and it is why a `DEPOSIT` to a 6850's data address really does hand the chip a byte: that is what a bus write *is*.
- **`peek`: through the decode, but *without a cycle*.** Same decode, same bank, same board — but no strobe, no side effect. **`DISASM`, `WHO` and the debugger's display use this, and they must.** *(Corrected 2026-07-11: §10.2 originally put `DISASM` in the first group. That was wrong, and quietly so — a disassembler built on real reads works perfectly against RAM and then, the first time someone disassembles a page with a 6850 mapped into it, **eats the console's input**. The bug would only appear when the memory map was unlucky.)* A board that cannot answer without side effects returns false, and the byte reads `FF` — which is honest, because on real hardware the data bus is only defined *during* a cycle.
- **`ROM`: behind the bus**, into whichever chip answers reads at that address. A **write-side** qualifier on `LOAD`, `DEPOSIT`, `FILL` and `MOVE`, and nothing else. Addresses are bus addresses like everywhere else.

- **The PC is a CPU address, and the CPU may not see the bus's address space.** On the MP-09 the DAT sits between them (§3). So a view that *starts from the PC* — the instruction on the register line, `NEXT`'s look at the opcode, HISTORY's bytes, where a bare `DISASM` continues — asks the CPU card first where that address lands on the bus (`CpuCard::toBus`, the identity on every other card) and reads there. Everything the operator types is still a bus address; only the PC is translated, because only the PC was never one.

**EVERY ADDRESS IN THIS MONITOR IS A BUS ADDRESS, 0x0000–0xFFFF.** There is exactly one address space the operator can type, and it is the one the CPU sees. *(Patrick, 2026-07-17: board-local offsets are out as too confusing — every address refers to the 64K address space.)*

**`ROM` is the PROM burner, and that is not a metaphor.** A ROM region does not decode a write cycle (§4.2), so `DEPOSIT FF00 41` cannot possibly reach it — nor should it, because on real hardware a bus write can't program a PROM either. You pull the chip and put it in a programmer, which is *not a bus operation*. `LOAD dbl.hex ROM` is exactly that, and it is why **the operator can write ROM while the guest cannot**, with no `writable` flag to leak and no originator tag on the bus.

It finds the chip by asking who answers a **read** there, which is the whole trick: a ROM does not decode a write, so asking who would take a write is asking the wrong question on precisely the chip you are trying to program. `Bus::respondersTo()` runs the real decode — so a board that is disabled or banked away does not answer, and you cannot burn a chip the machine cannot currently see, any more than the CPU could read it. Nobody home and contention are reported, never guessed at: `Machine::burn()` is the one implementation, and the monitor and MCP are two front ends onto it.

*(Superseded 2026-07-17. This was **`RAW <id>`**: it named a board, addressed that board's store by a **board-local offset**, and worked for reads as well as writes. All three are gone. **The board id** carried no information — through the bus you never name a board, the address picks it, so naming one was a second way to say a thing the address already said. **The offsets** were a second address space, and the same digits meaning two things depending on a qualifier is a trap laid for the operator. **The read side** existed only to reach a store the bus could not see — a bank that is not selected — which is reachable by selecting the thing you want to look at, exactly as the guest has to. What was left was the one thing a bus cycle genuinely cannot do, and that is this.)*

The alternative — giving `BusCycle` an `origin = Cpu | Monitor` field so ROM could accept "monitor" writes — was rejected. A real backplane cycle carries no such tag; that is *why* a monitor `DEPOSIT` is indistinguishable from a CPU write, and why a real ROM ignores both. Add the tag and every board built hereafter has to reason about it.

**There is no `BANK=` qualifier, and there must not be one.** Bank *count*, *size*, and the *select register* are all board-specific — the framework's S-100 memory boards banked in incompatible ways (§4.3) — so a `BANK=<n>` in the monitor would hardcode one banking model into a CLI that is supposed to know nothing board-specific, the same error as a bus that invents interrupt vectors (§4.4). No 6800 machine here banks; if one ever does, its live bank is a read-only `properties()` value, reported and never set from the monitor — the guest sets it by writing the card's select register, and reading it back is the only thing the monitor gets to do.

### 10.3 Intel HEX

Loader accepts record types **00** (data), **01** (EOF), **03**/**05** (start address — captured, optionally sets PC). **Validate every record's checksum and fail loudly with the record number** — a silently truncated load is a miserable bug to chase. Types 02/04 are accepted if they resolve within 64K, else error. `AT <addr>` biases the record addresses.

Writer emits 00/01 with a configurable record length (default 16) and an **`05`** (start *linear* address) record if an entry point is given; the loader accepts `03` and `05` both. **Round-trip is a test case:** `SAVE x.hex 0-FFFF` then `LOAD x.hex` must reproduce memory byte-for-byte.

Binary is a flat image: `LOAD` needs `AT <addr>`, `SAVE` needs an explicit range. Autodetection sniffs for a leading `:` and printable HEX records; `FORMAT=` forces it.

The same engine backs the MCP tools (`mem_load`, `mem_save`, `mem_dump`, `disasm`) — one implementation, two front ends.

### 10.3.1 Built-in ROMs

**The common ROMs are compiled into the binary.** There is no portable place to keep ROM images across the four targets — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the executable" are four different answers, each of which becomes a support question and a platform special case in exactly the code §2.1 exists to keep clean. ROMs are small (a boot PROM is 256 bytes), so embedding them makes the problem vanish: a fresh checkout boots on any OS with nothing to download.

Source images live in **`roms/`** in the repo. At build time CMake turns each into a byte array in a generated translation unit, and a registry maps `builtin:<name>` → `{span<const uint8_t>, crc32, description}`.

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xFF00
  mount = "builtin:dbl"      # compiled in
# mount = "roms/mine.bin"    # a bare path is a host file, and it always wins
```

`builtin:` reuses the scheme idiom already established for `connect` (`socket:`, `serial:`, `in:`/`out:`), so it adds no grammar. Consequences worth stating:

- **The board never knows the difference.** A region takes a `span<const uint8_t>`; whether it came from `.rodata` or from a file the host service read is not the board's business (§7). `builtin:` is resolved by the config loader, above the board.
- **No filesystem access at runtime** for a built-in.
- **`CONFIG SAVE` round-trips the name, not the bytes.**
- **Built-ins are a convenience, never a lock-in** — a path overrides, so anyone with a different dump of the same part uses it without patching the simulator.

**Every built-in ROM has a provenance row in `docs/roms.md`** — source, exact size, CRC32 — and a unit test verifies the CRC at build time. This is §0.1 applied to binaries: **a ROM image is a hardware fact**, and an embedded blob of unknown lineage is the worst kind of second-hand fact, because every piece of software above it would then be debugged against the wrong ground truth and it would look like a software bug for a very long time. Nothing is embedded without a source.

### 10.3.2 Symbol files — the assembler `.LST` listing

**A symbol table is NOT `LOAD`, and the wrong answer is silent.** `LOAD` is memory all the
way down: every format it accepts becomes bytes in the 64K address space, and its sniffer is
a two-way branch — a leading `:` is Intel HEX, everything else is a flat binary. So
`LOAD prog.LST AT 100` does not error; it deposits the listing file's ASCII into RAM. A symbol
table has no address space to land in — it is the debugger's *names* for one — so it is its
own top-level verb, `SYMBOLS`, the way `CONFIG` is a separate verb precisely because what it
loads is not memory. `core/symbols.h` mirrors `core/hex.h`: one implementation, and the
`SYMBOLS` command, the `startup` re-load, and the MCP tool are its front ends.

**Host-side, like a breakpoint.** The table belongs to no card — it is a view *of* the address
space, not a property *in* it — so it lives on `Machine`, not a board, and the "no
machine-level board state" rule (`machine.h`) does not reach it. It survives `RESET`, `POWER`
and `CONFIG LOAD` exactly as breakpoints do, and `SYMBOLS CLEAR` is its `NOBREAK`. A machine
file may name a symbol file in `startup`; `CONFIG SAVE` round-trips the **filename, not the
parsed table**, the same bargain `builtin:` makes for a ROM (§10.3.1).

**Reference first, display later.** A loaded symbol resolves anywhere a *true address* is
typed (`BREAK`, `DUMP`, `EXAMINE`, a `BREAK … IF` operand) — not where a byte is, which is why
the resolution is gated per call-site and not folded into the one overloaded address parser. A
symbol wins over a bare hex literal, with the same leading-zero escape that tells the register
`A` from the number `0A`. **Annotating disassembly (`JMP 0100` → `JMP START`, `JSR E0D0` →
`JSR RESET`) is built** — and it needs no operand-kind split across the opcode tables, because
an `Insn` melts its operand into text and that text is enough: a run of exactly four uppercase
hex digits is, in the 6800 emitter, only ever a 16-bit (extended) operand or a branch target (a
byte immediate or direct operand is two digits, an index displacement carries a sign). So
`Monitor::annotateOperands` names any four-hex token that *resolves* to a symbol and leaves the
rest alone — which does mean a 16-bit constant reads as a symbol when its value happens to match
one, the accepted price of not modelling operand kind. Two maps, two rules: a leading `NAME:`
line comes from the label-only reverse map (`byAddr`), so an `EQU` never heads a line; an operand
is named by `operandName`, which prefers a label but falls back to any symbol with the value, so
an `EQU` that is really an address still names its target.

**One format, absolute addresses only.** The loader reads a **Motorola `as0`/`as9` cross-assembler
listing** (`.LST`/`.PRN`): a line-number column, then an address/value column, the object bytes,
and `LABEL OP OPERAND`, with `*` starting a comment. The label and address columns are **detected
per file** (they differ between `as0`, SWIMON and KCACR listings), so the parser adapts rather
than assuming one fixed geometry. An `EQU`/`SET`/`=` line defines a **constant** — it feeds
name→value only, and **never the reverse map** — while any other label is a **real address** that
also feeds `byAddr`, so `START EQU 5` does not make address `0005` render as `START`, but a genuine
program label does name its target. (The old CP/M `.SYM` flat-symbol format and the Microsoft `M80`
relocatable-listing apostrophe guard were removed with the 8080 toolchain — this is a 6800 tool now,
reading what `as0`/`as9` emit.)

### 10.4 Line editing and history

The monitor is a REPL you will spend hours in, so it gets a line editor.

**What is built** (`src/cli/lineedit.cpp`, 167 lines over `platform/terminal.h`):

- **History** with Up/Down, **in memory for the session**.
- **Editing**: Left/Right, Ctrl-A/Ctrl-E, Ctrl-W, Ctrl-U, Delete (`ESC[3~`), and Ctrl-D as EOF
  on an empty line.

**What is NOT built, and was planned here:** history persisted to `~/.swtpcsim_history`,
Ctrl-R search, word motion, Ctrl-K, Home/End — and **tab completion**. The completion design
was the interesting part and it is worth keeping on the page, because it is *free* if it is ever
written: driven by the same `Board::properties()` reflection, it would complete command names,
then board `id`s, then that board's property names, then that property's legal enum values —
`SET sio2a BA<Tab>` → `BAUD=`, and `BAUD=<Tab>` offering the valid rates, with **no completion
tables to maintain** and a board added later completable the day it lands. **But there is no Tab
handling in the editor today**, so §5 must not be read as claiming completion among the things
reflection already buys. It buys `SET`, `SHOW`, the TOML loader, `CONFIG SAVE` and the MCP
schemas; not this.

**Implementation: hand-rolled, not `replxx`.** This section used to specify vendoring replxx as a
deliberate exception to "no third-party deps in the core," on the grounds that a cross-platform
line editor is weeks of work. **The exception was never taken** — the subset above turned out to
be small, and it sits behind `platform/terminal.h` like every other OS difference (§2.1), so
Win32 costs no `#ifdef` here. The cost of that choice is exactly the missing features listed
above; the benefit is that `swtpcsim` still has no third-party dependency at all (§2).

**Terminal-mode handoff.** The line editor and the emulated console both want raw mode and must trade it cleanly: `CONSOLE` hands the terminal to the guest, the `ATTN` key hands it back. **The terminal must be restored on every exit path, including a crash or a signal** — a simulator that leaves your shell in raw mode after a segfault is its own kind of bug. Windows console mode flags need the same care.

---

## 11. MCP server

`swtpcsim --mcp` — **stdio only**. (A `--mcp-port N` TCP form, to attach to a long-lived
machine, was specified here and is **not built**; there is no such flag.) Tools mirror the
monitor but are **structured** — every result is JSON, no screen-scraping.

The high-value tools are borrowed from the Python prototype's agent API, which is the right
shape. **The list below is the DESIGN, and nearly all of it is built.** The thirty-one tools that
exist today are `run`, `send`, `recv`, `regs`, `monitor`, `reset`, `roms`, `mem_dump`,
`mem_deposit`, `mem_load`, `mem_save`, `mem_search`, `mem_fill`, `disasm`, `step`, `breakpoints`,
`snapshot`, `restore`, `bus_trace`, `mount`, `connect`, `board_types`, `board_list`, `board_get`,
`board_add`, `board_set`, `who`, `bus_map`, `bus_io`, `bus_contention` and `bus_irq`. The two
still unbuilt are marked:

- `run(max_steps, idle_threshold) -> {reason: halt|breakpoint|idle|max_steps, steps, cycles}`
- `send(text)` — queue keystrokes to the console unit
- `recv()` — take what the guest has printed since last time. **This is what shipped instead of
  `expect`:** the pattern-matching and the retry policy belong to the caller, who is a language
  model and is better at both than a fixed matcher would be.
- *(not built, and won't be)* `expect(pattern, max_steps, idle_threshold)` — run until the
  pattern appears. Superseded on both ends: `recv` above puts the matching where it belongs, and
  `run`'s `until` argument already stops the instant a substring appears, which is the useful 90%
  of `expect` without a fixed matcher baked in.
- *(not built)* `screen() -> {rows, cols, grid}` — a VT100/ANSI screen emulator, so a test could
  assert on a **screen grid** rather than a byte stream. The one genuinely missing capability
  here: full-screen guest apps are still asserted against a byte stream. The engine now exists
  (`src/host/terminal/`, issue #244); the remaining work is feeding the MCP session's console
  stream through it.
- `regs`, `monitor` (run one monitor command and get its text back — the escape hatch, and the
  way to reach the corners the typed tools don't cover: a conditional `BREAK ... IF`, an octal
  listing, the trailing `key=value` options on `MOUNT`)
- `step`, `breakpoints`, `snapshot`, `restore`, `bus_trace` — the debugger, structured. `step`
  advances N instructions and returns the register file; `breakpoints` lists/adds/removes the
  same CPU-agnostic breakpoints the monitor sets; `bus_trace` returns the always-on flight
  recorder's last N cycles; `snapshot`/`restore` drive `Machine::snapshot()`/`restore()`.
- `mem_dump`, `mem_deposit`, `mem_load`, `mem_save`, `mem_search`, `mem_fill`, `disasm` — the
  same `rom` qualifier as the monitor, the same engine. `disasm` is the stateless disassembler
  over a non-invasive peek, so it runs with no CPU and reads a ROM.
- `mount`, `connect` — a host file into a board's unit, a serial unit onto an endpoint; typed
  `id`/`unit` rather than the monitor's `id:unit` string.
- `reset(kind: bus|cpu|power)`, `roms`
- `board_list`, `board_types`, `board_get(id)`, `board_add`, `board_set(id, key, value)` — **schemas generated from `Board::properties()`**
- `bus_map()`, `bus_io()`, `bus_contention()`, `who(addr)`, `bus_irq()` — structured, not the
  ASCII table. `bus_irq()` is the interrupt bus: the IRQ line and who pulls it, the CPU's I mask,
  and the four vectors read out of memory.

**MCP is a first-class interface, not a wrapper.** The monitor and MCP both sit on one `Machine` API and one `properties()` reflection layer, so they cannot drift. Any board added later is fully drivable from both without touching either.

---

## 12. Host file transfer

The way files move between the host and a guest is by **mounting a disk or tape image** — `MOUNT dc40 drive0 <file>` — and letting the guest's own filesystem (FLEX, or SWTBUG's S-record `L`oad) read and write it. The image can be a local file or, transparently, one served over the network: `MOUNT dc40 drive0 tnfs://server/flex.dsk` reaches a FujiNet **TNFS** server, and every board gets that for free because the only difference from a local file is *where the bytes are* (§7.7, `src/host/tnfs.h`).

### 12.1 The Host Bridge board (guest-initiated) — heritage, not shipped

> **Heritage note.** The framework's Altair origin carried a guest-initiated **Host Bridge** card of the project's own design — an S-100 board at I/O port `0xB0`, with matching CP/M `R.COM`/`W.COM`/`HDIR.COM` utilities (8080 assembler, every disk op a BDOS call) that copied files through it, sandboxed to a configured `hostdir` root that a guest filename could not escape. It was a real board and a genuine test of the board API. But it is **8080/CP/M hardware** — an I/O port and `.COM` programs — and does not belong in a memory-mapped 6800 machine, so it does not ship here.

The sandbox primitive it forced still earns its keep and stays: `src/host/hostdir.{h,cpp}` resolves a guest-supplied name against a root and **cannot escape it** — no `..` component, no absolute path, no symlink that resolves out — tested against a **real** filesystem with **real** symlinks, because a symlink escape cannot be tested against a fake one. It is where any future 6800 host-transfer board would reach the host.

### 12.2 Host-side filesystem access to an image — **deferred, and here is the hard part**

The intent is real: read and write files in a mounted image **with the guest not running at all** — no boot, no console driving. For Claude that is far more efficient than typing at a monitor or FLEX prompt, and it is the right way to stage a test fixture or pull out a build artifact.

**But `DISK LS` cannot be a generic monitor command, and an earlier draft of this design had it as one. That was wrong.** Reading a file out of an image requires *two* independent facts, and the simulator holds neither in a place a generic command can reach:

1. **The sector layout** — a property of the **controller**. A DC-4 `.DSK` is **soft-sector**: it holds the payload *only*, because the headers lived in the inter-sector gaps and never reached the file. A hard-sector controller's image carries the whole slot, headers and checksum included. A reader that does not know which kind it is holding reads garbage.
2. **The filesystem parameters** — for a FLEX disk, the System Information Record, the directory geometry and the reserved system tracks; for a CP/M image, the DPB and software skew. These belong to the **image's OS**, not to the controller, and two images on the same controller can legitimately differ.

So a naive `DISK LS` needs a wrapper per controller **×** per filesystem. Its apparent genericity would be a lie, and the failure mode is the worst kind: it produces a plausible-looking directory listing off a misparsed image.

**Therefore: no `DISK` verbs in the monitor and no `disk_*` tools over MCP, for now.** The capability is deferred, not cancelled; §7.3's `DiskImage` gives it a foundation (the *controller* declares the layout, so half the problem is already solved in the right place), and the remaining half — a named filesystem descriptor supplying the FLEX or CP/M geometry — is a later design task. `cpmtools`' `diskdefs` is the precedent for CP/M, and it exists precisely because this cannot be inferred; FLEX needs its own.

Until then, **mounting an image (§12) and letting the guest's own OS do the I/O is the supported path.**

---

## 13. Snapshots and deterministic replay

> **HALF BUILT.** `SNAPSHOT` and `RESTORE` are done — `Board::serialize()`/`deserialize()`
> exist on every board, the CPU core and the `Clock`; `StateWriter`/`StateReader`
> (`src/core/statefile.h`) are the byte-exact, little-endian, CRC-checked primitive; and
> `Machine::snapshot()`/`restore()` write and read a file the two monitor commands drive. A
> snapshot is machine **state**, restored into a machine built from the same config — it
> refuses a topology that does not match (§13.1). **`RECORD`/`REPLAY` and `ReplayStream` are
> NOT built** — the deterministic-replay half below still resolves at the prompt and says so
> (§10.1). It builds directly on the snapshot foundation and is the next phase.

- **Snapshot** = versioned, explicitly-serialized CPU + `Clock` time + every board's `serialize()`. Byte-identical across platforms. **Done.**
- **Replay** = snapshot + an event log (keystrokes, socket input, SDL keystrokes, DMA completions) stamped with **cycle timestamps**, so a session replays exactly. **Not built.**

This is the bug-reproduction and regression-test foundation, and it is why **every source of nondeterminism (host time, socket arrival, RNG, window events) must funnel through one clocked event queue.**

### 13.1 What travels, what is rebuilt (the serialize contract)

The line every `Board::serialize()` draws, and why the snapshot is small and portable:

- **Serialize (runtime state):** the CPU register file *and its hidden micro-state* (the
  EI-after-next latch, the mid-INTA fetch, and on a Z80 WZ/MEMPTR, IFF2, the interrupt mode);
  every board's registers, latches, RAM, in-flight buffers and `enabled_`; absolute-cycle
  deadlines as plain integers; the `Clock`'s `t_` and handle counter.
- **Rebuilt, never serialized:** the bus decode cache and the interrupt/hold wire-OR counts
  (re-derived from each board's latched pins after restore); the `Clock` event queue, whose
  entries are `std::function` closures that cannot travel — so **each board re-arms its own
  deadlines** in `deserialize()` from the state it just read, into a queue `Clock::deserialize`
  has emptied. A `Clock::Handle` is never written.
- **Skipped:** TOML straps/config (already correct in a matching machine); host resources — a
  `ByteStream`/socket, a `DiskImage`, the `Display`, a `HostDir` — re-opened from their
  unchanged config. **In-flight bytes that are not yet on the host DO travel**: an uncommitted
  disk-write buffer, a half-typed hostbridge transfer, a tape head position. Host-side operator
  views (`syms`, breakpoints) and diagnostics are not machine state and stay put.

There are **no free-floating counters** to serialize — rotation, TDRE/TBMT, the RTC and the
minidisk motor are all absolute deadlines or pure functions of `Clock::now()`, which is exactly
what lets a deadline survive as one integer once `t_` travels with it.

---

## 14. Every board ships with a `.md` — a gate, not a nicety

**No board is merged without `docs/boards/<board>.md`.** The board and its documentation are one deliverable. Template: `docs/boards/_TEMPLATE.md`.

The **Limitations** and **Quirks** sections are load-bearing. They are what you will want in two years when something doesn't boot, and what makes it possible for someone else — or Claude — to work on a board without rediscovering everything. Writing them *forces* the honest question "what did I not actually implement?", which is exactly the question a simulator author most wants to avoid.

---

## 15. Testing

- **CPU:** opcode, addressing-mode and condition-code coverage in `tests/test_cpu6800.cpp` and `tests/test_isa6800.cpp`, plus booting real SWTBUG / MON680 / FLEX through the acceptance tests (§3.2). The 6809 has the same unit coverage in `tests/test_cpu6809.cpp` and `tests/test_isa6809.cpp`, and its boot gate is S-BUG and FLEX9 on the `swtpc09` machine (`tests/acceptance/flex9.exp`). A hard CI gate.
- **Bus:** unit tests for decode caching, contention detection, and the floating-bus `FF`.
- **Boards:** acceptance tests that boot real period software on a whole machine and read back the terminal — FLEX off a DC-4 `.DSK`, MON680 over its 6850 console, and Kansas City Standard cassette.
- **End-to-end:** headless acceptance scripts in CI on all three platforms (Linux, a universal macOS binary, Windows). Note these drive the monitor via `-s`/`-x` and `expect(1)`, not an MCP `expect` tool — there is no such tool (§11).
- **Lint:** the no-`#ifdef` check (§2.1).

---

## 16. Lessons from the prior work — read before writing code

The framework carries hard-won lessons from its 8080/CP/M origin; `docs/porting-notes.md` has the full list. Two are timeless and shaped this code directly:

1. **Idle detection** is what makes automation work (§8, §7.5). A run loop that spins at 100% of a core is unusable under MCP; standing the host down when the guest is parked in `WAI` is the difference. Steal it.
2. **The RLC/RRC sentinel bug.** In the prior Python core, a carry-bit local named `cy` shadowed the cycle-count variable `cy`, so `step()` returned 0, which the run loop read as "breakpoint hit" — silently killing a long assembler run after ~2.8M steps. **This is why `step()` returns an explicit `StepResult`, never a sentinel** (§3.1).

The rest of `porting-notes.md` is CP/M-guest and hard-sector-disk specific — BIOS track-buffer flush timing, BDOS register clobbering, DCDD write-back ordering, M80's six-significant-character symbols — and it bit the 8080 world, not this one. It is kept as the record of *why* the abstractions here look the way they do, not as a checklist for 6800 work.

---

## 17. Blocked on documentation

**Nothing that ships is blocked on documentation.** Every board in the built-in machines — the `6800` and `mp09` CPU boards, the MP-S serial console (`mps`), the DC-4 floppy (`dc4`), and the Altair 680b's on-board 6850 console and Kansas City Standard cassette — is modeled from a **period manual** in `reference/`, listed in `docs/sources.md`. So is the one board only an example fits, the MP-T interrupt timer (`mpt`): `reference/MP-T Interrupt Timer.md`, from its assembly instructions and the MK5009 block diagram. Its one inference, rate code `07` = 10 s, is recorded there. The MP-ID interface driver (`mpid`), in the `swtpc09` machine, is modeled from its manual and schematic, with its 6840 from the Motorola data sheet: `reference/MP-ID Interface Driver Board.md` and `reference/MC6840 Programmable Timer Module.md`. What the schematic leaves open — the full-wave line pulse, and the counter on the PIA's A side counting timer 1's time-outs — was settled from FLEX9's own code, and is recorded there.

Per §0.1, when a future SWTPC/SS-30 board is wanted — an MP-L/MP-LA parallel port, an AC-30 cassette, or a CT-64/CT-1024 terminal — it is blocked on its *manual*, not on code. **Ask Patrick and he will source it** — do not reconstruct, guess, or read another simulator.

**The MP-09 6809 CPU board is modeled** (`mp09`) from its assembly instructions, schematic and the S-BUG source, distilled in `reference/MP-09 6809 CPU Board.md` and `reference/S-BUG Monitor.md`; the 6809 processor itself is modeled from the Motorola programming manual. One fact is still inferred rather than read: the DAT bypass for `FFxx` (§3).

> **🔴 THE FRAMEWORK ONCE NAMED SIMH's `mits_dsk.c` AS AUTHORITATIVE FOR A DISK CONTROLLER, AND IT WAS FLATLY WRONG.**
> `mits_dsk.c` is **SIMH**, and §0.1 — the first rule in this document — says we do not learn hardware
> from another emulator's source. The line sat contradicting the rule it shares a document with, and it
> cost real time: a controller's rotation was modeled on SIMH's advance-on-read counter, which makes the
> platter spin at the speed of whatever loop is polling it. The lesson outlives the S-100 board it was
> learned on: the manual and the ROM are authoritative, the other emulator is not.

---

## 18. Roadmap

The milestones live in the implementation plan, not a tracked doc. Milestone 1 was **CLI + MCP +
6800 + bus + RAM + MP-S serial → SWTBUG's `$` prompt**; milestone 2, **the DC-4 floppy booting
FLEX 2.0**. Both are done, and so is the 6809: the MC6809 core with its disassembler and
assembler, a plain `6809` board, and the SWTPC MP-09 in the `swtpc09` machine booting FLEX9. What
is next — the remaining reference conversions — is tracked as GitHub issues.
