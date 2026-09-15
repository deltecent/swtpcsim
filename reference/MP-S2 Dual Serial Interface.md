# SWTPC MP-S2 Dual Serial Interface

Source: [MP_S2_Serial_Interface.pdf](#) (SWTPC *MP-S2 Dual Serial Interface*, circa 1980;
scanned/edited by Michael Holley, Jul 5 2002;
`https://deramp.com/downloads/swtpc/hardware/MP_S2%20Dual%20Serial%20Interface/MP_S2_Serial_Interface.pdf`).
Fetched 2026-09-13.

The **MP-S2** is the later, **dual-port** serial board — **two 6850 ACIAs** on one 5.75″ × 4.75″
card — designed for the SWTPC **S/09** system, which assigns **16 addresses per I/O slot** (not
the MP-S's 4). Two DB-25 connectors: the **upper = PORT A** (upper ACIA), the **lower = PORT B**
(lower ACIA). **RS-232C only** — no TTY current-loop. Both ACIAs' `IRQ` lines are hardwired to
the system IRQ, so custom drivers must take care not to inadvertently enable ACIA interrupts.
The second port (ACIAs, drivers, and diodes marked "optional") is added by the **MP-SX**
expansion kit.

## Address map — the emulation payload

Sixteen addresses per slot. Only these offsets are decoded (**all others are not decoded**):

| Offset | Register |
|---|---|
| `+0` | PORT A (upper ACIA) status (read) / control (write) |
| `+1` | PORT A (upper ACIA) receive (read) / transmit (write) data |
| `+4` | PORT B (lower ACIA) status / control |
| `+5` | PORT B (lower ACIA) data |
| `+E` | Control Line Input Register (multiply decoded) |
| `+F` | Control Line Input Register (multiply decoded) |

Base address = **`$E000 + slot*0x10`** on an S/09 with I/O at `$E000`:

| Slot | Base | Slot | Base |
|---|---|---|---|
| 0 | `E000` | 4 | `E040` |
| 1 | `E010` | 5 | `E050` |
| 2 | `E020` | 6 | `E060` |
| 3 | `E030` | 7 | `E070` |

Worked example (slot 2): A control/status `E020`, A data `E021`, B control/status `E024`,
B data `E025`, Control Line Input Register `E02F`.

The 6850 register/bit model (master reset, divide/word/stop select, RTS/Tx control, and the
RDRF/TDRE/DCD/CTS/FE/OVRN/PE/IRQ status bits) is in [6850](6850.md).

### Control Line Input Register

A read-only "register" of buffered control-line inputs:

| Bit | Function |
|---|---|
| 0 | HOLD DWN input of PORT A connector (pin 19) |
| 1 | HOLD DWN input of PORT B connector (pin 19) |
| 2–7 | Reserved for future use |

## Baud, clock, and CTS handshaking

- Four jumper blocks set baud: **TCLOCK** (transmit rate) and **RCLOCK** (receive rate) per port;
  normally set equal. Each has an **EXT** position that takes an **external 16× clock** on the
  connector's **CLOCK IN** pin — commonly used with the SWTPC **CT-82** terminal.
- **CTS flow control** (pin 20): CTS RS-232 **high** = characters transmit continuously at
  **baud ÷ 10** chars/sec; CTS **low** = transmission inhibited. This only **holds up the
  computer** — no characters are lost. Connect to the device's BUFFER FULL / DATA TERMINAL READY.

## DB-25 connector pinout (per port)

| Pin | Signal |
|---|---|
| 1, 7 | Ground |
| 4, 5 | Tied together on-board (loop the device's RTS/CTS) |
| 2 | R DATA — RS-232 serial **input** to the MP-S2 |
| 3 | T DATA — RS-232C serial **output** from the MP-S2 |
| 8 | DCD **output** (buffered ACIA RTS; normally unused) |
| 12 | SDCD **input** (buffered ACIA DCD; normally unused) |
| 19 | HOLD DWN **input** (CT-82 key-held; read via the Control Line Input Register) |
| 20 | CTS **input** (see handshaking above) |
| 24 | CLOCK IN — TTL, feeds ACIA Rx/Tx clock in EXT mode; **16× the baud rate** |

All I/O lines except CLOCK IN are RS-232C levels (nominally +10 V high / −10 V low, ~10 mA).

## Related

- [6850](6850.md) — the MC6850 ACIA register/bit model both ports wire up.
- [SWTPC MP-S Serial Interface](MP-S%20Serial%20Interface.md) — the earlier single-ACIA board
  (4 addresses per slot, `$8000` base, RS-232 **and** TTY).
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
