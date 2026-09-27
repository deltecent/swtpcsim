# SWTPC MP-T Interrupt Timer

Source: [MP_T_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions MP-T Interrupt
Timer*, circa 1976; scanned/edited by Michael Holley Oct 15 2000, rev. Aug 7 2002; 15 pp,
real text layer), [MP_T_Schematic150.pdf](#) (the board schematic), [MK5009P.JPG](#) (the
Mostek MK5009 block diagram page), and SWTPC's clock program [INTCLK.TXT](#) +
[Intclk_s1.txt](#) — all from
`https://deramp.com/downloads/swtpc/hardware/MP_T%20Timer%20Interface/`. Fetched 2026-09-27.

The **MP-T** is an SS-30 option board: a **6820 PIA** and a **Mostek MK5009** counter/time
base with a 1 MHz crystal. Half the PIA programs the MK5009 and takes its output as an
interrupt; the other half is a buffered eight-bit input port with handshake, "just like half
of a standard MP-L parallel interface board". +5 V from its own regulator (~0.3 A); ~15 mA
from −12 V for the MK5009.

## Address map — the emulation payload

Four addresses per SS-30 slot, `8000 + N·4` (see [SS-50 SS-30 Bus](SS-50%20SS-30%20Bus.md)).
The PIA's register selects are **RS0 = A0, RS1 = A1**, so the slot is the PIA's four
registers in the standard order:

| Offset | Register |
|---|---|
| +0 | Peripheral Register A / Data Direction Register A (CRA bit 2 selects) |
| +1 | Control Register A |
| +2 | Peripheral Register B / Data Direction Register B (CRB bit 2 selects) |
| +3 | Control Register B |

The manual gives no default slot. SWTPC's INTCLK uses **`8010`** (slot 4). ⚠ INTCLK's header
calls that "port #5"; with four addresses a slot, `8010` is slot 4 — the manual's own port
table (PORT4 = `8010`–`8013`) agrees.

## Side B — the time base

- **PB0–PB3** drive the MK5009's binary rate inputs 2⁰–2³.
- **PB7** drives **RESET 0**. Writing `80` "will reset the oscillator/divider so the count will
  stop, and everything will be prepared for an interval measurement" — the programmable
  stopwatch. Clearing PB7 starts the count from zero.
- The MK5009's **TIME OUT** goes to **CB1**. The manual: configure CRB so "the CB1 line will
  respond to the **negative going edge**". DDRB = all outputs; DDRA = all inputs.

The rate table, as the manual prints it (its heading says "PIA A data word"; it means B):

| Code | Interval | Code | Interval |
|---|---|---|---|
| `00` | 1 µs | `08` | 100 s |
| `01` | 10 µs | `09` | 1 min |
| `02` | 100 µs | `0A` | 1 hour |
| `03` | 1 ms | `0B` | 10 min |
| `04` | 10 ms | `0C` | no output |
| `05` | 100 ms | `0D` | no output |
| `06` | 1 s | `0E` | 20 ms |
| `07` | *(missing)* | `0F` | no output |

⚠ **The manual's table skips `07`.** The MK5009 block diagram lists the divider taps ÷10⁰
through ÷10⁸, ÷2×10⁴, ÷6×10⁷, ÷6×10⁸ and ÷36×10⁸. Every tap but **÷10⁷** is accounted for by
the codes the table does print, and `07` is the only code the table does not. At 1 MHz, ÷10⁷ is
**10 s**, and the manual's own introduction lists "10 sec" among the board's intervals. So
`07` = 10 s. This is a reading of the two sources together, not a printed fact.

**All the taps come from one synchronous divider chain** (the diagram's "SYNCHRONOUS
DIVIDERS") reset by RESET 0, so every rate keeps its phase from the moment RESET 0 last fell.
**Assumed, not printed:** the TIME OUT square wave starts low after reset, so its first falling
edge is one full period after the count starts, and a rising edge half a period before each
falling one. Nothing in the sources contradicts it, and it matches the stopwatch use the manual
describes.

⚠ **After RESET the PIA's output register B is `00`.** A program that writes `FF` to DDRB
before it writes `80` to PRB drives PB7 low and code `0` for the instructions in between — the
MK5009 runs at 1 µs, and if CRB already selects CB1's edge the flag is set. INTCLK does exactly
that (DDRB, then CRB = `3D`, then `80`, then `06`) and never reads PRB before its `CLI`, so its
first interrupt comes at once. Read PRB after starting the timer to clear it.

## Side A — the input port

- **I0–I7** → buffered, non-inverting → **PA0–PA7** (one TTL load each).
- **C1** → buffered → **CA1**: the "data ready" handshake input.
- **CA2** → buffered → **C2**: the "data accepted" handshake output (sources 5.2 mA, sinks
  32 mA).
- All on the 12-pin connector on the board's top edge. Configuring side A is left to "the
  Hardware and Programming sections of the System Documentation Notebook".

## Interrupts

Two jumpers, **TIM–IRQ** and **INP–IRQ**, connect the timer side's and the input side's PIA
interrupt outputs to the bus **IRQ**; the manual says to fit both unless the board is used
with **NMI**. On the SWTPC 6800, IRQ vectors through the monitor ROM to the word at
**`A000`/`A001`** in the scratchpad RAM, which the program must load with its service routine.
The manual also advises a **`NOP` before every `CLI`** (see
[6800 NOP before SEI](6800%20NOP%20before%20SEI.md)) and notes that the 6820 is reset by the
bus RESET, so an MP-T raises no interrupt while the monitor runs.

## INTCLK — the published clock program

`INTCLK.TXT` (source) and `Intclk_s1.txt` (S1 records) load at `0100` with messages at
`0020`–`006D`, and set **`A048` = `0100`**, the PC SWTBUG's `G` returns to. The ISR (`INTSER`,
`013D`) reads `2,X` (PRB, clearing the flag), adds a second, and redraws `HH:MM.SS` in 12-hour
form — the hour's leading zero is printed as a space.

⚠ **The published words at `A000` and `A002` are swapped.** The comments and the code
(`LDX $A002` / `LDA A 2,X`) want the ISR at `A000` and the board at `A002`, but both the
source's `ORG $A000`/`FDB` pair and the S1 record `S107A0008010013D…` put `8010` at `A000`, so
as published the first interrupt jumps into the board's registers. ⚠ **`Intclk_s1.txt`'s last
line runs a bare `S9` onto the end of that record.** The copy in the simulator's `examples/mpt/`
fixes both and changes nothing else.

⚠ **SWTBUG drops keys typed while INTCLK prints.** SWTBUG's output routine reads the ACIA's
receive register before each character (to tell an ACIA from a PIA), so a key that arrives
while the `HH:MM.SS` prompt is printing is lost. Type the time after the prompt.
