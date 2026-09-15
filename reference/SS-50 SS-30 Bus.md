# SWTPC SS-50 / SS-30 Bus (MP-B Mother Board)

Source: [MP_B_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions — MP-B Mother Board*,
circa 1976; scanned/edited by Michael Holley, Sept 29 2000, rev. Apr 26 2003;
`https://deramp.com/downloads/swtpc/hardware/MP_B%20Mother%20Board/MP_B_AssemblyInstructions.pdf`).
Fetched 2026-09-13.

The **MP-B** is the SWTPC 6800's motherboard, and the reference for the two buses every board in
this simulator plugs into: the **SS-50** 50-line system bus (CPU + memory) and the **SS-30**
30-line I/O bus (interface boards). SWTPC never used those "SS-50/SS-30" names in period — the
manual just calls them the "system information buss" and the "interface information buss" — but the
names became standard and are used here. This file documents the memory map, the signal set, and
the per-slot address decode that the emulator's boards implement; the individual boards are the
[6800 CPU](MP-A%206800%20CPU%20Board.md), memory, and the [MP-S](MP-S%20Serial%20Interface.md) /
[MP-C](MP-C%20Serial%20Control%20Interface.md) interfaces.

The MP-B is a 9″ × 14″ board carrying one [MP-A](MP-A%206800%20CPU%20Board.md) processor slot, up
to four MP-M 4 K memory slots (plus two spares → 16 K), and **eight interface slots**. It provides
the line buffering and address decoding for all of them, so the plug-in boards are cheap. Two
MP-Bs can be paralleled for up to 32 K of RAM and up to fifteen interface slots.

## Memory map — the emulation payload

The MP-B decodes the full 64 K into fixed regions (its Figure 1):

| Region | Address | Contents |
|---|---|---|
| System RAM | `0000`–`7FFF` | Eight 4 K blocks (`4K MEMORY #0`…`#7`). |
| **SS-30 I/O window** | `8000`–`801F` | Eight I/O ports of four addresses each (see below). |
| Scratchpad RAM | `A000`–`A07F` | The monitor's MC6810 RAM (on the CPU board). |
| Monitor ROM | `E000`–`E1FF` | MIKBUG/SWTBUG (SWTBUG extends to `E3FF`). |
| Restart mirror | `FFF8`–`FFFF` | The monitor ROM re-decoded so the CPU's vector fetch reaches it. |

## SS-30 I/O ports — four addresses per slot

The I/O window `8000`–`801F` is divided into **eight ports of four sequential addresses**; port N
is based at `8000 + N·4`. **Port 1 (`8004`–`8007`) is reserved for the terminal / control
interface** — the console lives here. The other seven take any mix of serial
([MP-S](MP-S%20Serial%20Interface.md)) and parallel (MP-L) boards.

| Port | Addresses | | Port | Addresses |
|---|---|---|---|---|
| 0 | `8000`–`8003` | | 4 | `8010`–`8013` |
| 1 | `8004`–`8007` (control) | | 5 | `8014`–`8017` |
| 2 | `8008`–`800B` | | 6 | `8018`–`801B` |
| 3 | `800C`–`800F` | | 7 | `801C`–`801F` |

Two 74S138 1-of-8 decoders (IC3/IC6) enable exactly one interface board when its port's addresses
appear on the bus. On the 30-pin interface connector the four addresses are selected by two
**register-select lines, `RS0` = A0 and `RS1` = A1** — the low two address bits, carried alongside
buffered data, `R/W`, `VMA`, `Φ2`, the interrupt lines, the five baud clocks, and two user-defined
data lines (`UD3`/`UD4`).

> ⚠ **`RS0`/`RS1` = A0/A1 is the root of the MP-S "mirror" behavior.** The slot offers a board
> *two* register-select bits, but a board wires only as many as its chip has. A parallel board's
> MC6820 PIA has two selects and fills all four addresses; the [MP-S](MP-S%20Serial%20Interface.md)'s
> MC6850 ACIA has only **one** (`RS`, wired to A0), so A1 is ignored and the ACIA answers all four
> addresses with `base+2/base+3` mirroring `base+0/base+1`. That mirror is what SWTBUG's console
> probe depends on — see the MP-S and [SWTBUG](SWTBUG%20Monitor.md) emulation notes.

## SS-50 system-bus signals

The 50-line bus (the manual's Figure descriptions). **Active-low** lines are marked `#` in the
edited manual (the original used overbars): `D0`–`D7`, `NMI`, `IRQ`, `Φ2`, `VMA`, `R/W`, `RESET`,
`HALT`.

| Signal | Meaning |
|---|---|
| `D0#`–`D7#` | The 8 bidirectional data lines (buffered, inverted on the bus). |
| `A0`–`A15` | The 16 address lines. |
| `Φ1`, `Φ2#` | The two-phase non-overlapping clock; **all transfers happen on `Φ2`**, which also flags valid data. |
| `VMA#` | Valid Memory Address — low confirms a real bus cycle. |
| `R/W#` | High = read from memory/interface, low = write. |
| `RESET#` | Low resets CPU + interfaces and loads the monitor; asserted by power-up one-shot or `M. RESET`. |
| `M. RESET` | The manual RESET line (front-panel pushbutton), normally grounded momentarily. |
| `NMI#` | Non-maskable interrupt (cannot be inhibited in software). |
| `IRQ#` | Maskable interrupt request. |
| `HALT#` | Low halts the CPU and frees the bus; `BA` (Bus Available) high acknowledges. |
| `110b`/`150b`/`300b`/`600b`/`1200b` | Five baud-rate clocks generated on the CPU board and distributed to the serial interfaces. |
| `UD1`/`UD2` | User-defined lines (unassigned on the system bus). |
| `7–8 VDC`, `±12`, `GND` | Unregulated supply (each board has its own +5 regulator); ±12 feeds the RS-232 / 20 mA line drivers. |
| `INDEX` | An unused keying pin, cut per-slot to prevent mis-plugging. |

## Emulation notes

swtpcsim has **no central bus model of the SS-50/SS-30**; the generic `Bus` caches decode per
page/port and each board answers its own addresses via `Board::decodes()`. So this reference is
realized as board behavior, not a bus board:

- The `memory` board provides the `0000`–`7FFF` RAM, the `A000`–`A07F` scratchpad, and the monitor
  ROM regions (`E000`, plus a relocated mirror at `FC00` for the `FFF8`–`FFFF` vectors).
- Each interface board decodes its own four-address SS-30 port; the [`mps`](MP-S%20Serial%20Interface.md)
  board defaults to port 1 (`8004`) with a configurable `base`.
- The active-low nature of the physical bus lines is internal to the boards that historically drove
  them; the emulator's `Bus` presents plain (active-high) address/data, so board models do not
  re-invert.

⚠ **Nothing may sit above the monitor ROM.** The restart mirror at `FFF8`–`FFFF` is the monitor
ROM answering the top of memory; a board or RAM region decoded there would capture the reset vector
fetch. `machines/swtpc.toml` keeps the top of memory clear apart from the relocated ROM mirror.

## Related

- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) — the SS-50 processor board that buffers the bus.
- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) / [SWTPC MP-C Serial Control Interface](MP-C%20Serial%20Control%20Interface.md) — the SS-30 interface boards.
- [SWTBUG Monitor](SWTBUG%20Monitor.md) / [MIKBUG Monitor](MIKBUG%20Monitor.md) — the firmware whose console occupies port 1.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
