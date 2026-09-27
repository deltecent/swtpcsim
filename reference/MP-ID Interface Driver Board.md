# SWTPC MP-ID Interface Driver Board

Source: [MP_ID_Manual.pdf](#) (SWTPC *MP-ID Interface Driver Board*, circa 1980, 5 pp, **real
text layer**; scanned and edited by Michael Holley Mar 22 2006) and [MP_ID_Schematic.pdf](#)
(the one-sheet schematic), both from
`https://deramp.com/downloads/swtpc/hardware/MP_ID%206809%20Interface%20Driver%20Board/`.
Fetched 2026-09-27. The 6840 itself: [MC6840 Programmable Timer Module](MC6840%20Programmable%20Timer%20Module.md).

The MP-ID sits on the SWTPC **S/09** (6809) motherboard and does four jobs:

1. It **buffers and decodes the 30-pin I/O bus** for every I/O board.
2. It carries a **6820/6821 PIA** used as a parallel **printer port**.
3. It carries a **6840 timer** clocked from the **power line**, the "interrupt timer/clock".
4. It is the **baud-rate generator** (an MC14411 on a 1.8432 MHz crystal) for the bus's five
   baud-clock lines.

The manual: "All operations concerning the timer is handled in the system's disk operating
system — no attempt should be made to directly access the timer's registers."

## Address map — the emulation payload

A jumper picks the 8K block (48K–56K = `C000`, **56K–64K = `E000`**) and an eight-way jumper the
1K segment within it; each I/O port is 16 addresses. In the standard `E000` setting:

| Port | Address | What |
|---|---|---|
| 0–7 | `E000`–`E07F` | The eight I/O slots (select lines SEL0–SEL7 to the bus) |
| 8 | **`E080`** | **The PIA on the MP-ID** |
| 9 | **`E090`** | **The 6840 on the MP-ID** |

Register selects, from the schematic: the PIA takes **RS0 = A0, RS1 = A1**. The 6840 takes
**RS0 = A0, RS1 = A1, RS2 = A2**. A3 is not used by either, so each device appears twice in
its 16 addresses (the PIA four times).

Standard jumpers (manual): Slow Per/Norm = NOR, 0–7 = 0, 48-56/56-64 = 56-64, X1/8 = X1,
**EX/INT = INT**, 150/9600 = 9600, 6/12 = 12, **PIA IN/OUT = OUT**.

## The timer wiring (read from the schematic)

- **The line pulse.** 26 VAC from the transformer feeds two transistors, Q1 and Q2, one on each
  half of the AC wave. Their collectors are tied together into an IC13 (96S02) one-shot, so it
  fires on **both half-cycles**: **120 pulses a second on 60 Hz, 100 on 50 Hz**.
  - FLEX9 confirms the rate (see below): it divides the count by 12 (or 10) to get tenths of a
    second.
- **The pulse drives:**
  - **C1̄** (timer 1's clock);
  - through the **INT** jumper, **C3̄** as well (EXT takes C3̄ from an edge-connector pin
    instead).
- **The other 6840 pins:**
  - **O3 is wired to C2̄**: timer 2 counts timer 3's output.
  - **G1̄, G2̄, G3̄ are grounded**, so every timer is always gated on.
  - **O2** goes out to the edge connector's OUT pin.
  - The 6840's **IRQ̄** goes to the bus IRQ.
  - Its **RESET̄** is the bus reset.
- **IC7, a 74LS393** (two 4-bit ripple counters):
  - **O1 drives PIA PA0 directly**.
  - O1 also clocks the second half. That half's QD clocks the first half.
  - The eight bits reach the PIA as **PA0 = O1, PA1–PA3 = the second half's QA–QC, PA4–PA7 =
    the first half's QA–QD**.
  - The top bit (the first half's QD) also drives **CA1**.
  - Because O1 toggles at each timer-1 time-out and the 74LS393 counts O1's falling edges,
    **PA reads the number of timer-1 time-outs, mod 256**.
  - Both CLR pins come from an IC12 NAND off the bus RESET̄, so **a bus reset zeroes the
    count**.
- **The printer port:**
  - PIA **side B** drives the DB-25 through IC8, a 74LS245 whose direction is set by the PIA
    DIR jumper.
  - **CB2** (through IC15) is DATA READY, on DB-25 pin 22.
  - DB-25 pin 23, **BUSY or ACK**, comes back on **CB1**.
  - The PIA's IRQA̅ and IRQB̅ go to the bus IRQ.
- **A 555 (IC14)** drives a small speaker. It is fed from the unregulated supply and has no
  data-bus connection visible on the sheet.

⚠ All of the above is read from a single scanned sheet; the IC7 bit order in particular is
read from crossing lines. It agrees with TIME.CMD's arithmetic (below), which is the check.

## What FLEX9 expects

**The boot probe** (FLEX9 2.8:3, `FLEX.SYS` at C4D3):
- **Setup:** write CR2 = `01` (so the next address-0 write goes to CR1), then CR1 = `00`. That
  gives: timers released, timer 1 on the **external clock C1̄**, continuous, 16-bit. Then the
  latch = `FFFF`.
- **The measurement:** it reads timer 1's counter, spins about four million E cycles (8 × `F421`
  passes of `LEAX -1,X / BNE`), reads it again, and subtracts.
- **The check:** it looks the difference up in six windows (address C534: low, high, code).

| Count | Code | Reading |
|---|---|---|
| `00C7`–`00C9` (≈ 200) | `A2` | 100 pulses/s (50 Hz) at a **2 MHz** E clock |
| `00EB`–`00EE` (≈ 236) | `E2` | not identified (bit 6 set) |
| `00EF`–`00F1` (≈ 240) | `82` | 120 pulses/s (60 Hz) at 2 MHz |
| `011A`–`011D` (≈ 283) | `C2` | not identified (bit 6 set) |
| `018F`–`0191` (≈ 400) | `22` | 100 pulses/s (50 Hz) at **1 MHz** |
| `01DF`–`01E1` (≈ 480) | `02` | 120 pulses/s (60 Hz) at 1 MHz |

  Reading the codes: **bit 7 = 2 MHz CPU, bit 5 = 50 Hz line**, bit 1 = a timer is present.
  Every row is consistent with the 120/100-pulse reading of the schematic. A once-per-cycle
  pulse (60/s) would give 240 at 1 MHz, which lands in the "2 MHz, 60 Hz" row: wrong but
  accepted. The two bit-6 rows are about 1.18 times the others; what CPU speed they stand for
  is not known.
- **The result:** a hit returns the code, which FLEX keeps in **`$CC33`**. Bit 1 means a timer
  is present, and **bit 5 set means 50 Hz**. A miss prints **"Timer not available."** (C51F).

**`TIME.CMD`:**
- **Setup:** CRA = `30`, DDRA = `00` (side A all inputs), then CRA = `34`. CR2 = `81`, CR1 =
  `80` (O1 enabled, external clock, continuous). Timer 1's latch = `FFFF`. Then CR2 = `80`.
- **The read:** it takes **`LDX $E092`** (the timer-1 counter), **`LDB $E080`** (the IC7 count),
  and re-reads `E092` until it is steady.
- **The value:** it complements B, giving a 24-bit **decreasing** value, `~PA : counter`.
- **Elapsed time** is the start value minus the end value. It divides by **12**, or **10** when
  `$CC33` bit 5 is set, to get tenths of a second.
- **It refuses to run** when `$CC33` bit 1 is clear ("-- No timer available.").
