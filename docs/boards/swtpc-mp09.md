# SWTPC MP-09 processor board (`mp09`)

**Status:** done for the standard board (DAT and IC4). It is the CPU of the built-in `swtpc09`
machine, which comes up to S-BUG's prompt. Its FLEX9 boot is issue #3.

## The real hardware

SWTPC's 6809 processor board, circa 1980. It replaces the MP-A/MP-A2 6800 board in an SS-50
system. It carries:

- an **MC6809** (a 68B09 on the MP-09B), clocked at a quarter of its crystal: 4 MHz for 1 MHz
  operation, 8 MHz for 2 MHz;
- the **DAT** (dynamic address translator), a 74S189 16 × 4 RAM (IC11) between the CPU's
  A12–A15 and the bus's;
- four 2716-pinout sockets, IC1–IC4. **IC4 holds the S-BUG monitor** at `F800`–`FFFF`. IC1–IC3
  (`E000`/`E800`/`F000`) are switched off in a standard system;
- an MC14411 baud generator for the SS-30 bus's baud lines.

A 6809 system moves the SS-30 I/O window from `8000` to **`E000`** on the motherboard (the MP-B3,
or an MP-B with the modification in the MP-09 manual). The I/O boards are unchanged, so the MP-S
console sits at `E004` and a DC-4 at `E014`/`E018`.

## Sources

| Source | Path | Authority |
|---|---|---|
| SWTPC *Assembly Instructions — MP-09 Microprocessor Board* and the two-sheet schematic | `reference/MP-09 6809 CPU Board.md` | The DAT, the ROM decode, the memory map, the straps, the crystal |
| S-BUG 1.8 source and image | `reference/S-BUG Monitor.md`, `roms/SBUG/` | What the firmware requires of the DAT at reset |

The manual and the schematic agree on everything this board models except one point, which
neither states: which logical addresses bypass the DAT. That is inferred from S-BUG (below).

## Register reference

| Addr | Write | Read |
|---|---|---|
| logical `FFF0`–`FFFF` | loads DAT entry `n` (logical segment `n000`–`nFFF`) with the **complement** of the physical segment, from D0–D3. D4–D7 are ignored (they would go to the extended-address chip, IC8, which the standard board does not carry). The write also goes out on the bus. | not the DAT. The read goes through to the bus, where IC4 answers with the ROM's vectors. |
| physical `F800`–`FFFF` | — (a ROM) | IC4, the S-BUG ROM |

## How it is simulated

- **The core runs on a private inner bus.** The card gives its `Cpu6809` an inner `Bus` with
  one board on it, the DAT port. The port answers every cycle and makes the same cycle on
  the backplane at the translated address. The core and the backplane are unchanged, and the
  backplane's observers (BREAK MEM, TRACE, HISTORY) see **physical** cycles.
- **Translation:** logical `FF00`–`FFFF` passes through untranslated. Every other logical
  address takes its A12–A15 from the DAT: physical segment = `~entry & 0xF`.
- **IC4 is a plain backplane decode.** The card is a `Board` that answers reads of physical
  `F800`–`FFFF` with the ROM, and never a write. `rom` names the image: `builtin:sbug` by
  default, or a file in S-record, Intel HEX or flat binary, which must lie within
  `F800`–`FFFF`. An empty `rom` is an empty socket and decodes nothing.
- **Interrupts:** the backplane IRQ wire is copied onto the inner bus before each instruction.
  The 6809 samples it at the instruction boundary, so this is exact. NMI comes from the monitor
  as on the plain `6809` card. **FIRQ is not connected.**
- **The debugger's PC views** (the instruction on the register line, `NEXT`, HISTORY's bytes,
  a bare `DISASM` after a stop) ask `CpuCard::toBus` where the PC lands on the bus. Every
  address the operator types is still a bus (physical) address.
- It masters the bus (`BusMaster`) and carries one unit, `6809`.
- **Properties:** `clock_hz` (the CPU clock: 1000000 for the 4 MHz crystal, 2000000 for
  8 MHz; 0 runs flat out), `idle`, `rom`, `dat` (read-only: the physical segment for each
  logical segment `0`–`F`, as sixteen hex digits), `achieved_hz` (read-only).

### Reset

- `Reset::PowerOn`: the ROM is reloaded from `rom`. The DAT comes up holding junk, from a
  fixed seed so that a power-on is repeatable. The core resets and fetches its vector
  through the `FFxx` bypass.
- `Reset::Bus`: the core resets. **The DAT keeps its contents**; it is a RAM with no reset
  input.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| The DAT stores the **complement** (the 74S189's outputs invert). | S-BUG's identity load (`$0F` to `FFF0` … `$00` to `FFFF`) maps every segment backwards. S-BUG's RAM search finds nothing where it expects it, and it never signs on. |
| The DAT is **write-only**. A read of `FFF0`–`FFFF` is the ROM. | The 6809's vector fetch at `FFF8`–`FFFF` returns DAT nibbles instead of S-BUG's vectors. |
| Logical `FF00`–`FFFF` **bypasses** the DAT. | The reset vector and S-BUG's `START`, which loads the DAT, are fetched through junk. The machine runs off into whatever physical page it lands in. |
| IC4 is decoded on the **physical** address. | A guest that maps logical `F` elsewhere would still see the ROM at logical `F800`, and a backplane read of physical `F800` would not. |
| The DAT survives RESET. | A warm restart would lose a mapping that the software set up. |

## Limitations and deliberate departures

- ⚠ **The `FFxx` bypass is inferred, not read.** The schematic does not show clearly which
  lines feed the decode that deselects the DAT. Logical `FF00`–`FFFF` is the one decode that
  fits both the parts (an 8-input NAND, IC5) and the firmware (S-BUG runs from `FF00` while it
  loads the DAT). `reference/MP-09 6809 CPU Board.md` has the argument. If a boot misbehaves
  around `Fxxx`, suspect this first.
- **No extended addressing.** IC8 (A16–A19, "up to 384K") is an option that replaces the baud
  generator. The standard board has no IC8, so the bus stays 16-bit and the DAT's high nibble
  is dropped.
- **IC1–IC3 are not modeled.** Their switches are off in a standard system, because their
  windows conflict with the I/O and the DMA controller.
- **The baud generator is not modeled.** The simulated serial boards take their rate from
  their own `baud` property.
- **FIRQ is not connected.** The schematic takes the line to the bus edge, but no SS-50 pin in
  `reference/` carries it, and no board in the set drives it.
- **The DAT write also goes out on the bus.** Nobody decodes physical `FFFx` on a write in the
  standard system, so it only shows up in TRACE and HISTORY.
- `EXAMINE` treats the PC as a front-panel address register and does not translate it.

## Verification

- `tests/test_mp09.cpp`:
  - S-BUG runs from a junk DAT to `S-BUG 1.8 - 56K` and `>` over an MP-S at `E004`, and
    leaves the identity map;
  - the complement, and the backplane seeing the physical address;
  - the write-only DAT reading back as the ROM;
  - the `FFxx` bypass;
  - IRQ through the inner bus;
  - IC4's decode, and an empty or wrong-sized socket;
  - SNAPSHOT carrying the DAT.

  Breaking the complement or the bypass fails the suite.
- `tests/test_roms.cpp` checks S-BUG's CRC32 (`10A045A7`) against `docs/roms.md`.

## References

- `reference/MP-09 6809 CPU Board.md`
- `reference/S-BUG Monitor.md`
- `roms/SBUG/README.md`
