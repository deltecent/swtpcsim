# SWTPC SWTBUG Monitor ROM (v1.0)

Source: [SWTBUG_Users_Guide.pdf](#) (SWTPC *SWTBUG 6800 ROM Monitor — Version 1.0, Users
Guide*, SWTPC document circa 1977; scanned/edited by Michael Holley, Sept 17 2000, rev.
Aug 6 2002; kept in [`roms/SWTBUG/`](../roms/SWTBUG/)). The ROM image and its assembly
listing come from deramp.com
(`https://deramp.com/downloads/swtpc/software/SWTBUGA/`): `SWTBUGA.S19` (the S-record) and
`SWTBUGA.ASM` (the SWTPC source, retained beside the image as
[`roms/SWTBUG/SWTBUG.ASM`](../roms/SWTBUG/SWTBUG.ASM)). Fetched 2026-09-13.

**SWTBUG** is the ROM-resident monitor ("mini-operating system") of the SWTPC 6800 computer —
SWTPC's 1977 replacement for Motorola's [MIKBUG](MIKBUG%20Monitor.md), from which it keeps every
MIKBUG-compatible entry point but adds breakpoints, a byte search, vectored I/O, a floppy boot,
and the `$` prompt (MIKBUG printed `*`). It is a **1 KB 2716** at `E000`–`E3FF` with a 128-byte
scratchpad RAM at `A000`–`A07F`, both physically on the [MP-A](MP-A%206800%20CPU%20Board.md) /
[MP-A2](MP-A2%206800%20CPU%20Board.md) processor board. It is the firmware
[`machines/swtpc.toml`](../machines/swtpc.toml) boots (as **`builtin:swtbug`**), and as of
Stage 2 the emulator reaches its `$` prompt over the [`mps`](MP-S%20Serial%20Interface.md)
console. This file documents the monitor's memory footprint, commands, entry points, and the
handful of behaviors an emulator must honor; the MC6800 CPU model it runs on is in the
[Motorola M6800 Programming Reference Manual](Motorola%20M6800%20Programming%20Reference%20Manual.md).

## Memory footprint — the emulation payload

| Region | Address | Contents |
|---|---|---|
| Monitor ROM | `E000`–`E3FF` | The 1 KB SWTBUG program. |
| Reset/interrupt vectors | `E3F8`–`E3FF` | In-ROM vector table (see below), **mirror-decoded** so the 6800's `FFF8`–`FFFF` fetch reads them. |
| Scratchpad RAM | `A000`–`A07F` | The monitor's 128-byte variable/stack area (MC6810 on the CPU board). |
| Console I/O | `8004`–`8007` | I/O port 1, the control-interface slot (an [MP-S](MP-S%20Serial%20Interface.md) ACIA or an [MP-C](MP-C%20Serial%20Control%20Interface.md) PIA). |

On reset control enters **`START` at `E0D0`**, which sets the stack at `A042`, probes the port-1
device, master-resets it if it is an ACIA, and prints CR / LF / `$`. The `$` is the command
prompt (the `MCL` string at `E19D` = `0D 0A 15 00 00 00 '$' 04`: CR, LF, a CT-1024 erase-to-end
control, three nulls, `$`, and the `04` that terminates a `PDATA1` string).

### Reset/interrupt vectors (`E3F8`–`E3FF`)

| Vector | Address | Points to | Effect |
|---|---|---|---|
| IRQ | `E3F8`/`E3F9` | `IRQV` (`E000`) | Jumps through `[A000]` (the `IRQ` scratchpad pointer). |
| SWI | `E3FA`/`E3FB` | `SFE` (`E18B`) | Jumps through `[A012]` (`SWIJMP`); default is the register-dump/breakpoint handler. |
| NMI | `E3FC`/`E3FD` | `NMIV` | Jumps through `[A006]` (the `NMI` scratchpad pointer). |
| RESET | `E3FE`/`E3FF` | `START` (`E0D0`) | Power-up/RESET entry. |

The interrupt vectors indirect through scratchpad RAM, so a user program points them by storing
an address: IRQ ← `[A000]`, NMI ← `[A006]`, SWI ← `[A012]`. On the real board the 1 KB ROM is
decoded across the whole top 8 K, which is why the CPU's `FFF8`–`FFFF` fetch lands on the ROM's
`E3F8`–`E3FF` bytes.

## Commands

Each command is a single letter typed at the `$` prompt (`TABLE` at `E3D1`):

| Cmd | Syntax | Function |
|---|---|---|
| `M` | `M addr` | Memory examine/change. Prints `addr data`; then a hex value changes it, space/CR advances, `^` steps back, CR (or any non-hex) exits. |
| `R` | `R` | Register dump — `CC B A IX PC SP` from the target stack. |
| `G` | `G` | Go to user program via `RTI`; start address from `A048`/`A049`. Also resumes after a breakpoint. |
| `J` | `J addr` | Jump to `addr` (stack set at `A042`; `A048`/`A049` untouched). |
| `L` | `L` | Load a Motorola S1/S9 tape (ASCII) through the console; `?` on a checksum or store-verify failure. |
| `P` | `P` | Punch memory `A002`–`A005` (begin/end) as an S1 tape. |
| `E` | `E` | End-of-tape — punch the PC (`A048`/`A049`) and an `S9`. |
| `B` | `B addr` | Set/move a software breakpoint (writes `3F` = SWI). |
| `C` | `C` | Clear a CT-1024 screen (home-up + erase-to-frame). |
| `D` | `D` | Boot a SWTPC MF-68 mini-floppy (see the trap below). |
| `F` | `F hi lo bb` | Byte search: report every address in `hi`…`lo` holding byte `bb` (no spaces; high address first). |
| `O` | `O n` | Select an alternate port for `P`/`E`/`L` (an MP-C on port 0). |
| `Z` | `Z` | Jump to PROM at `C000` (equivalent to `J C000`). |

## Scratchpad variables (`A000`–`A047`)

| Addr | Name | Use |
|---|---|---|
| `A000` | `IRQ` | IRQ service pointer. |
| `A002` | `BEGA` | Punch begin address. |
| `A004` | `ENDA` | Punch end address (also byte-search top). |
| `A006` | `NMI` | NMI service pointer. |
| `A008` | `SP` | Saved target stack pointer. |
| `A00A` | `PORADD` | Active I/O port address. |
| `A00C` | `PORECH` | Echo on/off flag. |
| `A012` | `SWIJMP` | SWI service pointer (`E124` when no breakpoints are set, `E123` when they are). |
| `A014`–`A016` | `BKPT`/`BKLST` | Breakpoint address + saved byte. |
| `A042` | `STACK` | The monitor's stack top. |
| `A048`/`A049` | — | `G`-command start address (target PC). |

## MIKBUG-compatible entry points

SWTBUG keeps [MIKBUG](MIKBUG%20Monitor.md)'s published addresses so MIKBUG-era software calls the
same locations:

| Addr | Routine | Function |
|---|---|---|
| `E047` | `BADDR` | Input a 4-hex-digit address into the index register. |
| `E055` | `BYTE` | Input one byte (two hex frames). |
| `E07E` | `PDATA1` | Output a string until a `04` byte. |
| `E0C8` | `OUT4HS` | Output the 16-bit value at `[X]` as 4 hex + space. |
| `E0CA` | `OUT2HS` | Output the byte at `[X]` as 2 hex + space. |
| `E0D0` | `START` | Power-up/RESET entry. |
| `E0E3` | `CONTRL` | Re-enter the monitor command loop (a user program's tidy exit: `JMP $E0E3` / `7E E0 E3`). |
| `E1AC` | `INEEE` | Input one character from the console (7-bit). |
| `E1D1` | `OUTEEE` | Output one character to the console (8-bit). |

## Emulation notes

`machines/swtpc.toml` models SWTBUG as `builtin:swtbug`: the 1 KB image mounted at `E000`, a
second **relocated** copy of the same image mounted at `FC00` (so `FC00 + 0x3F8 = FFF8` carries
the vectors to the top of memory), 128 bytes of RAM at `A000`, general RAM below `8000`, and an
[`mps`](MP-S%20Serial%20Interface.md) ACIA console at `8004`. `startup = ["RESET", "RUN"]` boots
it; under `--mcp`, which does not honor a pending RESET, boot from **`E0D0`** (the `START` entry,
57552 decimal) directly.

⚠ **The console self-configuration depends on the ACIA answering `8006` as a mirror of `8004`.**
`START` calls `PIAINI` and then compares `[8004]` with `[8006]` (`LDAA 0,X` / `CMPA 2,X`): equal
→ it treats the port as an MC6850 ACIA and master-resets it; unequal → it treats it as an MC6820
PIA and bit-bangs the line. The [`mps`](MP-S%20Serial%20Interface.md) board decodes all four slot
addresses with A1 ignored so `8006`/`8007` mirror `8004`/`8005` — this is exactly what the probe
needs; decode only `8004`/`8005` and SWTBUG takes the PIA path, never resets the ACIA, and the
console emits garbage. See the MP-S emulation note.

⚠ **The single `BEL` at power-up is faithful, not a fault.** `PIAINI` stores `7` in the port's
data register while configuring it, and an ACIA transmits it.

⚠ **The `D` (disk boot) command targets the SWTPC MF-68 mini-floppy, not the DC-4.** It clears
`8014`, reads sectors through `8018`/`801B`, loads to `2400`, and `JMP $2400`. That controller is
out of scope until the DC-4/FLEX work (Stage 3); on the shipped `swtpc` machine `D` finds no
controller and returns to the prompt.

⚠ **`Z` jumps to `C000`.** On an [MP-A2](MP-A2%206800%20CPU%20Board.md) that is the LOW-PROM
window; the shipped machine has nothing there, so `Z` simply transfers to whatever `C000` holds.

## Related

- [MIKBUG Monitor](MIKBUG%20Monitor.md) — the Motorola predecessor whose entry points SWTBUG keeps.
- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) / [SWTPC MP-A2 6800 CPU Board](MP-A2%206800%20CPU%20Board.md) — the boards that carry this ROM and its scratchpad RAM.
- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) — the ACIA console (`8004`/`8005`) SWTBUG detects and drives.
- [SWTPC MP-C Serial Control Interface](MP-C%20Serial%20Control%20Interface.md) — the alternative PIA console SWTBUG can also drive.
- [SS-50 / SS-30 Bus](SS-50%20SS-30%20Bus.md) — the bus whose port-1 slot the console occupies.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
