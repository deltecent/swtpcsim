# Motorola MC6809/MC6809E Programming Manual

Source: [MC6809-MC6809E 8-Bit Microprocessor Programming Manual (Motorola Inc.) 1981.pdf](#) — © Motorola Inc. 1981 (M6809PM(AD); original issue March 1, 1981, reprinted May 1983).

This is the **canonical CPU reference** for the SWTPC 6809 machines that `swtpcsim`
emulates. Unlike the 680b/6800 references (where the standard Motorola instruction-set
reprint is deliberately out of scope), the 6809 core *is* this simulator, so this file keeps
the implementer-critical detail in full: the register/flag model, every addressing mode with
its exact index post-byte encoding and extra-cycle/byte costs, the complete instruction set
with opcodes (including the page-2 `$10` and page-3 `$11` prefixes), byte and cycle counts
and flag effects, the interrupt/stacking model, and the 6809-vs-6809E and 6809-vs-6800
differences. Numbers are transcribed from the manual's Programming Aid (Appendix D),
Indexed Addressing Mode Data (Appendix F), and the per-instruction detail pages
(Appendix A); OCR damage in the scan's flag columns and a few opcode digits was corrected
against the canonical, stable MC6809 opcode map, with genuine ambiguities flagged ⚠.

The 6809 is source-code (not binary) upward-compatible with the MC6800: the old addressing
modes are retained and many new ones added. It adds a second stack pointer (U), a second
index register (Y), the Direct Page register (DP), a 16-bit accumulator view (D = A:B), a
hardware unsigned MUL, LEA, TFR/EXG, PSH/PUL of arbitrary register sets, long relative
branches, PC-relative addressing, indexed indirection, and the FIRQ fast interrupt. It is
fully position-independent-code capable.

## Programming Model — Register Set

Five 16-bit and four 8-bit programmer-visible registers.

| Reg | Width | Role |
|---|---|---|
| **A** | 8 | Accumulator A |
| **B** | 8 | Accumulator B |
| **D** | 16 | Concatenation **A:B**, A = most-significant byte. Same physical registers as A and B. |
| **X** | 16 | Index register |
| **Y** | 16 | Index register |
| **U** | 16 | User stack pointer — controlled by the programmer; also indexable |
| **S** | 16 | Hardware stack pointer — used automatically by subroutine calls and interrupts; also indexable |
| **PC** | 16 | Program counter (address of next instruction; usable as an index base in PC-relative modes) |
| **DP** | 8 | Direct Page register — supplies A15–A8 of the effective address in direct addressing. **Cleared to $00 by hardware reset (for M6800 compatibility).** |
| **CC** | 8 | Condition Code register (below) |

X, Y, U, S are collectively the **pointer registers**; all four support the indexed
addressing modes and PSH/PUL. Both stack pointers always point *at* the top item of the
stack (the last byte pushed), and the stack grows toward lower addresses.

## Condition Code Register (CC)

Bit layout (Figure 1-2), MSB → LSB:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Flag | **E** | **F** | **H** | **I** | **N** | **Z** | **V** | **C** |
| Name | Entire | FIRQ mask | Half-carry | IRQ mask | Negative | Zero | Overflow | Carry |

- **C (bit 0)** — Carry/borrow out of bit 7 of the ALU on an 8-bit operation; also the bit shifted out by shifts/rotates. ⚠ For **MUL**, C = bit 7 of the result (special case, used for rounding).
- **V (bit 1)** — Two's-complement (signed) overflow of the previous operation.
- **Z (bit 2)** — Set when the result is zero.
- **N (bit 3)** — Copy of the most-significant bit of the result (sign).
- **I (bit 4)** — IRQ interrupt mask. When set, IRQ is not recognized. Set automatically by reset and on recognition of any interrupt (and by SWI).
- **H (bit 5)** — Half-carry: carry out of bit 3 on an 8-bit **addition** (ADDA/ADDB/ADCA/ADCB). Used only by DAA. ⚠ **Undefined after all subtract-like instructions** (SUB, SBC, CMP, NEG) and after shifts/rotates; unaffected by 16-bit compares.
- **F (bit 6)** — FIRQ interrupt mask. When set, FIRQ is not recognized. Set automatically by reset and on recognition of any interrupt (and by SWI).
- **E (bit 7)** — Entire flag. Records the *last* interrupt-stacking operation: **E=1 → the entire register set was stacked; E=0 → only PC and CC were stacked** (FIRQ). RTI reads the recovered E bit to decide how many registers to pull. ⚠ This is the FIRQ trap — see Interrupts.

Flag-column notation in the tables below: `*` = set/cleared per result, `-` = unaffected,
`0` = cleared, `1` = set, `U` = undefined.

## Addressing Modes

| Mode | Form | Effective address |
|---|---|---|
| **Inherent** | `MUL`, `ABX`, `NOP` | Implied entirely by the opcode (incl. accumulator-only / register-only ops). |
| **Immediate** | `LDA #$F0`, `LDX #$8004` | Operand is the 1 or 2 bytes following the opcode. Postbyte-immediate variants: EXG/TFR (register pair) and PSH/PUL (register set) — see post-byte figures below. |
| **Extended** | `LDA $CAT` | The 2 bytes after the opcode are the absolute 16-bit address. Not position-independent. |
| **Extended indirect** | `LDA [$F000]` | The 2 bytes after the post-byte point at a location holding the final address. Encoded as an indexed post-byte `$9F` (see below). |
| **Direct** | `LDA <CAT` | 16-bit address = **DP** (high byte) : the one byte after the opcode. Reaches 256 bytes in any one of 256 pages. |
| **Indexed** | `LDA ,X` etc. | Register X/Y/U/S (or PC) plus optional offset; a post-byte selects the variation (below). |
| **Relative (branch)** | `BRA`, `LBRA` | Short: 1 signed byte, range −128…+127 from the *following* opcode. Long: 2 signed bytes, range −32768…+32767 (full 64K). PC always points at the next instruction when the offset is added. |
| **PC-relative** | `LDA MSG,PCR`, `LEAX DATA,PCR` | A form of indexed addressing: 8- or 16-bit signed offset added to PC. Position-independent data access. |

### Post-byte for EXG / TFR (Figure 2-1A)

`b7-b4` = source register R1, `b3-b0` = destination register R2. **Pair must be both 8-bit
or both 16-bit.**

| Code | 16-bit reg | Code | 8-bit reg |
|---|---|---|---|
| `0000` | D (A:B) | `1000` | A |
| `0001` | X | `1001` | B |
| `0010` | Y | `1010` | CC |
| `0011` | U | `1011` | DP |
| `0100` | S | | |
| `0101` | PC | | |

⚠ All other bit combinations produce **undefined results**.

### Post-byte for PSH / PUL (Figure 2-1B)

One bit per register; set bit = include in the transfer. PSHS/PSHU push in the fixed order
below (PC first / to highest address, CC last / to lowest); PULS/PULU pull in the reverse
order. `PSHU`/`PULU` use S in the "S/U" slot (they save S), and `PSHS`/`PULS` use U there.

| bit | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 |
|---|---|---|---|---|---|---|---|---|
| reg | PC | U/S | Y | X | DP | B | A | CC |

### Indexed post-byte and extra cycles/bytes (Table F-2)

Register field in bits 6-5 of the post-byte: **X = 00, Y = 01, U = 10, S = 11** (shown as
`RR`). For PC-relative the field is a don't-care (`XX`). "+~" and "+#" are the **extra**
cycles and bytes added on top of the instruction's base indexed cost (the `4+`/`2+` etc. in
the instruction table).

| Type | Assembler form | Post-byte (bin) | +~ | +# | Indirect form | Post-byte (bin) | +~ | +# |
|---|---|---|---|---|---|---|---|---|
| Constant, no offset | `,R` | `1RR00100` | 0 | 0 | `[,R]` | `1RR10100` | 3 | 0 |
| Constant, 5-bit offset | `n,R` | `0RRnnnnn` | 1 | 0 | (defaults to 8-bit) | — | — | — |
| Constant, 8-bit offset | `n,R` | `1RR01000` | 1 | 1 | `[n,R]` | `1RR11000` | 4 | 1 |
| Constant, 16-bit offset | `n,R` | `1RR01001` | 4 | 2 | `[n,R]` | `1RR11001` | 7 | 2 |
| Accumulator A offset | `A,R` | `1RR00110` | 1 | 0 | `[A,R]` | `1RR10110` | 4 | 0 |
| Accumulator B offset | `B,R` | `1RR00101` | 1 | 0 | `[B,R]` | `1RR10101` | 4 | 0 |
| Accumulator D offset | `D,R` | `1RR01011` | 4 | 0 | `[D,R]` | `1RR11011` | 7 | 0 |
| Auto-increment by 1 | `,R+` | `1RR00000` | 2 | 0 | *not allowed* | — | — | — |
| Auto-increment by 2 | `,R++` | `1RR00001` | 3 | 0 | `[,R++]` | `1RR10001` | 6 | 0 |
| Auto-decrement by 1 | `,-R` | `1RR00010` | 2 | 0 | *not allowed* | — | — | — |
| Auto-decrement by 2 | `,--R` | `1RR00011` | 3 | 0 | `[,--R]` | `1RR10011` | 6 | 0 |
| PC-relative, 8-bit offset | `n,PCR` | `1XX01100` | 1 | 1 | `[n,PCR]` | `1XX11100` | 4 | 1 |
| PC-relative, 16-bit offset | `n,PCR` | `1XX01101` | 5 | 2 | `[n,PCR]` | `1XX11101` | 8 | 2 |
| Extended indirect | `[n]` | — | — | — | `[n]` | `10011111` (`$9F`) | 5 | 2 |

Notes for implementers:
- **Auto-increment is *post*-increment; auto-decrement is *pre*-decrement.** Single-step
  (`,R+`/`,-R`) is only allowed non-indirect (you cannot indirect through a half-adjusted
  pointer); double-step (`,R++`/`,--R`) allows indirection because it moves a full pointer.
- 5-bit offset is signed −16…+15 and lives entirely in the post-byte. 8-bit offset is signed
  −128…+127; 16-bit is signed −32768…+32767; both follow the post-byte.
- The designated register (and the accumulator, for accumulator-offset) is **not modified**
  by the offset addition — except of course the auto-inc/dec modes, which do modify it.
- Indirection fetches two bytes at the computed EA to form the final EA; it works with every
  indexed mode listed and with PC-relative.

## Instruction Set

Columns give, per addressing mode, the **op**code (hex), **~** cycles, **#** bytes. `+`
after a value = add the indexed post-byte's extra count from Table F-2. Flags are `H N Z V C`.

### 8-bit accumulator / memory operations

Immediate / Direct / Indexed / Extended forms (register-target A and B variants share the
same immediate/dir/idx/ext structure). Base indexed cost shown as e.g. `A9 4+ 2+`.

| Instr | Imm | Dir | Idx | Ext | Operation | H N Z V C |
|---|---|---|---|---|---|---|
| LDA | 86 2 2 | 96 4 2 | A6 4+ 2+ | B6 5 3 | M→A | - * * 0 - |
| LDB | C6 2 2 | D6 4 2 | E6 4+ 2+ | F6 5 3 | M→B | - * * 0 - |
| STA | — | 97 4 2 | A7 4+ 2+ | B7 5 3 | A→M | - * * 0 - |
| STB | — | D7 4 2 | E7 4+ 2+ | F7 5 3 | B→M | - * * 0 - |
| ADDA | 8B 2 2 | 9B 4 2 | AB 4+ 2+ | BB 5 3 | A+M→A | * * * * * |
| ADDB | CB 2 2 | DB 4 2 | EB 4+ 2+ | FB 5 3 | B+M→B | * * * * * |
| ADCA | 89 2 2 | 99 4 2 | A9 4+ 2+ | B9 5 3 | A+M+C→A | * * * * * |
| ADCB | C9 2 2 | D9 4 2 | E9 4+ 2+ | F9 5 3 | B+M+C→B | * * * * * |
| SUBA | 80 2 2 | 90 4 2 | A0 4+ 2+ | B0 5 3 | A−M→A | U * * * * |
| SUBB | C0 2 2 | D0 4 2 | E0 4+ 2+ | F0 5 3 | B−M→B | U * * * * |
| SBCA | 82 2 2 | 92 4 2 | A2 4+ 2+ | B2 5 3 | A−M−C→A | U * * * * |
| SBCB | C2 2 2 | D2 4 2 | E2 4+ 2+ | F2 5 3 | B−M−C→B | U * * * * |
| CMPA | 81 2 2 | 91 4 2 | A1 4+ 2+ | B1 5 3 | A−M (test) | U * * * * |
| CMPB | C1 2 2 | D1 4 2 | E1 4+ 2+ | F1 5 3 | B−M (test) | U * * * * |
| ANDA | 84 2 2 | 94 4 2 | A4 4+ 2+ | B4 5 3 | A∧M→A | - * * 0 - |
| ANDB | C4 2 2 | D4 4 2 | E4 4+ 2+ | F4 5 3 | B∧M→B | - * * 0 - |
| ORA | 8A 2 2 | 9A 4 2 | AA 4+ 2+ | BA 5 3 | A∨M→A | - * * 0 - |
| ORB | CA 2 2 | DA 4 2 | EA 4+ 2+ | FA 5 3 | B∨M→B | - * * 0 - |
| EORA | 88 2 2 | 98 4 2 | A8 4+ 2+ | B8 5 3 | A⊕M→A | - * * 0 - |
| EORB | C8 2 2 | D8 4 2 | E8 4+ 2+ | F8 5 3 | B⊕M→B | - * * 0 - |
| BITA | 85 2 2 | 95 4 2 | A5 4+ 2+ | B5 5 3 | A∧M (test) | - * * 0 - |
| BITB | C5 2 2 | D5 4 2 | E5 4+ 2+ | F5 5 3 | B∧M (test) | - * * 0 - |

### 8-bit read-modify-write and accumulator-target operations

Register-A form / Register-B form are inherent (2 cycles, 1 byte); memory forms take
Direct / Indexed / Extended. Flags apply to the result.

| Instr | A form | B form | Dir | Idx | Ext | Operation | H N Z V C |
|---|---|---|---|---|---|---|---|
| NEG | NEGA 40 2 1 | NEGB 50 2 1 | 00 6 2 | 60 6+ 2+ | 70 7 3 | 0−M→M | U * * * * |
| COM | COMA 43 2 1 | COMB 53 2 1 | 03 6 2 | 63 6+ 2+ | 73 7 3 | ~M→M | - * * 0 1 |
| LSR | LSRA 44 2 1 | LSRB 54 2 1 | 04 6 2 | 64 6+ 2+ | 74 7 3 | logical shift right, 0→b7 | - 0 * - * |
| ROR | RORA 46 2 1 | RORB 56 2 1 | 06 6 2 | 66 6+ 2+ | 76 7 3 | rotate right thru C | - * * - * |
| ASR | ASRA 47 2 1 | ASRB 57 2 1 | 07 6 2 | 67 6+ 2+ | 77 7 3 | arithmetic shift right (b7 held) | U * * - * |
| ASL/LSL | 48 2 1 | 58 2 1 | 08 6 2 | 68 6+ 2+ | 78 7 3 | shift left, 0→b0 | U * * * * |
| ROL | ROLA 49 2 1 | ROLB 59 2 1 | 09 6 2 | 69 6+ 2+ | 79 7 3 | rotate left thru C | - * * * * |
| DEC | DECA 4A 2 1 | DECB 5A 2 1 | 0A 6 2 | 6A 6+ 2+ | 7A 7 3 | M−1→M | - * * * - |
| INC | INCA 4C 2 1 | INCB 5C 2 1 | 0C 6 2 | 6C 6+ 2+ | 7C 7 3 | M+1→M | - * * * - |
| TST | TSTA 4D 2 1 | TSTB 5D 2 1 | 0D 6 2 | 6D 6+ 2+ | 7D 7 3 | test M | - * * 0 - |
| CLR | CLRA 4F 2 1 | CLRB 5F 2 1 | 0F 6 2 | 6F 6+ 2+ | 7F 7 3 | 0→M | - 0 1 0 0 |

Shift/rotate detail (Appendix A): **LSL/ASL** V = b7⊕b6 of the original, C = old b7, N = new
b7. **ASR** keeps the sign bit (b7), C = old b0, V unaffected. **LSR** forces N=0, C = old b0.
**ROL** V = b7⊕b6, C = old b7. **ROR** C = old b0, V unaffected. **DEC** V set only when the
operand was $80; **INC** V set only when the operand was $7F. **NEG** V set when operand was
$80, C = NOT(result was 0).

### 16-bit operations

| Instr | Imm | Dir | Idx | Ext | Operation | H N Z V C |
|---|---|---|---|---|---|---|
| LDD | CC 3 3 | DC 5 2 | EC 5+ 2+ | FC 6 3 | M:M+1→D | - * * 0 - |
| STD | — | DD 5 2 | ED 5+ 2+ | FD 6 3 | D→M:M+1 | - * * 0 - |
| LDX | 8E 3 3 | 9E 5 2 | AE 5+ 2+ | BE 6 3 | M:M+1→X | - * * 0 - |
| STX | — | 9F 5 2 | AF 5+ 2+ | BF 6 3 | X→M:M+1 | - * * 0 - |
| LDU | CE 3 3 | DE 5 2 | EE 5+ 2+ | FE 6 3 | M:M+1→U | - * * 0 - |
| STU | — | DF 5 2 | EF 5+ 2+ | FF 6 3 | U→M:M+1 | - * * 0 - |
| ADDD | C3 4 3 | D3 6 2 | E3 6+ 2+ | F3 7 3 | D+M:M+1→D | - * * * * |
| SUBD | 83 4 3 | 93 6 2 | A3 6+ 2+ | B3 7 3 | D−M:M+1→D | - * * * * |
| CMPX | 8C 4 3 | 9C 6 2 | AC 6+ 2+ | BC 7 3 | X−M:M+1 (test) | - * * * * |

### Page-2 (`$10` prefix) 16-bit operations

Opcode column shows the byte(s) **after** the `$10` prefix; cycles and bytes are the totals
including the prefix.

| Instr | Imm | Dir | Idx | Ext | Operation | H N Z V C |
|---|---|---|---|---|---|---|
| LDY | 10 8E 4 4 | 10 9E 6 3 | 10 AE 6+ 3+ | 10 BE 7 4 | M:M+1→Y | - * * 0 - |
| STY | — | 10 9F 6 3 | 10 AF 6+ 3+ | 10 BF 7 4 | Y→M:M+1 | - * * 0 - |
| LDS | 10 CE 4 4 | 10 DE 6 3 | 10 EE 6+ 3+ | 10 FE 7 4 | M:M+1→S | - * * 0 - |
| STS | — | 10 DF 6 3 | 10 EF 6+ 3+ | 10 FF 7 4 | S→M:M+1 | - * * 0 - |
| CMPD | 10 83 5 4 | 10 93 7 3 | 10 A3 7+ 3+ | 10 B3 8 4 | D−M:M+1 (test) | - * * * * |
| CMPY | 10 8C 5 4 | 10 9C 7 3 | 10 AC 7+ 3+ | 10 BC 8 4 | Y−M:M+1 (test) | - * * * * |

### Page-3 (`$11` prefix) 16-bit operations

| Instr | Imm | Dir | Idx | Ext | Operation | H N Z V C |
|---|---|---|---|---|---|---|
| CMPU | 11 83 5 4 | 11 93 7 3 | 11 A3 7+ 3+ | 11 B3 8 4 | U−M:M+1 (test) | - * * * * |
| CMPS | 11 8C 5 4 | 11 9C 7 3 | 11 AC 7+ 3+ | 11 BC 8 4 | S−M:M+1 (test) | - * * * * |

⚠ 16-bit compares (CMPD/CMPU/CMPX/CMPY/CMPS) leave **H unaffected**, unlike the 8-bit
compares which leave H **undefined**.

### Load Effective Address (indexed only) — new in 6809

| Instr | Idx | Operation | H N Z V C |
|---|---|---|---|
| LEAX | 30 4+ 2+ | EA→X | - - * - - |
| LEAY | 31 4+ 2+ | EA→Y | - - * - - |
| LEAS | 32 4+ 2+ | EA→S | - - - - - |
| LEAU | 33 4+ 2+ | EA→U | - - - - - |

⚠ Only **LEAX/LEAY** affect Z (useful for loop counting); **LEAS/LEAU affect no flags**
(so `LEAS -n,S` for stack allocation is flag-transparent).

### Jump / subroutine

| Instr | Dir | Idx | Ext | Rel | Notes |
|---|---|---|---|---|---|
| JMP | 0E 3 2 | 6E 3+ 2+ | 7E 4 3 | — | EA→PC |
| JSR | 9D 7 2 | AD 7+ 2+ | BD 8 3 | — | push PC on S, EA→PC |
| BSR | — | — | — | 8D 7 2 | short branch to subroutine |
| LBSR | — | — | — | 17 9 3 | long branch to subroutine (no `$10` prefix) |
| RTS | inherent | | | | 39 5 1 — pull PC from S |

None of JMP/JSR/BSR/LBSR/RTS affect flags.

### Branches (relative)

All **short** conditional branches are 2 bytes / 3 cycles. All **long** conditional
branches are formed by the `$10` prefix + the short opcode + a 16-bit offset: 4 bytes, and
**6 cycles if taken / 5 if not taken**. LBRA (`$16`) and LBSR (`$17`) are unprefixed;
LBRN is `$10 21`. Branches do not affect flags.

| Short | op | Long | op | Condition | Class |
|---|---|---|---|---|---|
| BRA | 20 | LBRA | **16** (5 cyc, 3 by) | always | — |
| BRN | 21 | LBRN | 10 21 | never | — |
| BSR | 8D | LBSR | **17** (9 cyc, 3 by) | to subroutine | — |
| BHI | 22 | LBHI | 10 22 | higher (C∨Z = 0) | unsigned |
| BLS | 23 | LBLS | 10 23 | lower or same (C∨Z = 1) | unsigned |
| BCC/BHS | 24 | LBCC/LBHS | 10 24 | C = 0 / higher-or-same | carry / unsigned |
| BCS/BLO | 25 | LBCS/LBLO | 10 25 | C = 1 / lower | carry / unsigned |
| BNE | 26 | LBNE | 10 26 | Z = 0 | — |
| BEQ | 27 | LBEQ | 10 27 | Z = 1 | — |
| BVC | 28 | LBVC | 10 28 | V = 0 | — |
| BVS | 29 | LBVS | 10 29 | V = 1 | — |
| BPL | 2A | LBPL | 10 2A | N = 0 | — |
| BMI | 2B | LBMI | 10 2B | N = 1 | — |
| BGE | 2C | LBGE | 10 2C | ≥ (signed) | signed |
| BLT | 2D | LBLT | 10 2D | < (signed) | signed |
| BGT | 2E | LBGT | 10 2E | > (signed) | signed |
| BLE | 2F | LBLE | 10 2F | ≤ (signed) | signed |

### Inherent / register / control instructions

| Instr | Op | ~ | # | Operation | H N Z V C |
|---|---|---|---|---|---|
| ABX | 3A | 3 | 1 | B(unsigned)+X→X | - - - - - |
| MUL | 3D | 11 | 1 | A×B→D (unsigned) | - - * - * |
| SEX | 1D | 2 | 1 | sign-extend B into A (D) | - * * 0 - |
| DAA | 19 | 2 | 1 | decimal-adjust A after add | - * * U * |
| NOP | 12 | 2 | 1 | no operation | - - - - - |
| ANDCC | 1C | 3 | 2 | CC ∧ imm → CC | per operand |
| ORCC | 1A | 3 | 2 | CC ∨ imm → CC | per operand |
| EXG R1,R2 | 1E | 8 | 2 | exchange register pair | - - - - - |
| TFR R1,R2 | 1F | 6 | 2 | R1 → R2 | - - - - - |
| PSHS | 34 | 5+ | 2 | push register set onto S | - - - - - |
| PULS | 35 | 5+ | 2 | pull register set from S | - - - - - |
| PSHU | 36 | 5+ | 2 | push register set onto U | - - - - - |
| PULU | 37 | 5+ | 2 | pull register set from U | - - - - - |

- **MUL:** C = bit 7 of the 16-bit result (⚠ special case for BCD rounding), Z per result;
  N, V, H unaffected. Unsigned; supports multi-precision signed/unsigned multiply.
- **PSH/PUL** cost **5 cycles + 1 per byte transferred** (each 16-bit register = 2 bytes).
- **DAA** H unaffected, V undefined, C set if a carry was generated *or* C was set before.
- **ANDCC/ORCC** change exactly the CC bits selected by the immediate mask (ANDCC clears,
  ORCC sets); the common use `ANDCC #$AF` clears both I and F to enable interrupts.

### Interrupt / synchronization instructions

| Instr | Op | ~ | # | Operation |
|---|---|---|---|---|
| SWI | 3F | 19 | 1 | set E, stack all, set I and F, vector from FFFA/FFFB |
| SWI2 | 10 3F | 20 | 2 | set E, stack all, vector from FFF4/FFF5 (does **not** set I/F) |
| SWI3 | 11 3F | 20 | 2 | set E, stack all, vector from FFF2/FFF3 (does **not** set I/F) |
| CWAI | 3C | ≥20 | 2 | CC ∧ imm → CC, set E, stack **all**, then wait for interrupt |
| SYNC | 13 | ≥4 | 1 | halt until an interrupt line is asserted |
| RTI | 3B | **6 / 15** | 1 | pull CC; if E=1 pull the entire state (15 cyc), else pull only PC (6 cyc) |

- **CWAI** ANDs CC with the immediate byte (typically to pre-clear I and/or F), sets E,
  **stacks the entire machine state up front**, then waits. When an enabled interrupt
  arrives, no further stacking is needed before vectoring — fast, deterministic latency.
  It replaces the M6800 `CLI`+`WAI` sequence but does **not** tri-state the buses.
  Immediate values: `$FF` enable neither, `$EF` enable IRQ, `$BF` enable FIRQ, `$AF` both.
- **SYNC** enters a sync state (BA/BS = 1/0). Cleared by any interrupt; if the interrupt is
  masked (or too short), execution resumes with the next instruction; if enabled, the
  interrupt is taken. Provides software/hardware handshake with minimal latency.
- **RTI** cycle count is E-bit-dependent — the FIRQ path pulls only CC+PC (6 cycles); the
  full path pulls the entire stacked state (15 cycles).

## Interrupts, Stacking, and Vectors

### Vector table (Table 3-1)

| Vector | Address (MS:LS) |
|---|---|
| Reset (RESET) | FFFE : FFFF |
| NMI | FFFC : FFFD |
| SWI | FFFA : FFFB |
| IRQ | FFF8 : FFF9 |
| FIRQ | FFF6 : FFF7 |
| SWI2 | FFF4 : FFF5 |
| SWI3 | FFF2 : FFF3 |
| Reserved | FFF0 : FFF1 |

### Priority

Highest → lowest: **NMI, SWI, FIRQ, IRQ, SWI2, SWI3.**

### Stacking order (Figure 4-1)

For a full (E=1) interrupt, the hardware pushes onto S (or the selected stack for PSH) in
this order, so that after stacking the **lowest** address holds CC and the **highest** holds
PC. Pull (RTI) recovers in the reverse order.

```
 higher addresses  (SP before stacking)
   PC.H
   PC.L
   U/S.H        (the *other* stack pointer: U saved when using S, S saved when using U)
   U/S.L
   Y.H
   Y.L
   X.H
   X.L
   DP
   B
   A
   CC           <- SP after stacking (points at CC)
 lower addresses
```

That is, in push order (first pushed → last pushed): PC (L then H written as SP
decrements), U/S, Y, X, DP, B, A, CC. 12 bytes for a full stack.

### Interrupt behavior

| Source | Edge/level | Mask | E set? | Registers stacked | Sets on entry |
|---|---|---|---|---|---|
| **NMI** | negative edge | non-maskable | yes | entire state (12 B) | I=1, F=1 |
| **FIRQ** | low level | F bit | **no (E=0)** | ⚠ **only PC + CC (3 B)** | I=1, F=1 |
| **IRQ** | low level | I bit | yes | entire state (12 B) | I=1 (F unchanged) |
| **SWI** | instruction | — | yes | entire state | I=1, F=1 |
| **SWI2** | instruction | — | yes | entire state | (I, F unchanged) |
| **SWI3** | instruction | — | yes | entire state | (I, F unchanged) |
| **RESET** | low > 1 cycle | — | — | none | DP=0, I=1, F=1, PC←(FFFE) |

⚠ **FIRQ is the classic implementer trap.** It clears E and stacks **only PC and CC**, then
sets both I and F. The matching `RTI` inspects the recovered E bit and pulls only PC+CC (6
cycles). A handler that assumes a full frame, or a core that always pulls 12 bytes in RTI,
will corrupt the stack. If a FIRQ handler needs A/X/etc., it must PSH/PUL them itself.

Other traps:
- ⚠ **NMI is blocked after reset until S is first loaded** (e.g. `LDS`). Reset clears the
  interrupt logic and arms NMI only once the hardware stack pointer is initialized. A core
  must model this "NMI disarmed until first S load" state.
- ⚠ **DP is cleared to $00 by reset** — direct-page accesses default to page 0 exactly like
  the M6800 until software loads DP (via `TFR A,DP` — there is no direct `LD DP`).
- SWI sets I and F (locks out both hardware interrupts); **SWI2 and SWI3 do not** mask
  IRQ/FIRQ, so they can be interrupted.
- Interrupt lines are sampled on the falling edge of Q; at least one bus cycle of latency
  occurs before recognition. NMI is edge-sensitive (a low sampled one cycle after a high).

## MC6809 vs MC6809E

The two parts are the same core and instruction set; they differ only in clocking and bus
signals. `swtpcsim` cares about which signals a machine model exposes.

| Aspect | MC6809 | MC6809E |
|---|---|---|
| Clock | On-chip oscillator (**EXTAL/XTAL**); generates **E** and **Q** internally | **E and Q supplied externally** — the CPU is clock-driven from the board |
| Slow-memory hold | **MRDY** input stretches E/Q | (no MRDY; use external clock stretching) |
| Bus arbitration | **DMA/BREQ** input, internal self-refresh | **TSC** (three-state control) tri-states address/data/R̄/W̄; **BUSY** output for read-modify-write / indivisible test-and-set |
| Cycle visibility | — | **LIC** (Last Instruction Cycle) output; **AVMA** (Advanced Valid Memory Address) output predicts next-cycle bus use |
| Shared pins | HALT, RESET, NMI, IRQ, FIRQ, R/W, BA, BS, A0–A15, D0–D7, VCC/VSS on both | same |

`BA:BS` state encoding (both parts): `00` normal/running, `01` interrupt-or-reset
acknowledge (during the 2-cycle vector fetch; decode A3–A1 to identify which vector),
`10` sync acknowledge, `11` halt / bus-grant acknowledge.

The 6809E is the variant used where an external system clock and bus sharing are needed
(e.g. multi-processor or DMA-heavy boards); the plain 6809 is the self-clocked single-master
part. For emulation the register/instruction model is identical.

## New instructions vs. the MC6800 (quick list)

For implementers coming from the 6800 core: **LEAX/LEAY/LEAS/LEAU**, **MUL**,
**PSHS/PSHU/PULS/PULU** (arbitrary register sets, two stacks), **TFR/EXG**, **SEX**,
**ABX**, **CWAI**, **SYNC**, the long branches **LBRA/LBSR** and **LBcc** (`$10` prefix),
**SWI2/SWI3**, the 16-bit **LDD/STD/ADDD/SUBD** and the extra 16-bit compares
(**CMPD/CMPS/CMPU/CMPX/CMPY**, plus **LDS/LDU/LDX/LDY/STS/STU/STX/STY** for the new Y/U
registers), and everything that flows from the **DP register**, the **Y** index register,
the **U** stack, PC-relative addressing, and indexed indirection.

## Notes / uncertainties

- Opcodes, cycle counts, byte counts, addressing-mode encodings and vector addresses here
  are the canonical, long-stable MC6809 values, transcribed from the manual's Appendix D
  (Programming Aid), Appendix F (Indexed Addressing Mode Data / Opcode Map) and Appendix A
  (per-instruction detail). A handful of digits and the flag columns were badly OCR-mangled
  in the scan (e.g. TSTA rendered as `40` where it is `4D`; BLT/LBLT `D`→`0`; BSR `8D`→`80`;
  the shift/rotate flag glyphs); these were corrected against the canonical map and the
  Appendix-A prose, not guessed.
- The scan's flag columns for the shift/rotate group are essentially unreadable as printed;
  the V/C/N semantics given above come from the Appendix-A per-instruction descriptions
  (LSL/ASL V=b7⊕b6, ASR/ROR/LSR C=old b0, etc.), which are unambiguous.
- `~` values marked with `+` require the indexed post-byte adder from Table F-2; PSH/PUL use
  "5 + 1 per byte"; RTI and long conditional branches are execution-path-dependent (noted
  inline). CWAI/SYNC are open-ended (they wait), so their listed cycles are the minimum.
