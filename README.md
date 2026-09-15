# swtpcsim

A C++ simulator of Motorola 6800 machines: the **SWTPC 6800** on the **SS-50/SS-30 bus**, and
the **MITS Altair 680b**.

`swtpcsim` is a **hardware development bench** that happens to run period software. The board is a
first-class modeled object rather than an implementation detail, because the point is to develop
**new hardware** as well as to run old software. Each SS-30 board decodes its own slice of the
6800's flat, memory-mapped address space; there is no I/O port space to arbitrate.

It boots **SWTBUG**, the SWTPC 6800's ROM monitor, and from there **FLEX 2.0 and 3.0** off DC-4
floppies; the Altair 680b's **MON680** monitor over a 6850 console; **CP68** off a FLEX-format
disk; and Kansas City Standard cassettes through the 680b's KCACR — every one a real period
artifact, running unmodified.

Every one of those boots is an **acceptance test**: it runs the period software on the whole
machine through the real CLI and checks what lands on the terminal. The examples that ship live in
`examples/`, one directory each with its media inside it, and they boot from a scratch directory
with no repository in sight.

```
$ swtpcsim examples/flex/flex2-40.toml
[console -- ^E returns to the monitor]

   $D
DATE (MM,DD,YY)? 1,15,80
+++CAT

+++
```

That is the whole of it: name the machine file, let SWTBUG come up at its `$` prompt, and type
`D` to boot FLEX off the DC-4. Nothing is faked — the sectors come off a `.DSK` image through a
modeled WD179x.

## Building

**There are no dependencies.** A C++20 compiler and CMake ≥ 3.20 is the entire list. The TOML
parser, the JSON encoder, the terminal emulator and the line editor are all in-tree, so a fresh
clone builds with nothing to download.

**SDL3 is the one exception, and it is detected rather than required.** Install it and the
built-in `terminal:` VT100/VT52/H19 window opens in a real window; leave it out and it builds
headless against a null display, every test still passes, and the build never asks you for
anything. `-DSWTPCSIM_ENABLE_SDL=OFF` forces headless even where SDL3 is present.

```sh
git clone https://github.com/deltecent/swtpcsim.git
cd swtpcsim
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -LE slow      # drop -LE slow for the full CPU exercisers
./build/swtpcsim swtpc               # a SWTPC 6800, SWTBUG in ROM
```

**Or just run the one-command build.** `./build.sh` (`build.bat` on Windows) configures a
Release build, builds it, and prints where the binary landed and what version it is — no
flags, no generator to choose, and a plain sentence to act on if CMake is missing. SDL3
stays optional; add `--with-sdl` to link a private static SDL3 and get a window.

**Built and run on Linux, macOS, and Windows.** The code is written to be portable — C++20, no
dependencies, and every OS difference confined to `src/platform/` behind a header with zero
conditionals — and all three platforms are proven: Linux (Ubuntu/GCC), macOS (a universal
`x86_64`+`arm64` binary, so Intel and Apple Silicon both), and Windows on MSVC. See
[`docs/building-linux.md`](docs/building-linux.md), [`docs/building-windows.md`](docs/building-windows.md),
and [`docs/porting-notes.md`](docs/porting-notes.md).

**CI runs the suite on every push.** GitHub Actions builds `swtpcsim` and runs the tests on all
three platforms — Linux, macOS, and Windows are each a required check, each configured with
`-Werror`/`/WX` so a warning on any toolchain reds the PR. The tests below are the same ones; you
can run them yourself with `ctest`.

## What is in the box

Board types — modeled from their own manuals. Every one is added with `BOARDS ADD <type> <id>` or
a `[[board]]` in a machine file, and `SHOW BOARDS` in the monitor prints this list with a one-line
description of each (`SHOW BOARD <type>` for one).

**CPU** — it decodes nothing; it *drives* the bus.

| Board | What it is |
|---|---|
| `6800` | The SWTPC / Altair 680b CPU board — a Motorola 6800, big-endian, with memory-mapped I/O and no port space. |

**Memory**

| Board | What it is |
|---|---|
| `memory` | RAM/ROM board — a list of regions and `PHANTOM*`. Plain, unbanked; this is where SWTBUG, MON680 and the scratchpad RAM live. |

**Serial and console**

| Board | What it is |
|---|---|
| `mps` | SWTPC MP-S — a 6850 ACIA on an SS-30 slot; control/status at the slot base and Rx/Tx data at base+1 (default `$8004`/`$8005`, the console slot SWTBUG assumes). |
| `680io` | Altair 680b onboard I/O — a 6850 ACIA console at `$F000`/`$F001` and the config-strap read port at `$F002`. |
| `680uio` | Altair 680b Universal I/O — a second 6850 serial port and a 6820 PIA parallel port in a relocatable window, plus fixed switch inputs and a non-latched output. |

**Storage — floppy**

| Board | What it is |
|---|---|
| `dc4` | SWTPC DC-4 — a WD179x with up to four 5¼″ drives; a true-sense drive/side latch at `$8014` and the WD179x registers at `$8018`–`$801B`. FLEX 2.0/3.0 boots from it via SWTBUG's `D` command. |

**Cassette**

| Board | What it is |
|---|---|
| `680kcacr` | Altair 680b KCACR — a 1602-family UART recording Kansas City Standard FSK, with software motor control and interrupt-driven transfer. `MOUNT` a tape, `WIND`/`REWIND` it. |

**Two ready-built machines** are compiled into the binary; `swtpcsim --list` names them:

| Machine | What it is |
|---|---|
| `swtpc` | The SWTPC 6800 — SWTBUG in ROM at `$E000`, an MP-S console at `$8004`, a DC-4 floppy, ready for a FLEX disk. |
| `altair680` | The MITS Altair 680b — MON680 in ROM, a 6850 console, and the machine that shows what a 6800 in a home was in 1976. |

A built-in is an ordinary machine file that happens to live inside the executable — the same TOML
format you would write yourself, and `CONFIG SAVE mine.toml` writes any running machine out as one
you can edit.

## The monitor and debugger

A SIMH-style command monitor with line editing and history, and a full symbolic debugger sharing
the same prompt. **STOP (`^E`) is the stop key** — never `^C`, because `^C` belongs to the guest
(FLEX reads it), and a stop key the guest also wants is one the guest eats. `HELP` lists every
command; it comes off the same table the monitor resolves against, so it cannot drift from what
the binary does.

```
swtpcsim> BOARDS
  ID    TYPE    I/O  UNITS                        MEMORY
  ----  ------  ---  ---------------------------  ------------------------------
  cpu0  6800    -    1 cpu: 6800                  -
  mps0  mps     -    1 serial: tty*               8004-8007  6850 ACIA 'tty'
  dc40  dc4     -    4 disk: drive0(empty), ...   8014-801B  WD179x + drive select
  mem0  memory  -    2 rom: rom0, rom1            0000-7FFF  ram  32K
                                                  E000-E3FF  rom  swtbug  phantom:all

  * holds the console
```

**Building and inspecting the machine.** `BOARDS`/`BOARDS ADD`/`BOARDS REMOVE` build it live;
`SHOW`, `SET`, `MOUNT`, `CONNECT` configure it; `CONFIG SAVE`/`CONFIG LOAD` write and reload a
whole machine as a TOML file. `EXAMINE`/`DEPOSIT`, `DUMP`, `FILL`, `MOVE`, `SEARCH` and
`COMPARE` work memory directly; `LOAD` reads Intel HEX and Motorola S-records.

**Debugging.** `REGS` shows the CPU; `STEP` and `NEXT` single-step (over calls); `DISASM`
disassembles, annotated with symbols. `BREAK <addr>` sets a breakpoint and `BREAK <addr> IF
<expr>` makes it conditional; tracepoints fire an action instead of stopping. `TRACE` records
every bus cycle, and `HISTORY` is a flight recorder you read back after the fact — so a crash
is a thing you rewind into, not a thing you try to reproduce. `SYMBOLS LOAD` pulls names from
a `.PRN` or `.SYM` listing so addresses read as labels everywhere. `SNAPSHOT` and `RESTORE`
freeze and thaw the entire machine to a file. `SET CONSOLE DEBUG` turns on a per-facility
trace log. [`docs/debugger/`](docs/debugger/) walks through all of it.

**An MCP server is built in** (`swtpcsim --mcp`), so Claude can drive the machine through
typed, structured tools instead of screen-scraping a text CLI. It runs on the *same*
`Machine` object as the monitor — not a wrapper, not a second model of the world. See
[`docs/manual/mcp.md`](docs/manual/mcp.md) and [`docs/DRIVING-WITH-AI.md`](docs/DRIVING-WITH-AI.md).

## Consoles and connections

**Any board that moves characters** can be connected to the console, a TCP socket, a real host
serial port, or a **built-in terminal window** the simulator draws itself — VT100, VT52 or H19,
via a `terminal:` endpoint, no telnet client and no external emulator. They are interchangeable:
the same board reaches any of them.

## Configuring a machine

A machine is a TOML file, and **the TOML keys for a board *are* its properties** — there is no
separate config schema anywhere, for any board. The loader, `SET`/`SHOW`, `CONFIG SAVE` and the
MCP tool schemas all come off one reflection layer, so they cannot drift, and a board added next
year is configurable the day it lands.

A built-in machine is one of these files, compiled into the binary. There is one machine language,
and the machines we ship are written in it.

```toml
# ./swtpcsim.toml -- a bare `swtpcsim` in this directory boots it.
[machine]
name = "myproject"
base = "swtpc"            # start from a machine, and say what is DIFFERENT

[[board]]
id    = "dc40"            # the SWTPC's floppy controller...
mount = "disks/flex.dsk"  # ...with this project's disk in it
```

`./swtpcsim.toml` is the one file the simulator *finds* rather than is *given*, and it is found
**only when the command line names nothing**. `swtpcsim swtpc` means `swtpc` in every directory on
earth. See [`docs/config.md`](docs/config.md).

## The rules this project actually runs on

**Each chip is modeled from its datasheet; each board from its manual.** A chip built from the one
monitor that happens to drive it implements the subset that monitor uses and quietly gets the rest
wrong. Where a chip is wired is a *fact about the board*, not the chip: the MP-S decodes its 6850
at an SS-30 slot base while the 680b's onboard I/O decodes its 6850 at `$F000`, so the address is
a property of the board, and `src/chips/mc6850.*` knows nothing about either.

**Never invent a hardware feature to fix a software symptom.** When a guest's output looks wrong,
the fix is a transform on the host or filter layer, or a bug in the monitor — never a behavior
the chip never had. Check the host, the filter and the monitor layers first.

**Boards respond to bus cycles; the CPU originates them.** The `6800` board decodes nothing — it
drives the bus, and every other board answers the addresses it owns. That single distinction is
what lets a new board be configurable, snapshot-able and MCP-drivable the day it is written,
without touching the bus.

**No `#ifdef`s for operating-system differences.** SIMH is riddled with them and is unreadable as
a result. OS differences live in an interface header with *zero* conditionals plus one
implementation file per OS, selected by CMake — no OS type ever appears in a signature, so no
caller ever needs a conditional to name one. A lint greps for `_WIN32`, `__APPLE__`, `__linux__`
and the OS headers anywhere outside `src/platform/`, and it is a **build dependency of the
library, not a test** — a rule you can merge and fix later is a rule you have already lost.

**A validation harness may not emulate the thing it is validating.** The acceptance tests boot
real period software on the whole machine through the real CLI and read back what lands on the
terminal — no stub stands in for the monitor, the disk controller or the console. That is the only
reason to believe any of them.

## Tests

```sh
ctest --test-dir build -LE slow     # unit + acceptance
ctest --test-dir build              # ...plus the full CPU exercisers
```

The acceptance tests are not unit tests: they boot period software on the whole machine through
the real CLI, and several ship with a **negative control** — the same script against a machine
that should *fail*, marked `WILL_FAIL`. If a control ever passes, the test it guards was passing
for the wrong reason and is worthless. That is the only reason to believe any of them.

## Documentation

**Start with the manual if you want to *run* it, and with `DESIGN.md` if you want to *change* it.**

| Document | What it covers |
|---|---|
| [`docs/manual/`](docs/manual/) | **The User Manual** — boot FLEX, drive the monitor, debug a guest, mount disks and tapes. Written for someone holding a release package and nothing else, so it cites no source file and no repository path. Builds to `swtpcsim-manual.pdf`, which is what ships. |
| [`docs/devguide/`](docs/devguide/) | **The Developer Guide** — Theory of Operation, and a worked example that adds a new board. Needs the source, so it does not ship. |
| [`DESIGN.md`](DESIGN.md) | The design, and the reasoning. Read this first. |
| [`DISTRIBUTION.md`](DISTRIBUTION.md) | How a release is built and where it goes — the packages, the machine each is built on, and the checks that must pass before one ships. Written to be followed step by step on a build machine that has never seen this repository. |
| [`docs/config.md`](docs/config.md) | *Why* the TOML format is shaped as it is, by annotated example. **Not the grammar** — that is the manual's, so there is one normative copy of it. |
| [`docs/cli-commands.md`](docs/cli-commands.md) | Why the monitor's commands rank and abbreviate as they do. **Not a command reference** — `HELP` is, and it comes off the same table the monitor resolves against. |
| [`docs/boards/`](docs/boards/) | One file per board: the real hardware, the register map, how it is simulated, and the quirks it reproduces. |
| [`docs/DRIVING-WITH-AI.md`](docs/DRIVING-WITH-AI.md) | Driving a running guest with an AI assistant over the built-in MCP server. |
| [`docs/sources.md`](docs/sources.md) | Where every hardware fact came from. |
| [`docs/porting-notes.md`](docs/porting-notes.md) | Hard-won lessons from the prior Python prototype. |
| [`docs/building-linux.md`](docs/building-linux.md), [`docs/building-windows.md`](docs/building-windows.md) | Building and running per platform — prerequisites, the serial-build memory trap, and what was verified. |

**Sourcing rule: period manuals and datasheets, never another emulator's source.** Reading past a
source to preserve an argument is the same failure as fabricating one.

## License

This project is licensed under the [MIT License](LICENSE) (© 2026 Patrick Linstruth).
