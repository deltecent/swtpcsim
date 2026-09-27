# Boards

A machine is a **backplane** with boards in it — the memory, the serial ports, the disk
controller, and the processor itself. There is no "machine" underneath the boards doing the real
work; take the boards out and there is nothing left but a bus.

`swtpcsim` is built that way on purpose, and it is the reason the CPU's crystal is a property of
the CPU *board* rather than of the machine. Not nitpicking: it is what lets you pull a board out,
put a different one in, and find out what the software does about it.

This chapter says what the boards **are** — what the real hardware was, what it is for, and what
will bite you. **It does not list their parameters.** Every key of every board is in the board
reference at the back of this manual, printed from the program's own tables, which is why it
cannot be wrong.

## The boards

Grouped by what they do — the same order as the sections below.

**Memory**

| Type | What it is |
|---|---|
| `memory` | RAM and ROM, as a list of regions |

**Processor**

| Type | What it is |
|---|---|
| `6800` | a Motorola 6800 — the CPU of the SWTPC 6800 and the MITS Altair 680b |
| `6809` | a Motorola 6809 — a processor board for a machine that you build |
| `mp09` | SWTPC MP-09 — a 6809 with its DAT address translator and the S-BUG monitor. The CPU of the SWTPC 6809 |

**Serial ports and consoles**

| Type | What it is |
|---|---|
| `mps` | SWTPC MP-S — a 6850 serial console on the SS-30 bus. The board SWTBUG talks to |
| `680io` | Altair 680b onboard I/O — a 6850 serial console and the configuration straps |
| `680uio` | Altair 680b Universal I/O — a second 6850 serial port and a 6820 parallel port |

**Cassette**

| Type | What it is |
|---|---|
| `680kcacr` | Altair 680b KCACR — the Kansas City audio-cassette interface |

**Floppy**

| Type | What it is |
|---|---|
| `dc4` | SWTPC DC-4 — a WD179x floppy controller. FLEX boots from it |

---

## Memory

## `memory` — RAM and ROM

A memory board is **a list of regions**, and the regions are the board. That is not a modelling
convenience; it is what a memory board was. One physical card carried banks of chips decoding
whatever ranges its jumpers said, and a card with RAM low and a ROM monitor high is a perfectly
ordinary card.

So the `swtpc` machine has a `memory` board that carries its RAM, its scratchpad RAM, and SWTBUG
in ROM all at once; `altair680` has one carrying a kilobyte of RAM and MON680 in a PROM.

### The reset vectors live at the top of memory

A 6800 fetches its RESET, IRQ, SWI and NMI vectors from the very top of the address space —
`FFF8`–`FFFF`. A ROM monitor has to answer there, and SWTBUG does it by being **mirrored**: the
1 KB image sits at `E000`, and the same image is placed a second time just below the top of memory
so a vector fetch at `FFF8`–`FFFF` reads the vectors the chip stored at `E3F8`–`E3FF`. A region
can ask for that second placement with `relocate` — see the configuring chapter — and it is how
`swtpc` comes up running SWTBUG's RESET entry.

### Two boards, one address — contention

Two boards can decode the same address, and when they do, **both drive the bus and the monitor
reports contention**, naming both and the page — the same fault a real backplane would have handed
you. The bus arbitrates nothing and picks no winner; a machine that boots is one whose boards each
own their own addresses, which is what the built-in machines do.

---

## The CPU board — it plugs in, and it drives the bus

## `6800` — a Motorola 6800

**The processor is a board like any other.** It plugs into the backplane, it can be removed, and
with `-n` you can build a machine that does not have one. What it does is **drive the bus** —
which makes it unlike every other board in the box, and is exactly what a CPU card did.

The 6800 has **no I/O port space**: everything is memory-mapped, so the CPU decodes nothing and
every device answers an address instead of a port. Its vectors sit at the top of memory — RESET at
`FFFE`, IRQ at `FFF8`, SWI at `FFFA`, NMI at `FFFC`.

It is the processor of two machines here: the **SWTPC 6800** (`swtpc`) and the **MITS Altair
680b** (`altair680`). Both boot a ROM monitor the moment they start.

Every CPU board carries the same three properties: **`clock_hz`** (the crystal), **`idle`**
(stands the processor down at a prompt), and the read-only **`achieved_hz`** (the speed it
actually reached).

## `6809` — a Motorola 6809

The `6809` board is a Motorola MC6809 processor and its crystal. It has the same three
properties as the `6800` board. No built-in machine uses it; the SWTPC 6809 uses `mp09`, below.
Add it to a machine file, or type `BOARDS ADD 6809 cpu0`.

The 6809 has seven vectors at the top of memory: SWI3 at `FFF2`, SWI2 at `FFF4`, FIRQ at
`FFF6`, IRQ at `FFF8`, SWI at `FFFA`, NMI at `FFFC` and RESET at `FFFE`. `SHOW BUS IRQ` shows
all seven. The board takes IRQ from the bus. Its FIRQ input is not connected.

`DISASM` and `EDIT` use the 6809 instruction set when this board is the processor. On a 6809,
`<nn` is a direct address and `>nnnn` is an extended address. `REGS` shows `A`, `B`, `X`, `Y`,
`U`, `S`, `DP`, `PC` and the flags `E F H I N Z V C`. `D` is reachable by name.

## `mp09` — SWTPC MP-09 processor board

The MP-09 is SWTPC's 6809 board, the processor of the built-in **`swtpc09`** machine. It is a
6809 like the `6809` board, and it carries two more things:

- **The S-BUG monitor**, in a 2K ROM socket at `F800`–`FFFF`. The `rom` property names what is
  in the socket: `builtin:sbug` by default, or a file of your own that fits in `F800`–`FFFF`.
  An empty `rom` leaves the socket empty.
- **The DAT**, an address translator. It maps each 4K block the 6809 addresses to a 4K block
  on the bus. The software loads it by writing to `FFF0`–`FFFF`; S-BUG does this at reset, and
  in a 56K system it maps every block to itself. Addresses `FF00`–`FFFF` are never
  translated. `SHOW cpu0` shows the map as the read-only `dat` property: sixteen digits, the
  bus block for each 6809 block `0` to `F`.

A 6809 system has its I/O at `E000`, not `8000`, so the MP-S console is at `E004` and the DC-4
at `E014`/`E018`. The addresses you type at the `swtpcsim>` prompt (`DUMP`, `DEPOSIT`,
`DISASM` with an address) are bus addresses. The views of the instruction at the PC translate
it through the DAT for you.

### The crystal is on the board — `clock_hz`

Because the crystal is soldered to the CPU card, `clock_hz` is the *board's* property, not the
machine's. **`clock_hz = 0` is the default, and it means run flat out** — on a modern host, many
times a real machine's speed.

```
SET cpu0 clock_hz=1000000
```

buys back a period ~1 MHz machine, and it is worth doing once. **What the guest sees is identical
either way**: instructions cost the same cycles and a cassette loads in the same number of them,
so the crystal buys period *feel*, not period *behaviour*.

That holds everywhere except at the edge of the machine. A guest counts instructions to measure
time, so flat out it retires a "three-second" timeout in milliseconds of yours — which is why
anything the guest times against the *outside* world (a transfer through a serial port) wants the
real crystal. The troubleshooting chapter has the full story.

To see where the guest's own clock has got to, ask the machine:

```
swtpcsim> SHOW CLOCK
clock  (emulated time -- cycles since POWER, and what they are worth)

  elapsed    1.103268 s   (2206536 cycles)
  crystal    2000000 Hz   SET cpu0 clock_hz=N
  pacing     free -- emulated seconds pass as fast as the host allows
```

Elapsed is counted in cycles since power and divided by the crystal above, so it is the
guest's time and not yours — booting FLEX costs the same emulated second whether you ran it
flat out or at 1 MHz. That is the number to measure a guest's own timeout against.

### `idle` — the CPU stands down at a prompt

At a prompt a guest is only spinning on a serial status register waiting for a keystroke, and flat
out that pins a core to accomplish nothing. **`idle` (on by default) stands the processor down
while the guest is polling an empty console** — a pinned core becomes a few percent — and **the
guest cannot tell**, because the moment a byte arrives the processor is back before the next poll.

---

## Serial ports and consoles

The boards that carry a serial port — a console for SWTBUG, MON680 or FLEX to talk through.

## `mps` — SWTPC MP-S serial interface

A **6850 ACIA** on an **SS-30 slot** — the board SWTBUG and MIKBUG expect for the console. It
answers a four-address slot: control/status at the slot base and the receive/transmit data
register at base+1, defaulting to `$8004`/`$8005`, the console slot. Unit `tty`. It is
memory-mapped, and the ACIA's interrupt pulls the 6800 IRQ.

### Only A0 reaches the chip

The 6850 has a single register-select line, `A0`, so it fills the **whole** four-address slot:
base+2 mirrors the base, base+3 mirrors base+1. That mirror is load-bearing — SWTBUG's power-up
probe compares the two to decide it is talking to an ACIA and master-resets it, and a board that
decoded only two addresses would take the wrong path and emit garbage at boot. You do not have to
think about it; it is here because the period software depends on it.

## `680io` — Altair 680b onboard I/O

The 680b's built-in I/O: a **6850 ACIA console**, unit `tty`, at `F000`/`F001`, plus the board's
**configuration-strap read port** at `F002`, which the monitor reads to learn how the console is
set up. Memory-mapped. This is the board `altair680` talks to, and MON680 comes up on it out of
the box.

## `680uio` — Altair 680b Universal I/O

The 680b expansion board: a **second 6850 serial port** (unit `serial`) and a **6820 PIA**
parallel port on one card, in an S9-relocatable window (default base `F000`: the serial port at
`F006`/`F007`, the PIA at `F008`–`F00F`), plus fixed switch inputs at `F003` and a latch-free
output at `F010`–`F013`. The PIA is exposed as sections — `p1a`/`p1b`, and `p2a`/`p2b` when a
second PIA is fitted with `pias=2` — each a line you `CONNECT`. Active-high.

---

## Cassette

## `680kcacr` — Altair 680b KCACR

The 680b's **audio-cassette interface**: a 1602-family UART recording the **Kansas City
Standard** FSK, so a byte on the bus becomes an audible tone on a tape and back again. It is
memory-mapped and **active-low**, with status/control at `F010` and the data register at `F011`.
Unit `tape`.

It adds **software motor control** (control bit D7 starts the transport, D6 stops it) and
**interrupt-driven transfer** (the D0/D1 enables pull the 6800 IRQ). Like every cassette it has a
**position**, so it brings the verbs **`WIND`**, **`REWIND`** and **`EXTRACT`**: `WIND` puts the
head at a time on the tape (`mm:ss`, or `START`/`END`), so a tape holding several programs one
after another is reachable; `REWIND` is the common case of `WIND START`; and `EXTRACT` demodulates
a mounted `.WAV` and writes each program it finds out as its own `.TAP` file. **The tapes chapter
is the one to read**, and the `altair680` cassette example is the machine to run.

---

## Floppy

## `dc4` — SWTPC DC-4 floppy controller

A **Western Digital WD179x** floppy controller on the SS-30 bus, up to four 5¼″ drives
(`drive0`–`drive3`), and **the board FLEX boots from**. It spans two slots: a drive-and-side
select latch at `$8014`, and the WD179x registers at `$8018`–`$801B` (command/status, track,
sector and data). Memory-mapped and DRQ-polled, running at 1 MHz.

FLEX 2.0 and 3.0 boot from it through SWTBUG's **`D`** command: mount a FLEX disk on `drive0`, type
`D`, and the loader reads the boot sector and brings FLEX up to its `+++` prompt. The disks chapter
covers the geometries it recognises (35- and 40-track single-sided, and double-sided) and how
mounting works; the `swtpc` FLEX example in `examples/` is that machine with a disk beside it.

### `speed` — instant or byte-timed

FLEX polls the controller's DRQ rather than timing it, so by default (`speed = full`) the board
collapses seek and per-byte timing and a disk boots near-instantly. Set `speed = real` and it times
each step and byte the way the hardware did — slower, and able to show a driver the Lost Data error
it would get if it fell behind.

---

## Working with the backplane at the prompt

Everything a machine file can do to a board, you can do by hand.

| Command | |
|---|---|
| `BOARDS` | what is in the backplane |
| `SHOW BOARDS` | the board types you can add, one line each |
| `SHOW BOARD <type>` | one type's description and its settings (add `UNITS` for just the units) |
| `BOARDS ADD <type> <id>` | fit a board |
| `BOARDS REMOVE <id>` | pull one out |
| `SHOW <id>` | one installed board's settings, with the legal values |
| `SET <id> <key>=<value>` | change one |

```
swtpcsim> BOARDS ADD 680uio uio0
swtpcsim> SET uio0 base=F000
swtpcsim> SHOW uio0
```

**The keys are the same keys.** `SET cpu0 clock_hz=1000000` at the prompt and `clock_hz = 1000000`
in a machine file are the same property reached two ways — there is no separate config schema,
which is the whole reason the board reference at the back can be exhaustive.

And when you have the machine you want:

```
swtpcsim> CONFIG SAVE mine.toml
```

writes it out, and it round-trips.

### Looking a board type over before you fit it

`SHOW BOARD <type>` reads the *catalog*. It builds one of that board, describes it, lists its
settings, and throws it away — nothing is added to the backplane. A disk controller, for instance,
ends by naming the units it carries:

```
swtpcsim> SHOW BOARD dc4
  dc4  SWTPC DC-4 floppy disk controller: a WD179x (1 MHz) with up to four drives...
  ...
  This board has units: drive
  SHOW BOARD dc4 UNITS for their properties.
```

Add `UNITS` and you get just the units — each under a heading that names its **kind** and, after
it, the **verb that fills it**:

```
swtpcsim> SHOW BOARD mps units

  Unit 'tty'  (serial, CONNECT)
  connect    The endpoint on the other end of this line (CONNECT sets this)
  ...
```

A **serial** unit is an endpoint, so you `CONNECT` it (to `console`, a socket, a real port). A
**disk**, **rom** or **tape** unit holds an image, so you `MOUNT` a file into it. A **cpu** unit is
soldered on — neither — and shows only its kind. The heading tells you which verb a unit takes
without your having to fit the board and find out.

Where a unit is filled from a *machine file* rather than by hand, `UNITS` shows the TOML table and
its keys instead — `[[board.drive]]` for a disk controller, with its `mount`, `readonly` and
`media` keys — so the file form is as discoverable as the prompt form.

## Contention

Two boards decoding the same address is **contention**, and it is a real thing that real backplanes
did. Both boards answer, both drive the bus, and what the processor reads is neither.

The simulator does not stop you. It does something more useful — **it tells you**:

```
swtpcsim> SHOW BUS CONTENTION
```

which names the address and names both boards. Fit two SS-30 boards at the same slot base and this
is what you will see, and it is a great deal more helpful than a guest that has mysteriously gone
mad.

Being able to build a machine that does not work is not a defect. **It is the point** — this is a
bench for developing hardware, and hardware that cannot be wired up wrong cannot be wired up at
all.
