# S-BUG — the SWTPC 6809 ROM Monitor

Sources, all from deramp.com `https://deramp.com/downloads/swtpc/hardware/MP_09%206809%20CPU%20Board/`,
fetched 2026-09-27:

- [SBUG_UsersGuide.pdf](#) — SWTPC *6809 SBUG-E Monitor ROM Version 1.5* user's guide, circa
  1980; scanned/edited by Michael Holley, Nov 11 2000.
- [sbug_src.txt](#) — the assembly source of **S-BUG 1.8** (`NAM SBUG18 MP-09 MONITOR`), as
  commented by Allen Clark and Wallace Watson, and "modified to SBUG09 ver 1.8 by Randy Jarrett".
- [SBUG_Listing.pdf](#) — the assembled listing.
- [SBUG_S1.TXT](#) — the ROM image in Motorola S1 records (`:SWTPC S-BUG 1.8` header line).
- [SBUG_Index.htm](#) — the directory's index page.

S-BUG is the monitor in socket IC4 of the [MP-09](MP-09%206809%20CPU%20Board.md). It is to the
6809 what [SWTBUG](SWTBUG%20Monitor.md) is to the 6800: the prompt the machine comes up to, and
the disk bootstrap. "It has been designed to work in a SWTPC MP-09 processor board. It will not
work in other processor boards." It requires "an **MP-S interface installed in I/O port 1**, at
least **4K of RAM** memory installed at any address at or below address `D000` (52K) and an
MP-B or MP-B2 motherboard patched to address **I/O devices at 56K (`E000` hex)**."

**The image is version 1.8. The user's guide describes version 1.5.** Where they differ, the
1.8 source and image win: they are what ships.

## The image

| | |
|---|---|
| Size | **2048 bytes**, one 2716 |
| Range | **`F800`–`FFFF`** (the S-records place it there) |
| Records | 64 × S1, all checksums good. No S9. The first line, `:SWTPC S-BUG 1.8`, is a title, not a record. |
| CRC32 | **`10A045A7`** (of the 2048 bytes `F800`–`FFFF`) |
| `FF79` | `F1` — the byte the guide's "run in a 6800 mainframe" patch changes to `F7` (see *I/O at 8000* below) |

Vectors, as stored at `FFF0`–`FFFF`:

| Vector | Address | Handler |
|---|---|---|
| (reserved) `FFF0` | `FFB2` | `V1` → `JMP [STACK]` (`DFC0`, USER-V) |
| SWI3 `FFF2` | `FFC6` | `SWI3E` — the supervisor-call dispatcher |
| SWI2 `FFF4` | `FFB6` | `V2` → `JMP [SWI2]` (`DFC4`) |
| FIRQ `FFF6` | `FFBA` | `V3` → `JMP [FIRQ]` (`DFC6`) |
| IRQ `FFF8` | `FFBE` | `V4` → `JMP [IRQ]` (`DFC8`) |
| SWI `FFFA` | `FFC2` | `V5` → `JMP [SWI]` (`DFCA`) |
| NMI `FFFC` | `FFB2` | `V1` → `JMP [STACK]` |
| RESET `FFFE` | **`FF00`** | `START` |

## Reset: DAT, RAM search, sign-on

`START` (at `FF00`) runs entirely from the top page, before any RAM is known:

1. **Load the DAT with the identity map.** `LDX #IC11` (`FFF0`), then store `$F`, `$E`, … `$1`
   to `FFF0`–`FFFE`. (The loop exits on `A = 0`, so it writes fifteen entries. The next
   instruction stores `$F0` to `FFFF`.) The complement of each segment number, low nibble only,
   exactly as the MP-09 manual's identity table.
2. **Find the top RAM block.** Starting at logical `D0A0`, save the word, write the pattern
   `$55AA`, and compare. Step down 4K until it matches. If it passes `00A0` without a match,
   restart. The first match's segment is made logical `D` (`STA $FFFD`, complemented), and the
   stack goes to `DFC0`. So **S-BUG's RAM (`DFC0`–`DFFF`) is always at logical `D`**.
3. **Map every other 4K block of RAM.** Probe each lower segment the same way, and build a
   16-byte table `LRARAM` at `DFD0`–`DFDF` of complemented segment numbers. `0` marks no RAM.
   Entry E is set to **`$F1`** (I/O: physical `E`) and entry F to **`$F0`** (physical `F`).
   The upper nibbles are "destined for IC8 and mem expansion" and are ignored by a 4-bit DAT.
4. **Put a block at `C000`**, for the DOS: if logical `C` has no RAM, the next lower block found
   is moved there.
5. **Compress** the rest to be contiguous from logical `0000` up.
6. Copy the 16 table bytes into the DAT (`FFF0`–`FFFF`), turn on echo, and go to `MONITOR`.

With **56K of physical RAM at `0000`–`DFFF`**, every step leaves the map as the identity.

The source's own worked example (48K, with no RAM at physical `8000`): table
`0F 0E 0D 0C 0B 0A 09 08 06 05 00 00 04 03 F1 F0` — logical `8000` is physical `9000`, and so on.
**An unmapped logical segment holds `0`, which translates to physical `F`.**

`MONITOR` (the `F800` entry) then:

- copies the eight RAM vectors to `DFC0`–`DFCF` (IRQ/FIRQ/SWI2/SWI3/USER → an `RTI`; SWI → the
  breakpoint handler; SVC origin/limit `FFFF`)
- sets `CPORT` (`DFE0`) to **`E004`** — the console ACIA's control/status address
- clears the breakpoints, and builds a dummy register frame with `CC = $D0`
- **initializes the ACIA**: master reset (`03`), then **`11`** = ÷16, 8 data bits, 2 stop bits,
  no parity; reads the data register once to clear it
- prints the sign-on and the RAM total, then enters `NEXTCMD`

## Console output

The sign-on (message `MSG1` + the RAM total + `MSG2`):

```
<NUL NUL NUL CR LF NUL NUL NUL>S-BUG 1.8 - 56K<CR LF NUL NUL NUL>
>
```

The total is counted in BCD (4K per mapped block, logical `0`–`D`), so a 56K system prints
**`56K`**. **The prompt is `>`**, printed by `PSTRNG` after a CR/LF and three NULs. Commands
echo as typed. A control-key command echoes as `^` plus the letter.

Console I/O goes through `CPORT`: `INCH` polls status bit 0 (RDRF), `OUTCH` polls status bit 1
(TDRE), and the data register is at `CPORT+1` (`E005`).

## RAM use

S-BUG reserves **`DFC0`–`DFFF`**:

| Address | Name | Use |
|---|---|---|
| `DFC0` | `STACK` | Top of the monitor's stack; also USER-V |
| `DFC2` | `SWI3` | SWI3 vector |
| `DFC4` | `SWI2` | SWI2 vector |
| `DFC6` | `FIRQ` | FIRQ vector |
| `DFC8` | `IRQ` | IRQ vector |
| `DFCA` | `SWI` | SWI vector (breakpoints) |
| `DFCC` | `SVCVO` | Supervisor-call table origin (`FFFF` = none) |
| `DFCE` | `SVCVL` | Supervisor-call table limit |
| `DFD0`–`DFDF` | `LRARAM` | The logical→real table: a copy of what the DAT holds |
| `DFE0` | `CPORT` | Console port address (re-vectorable) |
| `DFE2` | `ECHO` | Echo flag |
| `DFE3`–`DFFA` | `BPTBL` | Breakpoint table (5 entries) |

(⚠ The user's guide prints the first five vector addresses as `D8C0`–`D8C8`. That is a typo
for `DFC0`–`DFC8`: the source and the guide's own later paragraphs agree on `DFCx`.)

## Entry table (`F800`)

Called indirectly (`JSR [$F806]`). "It is inadvisable to call directly into the ROM routines."

| Addr | Name | Function |
|---|---|---|
| `F800` | `MONITOR` | Re-enter; re-init vectors and console, sign on |
| `F802` | `NEXTCMD` | Prompt for a command (no stack reset) |
| `F804` | `INCH` | Read a character into A (8 bits, no echo) |
| `F806` | `INCHE` | Read, mask to 7 bits, echo |
| `F808` | `INCHEK` | Z clear if a character is ready |
| `F80A` | `OUTCH` | Write A |
| `F80C` | `PDATA` | Print the string at X, ended by `04` |
| `F80E` | `PCRLF` | CR, LF, three NULs |
| `F810` | `PSTRNG` | `PCRLF`, then `PDATA` |
| `F812` | `LRA` | Logical X → 20-bit physical in A:X (from `LRARAM`) |

## Commands

One character, then hex arguments (four digits for an address). A CR in place of a digit aborts.

| Key | Command |
|---|---|
| `^A` `^B` `^C` `^D` `^P` `^U` `^X` `^Y` | Alter A, B, CC, DP, PC, U, X, Y |
| `B hhhh` | Set a breakpoint (SWI). Five maximum. |
| `D` | **Boot the DMAF1/DMAF2 8-inch floppy** (DMA controller at `F000`) |
| `E ssss-eeee` | Examine memory, hex + ASCII, 16 bytes a line |
| `G` | Go — resume after an SWI or breakpoint |
| `L` | Load a MIKBUG-format tape |
| `M hhhh` | Memory examine/change. `.`/`,`/most keys = next, `^` = back, CR = end; `?` after a byte that did not store |
| `P ssss-eeee` | Punch a MIKBUG-format tape |
| `Q ssss-eeee` | Memory test. Reports the logical and the 20-bit physical address of an error. |
| `R` | Display registers: `SP= US= DP= IX= IY=` / `PC= A= B= CC:` with `EFHINZVC` spelled out |
| `S` | Display the stack, `S` up to `DFC0` |
| **`U`** | **Boot the MF-68 / DC-x 5-inch minifloppy** — see below |
| `X` | Remove all breakpoints |

## `U` — the minifloppy boot

This is FLEX9's boot path on a DC-4. The guide: "The disk interface should be installed in
processor interface **slot 6**." From the source (`MINBOOT`):

| Register | Address | = |
|---|---|---|
| `Drvreg` | **`E014`** | slot 5 — the DC-x drive-select latch |
| `Comreg` | **`E018`** | slot 6 — WD179x command/status |
| `Secreg` | `E01A` | WD179x sector |
| `Datreg` | `E01B` | WD179x data |

1. `TST Comreg`, then **`CLR Drvreg`** — drive 0, side 0.
2. Delay (3 × 65536 loop passes), then **restore** with head load, verify, and 20 ms step:
   **`$0F`** to `Comreg`. Short delay, then wait for status bit 0 (BUSY) to clear.
3. Sector register = **1**. Short delay. **Read sector** with head load: **`$8C`**. Short delay.
4. Byte loop into **logical `C000`**: while BUSY, if DRQ (status bit 1) then store `Datreg` at
   `,X+`.
5. When BUSY drops: if status `AND $2C` is non-zero (lost data, CRC error, or record type —
   bits 2, 3 and 5), **`RTS` back to the prompt**. Otherwise put `C000` in the saved PC of the
   monitor's register frame and **`RTI`** — the boot sector runs at `C000`.

⚠ **Source defect** (as noted in issue #3): `sbug_src.txt` line 383 has two instructions on
one line — `LBSR OUT4H PRINT THE ADDRESS LBSR OUT2S PRINT 2 SPACES`. An assembler reads the
second `LBSR` as comment. **The ROM image contains both calls.** Trust `SBUG_S1.TXT`.

## `D` — the DMAF2 boot

Selects drive 0 through the DMAF2's latch at `F024` (`$DE`), programs the DMA channel at
`F000`–`F016` for the **physical** address of logical `C000` (via `LRA`), and reads track 0
sector 1 with the 1791 at `F020`. It jumps to `C000` like `U`. (The v1.5 guide says the boot
loads at `$0000`. The 1.8 source loads at `C000`.) swtpcsim has no DMAF2, so `D` hangs polling the
floating `F020` status (`BMI` on `FF`), which is what the guide says happens "if no disk interface is attached".

## I/O at 8000 — the 6800-mainframe patch

The guide: change `FF79` from **`F1` to `F7`**. That is the `LDA #$F1` in `FINTAB` that sets
logical `E`'s DAT entry. `F7` maps logical `E000` to **physical `8000`**, so S-BUG runs in an
unmodified 6800 motherboard with its I/O at `8000` — "but costs the user an additional 16K of
memory capability, and limits the 6809 system to running at 1MHz, and using the MF-68
minifloppy." swtpcsim ships the unpatched 1.8 image, for the standard `E000` system.

## Related

- [SWTPC MP-09 6809 CPU Board](MP-09%206809%20CPU%20Board.md) — the board, and the DAT S-BUG programs.
- [SWTBUG Monitor](SWTBUG%20Monitor.md) — the 6800 counterpart.
- [Western Digital WD177X-00](Western%20Digital%20WD177X-00%20-%20Datasheet.md) — the command bytes (`$0F`, `$8C`) and status bits.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
