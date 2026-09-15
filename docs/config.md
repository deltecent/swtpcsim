# Machine configuration (TOML)

A machine is described by a TOML file. The **same state** is reachable at runtime through the monitor, and `CONFIG SAVE` writes it back out.

**The TOML keys for a board *are* its `properties()`** (see `DESIGN.md` §5) — there is no separate config schema per board. The loader and `CONFIG SAVE` are the same code path, so they cannot drift, and a board added next year is configurable the day it lands with no changes here.

## Which machine you get

| You type | You get |
|---|---|
| `swtpcsim swtpc` | the built-in `swtpc` — **in every directory on earth** |
| `swtpcsim cfg/mine.toml`, `swtpcsim -f mine` | that file |
| `swtpcsim -m altair680` | the built-in, always — never a file, whatever the directory holds |
| `swtpcsim -n` | an empty backplane |
| `swtpcsim` | **`./swtpcsim.toml` if the working directory has one**, else the built-in `swtpc` |

**`./swtpcsim.toml` is the only file the simulator *finds* rather than is *given*, and it is found only when the command line names nothing at all.** That restriction is the whole licence for the feature. `looksLikeFile()` (`src/core/machines.h`) refuses to probe the disk on purpose — `swtpcsim swtpc` must not become a different machine the day somebody saves a file called `swtpc` next to it, because a command line that changes meaning because of its surroundings is a trap. That argument applies to every command that *names* a machine. A bare `swtpcsim` names none: it is not asking for `swtpc`, it is asking for whatever machine is sensible *here*, and letting the directory answer is the `make(1)` bargain rather than the trap.

**It is never silent.** When the working directory's file is used, the simulator says so on **stderr** — because the failure this can otherwise cause is spending twenty minutes on a machine you did not know you were running. It goes to stderr and not stdout so that a `-s` script's output stays exactly what the script printed.

A found file is an *ordinary* config file, so it can `base` off a built-in and say only what is different:

```toml
# ./swtpcsim.toml -- the project's machine. Just `swtpcsim` boots it.
[machine]
name = "myproject"
base = "swtpc"

[[board]]
id = "dc40"                # the swtpc's DC-4 floppy controller...
  [[board.drive]]
  unit  = 0
  mount = "FLEX2-40.DSK"   # ...with this project's disk in drive 0
```

## Where a relative path is relative to

There is one answer, and it is the whole of the rule.

> **A relative path resolves against the machine's directory** — the folder the machine file
> was loaded from.

That covers everything: `mount`, `base`, the `MOUNT`/`LOAD` commands inside a `startup` list, a
`-s` script, and anything you type at the prompt. One base, so a disk the machine ships with and
a disk you mount by hand are found in the same place. (A **built-in** machine has no directory
of its own, so its base is the directory you launched from — the only anchor it has.)

That a machine file's own paths point at its own directory is what makes it portable.
`examples/flex/flex2-40.toml` mounts the disk lying beside it:

```toml
[[board]]
type = "dc4"
id   = "dc40"
  [[board.drive]]
  unit  = 0
  mount = "FLEX2-40.DSK"     # the disk lying beside this file
```

…and means the file in its own directory. So both of these work, and mean the same machine:

```sh
cd examples/flex && swtpcsim flex2-40.toml     # the way you will actually run it
swtpcsim examples/flex/flex2-40.toml           # ...and from anywhere else
```

That matters because **the `examples/` tree — with its disks — is what we ship.** A user gets the binary and those files, not this repository. A machine file that only resolved from the repository root would be a machine file that only worked for us, and every example in this tree used to be exactly that.

The same base covers what you type. When you type

```
swtpcsim> MOUNT dc40:drive1 "scratch.dsk"
```

you get the `scratch.dsk` in the machine's own directory — the same folder its own disks come from — no matter which shell you launched from. Typed paths used to resolve against your shell instead, which is how the identical disk could appear under two different names; an acceptance test fails the build if a typed path ever stops resolving against the machine's directory.

**There is no search path.** A file is looked for in exactly one place. If it is not there, the error names the place it looked — not the name you wrote — because the whole point of a resolved path is to be able to see where it went.

**What is stored is what you wrote.** `SHOW` prints, and `CONFIG SAVE` writes back, the path *as it appears in the file* — so a machine saved out of `examples/flex/` still says `mount = "FLEX2-40.DSK"` and still loads from its own directory. Only the *narration* ("`mounted …`", "`loaded …`") names where the file actually was, because that is a report of what happened rather than a record of what was asked for.

Absolute paths and the `builtin:` scheme are never re-based: `mount = "builtin:swtbug"` is a ROM in the binary, not a file, and must never become one.

### This is not a sandbox

**Nothing on this page confines anything.** The rule answers one question — *what is a relative path relative to* — and that is a lookup, not a fence. A machine file may write `../../../etc/passwd` and it will be opened. `MOUNT` has never been restricted to a directory tree, and none of this is a security boundary.

## Verbs

- **`MOUNT`** refers to **host files** (disk and tape images).
- **`CONNECT`** refers to **sockets and serial ports** (and the console).

In TOML these appear as the `mount` and `connect` keys; at the monitor they are the `MOUNT` and `CONNECT` commands. They mean the same thing.

## Endpoints (for `connect`)

| Endpoint | Meaning | |
|---|---|---|
| `console` | The host keyboard and screen. Exactly one unit may hold it — connecting a second **steals** it, and says who from. | **built** |
| `null` | Discard. What an unconnected unit is bound to, which is why an unconnected line is not an error. | **built** |
| `loopback` | A jumper between TX and RX. The guest hears itself. | **built** |
| `socket:2323` | Listening TCP socket — a terminal emulator connects *in*. | **built** |
| `socket:host:port` | Outbound TCP connection. | **built** |
| `serial:/dev/cu.usbserial-X` | Real host serial port (POSIX). | **built** |
| `serial:COM3` | Real host serial port (Windows). | **built** |
| `file:path` | A file, for paper tape. | **built** |

Asking for one that is not built yet **says so by name**, rather than failing as though you had mistyped it.

## Example — the machine that exists today

This is `machines/altair680.toml`, and it runs: `swtpcsim altair680`. The Altair 680b is a
Motorola 6800: big-endian, and **memory-mapped — there is no IN/OUT port space.** Everything a
board answers, it answers at *addresses*.

```toml
[machine]
name    = "altair680"
startup = ["RESET", "RUN"]   # anything you can type, a config can do

# The CPU is a board like any other (DESIGN.md §3) -- and THE CRYSTAL IS ON IT, which is why
# clock_hz is this board's property and NOT the machine's.
[[board]]
type     = "6800"
id       = "cpu0"
clock_hz = 0                 # 0 = run flat out, AND IT IS THE DEFAULT.
                             # 500000 = a real 500 KHz 680b, real time, real waiting.

# The onboard I/O: the console 6850 ACIA answers at ADDRESSES F000/F001 (not ports), and the
# config-strap read port at F002. Because the 6850 has no jumpered word format -- its frame is
# a register the GUEST writes -- almost nothing here is line coding; `straps` is a board
# property because on the real card it is a set of solder pads read at F002.
[[board]]
type   = "680io"
id     = "io0"
straps = 0x00                # bit 7 clear = a terminal is present; bit 2 clear = two stop bits

  # The 6850 is one unit with its own endpoint and baud. A card with two ACIAs would give each
  # its own [board.unit.*], because they share NOTHING -- separate endpoints, separate jumpers.
  [board.unit.tty]
  connect = "console"
  baud    = 9600             # DECIMAL: a baud rate never is hex.

[[board]]
type = "memory"
id   = "mem0"
fill = "random"              # real static RAM does not come up zeroed

  # A card carries one or more POPULATED regions. The 680b's onboard 1K of static RAM sits at
  # 0000; the monitor keeps its stack and page-0 flags here, so page 0 must be RAM.
  [[board.region]]
  type = "ram"               # ram = writes are stored. rom = writes are not decoded.
  at   = 0000
  size = "1K"

  [[board.region]]
  type  = "rom"
  at    = FF00
  mount = "builtin:mon680"   # compiled in: nothing to download, same on every OS. The 256-byte
                             # PROM holds the monitor AND the reset/interrupt vectors at FFxx.
```

**Keep `[machine]` keys above the first `[[board]]`.** This is TOML scoping, not a rule of ours:
once a `[[board]]` (or any `[table]`) block opens, every bare key that follows belongs to *it*. A
`startup = [...]` written **after** a board block is parsed as a property of that board and
rejected as an unknown board key — the machine's own `startup` silently never gets set. Put
`name` and `startup` directly under `[machine]`, before you fit the first board, as above.

### The transform chain is the CONSOLE's, and only the console's

`UPPER`, `STRIP7OUT`, `CRLF`, `BSDEL` and the rest belong to `[console]` — **not** to a board and **not** to a unit. Every other endpoint (socket, serial port, tape, file) is **8-bit clean, always**, because the next thing down that line may be XMODEM and a filter would corrupt it silently (DESIGN.md §7.2).

```toml
[console]
upper     = true         # fold keyboard input to uppercase -- some period software wants caps
strip7in  = true
strip7out = true         # a Teletype ignores bit 8; some software sets it as a terminator
bsdel     = "bs"         # off | bs (fold DEL->BS) | del.
crlf      = false        # usually WRONG to turn on: period software sends its own LF
echo      = false        # local echo, for half-duplex hardware
bell      = true
```

What a **board** gets instead is **line coding** — and only where it is really a jumper:

```toml
[[board]]
type      = "680kcacr"   # the KCACR's word format IS a set of solder pads
id        = "acr0"
data_bits = 8            # NDB1/NDB2. A FRAME, not a mask
stop_bits = 2            # NSB
parity    = "none"       # NPB/POE
```

On a real serial port those are programmed into the real port. On the **console 6850** (the `mps`/`680io` ACIA) there is no such property at all: the 6850's word format is a register the *guest* writes, which is exactly why the `680io` board above carries `straps` but not `data_bits`.

At the monitor that is `SET CONSOLE UPPER=ON`, and `SHOW CONSOLE` prints it. **It is not `SET io0:tty UPPER=ON`** — a transform is the console's and nothing else's, because a card's line has to stay 8-bit clean or a filter on it corrupts XMODEM, silently.

`[console]` holds only what is genuinely about a *terminal*:

```toml
[console]
attn = 05                  # HEX. The key (^E) that drops from CONSOLE back to the monitor.
                           #   The guest NEVER SEES this byte, so it cannot disable it.
history = 100              # command lines kept in .swtpcsim_history (per launch dir).
                           #   Default 50; 0 turns the file off.
```

`rows`, `cols`, `pace`, `ansi` and `tabs` are specified in DESIGN.md §7.2 but **not built yet**.

### A second `mps` — same type, own config, different base

An SS-30 serial board is placed by its slot, so a second one is the same `type` with a different `base` (slot N lives at `8000 + 4·N`):

```toml
[[board]]
type = "mps"
id   = "mps1"
base = 8008                # SS-30 slot 2, clear of the console at slot 1 (8004)
```

## Example — a machine with a disk

This is `machines/swtpc.toml`, the machine `swtpcsim` boots by default. It adds an MP-S serial
console and a DC-4 floppy to the same 6800 CPU + memory, and its regions show a monitor ROM
mirror-decoded to the top of memory.

```toml
[machine]
name    = "swtpc"
startup = ["RESET", "RUN"]   # 6800 reset arms the FFFE/FFFF vector fetch; RUN takes it.

[[board]]
type     = "6800"
id       = "cpu0"
clock_hz = 0                 # flat out (the default); SET cpu0 clock_hz=1000000 for a real 1 MHz part

# The MP-S serial console: a single 6850 ACIA on SS-30 slot 1. `base` places the slot; the ACIA
# answers control/status at 8004 and Rx/Tx data at 8005 -- the addresses SWTBUG is hardwired to.
[[board]]
type = "mps"
id   = "mps0"
base = 8004                  # SS-30 slot 1 (8000 + 4*1), the console slot

  [board.unit.tty]
  connect = "console"
  baud    = 9600

# The DC-4 floppy controller: a WD179x on SS-30 slots 5+6. The drive-select latch sits at 8014
# and the WD179x registers at 8018-801B. FLEX boots from a disk here via SWTBUG's `D` command.
[[board]]
type   = "dc4"
id     = "dc40"
base   = 8018                # WD179x block; the drive-select latch sits at 8014
drives = 4

  [[board.drive]]            # up to `drives` sub-units; the board probes geometry from the image
  unit         = 0
  mount        = "FLEX2-40.DSK"
  writeprotect = true

[[board]]
type = "memory"
id   = "mem0"
fill = "random"

  # 32K of general RAM low in the map, clear of the SS-30 I/O window at 8000.
  [[board.region]]
  type = "ram"
  at   = 0000
  size = "32K"

  # RAM at A000-BFFF. Its first 128 bytes are SWTBUG's scratchpad (variables + stack); the rest
  # is where FLEX loads its resident system.
  [[board.region]]
  type = "ram"
  at   = A000
  size = "8K"

  # The SWTBUG monitor: a 1K 2716 at E000-E3FF, where its S-records self-place.
  [[board.region]]
  type  = "rom"
  at    = E000
  mount = "builtin:swtbug"

  # THE SAME chip, mirror-decoded to the top of memory so the 6800's FFF8-FFFF vector fetch reads
  # the vectors the ROM stores at E3F8-E3FF. `relocate = true` shifts the image so its first
  # record lands at FC00 (FC00 + 0x3F8 = FFF8) rather than requiring it to self-place there.
  [[board.region]]
  type     = "rom"
  at       = FC00
  mount    = "builtin:swtbug"
  relocate = true
```

Mount a different disk, or a second drive, from the prompt — a typed `MOUNT` resolves against the machine's own directory, exactly as the file's own `mount` does:

```
swtpcsim> MOUNT dc40:drive1 "scratch.dsk"
```

## The reference moved — and this file did not

**The normative TOML reference now lives in the User Manual** (`docs/manual/configuring.md`),
which is the document that ships to users and is therefore the document that has to be
complete. Every `[machine]` key, all four `[[board]]` forms, the region and drive tables, the
number rule, `[console]` — they are specified there, once.

They used to be specified *here as well*, and that was the bug. Two normative descriptions of
one format is precisely the "second schema" this project refuses everywhere else in the code,
committed in the docs instead — and the copy here had already drifted (it claimed a machine
could have "config-time properties" that are "rejected on a running machine", a rule that was
deleted in July 2026 and never existed in the code by the time anybody read the sentence).

**What stays here is the part that is not a specification: the arguments.** The worked examples
above, and the reasoning in them — why `clock_hz` is the CPU card's property and not the
machine's, why the console 6850's word format is the guest's business and not a jumper, why the
transform chain belongs to the console and to nothing else, why a second serial board needs
nothing but a different base. That is what a design document is for, and none of it belongs in a
user manual.

If you are looking for **what a key does**, read the manual. If you are looking for **why the
format is shaped like this**, you are in the right place.

## And the per-board keys are not written down anywhere by hand

A board's `properties()` **are** its TOML keys. There is no separate schema, and there is no
hand-maintained table of them — `docs/manual/ref/boards.md` is *printed from the binary* by
`tools/gen-reference.cpp`, and a test fails if it is stale.

That is not a convenience. The first table of the memory card's defaults that anybody typed out
by hand, reading the source carefully, got three of eight rows wrong.
