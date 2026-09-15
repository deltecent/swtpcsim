<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# Quick reference

## Getting out, and back in

| Key | Does |
|---|---|
| `^E` | **STOP** — stop the machine and take the keyboard back. Nothing is lost. |
| `RUN` | Resume, at the exact instruction it stopped on. |
| `QUIT` | Leave. (There is no `EXIT`.) |

## Editing the command line

`Tab` completes what you are typing — a command, then a board id, then its property names, then a property's values (`SET mem0 fill=` then `Tab`); a second `Tab` lists the choices. `Up`/`Down` walk the command history, saved per directory in `.swtpcsim_history`.

| Key | Does |
|---|---|
| `Ctrl-A` / `Ctrl-E` (or `Home` / `End`) | start / end of line |
| `Alt-B` / `Alt-F` (or `Ctrl-Left` / `Ctrl-Right`) | back / forward one word |
| `Ctrl-W` / `Ctrl-K` / `Ctrl-U` | erase word behind / to end of line / whole line |
| `Backspace` / `Delete` | erase before / under the cursor |

## Command line

```
swtpcsim [options] [machine]

  machine            a built-in name, or a config file (has a '/' or ends .toml).
                     Omitted: ./swtpcsim.toml if there is one, else `default`.
  -m, --machine <n>  ALWAYS a built-in name -- never a file.
  -f, --file <path>  ALWAYS a file -- never a built-in name.
  -n, --none         empty backplane: no boards, no memory, nothing.
  -l, --list         list the built-in machines and exit.
  -s, --script <f>   run a command script, then exit with its status.
  -x, --exec <cmd>   run one monitor command (repeatable), then exit.
  -i, --interactive  after --script/--exec, stay in the monitor.
      --mcp          MCP server on stdio.
  -v, --version      print the version and exit.
  -h, --help         print this help and exit.
```

## Monitor commands

Type the part before the bracket.

| Command | Does | Usage |
|---|---|---|
| `BO[ARDS]` | List, add, or remove boards on the backplane. | `BOARDS [LIST]\|ADD <type> <id> [k=v...]\|REMOVE <id>` |
| `B[REAK]` | Set a breakpoint on an address, memory/I/O access, or tape stop. | `BREAK [<addr> \| MEM R\|W <addr> \| TAPE STOP] [IF <expr> \| LOADS <expr>] [TRACE ON\|OFF]` |
| `COM[PARE]` | Compare a range of memory against another address. | `COMPARE <range> <addr>` |
| `C[ONFIG]` | Load or save the whole machine as a TOML file. | `CONFIG LOAD <f.toml> \| CONFIG SAVE <f.toml>` |
| `CONN[ECT]` | Attach a serial unit to an endpoint (console, socket, file, ...). | `CONNECT <id>:<u> <endpoint>` |
| `CONS[OLE]` | Show or set the host console's properties. | `CONSOLE [<k>=<v>...]` |
| `DE[POSIT]` | Write bytes into memory at an address. | `DEPOSIT <addr> <bytes...>` |
| `DI[SASM]` | Disassemble memory into instructions. | `DISASM [<addr>\|<range>] [n] [CPU=6800]` |
| `DISC[ONNECT]` | Unplug the endpoint from a serial unit. | `DISCONNECT <id>:<u>` |
| `DO` | Run a file of monitor commands, one per line, as if typed. | `DO <file>` |
| `D[UMP]` | Show memory as hex and ASCII. | `DUMP [<addr>\|<range>] [WIDTH=16]` |
| `E[DIT]` | Enter bytes into memory interactively from an address. | `EDIT <addr> [ROM]` |
| `EX[AMINE]` | Point the front panel at an address (and show that byte). | `EXAMINE [<addr>]` |
| `F[ILL]` | Fill a range of memory with a byte. | `FILL <range> <byte>` |
| `HE[LP]` | Show help for a command. | `HELP [<command>]` |
| `H[ISTORY]` | Replay the recent instruction (or bus-cycle) history. | `HISTORY [BUS\|CPU] [n]` |
| `L[OAD]` | Load a file into memory (binary, Intel hex or S-record). | `LOAD <file> [AT <addr>] [FORMAT=BIN\|HEX\|SREC] [ROM]` |
| `MA[CHINE]` | Load a built-in machine by name (MACHINE none empties the backplane). | `MACHINE <name> \| MACHINE none` |
| `M[OUNT]` | Put a disk or tape image into a drive; a .imd is converted to a raw .dsk beside it. | `MOUNT <id>[:<u>] <file> [WP] [CREATE] [extract[=<base>]] [k=v...]` |
| `MOV[E]` | Copy a range of memory to another address. | `MOVE <range> <dest> [ROM]` |
| `N[EXT]` | Step one instruction, running any JSR/BSR to completion. | `NEXT` |
| `NO[BREAK]` | Remove a breakpoint, or all of them. | `NOBREAK [id]` |
| `P[OWER]` | Power-cycle the machine -- the only thing that clears RAM. | `POWER` |
| `Q[UIT]` | Leave the simulator. | `QUIT` |
| `REGI[ON]` | Add a RAM or ROM region to a memory board. | `REGION ADD <id> type=ram\|rom at=<addr> [size=\|mount=]` |
| `RE[GS]` | Show the CPU registers (SET REG changes one). | `REGS \| SET REG <r>=<v>` |
| `RES[ET]` | Reset the machine, keeping RAM (RESET CPU resets just the processor). | `RESET [CPU]` |
| `REST[ORE]` | Load machine state back from a snapshot. | `RESTORE <file>` |
| `R[UN]` | Start or resume the machine, optionally at an address. | `RUN [addr]` |
| `SA[VE]` | Write a range of memory out to a file. | `SAVE <file> <range> [FORMAT=BIN\|HEX\|OCTAL\|PRN]` |
| `SEA[RCH]` | Find bytes or a string in a range of memory. | `SEARCH <range> <bytes...>\|"str"` |
| `SE[T]` | Change a property of a board, the console, display, a register, or the bus. | `SET <id>[:<u>]\|CONSOLE\|DISPLAY\|REG\|BUS <k>=<v>` |
| `SH[OW]` | Display the state of a board, the bus, or the machine. | `SHOW <id>\|BOARDS\|BOARD <type> [UNITS]\|MACHINES\|MACHINE [<name>]\|BUS [MAP\|IRQ\|CONTENTION]\|ROMS\|MOUNTS\|PATHS\|CONSOLE\|DISPLAY\|SYMBOLS\|VERSION` |
| `SN[APSHOT]` | Save the whole machine state to a file. | `SNAPSHOT <file>` |
| `STA[RTUP]` | Edit the machine's boot list (the commands CONFIG SAVE writes as startup = [...]). | `STARTUP [ADD <command> \| REMOVE <n> \| CLEAR]` |
| `S[TEP]` | Run one instruction (or n), showing the registers after each. | `STEP [n]` |
| `SY[MBOLS]` | Load or clear a symbol table for disassembly. | `SYMBOLS LOAD <file> [REPLACE] \| SYMBOLS CLEAR` |
| `T[RACE]` | Log every bus cycle while the machine runs. | `TRACE ON\|OFF [file] [MASK=IRQ,CONTENTION]` |
| `TY[PE]` | Feed text to the guest as if typed at its keyboard. | `TYPE "text"` |
| `U[NMOUNT]` | Take a disk or tape out of a drive. | `UNMOUNT <id>:<u>` |
| `W[HO]` | Say which board answers an address or I/O port. | `WHO <addr>` |

## Boards

**CPU**

| Type | What it is |
|---|---|
| `6800` | Altair 680b / SWTPC CPU board: a Motorola 6800. Decodes nothing -- it drives the bus. Memory-mapped I/O |

**Memory**

| Type | What it is |
|---|---|
| `memory` | RAM/ROM board: a list of regions -- plain, unbanked memory |

**Disk**

| Type | What it is |
|---|---|
| `dc4` | SWTPC DC-4 floppy disk controller: a WD179x (1 MHz) with up to four 5.25" drives (drive0..3). Spans two SS-30 ports -- a true-sense drive/side select latch at $8014 and the WD179x registers at $8018-$801B (default). Memory-mapped, DRQ-polled. FLEX 2.0/3.0 boots from it via SWTBUG's 'D' command |

**Serial**

| Type | What it is |
|---|---|
| `680io` | Altair 680b onboard I/O: a 6850 ACIA console ('tty') at F000/F001 and the config-strap read port at F002. Memory-mapped |
| `mps` | SWTPC MP-S serial interface: a 6850 ACIA console ('tty') on an SS-30 slot, control/status at the slot base and Rx/Tx data at base+1 (default $8004/$8005, the console slot). Memory-mapped; the ACIA IRQ pulls the 6800 IRQ. The board SWTBUG/MIKBUG's terminal routines assume |

**Tape**

| Type | What it is |
|---|---|
| `680kcacr` | Altair 680b KCACR audio-cassette interface: a 1602-family UART recording Kansas City Standard FSK, memory-mapped at F010 (status/control) and F011 (data), active-LOW. Adds software motor control (control D7=on, D6=off) and interrupt-driven transfer (D0/D1 enables pull the 6800 IRQ). MOUNT a tape, WIND/REWIND it |

**Parallel and printer**

| Type | What it is |
|---|---|
| `680uio` | Altair 680b Universal I/O: a second 6850 ACIA serial port ('serial') and a 6820 PIA parallel port (sections 'p1a/p1b', 'p2a/p2b' with pias=2) in an S9-relocatable window (default base F000: serial F006/F007, PIA F008-F00F), plus fixed switch inputs at F003 and a non-latched output at F010-F013. Memory-mapped, active-high |

## Machines

| Machine | What it is |
|---|---|
| `altair680` | The Altair 680b -- MITS's second machine, and a different animal from the 8800. |
| `swtpc` | The SWTPC 6800 -- Southwest Technical Products' 1975 computer, the machine this simulator is named for. |

## A machine file, in one look

```toml
[machine]
name    = "mine"
base    = "default"        # start from a machine, and say what is DIFFERENT
startup = ["RUN FF00"]     # the operator's own keystrokes. There is no BOOT verb.

[[board]]                  # type + a NEW id      -> ADD the card
type = "mps"               # type + an id from the base -> REPLACE it outright
id   = "mps0"              # NO type + an id      -> MODIFY the one already there
at   = 8004                # remove = true        -> PULL THE CARD OUT

  [board.unit.tty]         # a unit's own settings
  connect = "console"

  [[board.region]]         # a list the card owns (memory)
  type = "ram"
  at   = 0000              # hex: it is an address
  size = "32K"             # decimal: it is a size

  [[board.drive]]          # a list the card owns (disk controllers)
  unit  = 0
  mount = "flex.dsk"       # relative to THIS FILE

[console]                  # the HOST's terminal -- not a board
strip7out = true
base      = octal          # read/print the wire class in split octal
```

**Paths:** a path *inside* a machine file is relative to **that file**. A path you
*type* is relative to **your shell**.

## Endpoints — `CONNECT <id>:<unit> <endpoint>`

| Endpoint | Is |
|---|---|
| `console` | the host terminal. Exactly one unit may hold it. |
| `null` | nowhere. Writes vanish, reads never come. |
| `loopback` | itself — what you write comes back. |
| `scripted` | a caller in place of a human — what MCP and the tests type into. |
| `socket:PORT` | **listens** — this is telnet-in. |
| `socket:HOST:PORT` | **calls out**. |
| `serial:DEVICE` | a real serial port on this host. |
| `in:PATH` | a host file as a reader (paper tape). `?cps=N` paces it. |
| `out:PATH` | a host file as a punch — 8-bit clean, never truncating. |
| `terminal` | a window the simulator draws itself (SDL builds). `?emulation=vt100\|adm3a\|vt52\|h19`, `?size=COLSxROWS`. |
| `printer:QUEUE` | a real print queue on this host. |
| `<endpoint>\|FILE` | a tap: append `\|FILE` to any endpoint to also log the line. |
| `<endpoint>\|socket:PORT` | a live mirror: `telnet` in to watch and take over. `?ro` = watch-only. |

