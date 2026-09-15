# SWTPC MP-S Serial Interface

Source: [MP_S_AssemblyInstructions.pdf](#) (SWTPC *Assembly Instructions MP-S Serial
Interface*, circa 1976; scanned/edited by Michael Holley, Feb 1 2001, rev. Aug 7 2002;
`https://deramp.com/downloads/swtpc/hardware/MP_S%20Serial%20Interface/MP_S_AssemblyInstructions.pdf`).
Fetched 2026-09-13.

The **MP-S** is the standard serial I/O board of the SWTPC 6800 computer — a **6850 ACIA**
(IC1) on a 5¼″ × 3½″ card that plugs into an I/O slot of the MP-B motherboard. It is the board
the console terminal normally attaches to. Jumper-configurable **110 to 9600 baud**, and both
**RS-232C** and **20 mA TTY** (ASR-33) compatible; it will **not** drive 5-level Baudot TTYs.
Interrupt control is entirely under software. On-board +5 V regulator (IC2), ~0.2 A; ±12 V for
the RS-232/TTY line drivers comes from the computer supply.

## Address map — the emulation payload

The board occupies **four sequential addresses per I/O port slot**, based at **`$8000`**
(A port N = `$8000 + N*4`):

| Port | Address block |
|---|---|
| 0 | `8000`–`8003` |
| 1 | `8004`–`8007` (the control/console interface slot) |
| 2 | `8008`–`800B` |
| 3 | `800C`–`800F` |
| 4 | `8010`–`8013` |
| 5 | `8014`–`8017` |
| 6 | `8018`–`801B` |
| 7 | `801C`–`801F` |

Within each 4-address block, **only address line A0 reaches the ACIA's register-select pin**
(the 6850 has a single `RS` input — one bit of register select is all it has). A1 does not
participate, so the two register pairs each appear **twice** across the block:

- **A0 = 0** (block+0 *and* block+2) = ACIA **control register (write) / status register (read)**;
- **A0 = 1** (block+1 *and* block+3) = ACIA **transmit data (write) / receive data (read)**.

The board's own manual describes the upper two addresses as "reserved for the parallel
interfaces," reflecting SWTPC's *intended* SS-30 slot allocation (a parallel board such as the
MP-L would use them). But on an MP-S in a slot **by itself**, the 6850 answers all four addresses,
with block+2/block+3 **mirroring** block+0/block+1. This mirror is not incidental — see the
emulation note below.

So a console MP-S in port 1 answers control/status at **`$8004`** (and, mirrored, `$8006`) and
data at **`$8005`** (and, mirrored, `$8007`) — the addresses the ROM monitor's terminal routines
assume. The 6850 register model itself (master reset `11`, ÷1/÷16/÷64, word/parity/stop select,
RTS/Tx-interrupt control, and the RDRF/TDRE/DCD/CTS/FE/OVRN/PE/IRQ status bits) is in
[6850](6850.md); this board just wires that chip at the addresses above.

## How it works

- **IC1 is a 6850 ACIA** doing the start/stop framing in hardware. Its **`RTS` output doubles as
  a "Reader Control" (RC)** line to drive a TTY reader/punch that lacks an automatic control
  feature; the RC relay is controlled by RTS with respect to the TTY common line.
- **Echo** for full-duplex operation is done in software, not on the board.
- **Optical couplers IC3–IC5** (4N33) isolate the RS-232/TTY lines. **Both −12 V and +12 V** are
  required and are taken unregulated from the computer's power-supply board; only +5 V is
  regulated on-board (IC2, 7805).

## Baud, clock, and interrupt jumpers (power-on build constants)

- Baud is selected by a jumper across one of the **`110` / `150` / `300` / `600` / `1200`** pads.
  Additional/substitute baud values may come from the processor board's generator — the assembly
  note defers to the MP-A instructions for the full set (110–9600).
- The **Clock Output (CO)** line is a CMOS-level square wave at **≈16× the baud rate**; **Clock
  Input (CI)** feeds an external clock for self-clocking cassette systems. With no self-clocking
  cassette, **CO and CI are jumpered together**.
- Solder the **`I` pad** (between the `IRQ` and `NMI` pads) to `IRQ` to route the ACIA interrupt
  to the system **maskable** interrupt line.

## 10-pin I/O connector

Nine signal pins (tenth is a keying/index pin): **gnd**, **CO** (clock out, ≈16× baud, CMOS),
**CI** (clock in), **RI** (RS-232 input), **RO** (RS-232 output), **TC** (TTY common =
the −12 VDC bus), **TO** (20 mA TTY output), **RC** (reader control), **TI** (20 mA TTY input).
Unused RI must be tied to ground; unused TI must be tied to TC. In TTY mode the board runs
**110 baud, 1 start / 8 data / no parity / 2 stop**; RS-232 mode allows 110–9600 with the
terminal's format matched in software.

## Emulation notes

The emulator's `mps` board models one 6850 based at a configurable SS-30 slot (default `$8004`,
port 1 = the console) with the A0-only register select described above: it decodes all four
addresses of its slot, and address bit A1 is ignored, so `$8006`/`$8007` **mirror** `$8004`/`$8005`.

⚠ **The mirror is load-bearing — do not "optimize" it away by decoding only `$8004`/`$8005`.**
SWTBUG's power-up console probe (`SWTBUG.ASM` `PIAINI`) reads the candidate I/O device at both
`base+0` and `base+2` and compares them (`LDAA 0,X` / `CMPA 2,X`): equal → it is an ACIA, and the
monitor master-resets it and uses it as the console; unequal → it takes the PIA (parallel) path and
never resets the ACIA. If the board decodes only `$8004`/`$8005`, a read of `$8006` returns the
bus's undecoded value (≠ the `$8004` status byte), the probe fails, the console ACIA is never
reset, and the terminal emits garbage instead of the `$` prompt. The four-address decode is what
makes SWTBUG recognize its own console.

(The single `BEL` the monitor emits at power-up is *also* faithful, not a bug: `PIAINI` writes `7`
to the data register while configuring the device, and the ACIA transmits it.)

## Related

- [6850](6850.md) — the MC6850 ACIA register/bit model this board wires up.
- [SWTPC MP-A 6800 CPU Board](MP-A%206800%20CPU%20Board.md) — the processor/monitor board whose
  baud generator and MIKBUG terminal routines drive an MP-S in port 1.
- [SWTPC MP-S2 Dual Serial Interface](MP-S2%20Dual%20Serial%20Interface.md) — the later dual-ACIA
  board for the S/09 (16 addresses per slot, different base).
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
