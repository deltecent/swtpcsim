# M6800 early-mask CLI/SEI erratum — "NOP before SEI"

Source: [6800 NOP before SEI.pdf](#) (a deramp.com note by Mike Douglas quoting the Motorola
*1978 Microcomputer Data Library*, "Condition Code Register Operations," page 1-22;
`https://deramp.com/downloads/swtpc/software/6800%20NOP%20before%20SEI.pdf`). Fetched 2026-09-13.

## The erratum

On **early MC6800 mask sets** the interrupt-mask (`I`) manipulation instructions do not take
effect reliably unless the **preceding instruction has an odd opcode** (least-significant bit
of the opcode = 1). Motorola documented it thus:

> "A CLI-WAI instruction sequence operated properly with early M6800 processors only if the
> preceding instruction was odd (Least Significant Bit = 1.) Similarly it was advisable to
> precede any **SEI** instruction with an odd opcode — such as **NOP**. These precautions are
> not necessary for M6800 processors indicating manufacture in **November, 1977 or later**.
>
> Systems which require an interrupt window to be opened under program control should use a
> **CLI-NOP-SEI** sequence rather than CLI-SEI."

`NOP` is `$01` — an odd opcode — which is why it is the idiomatic guard instruction.

## Why it matters here

- **Period software carries the idiom.** 6800 code written for early machines opens an
  interrupt window with `CLI` / `NOP` / `SEI` rather than `CLI` / `SEI`. Seeing the lone `NOP`
  between the two, do not read it as dead code — it is this precaution.
- **It is a real hardware behavior, not a simulator concern to invent.** Per the project rule,
  never give the emulated CPU a behavior to chase a software symptom. This note exists so the
  early-mask timing quirk is *recognized*, not so it is reproduced.

## Emulation stance

swtpcsim's MC6800 models the **corrected (November-1977-or-later) mask**: `CLI` and `SEI`
change the `I` flag as documented regardless of the preceding opcode, so `CLI`-`NOP`-`SEI` and
`CLI`-`SEI` behave identically. The early-mask quirk is **not** reproduced. This file is kept
as provenance for the idiom and as the record that the omission is deliberate.

## Related

- [Motorola M6800 Programming Reference Manual](Motorola%20M6800%20Programming%20Reference%20Manual.md)
  — the CCR and the `CLI`/`SEI`/`WAI` instruction definitions.
- [`docs/sources.md`](../docs/sources.md) — the source manifest.
