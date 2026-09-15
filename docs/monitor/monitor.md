# The monitor

This is one of two short documents about **driving `swtpcsim` itself** — the program you
run, as opposed to the machines it emulates. It covers **the monitor**: the `swtpcsim>`
prompt where you start, stop, examine and reconfigure the machine. Its companion, *The
Debugger*, covers what you reach for at that prompt when something has gone wrong. Both stand
beside the *User Manual*, which describes the hardware — the boards, the machines, the disks
and the tapes. `swtpcsim` simulates the SWTPC 6800 and the MITS Altair 680b — Motorola 6800
machines on the SS-50/SS-30 bus; if that is new to you, the manual's opening chapter is the
place to start.

The `swtpcsim>` prompt is **the monitor**: the machine's operator's position, and its
debugger, which here are the same thing. On the Altair 680b that position was a real front
panel of switches and lamps; on the SWTPC 6800 it was a terminal at SWTBUG's `$`. Everything
those gave you the monitor can do — and a great deal they could not. It breakpoints, it
single-steps, it disassembles, and it will show you the bus itself: who decodes what, who is
pulling which interrupt line, and where two boards are fighting over an address. That is what
*The Debugger* is for, and it is most of why this program exists.

What it is not is a *menu* — a layer sitting between you and the machine, offering a fixed set
of things it is prepared to let you inspect. There is no debug mode to enter and nothing is
watching from the outside. A breakpoint is **the machine stopping**, not a script noticing that
it should have. Because the 6800 is **memory-mapped** — its devices live in the address space,
not a separate port space — `EXAMINE` and `DEPOSIT` run **real bus cycles**, with every side
effect a real one has: read the console ACIA's data register at `8005` from this prompt and you
have taken the byte, exactly as the guest would have. The operator's position and the debugger
are one object because on a 680b they were one object: a hand at the switches, reading lamps.

The machine does not have to be running for the monitor to work. Most of what follows —
examining memory, running a bus cycle, fitting a board — works on a machine with the power on
and the processor idle, which is exactly the arrangement the 680b's front panel was for.

## Commands resolve by prefix

There are **no aliases and no memorised abbreviations**. You type as much of a command as it
takes to be unambiguous, and the first command that matches wins.

`HELP` prints the whole menu with each command's shortest form in brackets:

```
  BO[ARDS]          B[REAK]           COM[PARE]         C[ONFIG]
  CONN[ECT]         CONS[OLE]         DE[POSIT]         DI[SASM]
  DISC[ONNECT]      DO                D[UMP]            E[DIT]
  EX[AMINE]         F[ILL]            HE[LP]            H[ISTORY]
  I[N]              L[OAD]            MA[CHINE]         M[OUNT]
  MOV[E]            N[EXT]            NO[BREAK]         O[UT]
  P[OWER]           Q[UIT]            REGI[ON]          RE[GS]
  RES[ET]           REST[ORE]         R[UN]             SA[VE]
  SEA[RCH]          SE[T]             SH[OW]            SN[APSHOT]
  STA[RTUP]         S[TEP]            SY[MBOLS]         T[RACE]
  TY[PE]            U[NMOUNT]         W[HO]
```

Type the part before the bracket. `D` is `DUMP`; `DE` is `DEPOSIT`; `RES` is `RESET`. Case
does not matter, here or in the name of any board.

`HELP <command>` gives you the usage and worked examples for one of them, and `?` is the same
as `HELP`.

> **`R` is `RUN`, not `RESET`.** It is the command you type every session, and it is the one
> that costs nothing if you did not mean it. A bare `R` that reset the machine would be one
> you had to set up again. `RESET` pays the letters: `RES`.

## Editing the command line

The prompt is a real line editor. The key labelled Backspace erases the character behind the
cursor whichever byte your terminal sends for it, the arrows move within the line, and the
editor keeps a **command history** — the up-arrow walks back through the lines you have typed
and the down-arrow returns toward the one you were in the middle of.

The history is **saved between sessions, per directory**. When you leave, the last commands
you typed are written to a hidden `.swtpcsim_history` in the directory you launched from, so
the next time you start the simulator *there* they are waiting on the up-arrow. Each project
directory keeps its own list; the file is written only when you are typing at a real terminal,
so a script, a pipe, or an automated run never leaves one behind. How many lines it keeps is
the `history` setting on the console — `SET CONSOLE history=200` to keep more, and
`SET CONSOLE history=0` to turn the file off entirely. It defaults to 50.

### Completing with `Tab`

`Tab` finishes whatever you are partway through typing, and it reads the candidates off the
machine in front of you — so a board you plug in is completable straight away, with nothing to
keep up to date:

- at the start of a line, a **command** — `SH`⇥ → `SHOW`;
- after `SET`, a **board id** (or `CONSOLE`, `DISPLAY`) — `SET me`⇥ → `SET mem0`;
- after a board, one of **its property names**, with the `=` put on ready for the value —
  `SET mem0 fi`⇥ → `SET mem0 fill=`;
- after the `=`, one of that property's **legal values** — `SET mem0 fill=`⇥ offers `zero`
  and `random`.

When more than one candidate fits, `Tab` fills in as far as they all agree and stops; press it
again and it lists them. When nothing fits, it does nothing.

### The keys

| Key | Does |
|---|---|
| `←` `→` | move one character |
| `Ctrl-A` / `Home` | to the start of the line |
| `Ctrl-E` / `End` | to the end of the line |
| `Alt-B` / `Ctrl-←` | back one word |
| `Alt-F` / `Ctrl-→` | forward one word |
| `Backspace` | erase the character before the cursor |
| `Delete` | erase the character under the cursor |
| `Ctrl-W` | erase the word before the cursor |
| `Ctrl-K` | erase from the cursor to the end of the line |
| `Ctrl-U` | erase the whole line |
| `↑` `↓` | walk back and forth through the command history |
| `Tab` | complete the word at the cursor |
| `Ctrl-D` | on an empty line, leave — the same as `QUIT` |

> **`Ctrl-E` here is end-of-line, not the STOP key.** At the prompt you are typing to the
> *editor*, so `Ctrl-E` jumps to the end of the line. Once a **running** guest holds the console,
> that same `Ctrl-E` is **STOP** and takes the keyboard back (below). Same key, two places, two
> jobs.

## Repeating the last command: `.`

Type `.` on a line by itself and the monitor runs your **last command again**, quietly —
there is no echo, just the command's own output. It costs one keystroke, and the commands
you most often want to repeat pick up where they left off: a bare `DISASM` disassembles the
next screenful, a bare `DUMP` shows the next page, and `STEP` steps again. So you type `DI`
once and then `.` `.` `.` to walk forward through a routine, or `S` and then `.` to single-step.

Pressing `.` again always repeats that same original command, never the previous `.`, so it
keeps doing the one thing however many times you press it. A `.` before you have typed
anything just tells you there is nothing to repeat yet.

## Reaching the host: `!`

A line that begins with `!` is not a monitor command at all — everything after the `!` is
handed to **your host shell**, word for word, and the monitor waits until it is done before it
prompts again. The rest of the line is passed through untouched, spaces and all:

```
!ls                 list the directory you started from
!vi HELLO.PRN       open a file in your editor, then :q back to the prompt
!cp game.dsk save.dsk   keep a copy of a disk without unmounting it
```

This is **your** shell, with your own privileges — not the machine's, and nothing the guest
can see or reach. It is also why an editor works: the monitor is not holding the keyboard when
it hands off, so `vi` gets a normal terminal and gives it back when it exits. The machine keeps
running underneath; `!` only borrows *you*, not the processor.

A bare `!` with nothing after it just reminds you of the form.

## Numbers: one rule, and it is not negotiable

> **On the wire → hex. Never on the wire → decimal.**

If the 6800 can see it, it is **hex**: an address, a device register, a data byte, a CPU
register. If it never leaves your head, it is **decimal**: a count, a width, a size, a drive
number.

**Hex is only the *default* for the wire class.** Switch the console to octal and that class reads
and prints in octal instead — the base the 680b's monitor and MITS's Motorola manuals used as
readily as hex. The rule does not change: octal is still the wire class, decimal is still the
counts. *Reading and writing in octal*, below, is how.

```
DUMP 100            address  -> 0100 hex
STEP 10             a count  -> ten instructions
DEPOSIT 8010 55     address and byte -> both hex
SET mps0:tty baud=9600    a baud rate -> nine thousand six hundred
```

You can always force the issue: `0x`, `$` and a trailing `h` force hex; `0o` and a trailing `q`
force octal; `0b` forces binary — which is what you want for a bit-mapped value like the 680b's
config straps, where eight bits would rather be eight digits; a leading `#` forces decimal; and a `K` or `M`
suffix is **always** decimal (`48K` is 49,152 — so `0x10K` is a contradiction and is rejected
rather than guessed at).

This rule is the same everywhere — in the monitor, in a machine file, and in every board's
settings. There is no second convention to learn.

What is not negotiable is the **classes** — which side of the line a number falls on. The base
the wire class is *printed* in is yours, and the next section is how.

### Reading and writing in octal

The 680b's programming manual and MITS's Motorola literature wrote numbers in **octal** as
readily as hex, and you can too:

```
SET CONSOLE base=octal
```

Now the **hex** half of the rule becomes **octal** — the wire class (addresses, data bytes, CPU
registers) is read and printed in **split octal**, each byte its own `000`–`377` group and a
16-bit address as two of them, the way a 680b front panel groups its address lamps:

```
EXAMINE 100         -> 000 100  076   (the byte 0x3E at address 0x40)
DUMP 100-100        -> 000 100  076
DISASM 0            -> JMP 022 064     (a jump to 0x1234)
```

A bare number is octal now too, so `100` is address `0x40`; the decimal class (counts, widths,
baud) does not change. The forcing markers still work in both directions — `0x1234` is hex even in
octal mode, and `0o377` is octal even in hex mode — so nothing is ever a base you cannot type your
way out of. `base=hex` (the default) puts it back. Set it once in a machine file (`[console] base
= octal`) to start there every time.

## Naming a board: `<id>[:<unit>]`

Every board in the machine has an **id** you chose (`cpu0`, `mps0`, `dc40`), and some boards
have **units** inside them — the console line on a serial board, the four drives on a floppy
controller, the ROM sockets on a memory board.

```
SHOW mps0                the board
SET  mps0:tty baud=1200  its console line
MOUNT dc40:drive1 my.dsk
```

**You may leave out anything that carries no information.** If there is only one floppy
controller in the machine, `dc4` will find it. If a board has only one thing you could mount
into, you need not name it — `MOUNT kc0 tape.tap` puts a cassette in the one recorder.

But anything **genuinely plural you must say**. There are four drives on that controller and
the machine will not guess which one you meant; it will tell you so and stop.

## Seeing the machine

```
swtpcsim> BOARDS
  ID    TYPE    UNITS                                            MEMORY
  ----  ------  -----------------------------------------------  --------------------------------
  cpu0  6800    1 cpu: 6800                                      -
  mps0  mps     1 serial: tty*                                   8004-8007  6850 ACIA 'tty'
  dc40  dc4     4 disk: drive0(empty), drive1(empty), ...        8014-8014  drive select
                                                                 8018-801B  WD179x
  mem0  memory  2 rom: rom0, rom1                                0000-7FFF  ram  32K
                                                                 A000-BFFF  ram  8K
                                                                 E000-E3FF  rom  swtbug
                                                                 FC00-FFFF  rom  swtbug

  * holds the console
```

That is the backplane: what is plugged in, what each board decodes on the bus, what is in its
units, and where it answers in memory. (The 6800 is memory-mapped, so the `I/O` column — a
separate port space — stays empty; every device shows up under `MEMORY`.)

| Command | Shows |
|---|---|
| `BOARDS` | the backplane |
| `SHOW <id>` | one board: every setting, its value, and what it will accept |
| `SHOW MACHINE` | the whole machine |
| `SHOW CONSOLE` | which unit holds your keyboard, and how bytes are being transformed |
| `SHOW DISPLAY` | the video window: whether it or the terminal has the keyboard, and whether it wears the CRT look |
| `SHOW BUS MAP` | who decodes which addresses — and what floats |
| `SHOW BUS IRQ` | the IRQ and NMI wires, who is pulling them, and the vectors at `FFF8`–`FFFF` |
| `SHOW BUS CONTENTION` | where two boards are fighting |

`SHOW <id>` is worth dwelling on, because it is the **only** thing you need in order to
configure a board. It lists every property, what it is set to, and what values are legal —
and those property names **are** the keys you write in a machine file. There is no second
schema anywhere in this program. The board reference at the back of this manual is printed
from the same source.

## Changing the machine

```
SET cpu0 clock_hz=1000000      give it the real 1 MHz crystal
SET mem0 fill=zero             RAM comes up zeroed instead of random
SET mps0:tty baud=1200         slow the console down
BOARDS ADD mps mps1 base=8008  fit a second serial board
BOARDS REMOVE mps1             pull it out
CONFIG SAVE mine.toml          write out the machine you are actually running
```

`CONFIG SAVE` round-trips: what it writes, `swtpcsim mine.toml` will boot.

## Running, and stopping

```
RUN E0D0     load the PC and go — the same two motions as the 680b panel's switches
RUN          carry on from wherever the processor is
```

**`RUN <addr>` is EXAMINE followed by RUN**, exactly as you would do it on the front panel.
There is no `BOOT` command in this program, and there should not be: a machine that ought to
start says so with the operator's own keystroke.

If a board holds the console, **the guest gets the keyboard** — every key, including `^C`,
which a FLEX program is entitled to read.

### STOP takes it back

**`^E`** presses the front-panel STOP switch. The host intercepts it *before the guest is ever
offered the byte*, so no program running inside the machine can disable it, trap it, or take it
from you.

```
$
STOP -- the machine is still at E201. RUN resumes.
swtpcsim>
```

**STOP halts the machine and gives you the monitor.** Nothing executes while this prompt is up.
But it stops the machine without *disturbing* it — STOP is not RESET and not POWER, so the
registers, the memory and the disk are exactly as the guest left them, and a bare `RUN` (no
address) picks up at the very instruction it was about to execute. That is what *"still at
E201"* is telling you.

`CONSOLE stop=1D` moves STOP to `^]` if `^E` collides with something the guest wants (the older
`attn=` spelling still works).

### What stops it for real

A `RUN` ends when it hits a **breakpoint**, or a `HLT` that nothing can wake — and it always
says which. With no console connected there is nothing to hand the keyboard to, so it simply
runs, and `^C` stops it.

**`RUN` and `^E` are the panel's RUN and STOP switches.** `RUN` starts the processor; STOP — or a
breakpoint, or a `HLT` — stops it and hands the monitor back. That prompt is the whole
distinction: **the monitor exists only while the machine is stopped.** While it runs, the guest
holds the keyboard and there is no `swtpcsim>` to type at; when you have the prompt, nothing is
executing. So every `SET`, `DEPOSIT` and `EXAMINE` you type acts on a *stopped* machine — a
property is never changed out from under a running instruction, and none is ever locked "while
running" or settable only then.

## Speed

**It runs flat out by default** — `clock_hz` on the CPU board is `0`, so a cassette that took a
real 680b a couple of minutes comes off in about a second. `SET cpu0 clock_hz=1000000` buys back
the 1 MHz SWTPC machine (`500000` for a 680b); what the guest sees is identical either way,
because the tape still costs the same cycle count — the crystal buys period *feel*, not
*behaviour*. `SHOW cpu0` reports `achieved_hz` beside it: the clock the run loop actually hit, a
measurement you cannot set.

The one exception is anything the guest times against the *outside* world — an XMODEM transfer
wants the real crystal, a cassette does not. The manual's Boards chapter (`clock_hz`, `idle`)
and its Troubleshooting chapter have the detail.

## RESET is not POWER

| | |
|---|---|
| `RESET` | the bus's RESET* line. The processor loads its PC from the **RESET vector** at `FFFE`/`FFFF` (→ the monitor's entry). **Memory survives**, disks stay mounted. |
| `POWER` | a power cycle. **This is the only thing that loses RAM** and re-reads the ROM images. |

The 6800 does not restart at `0000`: reset arms the two-byte fetch at the
top of memory and the first instruction it runs comes from wherever `FFFE`/`FFFF` point — SWTBUG's
RESET entry at `E0D0` on the SWTPC, MON680's on the 680b. `RESET` does not clear memory, because
pressing RESET on a real 6800 machine did not clear memory — that is behaviour a lot of period
software depends on.

`RESET*` is a **line on the backplane**, not an instruction the simulator carries out for you,
and every board hears it and answers the way its own silicon did — which is not the same answer
twice. The memory board touches no RAM (a RAM chip has no reset pin); the floppy controller
flushes the sector it was writing and deselects the drive; and the console board's 6850 does
**nothing at all**, because the 6850 has no reset pin for `RESET*` to land on — so its baud rate,
word format and interrupt enables all survive a reset, exactly as on the bench. Hit `RESET`
mid-write and you get what the hardware gave you: a half-written sector, a serial port still
configured as the dead program left it, and every byte of RAM intact.

**`POWER` is a different wire.** Switching the machine on drives `POC*` — Power-On Clear, its
own backplane line — and a board may treat the two differently, because the real cards did. The
console 6850 is the case that proves it: `RESET*` has no pin to land on, so a baud rate, word
format and interrupt-enable a crashed program left set **survive a `RESET`** and only return to
their power-on state on `POWER`. `POC*` is also the only moment RAM is allowed to forget: on
`POWER` the memory board refills itself — with **random bytes by default**, because static RAM
does not come up zeroed — and re-reads every ROM image.

| | The processor | The boards | RAM |
|---|---|---|---|
| `RESET` | loads PC from `FFFE`/`FFFF` | `RESET*` on the bus; each board answers as its silicon did — some do nothing | **survives** |
| `POWER` | loads PC from `FFFE`/`FFFF` | `POC*` on the bus; the boards come up as they do from cold | **refilled**, ROMs re-read |
