# Motorola M6800 Programming Reference Manual

Source: [Motorola_M6800_Programming_Reference_Manual_M68PRM(D)_Nov76.pdf](#) (Motorola
*M6800 Programming Reference Manual*, © Motorola Inc. 1976; document **M68PRM(D)**, First
Edition, November 1976).

The canonical CPU reference for this project. swtpcsim emulates the SWTPC 6800/6809
machines, whose processor is the Motorola **MC6800** — so this file is the authoritative,
opcode-level distillation of the 6800 instruction set: the programmer's register/flag model,
the seven addressing modes, and the **complete 72-mnemonic / 197-opcode** instruction set
with per-opcode addressing mode, byte count, machine-cycle count, and H·I·N·Z·V·C flag
effect. It also captures the stack/interrupt/reset model an emulator core needs.

This is the "elsewhere" that the companion [Altair 680b Programming Manual](Altair%20680b%20Programming%20Manual.md)
reference deliberately points to: that reference omits the generic M6800 instruction set on
purpose and keeps only the 680b-specific assembler and 6850-ACIA material. **The full
opcode/cycle/flag detail lives here.** (The 680b, the SWTPC machines, and every other 6800
system share this exact ISA; system-specific I/O addresses are a wiring fact documented per
machine, never in the CPU manual.)

This is a text-only distilled reference written *from* the scan; the scan itself is not
redistributed. Where the scan's OCR is garbled, opcodes and cycle counts below were
reconstructed from the manual's own Table 3-1 (hexadecimal machine-code map), Figure 4-1
(execution times), and the per-instruction definitions in Appendix A, cross-checked for
internal consistency; any residual uncertainty is flagged with ⚠.

## 1. Programming model

The MC6800 is an 8-bit MPU with a **16-bit address bus** (64 KiB) and **memory-mapped I/O**
— there is no separate I/O space and no `IN`/`OUT` instructions; peripheral registers (PIA,
ACIA, …) occupy ordinary memory addresses. Clock rates up to 1 MHz; instruction time is a
fixed number of clock (machine) cycles per opcode. 40-pin package.

The program-visible registers (Figure 2-2):

| Register | Width | Role |
|---|---|---|
| **ACCA** | 8 | Accumulator A |
| **ACCB** | 8 | Accumulator B |
| **IX** | 16 | Index register (base for indexed addressing) |
| **PC** | 16 | Program counter |
| **SP** | 16 | Stack pointer (points at the *next free* byte; stack grows **downward**) |
| **CCR** | 8 (6 used) | Condition Code Register: **H I N Z V C** |

The MPU recognizes **197** of the 256 possible opcodes (59 unassigned), forming **72**
distinct mnemonics; the larger opcode count comes from mnemonics that support several
addressing modes. Instructions are **1 to 3 bytes**.

### Condition Code Register (CCR)

Bit layout (Appendix A, §A.1(d)). The two high bits have no flag and **always read as 1**:

```
 bit  7   6   5   4   3   2   1   0
      1   1   H   I   N   Z   V   C
```

| Bit | Name | Meaning |
|---|---|---|
| 5 | **H** | Half-carry — carry out of bit 3. Set by ABA/ADD/ADC only; used by **DAA**. |
| 4 | **I** | Interrupt mask. When set (1), the MPU ignores `IRQ`. Does **not** mask `NMI`. |
| 3 | **N** | Negative — copy of the result's most-significant bit (bit 7, or bit 15 for 16-bit results). |
| 2 | **Z** | Zero — set when the result is all-zero. |
| 1 | **V** | Two's-complement overflow. |
| 0 | **C** | Carry/borrow — carry out of the MSB (add), or borrow (subtract/compare). |

⚠ **Bits 7 and 6 are not implemented and read as 1.** When the CCR is pushed to the stack
(interrupt/SWI/WAI) or transferred to A by **TPA**, bit positions 7 and 6 come out **set**.
Model the CCR as `0xC0 | (H<<5|I<<4|N<<3|Z<<2|V<<1|C)`.

## 2. Addressing modes

The 6800 has **seven** addressing modes. The assembler selects the mode from the operator and
operand syntax; several instructions accept more than one mode (hence 197 opcodes for 72
mnemonics). Byte 2 is the high byte of a 16-bit address/operand; byte 3 is the low byte.

| Mode | Bytes | How it works |
|---|---|---|
| **Inherent** (implied) | 1 | Operand(s) implied by the opcode; no operand field (25 such instructions, e.g. `ABA`, `NOP`, `SWI`). |
| **Accumulator** | 1 | Operand field is just `A` or `B` (13 operators, e.g. `INCA`, `ASRB`, `PSHA`). Read-modify-write and push/pull forms. |
| **Immediate** | 2 (⚠ 3 for CPX/LDS/LDX) | `#value`; the operand byte(s) follow the opcode. |
| **Direct** (zero-page) | 2 | 8-bit address `00`–`FF` in byte 2; addresses only the first 256 bytes. |
| **Extended** | 3 | Full 16-bit absolute address (byte 2 = high, byte 3 = low). |
| **Indexed** | 2 | `offset,X` — byte 2 is an **unsigned 8-bit** offset (0–255) added to IX **at run time** to form the effective address. `,X` / `X` alone = offset 0. |
| **Relative** | 2 | Branches only. Byte 2 is a **signed 8-bit** two's-complement offset. |

**Immediate width exception.** `CPX`, `LDS`, `LDX` take a **16-bit** immediate operand, so
their immediate form is **3 bytes** (operand MS byte first). All other immediate instructions
are 2 bytes with an unsigned 8-bit operand (0–255).

**Direct vs. Extended is chosen by value, at assembly time.** For an operator that supports
both, the assembler emits **direct** (2 bytes) when the address is 0–255 and **extended**
(3 bytes) when it exceeds 255. Some operators support extended but not direct — those always
assemble to 3 bytes regardless of value. This is a semantic choice, not syntactic; model it if
you build a 6800 assembler/disassembler.

**Relative branch range.** With `PC` = address of the branch's first byte and `R` the signed
offset stored in byte 2: the destination `D = (PC + 2) + R`, valid over
`(PC + 2) − 128 ≤ D ≤ (PC + 2) + 127`. To reach farther, use `JMP`/`JSR` (extended/indexed,
not relative).

## 3. Stack, interrupts & reset

### Stack pointer behavior

SP holds the address of the **next free** byte. On a push, the byte is written to `(SP)`, then
SP is **decremented**. On a pull, SP is **incremented first**, then the byte is read. The stack
therefore grows toward lower addresses. `TSX` loads IX = SP + 1; `TXS` loads SP = IX − 1.

### State saved on interrupt / SWI / WAI (Figure 3-2)

Seven bytes are pushed, in this order (highest address first, SP ending 7 below where it
started). If SP = `m` before, the layout after is:

| Address | Contents |
|---|---|
| m | PCL (PC low) |
| m−1 | PCH (PC high) |
| m−2 | IXL |
| m−3 | IXH |
| m−4 | ACCA |
| m−5 | ACCB |
| m−6 | CCR (as `11HINZVC` — **bits 7,6 stored set**) |
| m−7 | ← SP now points here |

Push order: **PCL, PCH, IXL, IXH, ACCA, ACCB, CCR**. `RTI` pulls them in reverse
(CCR, ACCB, ACCA, IXH, IXL, PCH, PCL) and restores all flags including I. The saved PC is the
address of the *next* instruction for `NMI`/`IRQ`; for `SWI`/`WAI` it is the address of the
SWI/WAI instruction **plus one**.

`BSR`/`JSR` push only the 2-byte return address (PCL then PCH); `RTS` pulls it back.

### Vectors

A block of read-only pointers sits at the top of memory (the manual calls the top address
`n`; on a full 64 KiB machine `n` = `FFFF`). Each pointer is 2 bytes, high byte first:

| Vector | Address (hi/lo) | Trigger |
|---|---|---|
| **IRQ** (maskable interrupt request) | `FFF8` / `FFF9` | `IRQ` line low **and** I = 0. Manual calls this the "internal interrupt pointer" (`n−7`/`n−6`). |
| **SWI** (software interrupt) | `FFFA` / `FFFB` | `SWI` opcode (`n−5`/`n−4`). |
| **NMI** (non-maskable interrupt) | `FFFC` / `FFFD` | Negative edge on `NMI` (`n−3`/`n−2`); **not** gated by I. |
| **RESET / restart** | `FFFE` / `FFFF` | Positive edge on `RESET`/power-on (`n−1`/`n`). |

On any of IRQ/NMI/SWI: state is stacked (7 bytes above), **I is set to 1**, then PC is loaded
from the vector. RESET does **not** stack anything — it simply loads PC from `FFFE`/`FFFF`
and begins execution (with I set).

### Interrupt behavior notes

- **I bit** masks only `IRQ`. `SWI` sets I; `RTI` restores I from the stacked CCR (so I may
  come back either set or clear). `CLI`/`SEI`/`TAP` also change I.
- **Latency: 12 machine cycles** from the end of the instruction in progress — **except
  immediately after a `WAI`, when it is 4 cycles** (`WAI` pre-stacks the state precisely to
  shorten this). ⚠ Load-bearing for cycle-accurate interrupt timing.
- **Look-ahead:** if an interrupt/halt is signalled during the last cycle of an instruction,
  the request is deferred until the *following* instruction also completes.
- **WAI:** pushes state, then halts until an interrupt arrives. If I = 1 when `WAI` runs, only
  `NMI` or `RESET` can resume it. When the `IRQ` arrives (I=0), the MPU sets I and vectors
  **without re-stacking** (state was already saved by WAI) — hence the 4-cycle latency.

## 4. Instruction set

Legend for the flag columns: **●** = set or cleared according to the operation's rule; **–** =
not affected; **0** = always cleared; **1** = always set; **⇅** = restored from stack;
**?** = undefined. Cycle counts are machine (clock) cycles. Opcodes are hexadecimal.

### 4.1 Accumulator / memory — read operations

Two-operand forms exist for both accumulators (mnemonic + `A` or `B`, e.g. `LDAA`/`LDAB`).
Modes: IMM (2 B / 2 cyc), DIR (2 B / 3 cyc), EXT (3 B / 4 cyc), IND (2 B / 5 cyc).

| Mnemonic | Operation | IMM (A/B) | DIR (A/B) | EXT (A/B) | IND (A/B) | H | I | N | Z | V | C |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **LDA** | ACCX ← M | 86 / C6 | 96 / D6 | B6 / F6 | A6 / E6 | – | – | ● | ● | 0 | – |
| **ADD** | ACCX ← ACCX + M | 8B / CB | 9B / DB | BB / FB | AB / EB | ● | – | ● | ● | ● | ● |
| **ADC** | ACCX ← ACCX + M + C | 89 / C9 | 99 / D9 | B9 / F9 | A9 / E9 | ● | – | ● | ● | ● | ● |
| **SUB** | ACCX ← ACCX − M | 80 / C0 | 90 / D0 | B0 / F0 | A0 / E0 | – | – | ● | ● | ● | ● |
| **SBC** | ACCX ← ACCX − M − C | 82 / C2 | 92 / D2 | B2 / F2 | A2 / E2 | – | – | ● | ● | ● | ● |
| **CMP** | ACCX − M (test only) | 81 / C1 | 91 / D1 | B1 / F1 | A1 / E1 | – | – | ● | ● | ● | ● |
| **AND** | ACCX ← ACCX ∧ M | 84 / C4 | 94 / D4 | B4 / F4 | A4 / E4 | – | – | ● | ● | 0 | – |
| **ORA** | ACCX ← ACCX ∨ M | 8A / CA | 9A / DA | BA / FA | AA / EA | – | – | ● | ● | 0 | – |
| **EOR** | ACCX ← ACCX ⊕ M | 88 / C8 | 98 / D8 | B8 / F8 | A8 / E8 | – | – | ● | ● | 0 | – |
| **BIT** | ACCX ∧ M (test only) | 85 / C5 | 95 / D5 | B5 / F5 | A5 / E5 | – | – | ● | ● | 0 | – |

For SUB/SBC/CMP, **C is the borrow** (set if the subtrahend, plus prior borrow for SBC, exceeds
the minuend). LDAA immediate `86 nn` is the workhorse constant load.

### 4.2 Accumulator / memory — store operations

STA has no immediate form. Modes: DIR (2 B / 4 cyc), EXT (3 B / 5 cyc), IND (2 B / 6 cyc).

| Mnemonic | Operation | DIR (A/B) | EXT (A/B) | IND (A/B) | H | I | N | Z | V | C |
|---|---|---|---|---|---|---|---|---|---|---|
| **STA** | M ← ACCX | 97 / D7 | B7 / F7 | A7 / E7 | – | – | ● | ● | 0 | – |

### 4.3 Read-modify-write / single-operand (memory or either accumulator)

Modes: accumulator A / B (1 B / 2 cyc), EXT (3 B / 6 cyc), IND (2 B / 7 cyc). These operate on
ACCA, ACCB, or a memory byte.

| Mnemonic | Operation | A | B | EXT | IND | H | I | N | Z | V | C |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **CLR** | M/ACCX ← 0 | 4F | 5F | 7F | 6F | – | – | 0 | 1 | 0 | 0 |
| **COM** | M/ACCX ← ¬M (one's comp) | 43 | 53 | 73 | 63 | – | – | ● | ● | 0 | 1 |
| **NEG** | M/ACCX ← 0 − M (two's comp) | 40 | 50 | 70 | 60 | – | – | ● | ● | V* | C* |
| **INC** | M/ACCX ← M + 1 | 4C | 5C | 7C | 6C | – | – | ● | ● | V† | – |
| **DEC** | M/ACCX ← M − 1 | 4A | 5A | 7A | 6A | – | – | ● | ● | V‡ | – |
| **TST** | M − 00 (test only) | 4D | 5D | 7D | 6D | – | – | ● | ● | 0 | 0 |
| **ASL** | arithmetic/logical shift left (C ← b7, b0 ← 0) | 48 | 58 | 78 | 68 | – | – | ● | ● | N⊕C | ● |
| **ASR** | arithmetic shift right (b7 kept, C ← b0) | 47 | 57 | 77 | 67 | – | – | ● | ● | N⊕C | ● |
| **LSR** | logical shift right (b7 ← 0, C ← b0) | 44 | 54 | 74 | 64 | – | – | 0 | ● | N⊕C | ● |
| **ROL** | rotate left through C | 49 | 59 | 79 | 69 | – | – | ● | ● | N⊕C | ● |
| **ROR** | rotate right through C | 46 | 56 | 76 | 66 | – | – | ● | ● | N⊕C | ● |

\* **NEG:** V set **iff** the operand was `80` (the only value that overflows on negation); C set
in all cases **except** when the operand was `00`.
† **INC:** V set **iff** the operand was `7F` (→ `80`).
‡ **DEC:** V set **iff** the operand was `80` (→ `7F`).
For the shifts/rotates, **V = N ⊕ C** (the new N XOR the new C); C is the bit shifted out.

### 4.4 Index register & stack pointer (16-bit) operations

| Mnemonic | Operation | IMM | DIR | EXT | IND | Bytes (IMM/DIR/EXT/IND) | Cyc (IMM/DIR/EXT/IND) | H | I | N | Z | V | C |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **LDX** | IX ← M:M+1 | CE | DE | FE | EE | 3/2/3/2 | 3/4/5/6 | – | – | ● | ● | 0 | – |
| **STX** | M:M+1 ← IX | – | DF | FF | EF | –/2/3/2 | –/5/6/7 | – | – | ● | ● | 0 | – |
| **LDS** | SP ← M:M+1 | 8E | 9E | BE | AE | 3/2/3/2 | 3/4/5/6 | – | – | ● | ● | 0 | – |
| **STS** | M:M+1 ← SP | – | 9F | BF | AF | –/2/3/2 | –/5/6/7 | – | – | ● | ● | 0 | – |
| **CPX** | IX − M:M+1 (compare) | 8C | 9C | BC | AC | 3/2/3/2 | 3/4/5/6 | – | – | N§ | Z§ | V§ | – |

For LDX/LDS/STX/STS, **N reflects bit 15** of the value and V is cleared.
§ **CPX** is a partial compare: it does two byte subtractions (IXH−M, IXL−M+1). **Z** is set
iff *both* halves are equal (usable for `BEQ`/`BNE`). ⚠ **N and V are derived only from the
high-byte subtraction and are *not* meaningful for signed branches** — the manual explicitly
says they "are not intended for conditional branching." **C is not affected.**

Register-only stack/index moves (all **inherent, 1 byte**, no flags affected except where
noted):

| Mnemonic | Operation | Opcode | Cyc | Flags |
|---|---|---|---|---|
| **INX** | IX ← IX + 1 | 08 | 4 | only **Z** ● |
| **DEX** | IX ← IX − 1 | 09 | 4 | only **Z** ● |
| **INS** | SP ← SP + 1 | 31 | 4 | none |
| **DES** | SP ← SP − 1 | 34 | 4 | none |
| **TSX** | IX ← SP + 1 | 30 | 4 | none |
| **TXS** | SP ← IX − 1 | 35 | 4 | none |

⚠ **INX/DEX affect only Z** (not N or V); INS/DES/TSX/TXS affect **no** flags. A common gotcha:
you cannot test the sign of IX after INX/DEX.

### 4.5 Accumulator↔accumulator, transfers, DAA

All **inherent, 1 byte, 2 cycles**:

| Mnemonic | Operation | Opcode | H | I | N | Z | V | C |
|---|---|---|---|---|---|---|---|---|
| **ABA** | ACCA ← ACCA + ACCB | 1B | ● | – | ● | ● | ● | ● |
| **SBA** | ACCA ← ACCA − ACCB | 10 | – | – | ● | ● | ● | ● |
| **CBA** | ACCA − ACCB (compare) | 11 | – | – | ● | ● | ● | ● |
| **TAB** | ACCB ← ACCA | 16 | – | – | ● | ● | 0 | – |
| **TBA** | ACCA ← ACCB | 17 | – | – | ● | ● | 0 | – |
| **DAA** | decimal-adjust ACCA (BCD) | 19 | – | – | ● | ● | ? | ● |

⚠ **DAA** adjusts ACCA to a valid BCD result after ABA/ADD/ADC on packed-BCD operands, using
the current C and H flags. It **adds `00`, `06`, `60`, or `66`** to ACCA and sets C per this
table (from the manual). **V is undefined** after DAA.

| C before | ACCA bits 4-7 | H | ACCA bits 0-3 | Value added | C after |
|---|---|---|---|---|---|
| 0 | 0–9 | 0 | 0–9 | 00 | 0 |
| 0 | 0–8 | 0 | A–F | 06 | 0 |
| 0 | 0–9 | 1 | 0–3 | 06 | 0 |
| 0 | A–F | 0 | 0–9 | 60 | 1 |
| 0 | 9–F | 0 | A–F | 66 | 1 |
| 0 | A–F | 1 | 0–3 | 66 | 1 |
| 1 | 0–2 | 0 | 0–9 | 60 | 1 |
| 1 | 0–2 | 0 | A–F | 66 | 1 |
| 1 | 0–3 | 1 | 0–3 | 66 | 1 |

### 4.6 Condition-code register operations

All **inherent, 1 byte, 2 cycles**:

| Mnemonic | Operation | Opcode | Effect |
|---|---|---|---|
| **CLC** | Clear carry | 0C | C ← 0 |
| **SEC** | Set carry | 0D | C ← 1 |
| **CLV** | Clear overflow | 0A | V ← 0 |
| **SEV** | Set overflow | 0B | V ← 1 |
| **CLI** | Clear interrupt mask | 0E | I ← 0 (enable IRQ) |
| **SEI** | Set interrupt mask | 0F | I ← 1 (disable IRQ) |
| **TAP** | ACCA → CCR | 06 | H,I,N,Z,V,C ← ACCA bits 5-0 (bits 7,6 ignored) |
| **TPA** | CCR → ACCA | 07 | ACCA bits 5-0 ← CCR; **ACCA bits 7,6 ← 1** |

⚠ **TAP** can set/clear **any** flag, including I — it is a way to change the interrupt mask.
**TPA** always returns bits 7 and 6 **set** in ACCA (the unimplemented CCR bits).

### 4.7 Jumps, branches, subroutines, interrupts

Unconditional flow and subroutine/interrupt control:

| Mnemonic | Operation | Mode / opcode | Bytes | Cyc | Flags |
|---|---|---|---|---|---|
| **NOP** | no operation | inherent 01 | 1 | 2 | none |
| **JMP** | PC ← addr | EXT 7E · IND 6E | 3 / 2 | 3 / 4 | none |
| **JSR** | jump to subroutine | EXT BD · IND AD | 3 / 2 | 9 / 8 | none |
| **BSR** | branch to subroutine | REL 8D | 2 | 8 | none |
| **RTS** | return from subroutine | inherent 39 | 1 | 5 | none |
| **RTI** | return from interrupt | inherent 3B | 1 | 10 | **⇅ all restored from stack** |
| **SWI** | software interrupt → `FFFA/B` | inherent 3F | 1 | 12 | **I ← 1**; others unaffected |
| **WAI** | stack state, wait for interrupt | inherent 3E | 1 | 9 | none (I set when the IRQ is taken) |

Conditional branches — **all Relative, 2 bytes, 4 cycles, no flags affected**. Condition is
tested *before* the branch; offset is signed 8-bit (§2).

| Mnemonic | Opcode | Branch taken when | Signed/unsigned use (after CMP/SUB/CBA/SBA) |
|---|---|---|---|
| **BRA** | 20 | always | unconditional |
| **BCC** | 24 | C = 0 | unsigned ≥ (a.k.a. BHS) |
| **BCS** | 25 | C = 1 | unsigned < (a.k.a. BLO) |
| **BEQ** | 27 | Z = 1 | = |
| **BNE** | 26 | Z = 0 | ≠ |
| **BHI** | 22 | C ∨ Z = 0 | unsigned > |
| **BLS** | 23 | C ∨ Z = 1 | unsigned ≤ |
| **BPL** | 2A | N = 0 | result ≥ 0 |
| **BMI** | 2B | N = 1 | result < 0 |
| **BVC** | 28 | V = 0 | no overflow |
| **BVS** | 29 | V = 1 | overflow |
| **BGE** | 2C | N ⊕ V = 0 | signed ≥ |
| **BLT** | 2D | N ⊕ V = 1 | signed < |
| **BGT** | 2E | Z ∨ (N ⊕ V) = 0 | signed > |
| **BLE** | 2F | Z ∨ (N ⊕ V) = 1 | signed ≤ |

### 4.8 Stack data operations

| Mnemonic | Operation | Opcode (A/B) | Bytes | Cyc | Flags |
|---|---|---|---|---|---|
| **PSH** | push ACCX → stack | 36 / 37 | 1 | 4 | none |
| **PUL** | pull stack → ACCX | 32 / 33 | 1 | 4 | none |

## 5. Opcode map (Table 3-1)

The first (or only) byte identifies both the instruction and its addressing mode. Unassigned
codes (`*`) — **59 of them** — are not guaranteed to do anything; see quirks. Columns below are
the high nibble; each cell is mnemonic + mode (IMM/DIR/EXT/IND/REL, or accumulator A/B, or
inherent). This is the reconstructed, corrected form of the scan's Table 3-1.

- `00`–`1F`, `30`–`3F`: inherent / accumulator-transfer / stack / CCR / flow (NOP 01, TAP 06,
  TPA 07, INX 08, DEX 09, CLV 0A, SEV 0B, CLC 0C, SEC 0D, CLI 0E, SEI 0F, SBA 10, CBA 11,
  TAB 16, TBA 17, DAA 19, ABA 1B, TSX 30, INS 31, PULA 32, PULB 33, DES 34, TXS 35, PSHA 36,
  PSHB 37, RTS 39, RTI 3B, WAI 3E, SWI 3F).
- `20`–`2F`: relative branches (BRA 20, BHI 22, BLS 23, BCC 24, BCS 25, BNE 26, BEQ 27, BVC 28,
  BVS 29, BPL 2A, BMI 2B, BGE 2C, BLT 2D, BGT 2E, BLE 2F). `8D` = BSR.
- `40`–`4F` = ACCA single-operand ops; `50`–`5F` = ACCB single-operand ops;
  `60`–`6F` = same ops **indexed**; `70`–`7F` = same ops **extended** (NEG …0, COM …3, LSR …4,
  ROR …6, ASR …7, ASL …8, ROL …9, DEC …A, INC …C, TST …D, JMP/CLR …E/…F). `6E`/`7E` = JMP.
- `80`–`8F` = ACCA IMM, `90`–`9F` = ACCA DIR, `A0`–`AF` = ACCA/index IND, `B0`–`BF` = ACCA/index
  EXT; `C0`–`CF` = ACCB IMM, `D0`–`DF` = ACCB DIR, `E0`–`EF` = ACCB/index IND,
  `F0`–`FF` = ACCB/index EXT. Within each block the low nibble selects the operation
  (SUB 0, CMP 1, SBC 2, AND 4, BIT 5, LDA 6, STA 7, EOR 8, ADC 9, ORA A, ADD B) plus the 16-bit
  index/stack ops at C/E/F (CPX/LDS/LDX 8C·CE·8E…, JSR AD/BD, LDS/LDX/STS/STX at E/F).

The **59 unassigned** opcodes (reconstructed from the opcode-map structure and validated
against the manual's "59 of the 256 possible codes being unassigned" — note especially that
`STA`/`STAB` have **no immediate** form, so `87` and `C7` are unassigned, and `STS`/`STX` have
none either, so `8F`/`CF`):

```
00 02 03 04 05 12 13 14 15 18 1A 1C 1D 1E 1F 21 38 3A 3C 3D   (20, control column)
41 42 45 4B 4E 51 52 55 5B 5E 61 62 65 6B 71 72 75 7B         (18, single-operand blocks)
83 87 8F 93 9D A3 B3 C3 C7 CC CD CF D3 DC DD E3 EC ED F3 FC FD (21, memory/index blocks)
```

The remaining **197** codes are assigned as tabulated above. Unassigned codes have no defined
behavior in the manual — see §6.

## 6. Quirks & traps for an implementer

- ⚠ **CCR top two bits read as 1.** TPA, and every CCR byte pushed on interrupt/SWI/WAI, come
  out with bits 7,6 = 1 (`0xC0` mask). Guest code that pushes/pops the CCR or does `TPA`/`TAP`
  round-trips relies on this.
- ⚠ **CPX N/V are not signed-branch-safe.** Only Z is meaningful for equality; N and V come
  from the high-byte subtraction alone and C is untouched. `BGT`/`BLT` after CPX do **not** give
  a correct 16-bit signed comparison. Emulate the exact bit rules, not a clean 16-bit compare.
- ⚠ **INX/DEX set only Z; INS/DES/TSX/TXS set nothing.** No sign/carry info from index math.
- ⚠ **NEG of `80` leaves `80`** (sets V); **NEG of `00`** is the only case that clears C.
- ⚠ **DAA leaves V undefined** and depends on H — H is produced only by ABA/ADD/ADC, so DAA is
  only meaningful right after one of those on BCD data.
- ⚠ **Shift/rotate V = N ⊕ C** (post-shift). `LSR` forces N = 0, so its V equals the bit
  shifted out.
- ⚠ **`WAI` stacks state up front** → 4-cycle interrupt latency vs. 12 otherwise; and if I is
  already set when `WAI` executes, only `NMI`/`RESET` can wake it.
- ⚠ **RESET does not stack anything**; NMI/IRQ/SWI stack 7 bytes and set I. NMI ignores I.
- ⚠ **Undocumented opcodes / HCF.** The manual lists 59 codes as unassigned and says nothing
  about their behavior. On real MC6800 silicon several unassigned codes are inert or duplicate
  others, and two — commonly cited as `9D` and `DD` — are the infamous **"HCF" (Halt and Catch
  Fire)** test codes that drive the address bus in a runaway count and can only be exited by
  RESET. The 1976 manual does **not** document any of this; if the emulator needs bug-for-bug
  fidelity, treat unassigned opcodes explicitly rather than assuming NOP. ⚠ The exact HCF
  opcode(s) are **not in this scan** — verify against silicon/errata before relying on them.
- ⚠ **Memory-mapped I/O only.** No `IN`/`OUT`; peripheral registers are ordinary addresses.
  The CPU manual never gives device addresses — those are a per-machine wiring fact (SWTPC,
  680b, etc.).

## 7. Key facts at a glance

| | |
|---|---|
| MPU | Motorola **MC6800**; 8-bit data, 16-bit address (64 KiB), **memory-mapped I/O** |
| Registers | ACCA, ACCB (8) · IX, PC, SP (16) · CCR = `11 H I N Z V C` |
| CCR bits | H=5, I=4, N=3, Z=2, V=1, C=0; **bits 7,6 read/stack as 1** |
| Instruction set | **72** mnemonics / **197** opcodes (59 unassigned); 1–3 bytes |
| Addressing modes | Inherent(1) · Accumulator(1) · Immediate(2, **3 for CPX/LDS/LDX**) · Direct(2) · Extended(3) · Indexed(2, unsigned 8-bit offset + IX) · Relative(2, signed 8-bit) |
| Direct vs Extended | address ≤255 → direct, >255 → extended (assembler decides by value) |
| Branch range | `(PC+2) − 128 … (PC+2) + 127` |
| Stack | SP → next free byte; grows **down**; push writes then decrements |
| Interrupt stack order | PCL, PCH, IXL, IXH, ACCA, ACCB, CCR (7 bytes); CCR bits 7,6 = 1 |
| Vectors | IRQ `FFF8/9` · SWI `FFFA/B` · NMI `FFFC/D` · RESET `FFFE/F` |
| Interrupt masking | I masks IRQ only (not NMI); SWI/IRQ/NMI set I; RTI restores it |
| Interrupt latency | **12 cycles** from end of instruction; **4 cycles** after `WAI` |
| RESET | loads PC from `FFFE/F`, no stacking |
| SWI/WAI/RTI/RTS | 3F/12 · 3E/9 · 3B/10 · 39/5 cycles |
