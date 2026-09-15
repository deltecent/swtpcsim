# SWTPC MP-A 6800 Microprocessor/System Board

Source: [MP_A_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions MP-A Microprocessor/
System Board*, circa 1976; scanned/edited by Michael Holley, Oct 1 2000, rev. May 30 2002;
`https://deramp.com/downloads/swtpc/hardware/MP_A%206800%20CPU%20Board/MP_A_AssemblyInstructions.pdf`).
The MP-A directory also holds the schematic, SWTBUG/FLEX mods, MIKBUG notes (`MikbugEn100.pdf`,
`mikbug.txt`), the high-baud mod, and system-checkout instructions:
`https://deramp.com/downloads/swtpc/hardware/MP_A%206800%20CPU%20Board/`. Fetched 2026-09-13.

The **MP-A** is the primary logic board of the SWTPC 6800 — a 5½″ × 9″ card carrying the
**MC6800 MPU** and everything around it: the ROM-resident mini-operating system, its scratch
RAM, the clock, and the baud-rate generator. It buffers all 16 address and 8 data lines out to
the **MP-B** motherboard through a 50-pin connector. On-board +5 V regulator (IC17), ~0.8 A
typical.

## Board contents and the memory map — the emulation payload

| Part | Device | Role / address |
|---|---|---|
| IC1 | **MC6800** | The MPU. |
| IC2 | **MC6830L7** ROM (1024 × 8) | Stores the **mini-operating system** (Motorola **MIKBUG**). Only the **lower 512 words are used**; the upper 512 are disabled. **Mapped `E000`–`E1FF`.** |
| IC3 | **MC6810** RAM (128 × 8) | The monitor's **scratchpad RAM**. Small programs (e.g. the cassette diagnostics) load directly into this RAM without any MP-M memory card — the **`A000`–`A07F`** scratch region. |
| IC4 | **MC14411** | Crystal-controlled **baud-rate generator**: produces the five clock frequencies for the serial/control interfaces. Serial ports run **110/150/300/600/1200**; the control interface runs **110 or 300**. |
| IC11 | **555 (1455)** timer | Generates the **power-up and manual-pushbutton RESET** that loads the ROM operating system. |
| IC20 | 7474 D flip-flop (½) | Timed **processor halt for DMA**. |
| IC5–IC7 | DM8097 | Non-inverting address-line buffers (16 lines). |
| IC8, IC9 | DM8835 | Inverting bidirectional data-bus transceivers (8 lines). |
| XTAL1 | **1.7971 MHz** parallel-resonant crystal | The processor clock source (via IC4). |

On power-up or front-panel **RESET**, the machine jumps into the ROM firmware at `E000`, which
takes terminal control. The mini-OS needs only its 128-byte 6810 RAM to run, which is why
diagnostics can load into `A000`–`A07F` on a bare machine.

> ⚠ **6800 reset/interrupt vectors:** this assembly manual states the ROM answers at
> `E000`–`E1FF` but does **not** describe how the `FFF8`–`FFFF` vector region is decoded. The
> MCM6830L7 MIKBUG ROM is known to also supply those vectors, but do not assume specifics from
> this document — confirm against the schematic / MIKBUG listing before wiring vector decode.

## MIKBUG monitor entry points

The ROM firmware is Motorola **MIKBUG** (a registered trademark of Motorola). Its terminal
service routines — used by period software and named in the AC-30 diagnostics — live in the ROM
page:

| Routine | Address | Function |
|---|---|---|
| `INEEE` | `$E1AC` | Input one character from the control terminal. |
| `PDATA1` | `$E07E` | Output a string (terminated per the monitor's convention). |
| `OUTEEE` | `$E1D1` | Output one character to the control terminal. |

The MIKBUG operator functions ("**Go to User's Program**", "**Display contents of MPU
Registers**", memory-change, etc.) are documented in Motorola's **Engineering Note 100**
(`MikbugEn100.pdf` in the source directory). Later SWTPC machines shipped **SWTBUG** in place of
MIKBUG (`MP_A_SWTBUG_Mods.pdf`); **FLEX** and high-baud mods are also in the directory.

## Related

- [Motorola M6800 Programming Reference Manual](Motorola%20M6800%20Programming%20Reference%20Manual.md)
  — the MC6800 CPU/instruction reference for the MPU on this board.
- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) — the serial board (port 1,
  `$8004`/`$8005`) MIKBUG's terminal routines talk to.
- [SWTPC AC-30 Cassette Interface](AC-30%20Cassette%20Interface.md) — cassette diagnostics that
  load into this board's `A000`–`A07F` scratch RAM and call the MIKBUG routines above.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
