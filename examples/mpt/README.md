# An interrupt-driven clock on the MP-T timer

The SWTPC **MP-T** is an interrupt timer board: a 6820 PIA in an SS-30 slot, with a Mostek
MK5009 time base behind its B side. A program picks a rate, from 1 µs to one hour, and the
board interrupts the 6800 once a period. This example runs **INTCLK**, the clock program
SWTPC published with the board. It asks for the time, then keeps it on the console, one tick
per interrupt.

```
cd examples/mpt
swtpcsim mpt.toml
```

The machine file loads `INTCLK.S19` into memory and starts SWTBUG, so you land at its `$`
prompt. Type `G` to run the program:

```
$ G
SWTPC 6800 COMPUTER SYSTEM TIME:
HH:MM.SS
```

**Wait for the `HH:MM.SS` line**, then type the time: all eight characters, as `HH:MM.SS`
(`12:59.58`, `09:05.00`). The clock is in 12-hour format. INTCLK takes no Return. After the
eighth character it starts the timer, and the time starts to move, one second at a time:

```
12:59.58
12:59.59
 1:00.00
 1:00.01
```

In a terminal, each new time overwrites the last on the same line. `Ctrl-E` returns to the
`swtpcsim>` prompt.

Three things you will see are what the real machine does:

- **The first tick comes at once.** INTCLK sets the MP-T up in an order that lets the MK5009
  run at 1 µs for a few instructions before the program holds it. That sets the board's
  interrupt flag, and the program never clears the flag before it enables interrupts. So the
  time you type appears one second on, straight away. After that, the ticks are a second
  apart.
- **The hour has no leading zero.** After `12`, INTCLK writes a space: ` 1:00.00`.
- **Characters you type early are lost.** SWTBUG's output routine reads the console's receive
  register before every character it prints. Anything you type before `HH:MM.SS` has
  finished printing is dropped, so wait for it.

The machine runs at a real 1 MHz (`clock_hz = 1000000` in `mpt.toml`), so a second on the
console is a second on the wall. At full host speed, the MK5009, which counts the machine's
own microseconds, would run the clock many times too fast.

## The files

| File | What it is |
|---|---|
| `mpt.toml` | The SWTPC 6800 with SWTBUG, the MP-S console, 40K of RAM, and the MP-T in SS-30 slot 4 (`8010`–`8013`). |
| `INTCLK.ASM` | The program's source. |
| `INTCLK.S19` | The program as Motorola S-records, which `mpt.toml` loads. |

**Both files are corrected from the published copies.** In SWTPC's listing, the two words
at `A000` and `A002` are the wrong way round. SWTBUG's interrupt handler jumps through the
word at `A000`, and INTCLK reads the MP-T's address from `A002`. So `A000` must hold the
interrupt routine (`013D`) and `A002` the board (`8010`). The published S-records put them
the other way round, and the first interrupt jumps into the MP-T's registers. The header
comment's "port #5" is also corrected: with four addresses to a slot, `8010` is slot 4.
Nothing else is changed. The source's opening comment says the same.

## How the MP-T is programmed

INTCLK does what any MP-T program does, at the board's base address `8010`:

| Write | To | Why |
|---|---|---|
| `FF` | `8012` (DDRB) | All eight B lines become outputs. |
| `3D` | `8013` (CRB) | Select the data register. CB1 interrupts on its falling edge. |
| `80` | `8012` (PRB) | PB7 high holds the MK5009 at zero. |
| `06` | `8012` (PRB) | PB7 low starts it; `6` selects one second. |

Its interrupt routine reads `8012`, which clears the flag, then adds a second. The board and
its rate table are in the manual's `mpt` section.
