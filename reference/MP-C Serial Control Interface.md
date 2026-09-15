# SWTPC MP-C Serial Control Interface

Source: [MP_C_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions — MP-C Serial, Control
Interface*, circa 1976; scanned/edited by Michael Holley, Feb 1 2001;
`https://deramp.com/downloads/swtpc/hardware/MP_C%20Serial,%20Control%20Interface/MP_C_AssemblyInstructions.pdf`).
The MP-C's address and register model are fixed by the firmware that drives it — see the
[MIKBUG](MIKBUG%20Monitor.md) listing and Engineering Note 100. Fetched 2026-09-13.

The **MP-C** is the SWTPC 6800's **control interface** — the serial terminal board that occupies
**I/O port 1** and is the board [MIKBUG](MIKBUG%20Monitor.md) was written for. Unlike the
[MP-S](MP-S%20Serial%20Interface.md) (which has an MC6850 ACIA doing the framing in hardware), the
MP-C is built around an **MC6820 PIA**: the monitor bit-bangs the serial line through PIA bits in
software-timed loops. It is a 5¼″ × 3½″ board, jumper-configurable for **110 baud (10 cps) or 300
baud (30 cps)**, RS-232C **or** 20 mA TTY (upper-case ASCII only — not Baudot). This file documents
its address/register model and, above all, why swtpcsim boots SWTBUG on an MP-S ACIA instead of
MIKBUG on an MP-C PIA.

## Address and register model — the emulation payload

Every SWTPC system needs exactly one control interface, and the motherboard reserves **port 1**
(`8004`–`8007`) for it (see [SS-50/SS-30 Bus](SS-50%20SS-30%20Bus.md)). The MP-C's MC6820 PIA fills
all four addresses — a PIA has two register-select inputs, wired to `RS0` = A0 and `RS1` = A1:

| Address | PIA register |
|---|---|
| `8004` | Port-A data / data-direction register (A). |
| `8005` | Port-A control/status register. |
| `8006` | Port-B data / data-direction register (B). |
| `8007` | Port-B control/status register. |

The monitor uses the port-A bit as the serial line and strobes the unused PIA lines for reader/
punch control. The MC6820 register model itself (the CRA/CRB bit-2 data/DDR select, the
CA1/CA2/CB1/CB2 handshake, power-on reset to all-inputs) follows the standard Motorola PIA.

## Serial characteristics

| Item | Value |
|---|---|
| Baud | 110 (10 cps) or 300 (30 cps), jumper `C`/`D` to the `110`/`300` pads. |
| Format | 110 baud: 1 start / 8 data / no parity / 2 stop. 300 baud: 1 stop bit. |
| Levels | RS-232C or 20 mA TTY (ASR-33). |
| Clock out (CO) | ≈16× baud: **1758.8 Hz** at 110 baud, **4800 Hz** at 300 baud. |
| Connector | The same 10-pin interface as the MP-S: gnd, CO, CI, RI, RO, TC, TO, RC, TI. |

RC (Reader Control) drives an automatic reader/punch relay on a TTY that lacks one; the monitor
strobes it through spare PIA lines for its `L`/`P`/`E` tape functions.

## Emulation notes

**swtpcsim does not ship an MP-C board.** The shipped `swtpc` machine uses SWTBUG driving an
[MP-S](MP-S%20Serial%20Interface.md) ACIA at the same port 1, for a concrete reason:

⚠ **A faithful MP-C console is a software UART tied to CPU timing.** With no ACIA, the monitor
generates and samples every bit in code — `INEEE`/`OUTEEE` toggle a PIA bit inside delay loops
whose counts assume the ~0.9 MHz E-clock, aided by an MC14536 one-shot timer. Emulating that
correctly needs cycle-accurate CPU timing *and* a modelled PIA + timer, versus the free-running
clock the shipped machine uses. SWTBUG detects an ACIA and lets hardware frame the characters, so
`machines/swtpc.toml` boots SWTBUG on an MP-S. (SWTBUG can still *drive* an MP-C — its console
probe recognizes a PIA and takes the bit-bang path — but the emulator provides the ACIA path.)

⚠ **The four-address PIA decode is the same mechanism the MP-S mirrors.** The MP-C genuinely uses
all four addresses (two register pairs); the MP-S ACIA only appears to, by mirroring A1. SWTBUG's
`[8004]` vs `[8006]` probe distinguishes the two — see the [MP-S](MP-S%20Serial%20Interface.md) and
[SWTBUG](SWTBUG%20Monitor.md) emulation notes.

## Related

- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) — the ACIA console the shipped machine uses instead.
- [MIKBUG Monitor](MIKBUG%20Monitor.md) — the firmware written for this PIA console.
- [SWTBUG Monitor](SWTBUG%20Monitor.md) — the shipped monitor, which can detect either an MP-C PIA or an MP-S ACIA.
- [SS-50 / SS-30 Bus](SS-50%20SS-30%20Bus.md) — the port-1 control slot this board occupies.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
