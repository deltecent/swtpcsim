# Motorola MIKBUG / MINIBUG ROM (MCM6830L7)

Source: [MikbugEn100.pdf](#) (Motorola *Engineering Note 100 — MCM6830L7 MIKBUG/MINIBUG ROM*,
by Mike Wiles & Andre Felix; scanned/edited by Michael Holley, Oct 21 2000;
`https://deramp.com/downloads/swtpc/hardware/MP_A%206800%20CPU%20Board/MikbugEn100.pdf`). The
Rev 9 assembly listing is `mikbug.txt` in the same directory
(`.../MP_A%206800%20CPU%20Board/mikbug.txt`). Fetched 2026-09-13.

**MIKBUG** is Motorola's original 6800 monitor (a trademark of Motorola, ©1974), the firmware
SWTPC's [MP-A](MP-A%206800%20CPU%20Board.md) shipped before [SWTBUG](SWTBUG%20Monitor.md) replaced
it. It lives in an **MCM6830L7** mask ROM that actually holds *three* programs — **MIKBUG Rev 9**
(`E000`–`E1FF`, 512 bytes), **MINIBUG Rev 4** (`FE00`–`FEFF`, 256 bytes), and a **Test Pattern**
(`EE00`–`EEFF`, 256 bytes) — selected by which addresses the board decodes. This file documents it
as the ancestor of the shipped SWTBUG monitor and as the origin of the entry-point addresses
period software calls; **swtpcsim does not ship a MIKBUG machine** (see the emulation note for
why). The CPU model is in the
[Motorola M6800 Programming Reference Manual](Motorola%20M6800%20Programming%20Reference%20Manual.md).

## Memory map — MIKBUG Rev 9

| Region | Address | Contents |
|---|---|---|
| Console PIA (MC6820) | `8004`–`8007` | `8004` data-A, `8005` control/status-A, `8006` data-B, `8007` control/status-B. |
| Scratchpad RAM (MC6810) | `A000`–`A07F` | Monitor variables and stack. |
| MIKBUG ROM | `E000`–`E1FF` | The 512-byte monitor. |
| Restart mirror | `FFF8`–`FFFF` | The ROM also answers the top addresses so the vector fetch reads its table. |

The ROM's enable inputs are active-high and A9 is grounded, so it responds at `E000`–`E1FF`
*and* at `FFF8`–`FFFF`. Engineering Note 100 is emphatic that **no device may sit above the ROM's
addresses**, or the restart fetch breaks. The vectors at the end of the image are, in order,
`IO` (IRQ), `SFE` (SWI), `POWDWN` (NMI), `START` (RESET); IRQ indirects through `[A000]` and NMI
through `[A006]`.

## Commands

MIKBUG Rev 9 prompts with `*` (SWTBUG later changed this to `$`). One letter each:

| Cmd | Function |
|---|---|
| `L` | Load a Motorola S1/S9 tape. |
| `M` | Memory examine/change (space to change, CR to close). |
| `R` | Display the target registers — `CC B A XH XL PH PL SH SL`. |
| `P` | Print/Punch memory `A002`–`A005` as an S1 tape. |
| `G` | Go to the user program via `RTI`. |

> ⚠ The Rev 9 source header comment lists the punch command as `F`, but the command-loop code
> actually matches `'P'` (`CMP B #'P` → `PUNCH`). The operative character is **`P`**; the `F` in
> the comment is a listing error. (SWTBUG reassigned `F` to its byte-search command.)

## Selected entry points (Rev 9)

These are the addresses [SWTBUG](SWTBUG%20Monitor.md) deliberately preserved:

| Addr | Routine | Function |
|---|---|---|
| `E047` | `BADDR` | Input a 4-hex-digit address. |
| `E055` | `BYTE` | Input one byte (two hex frames). |
| `E07E` | `PDATA1` | Output a string until a `04` byte. |
| `E0C8` | `OUT4HS` | Output `[X]` as 4 hex + space. |
| `E0CA` | `OUT2HS` | Output `[X]` as 2 hex + space. |
| `E0E3` | `CONTRL` | Re-enter the command loop. |
| `E1AC` | `INEEE` | Input one character. |
| `E1D1` | `OUTEEE` | Output one character. |

## MINIBUG Rev 4

The second program in the same ROM, at `FE00`–`FEFF`. It assumes a hardware **UART** rather than
a bit-banged PIA (status at `FCF4`, data at `FCF5`), uses RAM at `FF00`, restarts through a
separate address generator at `FFFE`/`FFFF`, and offers a reduced command set (`L` load, `M`
memory, `R` registers, `G` go). It is documented here for completeness; it is not an SWTPC
configuration.

## Emulation notes

**swtpcsim ships [SWTBUG](SWTBUG%20Monitor.md), not MIKBUG.** The two are entry-point compatible,
but MIKBUG's console is fundamentally different and hard to emulate faithfully:

⚠ **MIKBUG bit-bangs its serial line through an MC6820 PIA in software.** There is no ACIA doing
the framing — `INEEE`/`OUTEEE` toggle a PIA bit inside timing loops (`DEL`/`DE`) whose delays are
calibrated to the CPU clock, with an MC14536 one-shot as the interface timer. A faithful MIKBUG
console would therefore need cycle-accurate CPU timing *and* a modelled MP-C PIA plus its timer —
not the free-running clock the shipped machine uses. SWTBUG sidesteps all of this by auto-detecting
the [MP-S](MP-S%20Serial%20Interface.md) ACIA and letting hardware do the framing, which is why
`machines/swtpc.toml` boots SWTBUG. See [MP-C](MP-C%20Serial%20Control%20Interface.md) for the PIA
console board itself.

⚠ **MIKBUG's PIA console still relies on the port answering all four slot addresses** (`8004`–
`8007` = the two PIA register pairs). That is the same four-address SS-30 decode SWTBUG's ACIA
probe leans on — the mechanism is shared even though the chip differs. See the
[SS-50/SS-30 Bus](SS-50%20SS-30%20Bus.md) reference.

## Related

- [SWTBUG Monitor](SWTBUG%20Monitor.md) — SWTPC's 1977 successor, the monitor swtpcsim ships.
- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) — the board carrying the MIKBUG ROM and its scratchpad.
- [SWTPC MP-C Serial Control Interface](MP-C%20Serial%20Control%20Interface.md) — the PIA console board MIKBUG bit-bangs.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
