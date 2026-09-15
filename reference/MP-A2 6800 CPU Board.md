# SWTPC MP-A2 6800 Microprocessor/System Board

Source: [MP_A2_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions — MP-A2
Microprocessor/System Board*, circa 1978; scanned/edited by Michael Holley, Sept 21 2000, rev.
Mar 29 2004;
`https://deramp.com/downloads/swtpc/hardware/MP_A2%206800%20CPU%20Board/MP_A2_AssemblyInstructions.pdf`).
Fetched 2026-09-13.

The **MP-A2** is the second-generation SWTPC 6800 processor board, superseding the
[MP-A](MP-A%206800%20CPU%20Board.md). It is the same essential machine — an **MC6800** MPU, an
MC6830 mask ROM for the monitor, an MC6810 128-byte scratchpad, and an MC14411 baud generator —
but built for **2716 EPROMs**: it adds sockets for up to 8 K of Intel-2716-pinout PROM with
relocatable addressing, a switch to pick MIKBUG or SWTBUG, and address-select switches for
external memory above 40 K. It buffers all 16 address and 8 data lines onto the SS-50 bus. This
file records what differs from the MP-A and what the emulator must reflect (chiefly the SWTBUG
crystal and the PROM windows); the shared MPU/scratchpad/monitor detail is in the
[MP-A](MP-A%206800%20CPU%20Board.md) reference.

## What the MP-A2 adds

| Feature | Detail |
|---|---|
| 2716 PROM sockets | IC23–IC26, four 2 K windows: `0`–`2K`, `2`–`4K`, `4`–`6K`, `6`–`8K` addressed. Relocatable for a custom monitor or dedicated controller. |
| MIKBUG **or** SWTBUG | IC2 = MC6830 `L7`/`L8`/`P8` (MIKBUG) or the SWTBUG mask; DIP switch **S1** (7 used sections) configures which and where memory decodes. |
| Dual +5 regulators | IC21/IC22, for heat; ~1 A total. |
| Adjustable CPU clock | Resistor-set clock speed. |
| External-memory selects | S1 can switch the entire 64 K external above 40 K. |

## The two crystals — the emulation payload

`XTAL1` differs by monitor, and it sets the CPU/E-clock rate:

| Monitor | Crystal | Notes |
|---|---|---|
| MIKBUG | **1.7971 MHz** | The original MP-A rate. |
| SWTBUG | **1.8432 MHz** | SWTBUG's baud timing wants the 1.8432 MHz part. |

The E (bus) clock is the crystal ÷2, so ≈**0.899 MHz** (MIKBUG) or ≈**0.922 MHz** (SWTBUG) — both
just under the nominal "1 MHz 6800." A user who wants period-accurate timing sets the CPU board's
`clock_hz` accordingly; the shipped `swtpc` machine runs flat out by default.

## Memory map (S1 = default, monitor high)

The 6800's 64 K is split RAM / ROM / PROM / I/O (the MP-A2's map):

| Region | Address | Contents |
|---|---|---|
| System RAM | `0000`–`7FFF` | Up to 32 K (4 K MP-M or 8 K MP-8M blocks). |
| I/O window | `8000`–`801F` | The eight SS-30 ports (fixed on the motherboard). |
| Scratchpad RAM | `A000`–`A07F` | Monitor variables/stack (first 128 bytes of the 40–48 K block). |
| LOW PROM | `C000`–`DFFF` | 2716 window (2/4/6/8 K), starting at 48 K. The target of SWTBUG's `Z` command. |
| Monitor ROM | `E000`–`E1FF` (SWTBUG `–E3FF`) | MIKBUG or SWTBUG. |
| HIGH PROM | `E000`–`FFFF` | Custom-monitor window (2/4/8 K increments); with a HIGH PROM the reset/interrupt vectors must live in the highest-addressed PROM. |

> ⚠ **No RAM above 56 K (`E000`).** The monitor (or a HIGH PROM) occupies the top of memory and
> the board's decode forbids intermixing RAM and PROM/ROM in the same 8 K segment above `8000`.

## Emulation notes

swtpcsim does not model the MP-A2 as its own board — the generic `6800` CPU board plus the
`memory` board's regions cover what the shipped `swtpc` machine needs. This reference is the
authority for two things that *are* configuration:

- **The SWTBUG crystal is 1.8432 MHz** (E ≈ 0.922 MHz). `machines/swtpc.toml` boots SWTBUG, so a
  period-accurate run uses `SET cpu0 clock_hz=922000` (or the exact ÷2 of 1.8432 MHz) rather than
  the MIKBUG rate.
- **The LOW-PROM window is `C000`.** SWTBUG's `Z` command (`J C000`) assumes a PROM there; the
  shipped machine leaves it empty. A dedicated-controller machine would mount a `builtin:` PROM at
  `C000` (LOW) or across `E000`–`FFFF` (HIGH, carrying its own vectors) exactly as the MP-A2's
  sockets allow.

## Related

- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) — the first-generation board; the shared MPU/scratchpad/monitor detail.
- [SWTBUG Monitor](SWTBUG%20Monitor.md) / [MIKBUG Monitor](MIKBUG%20Monitor.md) — the two firmwares S1 selects between.
- [SS-50 / SS-30 Bus](SS-50%20SS-30%20Bus.md) — the buses this board drives.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
