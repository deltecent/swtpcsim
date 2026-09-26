# Monitor commands and their abbreviations

**Settled 2026-07-11 by Patrick.** The table in `src/cli/commands.cpp` is the only copy of this; HELP and everything below are generated from it.

> **This is not the command reference, and it never was.** It is the argument for *why the
> commands rank and abbreviate the way they do* — which is a design decision, and belongs in a
> design document.
>
> **The reference is `docs/monitor/ref/commands.md`**: every command, its usage, its help and its
> examples, *printed from the same `CommandDef` table the monitor resolves against* by
> `tools/gen-reference.cpp`, with a test that fails if it goes stale. It cannot disagree with the
> program, because it is the program's own words.
>
> (The README used to advertise *this* file as "Every monitor command". It was not, and there was
> no such document until the manual was written.)

## The rule

**The table is in priority order, and the first command whose name starts with what you typed wins.** That is the whole algorithm — there is no minimum-abbreviation column, no priority number, and nothing that treats a one-letter word specially. One letter is just a short prefix.

`D` dumps because DUMP is listed above DEPOSIT, DISASM and DISCONNECT. It follows, without anyone deciding it, that DEPOSIT needs `DE` and DISASM needs `DI`. Reorder the table and every abbreviation in the monitor re-derives itself, including the ones printed by HELP.

This is why **UNMOUNT is not called DISMOUNT**: it's the plainer word, it takes `U` (which nothing else wanted), and removing it from the D-cluster is what let DISASM fall from `DISA` to `DI`. Nobody worked that out — the table did.

**The one invariant:** no command name may be a strict prefix of another. If one were, its full, correctly-spelled name would resolve to whichever came first and there would be no way left to type the other. Renaming REGS to REG would break exactly this; `tests/test_cli.cpp` fails if anyone tries.

## The ranking

The eight that own their prefix, in Patrick's ranking (2026-07-11): **DUMP, STEP, RUN, HISTORY, MOUNT, BREAK, EDIT, CONFIG.**

> **GO is gone (Patrick, 2026-07-12).** `RUN` is the switch on the front panel, and there was never a second thing for GO to be — see [RUN](#run-is-the-switch-on-the-panel) below.
>
> **`R` RUNS (Patrick, 2026-07-13).** It used to reset. That is the same call as `D` dumping: the shortest key on the keyboard goes to the command that cannot destroy anything, and the one that throws the machine's state away costs you letters. RUN is what you type every session, and a `RUN` you did not mean costs nothing — a bare `R` that reset the machine is a machine you have to set up again. RESET pays `RES`, and the rest of the cluster falls out of the table order below with nobody deciding it: `RE` REGS, `REC` RECORD, `REP` REPLAY, `RES` RESET, `REST` RESTORE, `REGI` REGION.

| Type | Command | Notes |
|---|---|---|
| `D` | DUMP | |
| `S` | STEP | |
| `N` | NEXT | STEP that runs *over* a JSR/BSR instead of into it |
| `R` | RUN | the panel's switch. `G` names nothing at all now |
| `H` | HISTORY | the ring of recent bus cycles — always recording |
| `M` | MOUNT | |
| `B` | BREAK | |
| `E` | EDIT | interactive DEPOSIT — a byte at a time, or a mnemonic assembled in place |
| `C` | CONFIG | |
| `SE` | SET | beats SEARCH — you type it far more often |
| `SH` | SHOW | |
| `DE` | DEPOSIT | the front panel keeps its word; it costs one letter |
| `EX` | EXAMINE | the panel's other switch. Bare `EX` = EXAMINE NEXT |
| `L` | LOAD | |
| `SA` | SAVE | |
| `F` | FILL | |
| `SEA` | SEARCH | |
| `COM` | COMPARE | |
| `MOV` | MOVE | |
| `W` | WHO | |
| `BO` | BOARDS | `BOARD` works too — it is a prefix, not an alias |
| `RE` | REGS | the first RE- word in the table, so it takes `RE` outright |
| `REGI` | REGION | `REG` is REGS |
| `DI` | DISASM | |
| `U` | UNMOUNT | not DISMOUNT — see above |
| `DISC` | DISCONNECT | |
| `CONS` | CONSOLE | **configures** the console. It does not start the machine — RUN does |
| `CONN` | CONNECT | `console | null | loopback | scripted | socket:PORT | socket:HOST:PORT | serial:DEVICE | file:PATH` |
| `RES` | RESET | it sits with POWER, and it pays three letters — see above |
| `P` | POWER | |
| `T` | TRACE | logs every bus cycle — to the console or a file |
| `STO` | STOP | *waiting on a monitor that runs alongside the machine — ATTN leaves a RUN today* |
| `SN` | SNAPSHOT | writes the machine's state to a file |
| `REST` | RESTORE | reads a snapshot back into a machine of the same shape |
| `REC` | RECORD | *waiting on RECORD/REPLAY — it builds on SNAPSHOT* |
| `REP` | REPLAY | *waiting on RECORD/REPLAY — it builds on SNAPSHOT* |
| `NO` | NOBREAK | `N` is NEXT — the step you type mid-debug wins the letter |
| `HE` | HELP | or `?` |
| `Q` | QUIT | the only way out — there is no EXIT |

## Two deliberate breaks with SIMH

**`D` dumps.** SIMH's `D` is DEPOSIT and its `E` is EXAMINE; on the Altair itself, DEPOSIT and EXAMINE are the two front-panel switches. But `D` is dump in most ROM monitors, and SIMH's choice to make it deposit has long been a sore point for Patrick. It also puts the shortest key on the keyboard on the command that cannot destroy anything, and makes you type two letters to change memory. That is the better default regardless of heritage.

**`E` edits and `EX` examines.** There is no EXIT — `QUIT` is the one word for leaving, so `E` and `EX` go to the two commands you actually type.

## HELP has two forms

**Bare `HELP` lists the names and nothing else** — the whole set in about ten lines:

```
swtpcsim> HELP

  BO[ARDS]          B[REAK]           COM[PARE]         C[ONFIG]
  CONN[ECT]         CONS[OLE]         DE[POSIT]         DI[SASM]
  DISC[ONNECT]      D[UMP]            E[DIT]*           EX[AMINE]
  F[ILL]            HE[LP]            H[ISTORY]         I[N]
  L[OAD]            M[OUNT]           MOV[E]            N[EXT]
  NO[BREAK]         O[UT]             P[OWER]           Q[UIT]
  REC[ORD]*         REGI[ON]          RE[GS]            REP[LAY]*
  RES[ET]           REST[ORE]*        R[UN]             SA[VE]
  ...
```

The list is **alphabetical**, not ranked — you are hunting for a name, and the ranking is not something you can look a name up by. The brackets are where the ranking shows through: `R[UN]` and `RES[ET]` sit two rows apart and tell you the whole story without a word of explanation.

When you type HELP you are almost always hunting for a name you half-remember, and a wall of usage lines is the worst possible shape for that: it doesn't fit on a screen, so the thing you were looking for scrolls off the top. `*` marks a command that resolves but isn't built yet.

**`HELP <command>`** is where the usage and the examples live:

```
swtpcsim> HELP D

  D[UMP]
  DUMP [<addr>|<range>] [WIDTH=16]

  Hex and ASCII. A bare address runs to the END OF ITS PAGE, and a bare DUMP
  continues from there -- so the rows and the columns both stay page-aligned
  however you first landed. WIDTH is a count, so it is decimal.
    D 100        0100-01FF, a whole page
    D 0001       0001-00FF: stops on the boundary, last line full
    D            the next page
```

Note that `HELP D` works — the argument goes through the same prefix resolver as everything else, so you never have to spell a command out just to ask about it.

**Both forms are generated from the command table.** A hand-written help text is a second list of commands, and a second list of commands is a list that is wrong.

## EXAMINE and DEPOSIT are the front panel's two switches

DUMP answers *"what is around here."* **EXAMINE answers *"what is AT here,"*** which is a different question and deserves its own verb — paging 256 bytes to read one is how you lose the byte in the noise.

**Bare `EXAMINE` is the panel's EXAMINE NEXT**: it steps one byte. `EX 100`, then `EX`, `EX`, `EX` walks memory a byte at a time, exactly as the switch does.

```
swtpcsim> EX 100
0100  C3  .  11000011
swtpcsim> EX
0101  00  .  00000000
swtpcsim> EX
0102  F8  .  11111000
swtpcsim> EX
0103  41  A  01000001
```

The bits are there because the panel showed them on eight LEDs, and because when you are down to one byte you are usually looking at a flag.

EXAMINE keeps **its own cursor**, separate from DUMP's. They step by different amounts — a byte versus a page — and sharing one latch would mean a `D` silently threw your examine position 256 bytes down the road.

### EXAMINE *is* the CPU

The panel has **no address latch of its own.** EXAMINE stops the processor, jams the address switches into the **program counter**, and the CPU drives the address lines and MEMR\*. Everything else follows from that one fact.

**`EX <addr>` is a `JMP <addr>` you can see the destination of.** It loads the PC, so STEP afterwards executes *there* — and `RUN <addr>` is exactly EXAMINE followed by RUN, which is the pair of switches you throw on the panel.

```
swtpcsim> EX E000
E000  FE  .  11111110
swtpcsim> STEP
H0I1N0Z0V0C0 A=02 B=10 X=0055 SP=A03D PC=E003  JMP 00,X
```

**The PC is the cursor** — not a copy of it. Bare `EX` (EXAMINE NEXT) steps the program counter, because that is the only counter the panel has. Keeping a private latch beside it would mean a bare `EX` after a STEP quietly dragged the PC *backwards* to wherever the latch had been left.

**With no CPU card, EXAMINE is an error, not a degraded mode.** Nothing is driving the address lines. It is the CPU that drives them and the memory-read bus signals, so with no CPU none of that works (Patrick, 2026-07-11).

```
swtpcsim> EX 0
no CPU in this machine.  BOARDS ADD 6800 cpu0
```

There is no exception any more. `EX 0 RAW mem0` was one — the PROM burner reaching behind the bus, needing no CPU because it ran no cycle — and it went when `RAW` did (§10.2). Reading behind the bus bought nothing: a ROM answers reads like any other chip, and a bank or a board the CPU cannot see is one you *select* (`SET mem0 bank=3`). Writing behind it is real and survives, as `LOAD … ROM`.

**EXAMINE is the only memory command that needs a CPU.** DUMP, DEPOSIT, FILL, SEARCH, COMPARE and MOVE all work on an empty backplane, because you have to be able to debug the simulator without a processor in it (Patrick, 2026-07-11).

## BOARDS is the backplane

**The command is plural, and both spellings work.** `BOARD` is a *prefix* of `BOARDS`, and prefixes are the whole resolver — so `BOARDS`, `BOARD` and `BO` are one command, with no alias, no second table entry, and nothing to keep in sync. A bare `BOARDS` lists them; you do not have to say `LIST`.

```
swtpcsim> BOARDS
  ID    TYPE    I/O  UNITS                    MEMORY
  ----  ------  ---  -----------------------  --------------------------------------------
  cpu0  6800    -    1 cpu: 6800              -
  mps0  mps     -    1 serial: tty*           8004-8007  read/write  6850 ACIA 'tty'
  dc40  dc4     -    4 disk: drive0..drive3   8014        drive select  D0-D1 drive, D6 side
                                              8018-801B   WD179x  command/track/sector/data
  mem0  memory  -    2 rom: rom0, rom1        0000-7FFF  ram  32K
                                              A000-BFFF  ram  8K
                                              E000-E3FF  rom  swtbug
                                              FC00-FFFF  rom  swtbug

  * holds the console
```

**The I/O column is empty on every board, because the 6800 has no port space.** A 6800 board answers *memory* addresses — the SS-30 window at `$8000–$801F` and the ROM/RAM ranges — so everything it decodes is in the MEMORY column, and `I`/`O` (bus cycles) reach it there.

**Each decoded range gets its own line, and says what it is.** The old listing printed `mem:0000-7FFF,E000-E3FF` and stopped there — which cannot answer the only question worth asking about that card: *which of those is the ROM, and which ROM is in it?* Both facts were in the map all along and were being thrown away. A card carries several regions, and squashing them into one comma list is exactly what hid the difference.

**An empty socket is not in the memory column, because it decodes nothing.** It shows up in UNITS instead, as `rom1(empty)` — there is a socket, and there is no chip in it. Those pages float to `FF`, as they do on the bench.

**UNITS is what you type at `MOUNT` and `CONNECT`**, which is why the designations are there and not merely the count. `*` marks the unit holding the console.

## Naming a card: `<id>[:<unit>]`

**A name is case-blind.** `MOUNT DC40:DRIVE0 flex.dsk` and `mount dc40:drive0 flex.dsk` are the same command. `DC40` and `dc40` are not two boards that happen to match — they are *one board*, which is why `BOARDS ADD dc4 DC40` is refused when `dc40` is already in a slot.

**The trailing index is optional when it distinguishes nothing.** The `0` in `dc40` is not an index the simulator parses; it is a character in a string the machine file chose (`id = "dc40"`). It is there to tell two disk controllers apart — and when there is only one in the machine, it tells nothing apart. So `DC4` finds `dc40`, and `MPS` finds `mps0`.

This is **not** prefix matching, and deliberately so: only a run of trailing digits may be dropped. `DC` finds nothing. (Verbs *are* prefix-matched — that is the table at the top of this page — but a verb list is fixed and reviewed, and a backplane is whatever you plugged into it.)

**A lone unit needs no naming.** The MP-S has exactly one serial unit, and it is called `tty`; naming it adds no information. So `CONNECT MPS console` is `CONNECT mps0:tty console`. The candidates are filtered by the kind the verb can act on, so `MOUNT` looks only at media units and `CONNECT` only at serial ones — which is why the MP-S (one `tty`) is unambiguous to `CONNECT`, and the DC-4 (`drive0`…`drive3`) is not to `MOUNT`.

**Anything genuinely plural you must still say, and it tells you so.** A shorthand that quietly picked one of four drives would be worse than no shorthand:

```
swtpcsim> MOUNT DC40 FLEX2-35.DSK
dc40 has 4 units you could mount into: drive0 drive1 drive2 drive3. Name one -- dc40:drive0

swtpcsim> MOUNT DC4 FLEX2-35.DSK            (with two DC-4 controllers in the machine)
DC4: ambiguous -- dc40 dc41. Name the one you mean.
```

**All of this is the *prompt's* grammar, and it stops at the prompt.** A machine file is matched exactly: an `[[board]] id = "dc4"` that silently reached into the base and modified `dc40` would be a config that does something other than what it says. Same line this project already draws for relative paths (`docs/config.md`) — what you *type* and what a *file* says are resolved by different rules, on purpose. The machine files shipped in `machines/` all spell their units out in full.

## SHOW BUS IRQ is the only window onto the interrupt wiring

`SHOW BUS` has four views: `MAP` (memory), `IO` (ports), `CONTENTION` (who collides), and `IRQ`. The first three describe things you could find out another way — a wrong decode collides, or reads `FF`, and either way *something happens*. **The interrupt wiring is different: it is one shared wire and four vectors, none of them addressable, and getting it wrong fails in total silence.**

```
swtpcsim> SHOW BUS IRQ
INTERRUPTS
  CPU     I mask SET         IRQ is masked (SEI); NMI and SWI still vector
  IRQ     idle               the shared maskable wire (FFF8)

  VECTOR         POINTS AT   (as programmed in memory now)
  FFF8  IRQ    -> E000
  FFFA  SWI    -> E18B
  FFFC  NMI    -> E1A7
  FFFE  RESET  -> E0D0
```

On the 6800 every interrupt takes its handler address from a **fixed vector at the top of memory**, and there is no priority encoder, no acknowledge cycle, and no card that jams a vector onto the data bus. So this view reports two things. First, the **maskable IRQ wire**: who is pulling it — a shared wire-OR, shown as `idle` when nobody is or `ASSERTED, pulled by <board>` when one or more are — and whether the CPU's `I` mask would currently let it be taken. (NMI is an edge on a dedicated pin, not a level on the bus, so there is no wire to survey; it is named only for its vector.)

Second, **the four vectors themselves**, read straight out of memory with `peek()` so the view perturbs nothing. This is where the silence hides: a machine whose ROM never wrote `$FFF8`, or wrote it wrong, runs perfectly until the first interrupt and then vanishes into whatever `$FFF8` happens to point at. Reading them back here — `IRQ → E000`, the real SWTBUG handler — is the difference between "the interrupt wiring is right" and "nothing has interrupted yet."

### It tells you when the CPU would refuse the interrupt

Like `SHOW BUS CONTENTION`, this view has an opinion. A board can pull the IRQ line while the CPU has interrupts masked (`SEI`) — a machine that runs, looks busy, and never takes the interrupt. That is exactly what a service routine does to itself on entry, and in every other view it is indistinguishable from a lost interrupt, so an IRQ asserted under a set `I` mask is called out by name under `WARNINGS`. A bare `SHOW BUS` prints the short `INTERRUPTS` summary — the wire and the mask — without the vector table.

## RUN is the switch on the panel

**`RUN [addr]` is the only way to start the machine**, and `RUN <addr>` is EXAMINE + RUN — it loads the PC first, exactly as you would on the panel.

**GO was deleted for it (2026-07-12).** There was never a second thing for GO to be. A *headless* run — no terminal handover, ^C to stop — is not a mode the operator picks; it is simply what happens when **nothing holds the console**, and the machine already knows that. Whether your keys reach the guest is a fact about the backplane, not a question for you:

| the backplane | what RUN does |
|---|---|
| a unit holds the console | the guest gets the keyboard — every key, including ^C — and the machine runs at the CPU card's real clock |
| nothing holds the console | there is nothing to hand over, so it just runs, flat out |

Both stop on a breakpoint, on a HLT nothing can wake, and on ATTN, and both say which. That is why GO had nothing left to be.

### ATTN is the stop key. ^C is not.

**Ctrl-C belongs to the guest** — a running program reads it, and a stop key the guest also wants is one that either breaks the guest or gets eaten by it. So the way out is **ATTN (^E)**, and it is the same key whatever is in the backplane: with a console, with no console, on a terminal, always.

The host intercepts it before the guest is ever offered the byte, so **the guest cannot disable it** — it is a key on the *front panel*, not on the terminal. And **ATTN stops the machine without disturbing it**: nothing executes while this prompt is up, but nothing is lost either — the monitor tells you the address it is sitting at, and a bare `RUN` resumes from exactly where you were.

```
swtpcsim> RUN E000
[console -- ^E returns to the monitor]

   $
                                    ← you press ^E
[monitor -- the machine is still at E216. RUN resumes]
```

**ATTN is tracked on console input and nowhere else.** A unit on a socket, a serial port or a loopback is *not* the console, and its data passes through untouched — `05` down a socket is a byte of somebody's protocol, and scanning a modem line for a key that only exists on the operator's terminal would be corrupting the data, not a feature.

## CONSOLE configures the console — it does not run the machine

```
swtpcsim> CONSOLE
console  (the host keyboard and screen -- not a tty)

  property         value            legal
  attn             0x5              0x1..0x1F
  base             hex              hex|octal
  upper            false            true|false
  strip7in         false            true|false
  strip7out        false            true|false
  crlf             false            true|false
  echo             false            true|false
  bell             true             true|false
  bsdel            off              off|bs|del

  held by  mps0:tty

swtpcsim> CONSOLE attn=1D          ← make it ^]
```

`CONSOLE k=v` sets, bare `CONSOLE` shows. (`SET CONSOLE` and `SHOW CONSOLE` are the same thing said the long way.) It used to *enter* console mode, and that was wrong twice over: a command that starts the CPU because you asked to look at a setting is a trap, and "start the machine" already has a name.

### Every property is settable

There is no "runtime vs config-time" column, and no property that `SET` will refuse. **You can only type at the prompt when the machine is stopped** — by ATTN, by a breakpoint, by a HLT, which is the panel's STOP switch — so there is no moment at which a `SET` could race a running CPU. And on real hardware the rule would be a fiction anyway: a board being worked on is often sitting on an extender card, getting changed with the power on (Patrick, 2026-07-12).

There *was* such a gate. It never once fired, because nothing in the simulator ever set the flag it was conditioned on. A rule the code only pretends to enforce is worse than no rule at all.

### The endpoints

`CONNECT <id>:<unit> <endpoint>`. The **monitor** knows this grammar and no board is permitted to, which is why `CONNECT mps0:tty serial:/dev/tty.usbserial-AL009KFH` needed **not one line of code in the MP-S**.

**The list of endpoints is not copied here.** It is the User Manual's (the serial chapter) and
`HELP CONNECT`'s, and `HELP` prints it from `endpointHelp()` — the same function the resolver
answers to. What belongs here is why a few of them are shaped the way they are:

- **`null` is a DB-25 with nothing behind it**, not an error. An unconnected 6850 works fine and
  talks to nobody.
- **`loopback` jumpers TX to RX — and RTS→CTS, DTR→DCD/DSR**, exactly like the loopback plug in
  the drawer. It is the one endpoint that can test modem control with no hardware.
- **A client connecting to a listening `socket:` or `telnet:` *is* carrier appearing.** One
  client at a time; the listener survives a disconnect, so the next caller is the phone ringing
  again. A call out is non-blocking, and a session still being established is a phone still
  ringing: the board correctly sees no carrier yet.
- **`socket:` is raw and `telnet:` negotiates**, and they are two endpoints rather than an option
  because they are for different far ends. A program, or another machine, wants the guest's
  bytes and nothing else. A person with a `telnet` client needs the echo and line-mode
  handshake, or every key appears twice. `telnet:` also greets its caller by default, and
  `socket:` does not, because a banner is data to a machine on the far end.
- **`serial:` is the one place where the pins are the pins.** The board programs its baud and
  frame, and with `SET mps0:tty cts=wired` the far end can genuinely stop your transmitter.

A device that is not there does **not** silently become a `NullStream` — it is an error, and `serial:` lists the ports that *are* on the host, because a cable that enumerated under a different name is ten minutes of a person doubting the simulator.

### Which unit is the console?

**The one that is cabled to it** — the console is an endpoint like any other, so with two MP-S boards and two ports it is simply whichever unit you connected:

```
swtpcsim> CONNECT mps0:tty console
swtpcsim> CONNECT mps1:tty console
console: taken from mps0:tty
```

**Exactly one unit may hold it, because there is exactly one keyboard.** Two boards reading it would each get half your keystrokes — invisible until you are debugging why every other character vanished. Interactively, connecting a second one **steals** it and says who from: you are moving the cable, and the last port you plug in is the one you meant.

**A config file that names two consoles is refused.** There is no "last" about a file — it is a typo, not a decision — so it fails the load and names both offenders.

### The keyboard is buffered by the host

Keys land in a buffer belonging to the *host*, and a card takes characters from it. That is what lets ATTN be watched whether or not anybody is reading — while the guest is busy computing, and even when there is no serial card in the machine at all. It is also what lets anything *type for you*: an injected byte and a human's are indistinguishable to the board, because at the level the board sees, there is no difference. MCP's `send`/`expect` will be built on exactly that.

It does not make the UART any less real: the 6850 still holds **one** character, still sets RDRF when it does, and still takes the next only when the guest has cleared the last. The buffer is the *line*, and a line is buffered and flow-controlled. It is a real keyboard buffer, so it is finite — type past the end and the keys are dropped, and the drop is counted rather than silently swallowed.

And because looking at one byte is exactly when you need to know it *is* a byte:

```
swtpcsim> EX 8000
8000  FF  .  11111111   (nobody drives this -- the bus floated it)
```

## SNAPSHOT and RESTORE: the machine's state, saved and reloaded

`SNAPSHOT <file>` writes the whole machine's **state** — the CPU, the clock, and every board's
registers, RAM and latches — to a file. `RESTORE <file>` reads it back into a machine you have
already built.

```
swtpcsim> SNAPSHOT before-boot.snap
snapshot written to before-boot.snap
swtpcsim> RESTORE before-boot.snap
restored from before-boot.snap
```

**A snapshot is state, not configuration.** It does not carry the *shape* of the machine — which
boards, at which ports — the way a machine file does. So RESTORE loads into a machine of the
**same shape** (build it with the same machine file, or a `CONFIG LOAD`, first), and a file that
does not match — a board missing, a type changed — is refused with the reason and your running
machine is left untouched. A corrupt or truncated file is caught by its checksum and refused the
same way.

`RECORD` and `REPLAY` — a recorded session you can play back exactly — build on this and are not
implemented yet; see below.

## Commands that do not exist yet still resolve

RECORD, REPLAY, STOP and the rest are in the table, and typing `REC` today prints:

```
swtpcsim> REC
RECORD: not implemented yet -- waiting on RECORD/REPLAY (it builds on SNAPSHOT, now done).
```

**This is the point, not an oversight.** If only the built commands were listed, `S` would mean SHOW today and silently start meaning STEP the day the CPU lands — and someone's fingers would keep typing `S` and get something else. Abbreviations are a contract with muscle memory, so the contract is fixed now, before anyone has any muscle memory to break.

## DUMP: a page at a time, and the columns never move

**A bare address dumps to the end of its page.** `D 100` is not a request to see one byte — nobody has ever wanted that — it is *"show me what's at 0100"*, and the answer is a page.

**`DUMP` with no argument at all** continues from where the last dump stopped. Type `D`, `D`, `D` and you page through memory, which is how you actually read it: you rarely know the address of the thing you're looking for, only that it's somewhere after the thing you just saw.

**Everything stays page-aligned — rows as well as columns.** `D 0001` opens on the `0000` line with the `0000` column left *blank*, so the byte at `0001` sits under the `01` heading; and it **stops at `00FF`**, not 256 bytes later:

```
swtpcsim> D 0001
0000     42 43 00 00 00 00 00  00 00 00 00 00 00 00 00   BC.............
0010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................
...
00F0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................

swtpcsim> D
0100  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................
```

The whole reason to print a hex address on every line is that the *column position* tells you the low nibble without counting — so if the columns shifted with the start address, every byte would be off by one and you wouldn't notice until it mattered. The same argument decides the end: counting out exactly 256 bytes from `0001` would dangle a one-byte line at `0100` and throw every subsequent `D` off the page grid. **Stopping on the boundary means the last line is always full and the next `D` opens cleanly on `0100`** — one accidental keystroke on the start address doesn't misalign the rest of the session.

**An explicit range means exactly what it says.** `D 100-10F` and `D 100/20` don't expand; only the bare address form does. That distinction is deliberate: `range` is shared with `FILL`, `MOVE`, `SEARCH` and `SAVE`, and a bare address quietly meaning 256 bytes *there* would be a footgun — `FILL 100 5A` must fill one byte.

Giving DUMP a range or an address moves the resume mark; nothing else does.

## DEPOSIT and EXAMINE reach a memory-mapped device

The 6800 has **no I/O port space** — a UART, a floppy controller, a PIA is just an
address. So there is no `IN`/`OUT`: you reach a device register the same way you reach
RAM, with `DEPOSIT` (a real bus write) and `EXAMINE` (a real bus read), and both report
**whether anybody actually answered**. That is the whole reason the bus/board boundary
exists: `FF` from a board and `FF` from an empty slot are the same byte and completely
different faults.

```
swtpcsim> EXAMINE C000
C000  FF  .  11111111   (nobody drives this -- the bus floated it)

swtpcsim> DEPOSIT C000 41
C000: no board decodes writes here. byte discarded.
```

To look without touching — who *would* answer without running a cycle — use **`WHO <addr>`**.

## Numbers: on the wire → hex, never on the wire → decimal

**Settled 2026-07-11 by Patrick** (DESIGN.md §10.0.1). The base belongs to the **operand**, not to the command line.

| | |
|---|---|
| **HEX** — the machine sees it | addresses, data bytes |
| **DECIMAL** — only you see it | counts, widths, sizes, baud rates, unit numbers |

```
D 100            dump from 0100h
D 100/20         0100h..011Fh    (LEN is part of the address expression: hex)
DE 100 C3 00 F8  bytes are hex
EX 8005          an address is hex
D 0 WIDTH=10     ten bytes per line -- a width is a count, so it is DECIMAL
SET mps0 baud=9600                nine thousand six hundred
```

A single global base was never really available — `baud=9600` cannot mean 38400 — so the rule had to bend somewhere. It bends where it means something. The alternative ("everything in a command is hex") buys one sentence of simplicity and pays for it with `STEP 20` stepping 32 times, silently, forever.

**Overrides work everywhere, both directions**, because a rule you can't type your way out of is a trap: `0x20`, `$20` and `20h` force hex; `#32` forces decimal; `0b1010` is binary; `1_000` is just spacing.

**A `K`/`M` suffix is always decimal** — `10K` is 10,240, never 16K, which is why nobody has ever had to ask. So `0x10K` is a contradiction and is **rejected**, not guessed at:

```
swtpcsim> REGION ADD mem0 type=ram at=8000 size=0x10K
mem0: memory: [[board.region]] size: a K/M suffix is always decimal -- drop the hex marker: '0x10K'
```

Note that `at=F000` needs no `0x` — it's an address, so it's already hex.

## The backspace problem, and why there is a line editor

`^H` was appearing in the input line. The cause is that **a terminal has exactly one erase character**: the tty driver's `VERASE` is a single byte, and whether your backspace key sends BS (`0x08`) or DEL (`0x7F`) depends on the emulator, the OS and `$TERM`. When the two disagree, the byte you send is not the byte the driver erases with — so it is just a control character, and the driver puts it in the line, where it prints as `^H`.

**There is no VERASE setting that fixes this**, because there is no single right answer. So `src/cli/lineedit.cpp` takes the terminal out of canonical mode and edits the line itself, treating **both `0x08` and `0x7F` as backspace**. Then it does not matter which one your terminal chose, and it does not matter what OS you are on.

It also brings history (↑/↓), cursor movement (←/→, Ctrl-A/E), Ctrl-U, Ctrl-W, and Ctrl-C to abandon a line. Anything else that arrives as a stray control byte is **dropped, not inserted** — which is how `^H` got into the line to begin with.

A pipe, a script, or `--mcp` is not a terminal, and takes a plain `getline` path that never touches terminal state.

It also brings **tab completion**, driven by `Board::properties()` (DESIGN.md §10.4) — the same reflection layer that already backs SET, SHOW, the TOML loader and the MCP schemas. `Tab` completes a command, then a board id or `id:unit` target, then that board or unit's property names, then a property's legal values; `MOUNT`, `CONNECT` and the board verbs complete their target the same way.
