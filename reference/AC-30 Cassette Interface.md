# SWTPC AC-30 Cassette Interface — diagnostics

Source: [AC30_Diagnostics.pdf](#) (SWTPC *6800/CT-1024/AC-30 Cassette Tape Diagnostic Programs*,
circa 1976; scanned/edited by Michael Holley, Feb 19 2002; listings re-assembled with the TSC
6800 Assembler, opcodes identical to the originals;
`https://deramp.com/downloads/swtpc/hardware/AC30%20Cassette%20Interface/AC30_Diagnostics.pdf`).
Fetched 2026-09-13.

The **AC-30** is the SWTPC audio-cassette interface, sitting **between the CT-1024 terminal and
the 6800 computer** on the serial path (Kansas City / 300 baud). Unlike the Altair 680b KCACR,
the AC-30 is **not a memory-mapped board**: the computer drives it by **sending ASCII
device-control characters to the terminal**, which decodes them and forwards record/read
on/off to the interface. Motor start/stop is a hardware feature keyed off those commands plus
front-panel switches. This document is the three **diagnostic programs**, and the emulation
value is the **command bytes**, the **MIKBUG entry points**, and the **scratch-RAM load map**.

## Control model — the emulation payload

- **The interface is commanded by control characters output through the normal terminal path**
  (via MIKBUG's `OUTEEE`), not by writes to an I/O port. The explicitly labelled commands:

  | Byte | Meaning |
  |---|---|
  | `$11` (DC1) | **Record ON** / **Read ON** (turn the record or read logic on) |
  | `$14` (DC4, Ctrl-T) | **Record OFF** (also stops the recorder motor in AUTO) |

  The diagnostics also emit `$12`/`$13`/`$7F` in their setup/fill strings; treat the full device
  protocol as CT-1024-terminal-dependent and reproduce byte sequences verbatim rather than
  inferring a register map.

- **Read echo** is suppressed by writing the terminal control port: `LDA A #$3C / STA A $8007`
  (port-1 control register on an `$8000`-based I/O map) disables control-interface echo so tape
  data does not print during a read.

- **Motor control:** with the Motor Control switch in **AUTO**, record-on triggers an adjustable
  one-shot delay before "marking" carrier is laid down (lets the recorder reach speed). Software
  must add a matching start-up delay (the `DELAY` subroutine below) before recording data.

## MIKBUG entry points used

The diagnostics load into the MP-A monitor's 128-byte scratch RAM and call the ROM routines:

| Routine | Address | Function |
|---|---|---|
| `OUTEEE` | `$E1D1` | Output one character to the terminal (and thus the AC-30). |
| `INEEE`  | `$E1AC` | Input one character. |
| `PDATA1` | `$E07E` | Output a string. |

See [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) for the monitor and its RAM.

## The three programs (load into `$A000`–`$A07F` scratch RAM)

- **TAPWRT-1** — test-tape generator. Writes ASCII `$40`–`$60` (`@A…_`) repeatedly, then a
  Ctrl-T (`$14`) to stop the record logic/motor. Loaded in two parts (`A014`–`A033`,
  `A048`–`A06B`) to avoid the monitor's stack. Delay constant at **`A01B`** (≈0.2 s × value;
  default `$0F` ≈ 3.2 s). For a **continuous** (non-incremental) tape, patch `A01C`/`A01D` to
  `$01` (NOP) to remove the software delays. Restart at `A014`.
- **TAPRED-1** — read/verify. Turns read on, hunts for `@`, then checks each following character
  in order; prints `/` per good block, `X` on any mismatch (including the Ctrl-T that ends each
  block). Loaded `A048`–`A07E`; restart at `A04A`. Consistent errors on reread = record error;
  inconsistent = read error.
- **FIVPNT-1** — calibration-tape generator. Writes a continuous string of ASCII `5` (`$35`,
  bit pattern `0011 0101` — a good alternating pattern) with a rubout (`$7F`) between fives for
  resync; string bytes `$13,$12,$7F,$35,$04`. Read it back in **LOCAL** mode and adjust trimmer
  **R16** midway between the errored-read settings; correct calibration reads a solid stream of
  `5`s. Restart at `A04F`.

Kansas City Standard, **300 baud**, RS-232 300-baud terminal. Programs are started via MIKBUG's
"Go to User's Program" and stopped only by **RESET** (then re-point the PC per "Display contents
of MPU Registers"), per Engineering Note 100.

## Related

- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) — the MIKBUG monitor, its scratch RAM
  (`A000`–`A07F`), and the entry points these diagnostics call.
- [Altair 680b KCACR](Altair%20680b%20KCACR.md) — the *other* 6800-family cassette interface, for
  contrast: memory-mapped (`F010`/`F011`) with software motor control, where the AC-30 is
  command-character driven through the terminal.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
