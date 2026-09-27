# Motorola MC6840 Programmable Timer Module (PTM)

Source: [MC6840 Programmable Timer Module (Motorola).pdf](#) (Motorola Semiconductor data
sheet, 14 pp, scanned — **no text layer**), from
`https://colorcomputerarchive.com/repo/Documents/Datasheets/MC6840%20Programmable%20Timer%20Module%20(Motorola).pdf`.
Fetched 2026-09-27. The same sheet is on alldatasheet.com and datasheetcatalog.com.

The MC6840 is an M6800-family peripheral with **three 16-bit down-counters**, each with its own
16-bit latch, control register, clock input **Cx̄**, gate input **Gx̄** and output **Ox**, plus a
shared status register and an open-drain **IRQ̄**. Parts: MC6840 (1.0 MHz), MC68A40 (1.5 MHz),
MC68B40 (2.0 MHz).

## Pins

| Pin | Name | Pin | Name |
|---|---|---|---|
| 1 | V<sub>SS</sub> | 28 | C1̄ |
| 2 | G2̄ | 27 | O1 |
| 3 | O2 | 26 | G1̄ |
| 4 | C2̄ | 25–18 | D0–D7 |
| 5 | G3̄ | 17 | E |
| 6 | O3 | 16 | CS1 |
| 7 | C3̄ | 15 | CS0̄ |
| 8 | RESET̄ | 14 | V<sub>CC</sub> |
| 9 | IRQ̄ | 13 | R/W̄ |
| 10–12 | RS0–RS2 | | |

Selected when CS0̄ = 0 and CS1 = 1.

## Register selection — Table 1

| RS2 | RS1 | RS0 | Write (R/W̄ = 0) | Read (R/W̄ = 1) |
|---|---|---|---|---|
| 0 | 0 | 0 | CR20 = 0: Control Register #3; CR20 = 1: Control Register #1 | No operation |
| 0 | 0 | 1 | Control Register #2 | Status Register |
| 0 | 1 | 0 | MSB Buffer Register | Timer #1 Counter (MSB) |
| 0 | 1 | 1 | Timer #1 Latches | LSB Buffer Register |
| 1 | 0 | 0 | MSB Buffer Register | Timer #2 Counter (MSB) |
| 1 | 0 | 1 | Timer #2 Latches | LSB Buffer Register |
| 1 | 1 | 0 | MSB Buffer Register | Timer #3 Counter (MSB) |
| 1 | 1 | 1 | Timer #3 Latches | LSB Buffer Register |

- **Writing a latch:** write the MSB to the (single, shared) **MSB Buffer**, then write the LSB
  to "Timer #X Latches". The LSB write transfers MSB buffer + LSB into the 16-bit latch. A
  6800/6809 `STX`/`STD` to the MSB address does exactly this (MSB first).
- **Reading a counter:** reading "Timer #X Counter" returns the counter's MSB **and copies its
  LSB into the LSB Buffer**, which the next address returns. `LDX`/`LDD` gives a coherent
  16-bit value.
- ⚠ **Do not use read-modify-write instructions** (`INC`, `ASL`, …) on the PTM: R/W̄ is a
  register select, so the write goes to a different register than the read.
- The three MSB-buffer addresses are one buffer; the three LSB-buffer addresses are one buffer.

## Control registers — Table 2

CR2 has its own address. CR1 and CR3 share address 0, selected by **CR20**. After RESET̄ every
control bit is 0 except CR10, so the order CR3, CR2, CR1 works.

| Bit | Meaning |
|---|---|
| CRx7 | Output enable: 0 = Ox held low, 1 = timer output on Ox |
| CRx6 | Interrupt enable: 1 = this timer's flag drives IRQ̄ |
| CRx5–CRx3 | Mode and interrupt control (Table 3) |
| CRx2 | 0 = one 16-bit counter; 1 = dual 8-bit counter |
| CRx1 | Clock source: 0 = external Cx̄; 1 = the E clock |
| CR10 | **Internal reset**: 1 = all timers held preset; 0 = timers run |
| CR20 | Address bit: 0 = CR3 at address 0; 1 = CR1 at address 0 |
| CR30 | Timer 3 only: 1 = clock prescaled ÷8 |

**CR10 = 1** presets every counter from its latch, disables every counter clock, sets the
outputs and clears every interrupt flag. Latches and control registers are not disturbed and may
be written while it is set.

## Modes — Tables 3 and 4

| CRx3 | CRx4 | CRx5 | Mode | Counter initialization |
|---|---|---|---|---|
| 0 | 0 | 0 | Continuous | G↓, a latch write, or reset |
| 0 | 1 | 0 | Continuous | G↓ or reset |
| 0 | 0 | 1 | Single-shot | G↓, a latch write, or reset |
| 0 | 1 | 1 | Single-shot | G↓ or reset |
| 1 | 0 | 0 | Frequency comparison: IRQ if gate period < time-out | |
| 1 | 0 | 1 | Frequency comparison: IRQ if gate period > time-out | |
| 1 | 1 | 0 | Pulse-width comparison: IRQ if gate low time < time-out | |
| 1 | 1 | 1 | Pulse-width comparison: IRQ if gate low time > time-out | |

"Reset" means RESET̄ = 0 or CR10 = 1.

## Counting

- **Counter initialization** copies the latch into the counter and clears that timer's
  interrupt flag.
- **Enable:** the counter counts when no reset condition holds and, in continuous mode, the gate
  is low. In single-shot mode the count does not depend on the gate once started.
- **Clock:** each clock is either an E cycle or a recognized **negative transition** of Cx̄
  (synchronized: recognized on the fourth E after the edge; high and low must each last at least
  one E period).
- **16-bit mode:** a time-out comes **N + 1 clocks** after initialization, where N is the latch.
  At time-out the flag is set and the counter reloads from the latch (it "recycles").
- **Dual 8-bit mode:** latch MSB = M, LSB = L. The LSB counts L…0. At each LSB underflow it
  reloads from L and the MSB decrements. A time-out comes after **(L + 1)(M + 1)** clocks.
  Special case: L = 0 behaves like 16-bit with time-out after M + 1 clocks.

## Outputs — Tables 5 and 6

- **Continuous, 16-bit:** Ox is a **square wave** that goes low at initialization and toggles
  at every time-out. Half-period = (N + 1) clocks.
- **Continuous, dual 8-bit:** Ox is low for M(L + 1) + 1 clocks and high for L clocks. Period
  (L + 1)(M + 1).
- **Single-shot:** as continuous, but Ox gives **one** pulse per initialization and then stays
  low. Time-outs still set the flag and recycle. N = 0 (or L = M = 0) disables the output.
- **Comparison modes:** Ox is low until the first time-out, then toggles at each time-out; not
  defined for typical use.
- CRx7 = 0 holds Ox low whatever the mode. Clearing CRx7 while Ox is high takes it low on the
  next E.

## Status register and interrupts

| Bit | Meaning |
|---|---|
| 7 | Composite interrupt flag: I1·CR16 + I2·CR26 + I3·CR36 |
| 2 | I3, timer 3 interrupt flag |
| 1 | I2, timer 2 interrupt flag |
| 0 | I1, timer 1 interrupt flag |
| 6–3 | Unused, read as 0 |

- **IRQ̄** is asserted exactly when status bit 7 is 1. It is open-drain.
- A flag is set at the timer's time-out (in continuous and single-shot modes).
- **A flag is cleared by:**
  - a timer reset (RESET̄ = 0 or CR10 = 1);
  - **reading that timer's counter after reading the status register while the flag was set**
    (the RS-RT sequence; it prevents losing a time-out that happens between the two reads);
  - a latch write (W) or a counter initialization that affects that timer.

## RESET̄

A low RESET̄ (synchronized over two to three E cycles): presets every latch to **FFFF**, clears
every control bit except **CR10, which is set**, copies the latches into the counters, resets
the outputs and disables the clocks, and clears every status flag.
