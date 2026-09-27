# SWTPC MP-09 6809 Microprocessor Board

Source: [MP_09B_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions — MP-09
Microprocessor Board*, circa 1980; scanned/edited by Michael Holley, Nov 6 2000;
`https://deramp.com/downloads/swtpc/hardware/MP_09%206809%20CPU%20Board/MP_09B_AssemblyInstructions.pdf`),
and the two-sheet schematic [MP_09_SchematicLeft.jpg](#) / [MP_09_SchematicRight.jpg](#)
(*Schematic MP-09A Processor Board*, same directory). Fetched 2026-09-27.

The **MP-09** is SWTPC's 6809 processor board. It replaces the MP-A/MP-A2 6800 board in an
SS-50 system. It carries the **MC6809** (IC14, a 68B09 on the MP-09B), the **S-BUG** monitor
ROM, a **dynamic address translator (DAT)**, the MC14411 baud-rate generator, and the bus
buffers. The manual's own feature list: SS-50 compatible, paged memory addressing, extended
addressing up to 384K (an option, below), sockets for up to 8K of 2716-pinout ROM/RAM, and
"tight address decoding".

## Crystal and clock

| Part | Value | Note |
|---|---|---|
| Y1 (CPU) | **4.0 or 8.0 MHz** | "A 4.0 MHz crystal is used for 1 MHz computer operation and a 8.0 MHz crystal is used for 2 MHz operation" (parts list). The MC6809 divides its crystal by 4. |
| Y2 (baud) | 1.8432 MHz | For the MC14411 (IC24). |

## The dynamic address translator (DAT) — the emulation payload

From the manual (*Dynamic Address Translator*, pp. 11–13; the section was copied from the
earlier MP-09 manual into the MP-09B one):

- It "changes (or translates) the addresses output on the **upper four address lines** by the
  6809 processor **before these addresses are output onto the system bus**." It splits the 64K
  space into **sixteen 4K segments**. The **logical** address is the one the processor puts
  out. The **physical** address is the one on the 50-pin bus.
- "Electrically the address translator is a **4-bit wide, 16-position** high speed random
  access memory. It is physically and logically addressed in the upper 16 bytes of memory
  (**`FFF0`–`FFFF`**) as **write only memory**."
- "It is loaded by writing the **complement** of the desired upper four bits of the physical
  address using the **lower 4 bits of the data byte** to the memory location corresponding to
  the selected logical address. The 4K logical address segments start sequentially from
  `FFF0`." So **`FFF0 + n` holds the map for logical segment `n`** (`n000`–`nFFF`).
- One physical segment can appear at several logical addresses: "a physical address in memory
  may have from 0 to 16 logical addresses."

The manual's identity table (data = complement of the segment number):

| Write to | Logical | Data | Physical |
|---|---|---|---|
| `FFF0` | `0000`–`0FFF` | `0F` | `0000`–`0FFF` |
| `FFF1` | `1000`–`1FFF` | `0E` | `1000`–`1FFF` |
| … | … | … | … |
| `FFFE` | `E000`–`EFFF` | `01` | `E000`–`EFFF` |
| `FFFF` | `F000`–`FFFF` | `00` | `F000`–`FFFF` |

(⚠ The scan's rows for `FFF8` and `FFFB` print "`8000`–`BFFF`" as the logical range. That is a
typo: the pattern and the data column make them `8000`–`8FFF` and `B000`–`BFFF`.)

Worked example (manual): writing `0E` to `FFF0` moves physical `1000`–`1FFF` to logical
`0000`–`0FFF`. Physical `1000` then *also* stays at logical `1000`, because that entry was not
changed.

### What the schematic adds

- **IC11 is a 74S189** (16×4 RAM, inverted open-collector outputs `DO1̄`–`DO4̄`). Its outputs
  drive the **bus A12–A15** through pull-ups R9, R10, R13 and R14. The inverted outputs are why the
  manual says "write the complement". The 6809's own A12–A15 do not reach the bus directly. A0–A11
  are buffered straight through (IC12/IC13, non-inverting).
- **IC10, a 74LS157** data selector, feeds IC11's four address inputs. It chooses between the
  CPU's **A12–A15** (translate) and **A0–A3** (a write to `FFFx` loads entry `x`). The select
  and IC11's `S̄` come from IC5 (74LS30, 8-input NAND), IC6 (74LS02) and IC21 (74LS00).
- **When IC11 is not driving (deselected, or during its write), the pull-ups hold bus A12–A15
  high: physical `Fxxx`.**
- ⚠ **Not traceable on the scan:** exactly which lines feed IC5's eight inputs, and so exactly
  which logical range deselects the DAT. See *Reset* below for the behavior the firmware
  requires.
- **IC8** is a second 74S189, for **extended addressing** (A16–A19, "up to 384K"). The parts
  list: "The board may have **either IC8 or IC24** installed (not both). Boards without
  extended addressing need IC24 installed and R21–R24 omitted." The standard board has
  **IC24 (the baud generator) and no IC8**: a 4-bit DAT, and a 16-bit physical bus.

### Reset: the firmware requires `FFxx` to reach the ROM before the DAT is loaded

The 74S189 is a RAM with no reset, so its contents at power-up are unknown. S-BUG's reset
vector is **`FF00`**, and its first act is to load all sixteen DAT entries *while executing
from `FF00`–`FF1x`* (see [S-BUG Monitor](S-BUG%20Monitor.md), *Reset*). The loop writes entry
`F` part way through. So instruction fetches from logical `FFxx` must reach the ROM *whatever
the DAT holds*. That is only possible if the DAT is bypassed (outputs off, pull-ups → physical
`F`) for those addresses. An **8-input NAND (IC5) on logical A8–A15** — "the top page, `FFxx`"
— is the one decode that fits both the part and the firmware. It would also explain why
S-BUG's `START` is placed at `FF00` rather than anywhere in `F800`–`FFFF`.

⚠ **This is an inference, not a reading of the scan.** The emulation models it as:
**logical `FF00`–`FFFF` bypasses the DAT (physical `FFxx`); writes to logical `FFF0`–`FFFF`
load it; every other logical address is translated.** A clearer scan of the IC5/IC6/IC21
decode would settle it. **If an S-BUG or FLEX9 boot misbehaves in a way that touches `Fxxx`,
suspect this first, and get a real answer from a better source before changing anything
else.**

## The ROM sockets

Four 24-pin sockets, IC1–IC4, all "2516 [2716] pin compatible ROM or RAM":

| Socket | Window | Enabled by | Standard system |
|---|---|---|---|
| IC1 | `E000`–`E7FF` | S1 switch (`E000-E7FF ROM/RAM SELECT`) | **OFF** — "the physical address of the component conflicts with that of the interface address" |
| IC2 | `E800`–`EFFF` | S1 switch | **OFF** — same reason |
| IC3 | `F000`–`F7FF` | S1 switch | **OFF** — "conflict with that of the DMA controller address assignment" |
| **IC4** | **`F800`–`FFFF`** | always | **The monitor (S-BUG).** "IC4 is reserved for the system monitor and is addressed from `F800`–`FFFF`." |

The socket windows are decoded on the **physical** (translated) address. On the schematic,
IC7 (74LS138) takes its enable from IC21, a NAND of the **bus** A14/A15 (the DAT's outputs),
and its outputs `Y4`–`Y7` select IC1, IC2, IC3 (through S1 B, C, D) and IC4 (`Y7`, no switch,
through IC6). The manual agrees: it speaks of each socket's "physical address".

A RAM/ROM programming strip for each of IC1–IC3 sets the socket's R/W̄ pin. ("The position
of the jumper is not important" when a ROM is fitted.)

## Memory map (the standard 56K system)

From *Memory Map for the MP-09* (p. 14):

| Physical | Contents |
|---|---|
| `0000`–`DFFF` | **Up to 56K of user RAM.** "The actual physical location of the memory within the system is not important since the monitor locates and logically re-addresses all RAM memory plugged into the system." No two boards may share a physical address. |
| `E000`–`EFFF` | **I/O.** The MP-B/MP-B2 motherboard is modified (MP-B3 needs no modification) to "relocate the I/O addresses from the original `8000`–`8FFF` (32K–36K) assignment to `E000`–`EFFF` (56K–60K)." The SS-30 slots keep their 6800 layout, rebased: port *n* at `E000 + 4n`. |
| `F000`–`F3FF` | The DMAF1/DMAF2 8-inch disk controller, when fitted (moved from `9000`; the DMAF1 needs the modification on p. 16). |
| `F800`–`FFFF` | S-BUG in IC4. |

After S-BUG's RAM search: "With 8K of RAM memory, the memory will be sequentially mapped so it
is logically addressed from `0000`–`DFFF`. Any additional memory will reside from
`0000`–`BFFF`. The DOS, if resident, will reside from `C000`–`DBFF`. The monitor loads the system
stack pointer with `DFFF`, and sets the direct page register to zero."

## Straps and switches

**DIP switch S1** (four positions, left to right):

| Switch | Function |
|---|---|
| LOW BAUD | ON (the normal setting): 110/150/300/600/1200/4800/9600 baud. OFF: the high set (440/600/38,400/1200/2400/19,200/4800). |
| `E000-E7FF` | IC1 socket enable (normally OFF). |
| `E800-EFFF` | IC2 socket enable (normally OFF). |
| `F000-F7FF` | IC3 socket enable (normally OFF). |

**Eight programming strips:**

| Strip | Setting |
|---|---|
| `150b/9600b` | Which clock goes on bus line `150b (S3)`. |
| `4800b/600b` | Which clock goes on bus line `600b (S1)`. |
| `110b/BR` | Baud clock *or* bus-request input on the shared `110b/BR` line. "For now it is suggested that you set this option for the BR function." |
| `2S/3S` | Float the baud drivers when halted (S/09 only). Install at **2S**. |
| `BA/BA&BS` | Float the address/data bus on BA, or on BA∧BS. Install at **BA** (BA&BS only with a DMAF1). |
| `RAM/ROM` ×3 | IC1–IC3 socket type. |

## Other bus signals

- **FIRQ.** The schematic brings a `FIRQ̄` line from the bus edge to the MC6809's FIRQ pin
  (IC14 pin 4), beside `IRQ̄`. Neither source names the SS-50 pin that carries it, and the
  6800-era [SS-50 bus reference](SS-50%20SS-30%20Bus.md) has no FIRQ line. No SWTPC board
  modelled in swtpcsim drives it.
- **NMI.** IC9 (a 555) debounces the front-panel ABORT/NMI switch into the MC6809's NMI.
  (The 6800 mainframe does not wire ABORT: "The ABORT switch is not used as 6800 mainframes.")
- **RESET.** IC18 (a 555) makes the power-up reset. The front-panel RESET button connects
  **directly to the processor card** by its own cable, not through the bus.
- **M.RST → M.RDY.** "The MP-09 processor card does not use the M.RST (manual reset) line
  carried on the 50-pin bus as do its 6809 [sic, 6800] MP-A and MP-A2 predecessors. This line
  has instead been renamed the Memory Ready (M.RDY) line." It feeds the MC6809's MRDY.
- **HALT, BUS REQ.** IC15 (74LS74) synchronizes the bus HALT and bus-request inputs.

## Emulation notes

- The **`mp09`** board is the MC6809 plus the DAT plus IC4. IC1–IC3 are unpopulated (their S1
  switches OFF, as the manual directs). IC8 (extended addressing) is not fitted, as on the
  standard board.
- The backplane stays 16 bits. What goes on it is the **physical** address.
- `clock_hz` is the E-clock: 1 MHz (4 MHz crystal) or 2 MHz (8 MHz crystal), or 0 for flat out,
  like the other CPU boards.
- The DAT bypass for `FFxx` is modelled as described under *Reset* above, and flagged as an
  inference.

## Related

- [S-BUG Monitor](S-BUG%20Monitor.md) — the firmware in IC4, and what it does with the DAT.
- [Motorola MC6809 / MC6809E Programming Manual](Motorola%20MC6809-MC6809E%20Programming%20Manual.md) — the processor.
- [SWTPC MP-A2 6800 CPU Board](MP-A2%206800%20CPU%20Board.md) — the board it replaces.
- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) — the console S-BUG requires in I/O port 1.
- [SS-50 / SS-30 Bus](SS-50%20SS-30%20Bus.md) — the buses.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
