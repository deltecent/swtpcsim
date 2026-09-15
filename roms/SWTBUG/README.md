# SWTBUG — SWTPC 6800 monitor v1.0

The System Monitor for the **SWTPC 6800** computer, a 1977 replacement for MIKBUG that
prints the `$` prompt. It is a **Motorola 6800** program in **Motorola S-record** form
(`SWTBUG.S19`) — a 1 KB 2716 EPROM at `E000`–`E3FF`.

- **`SWTBUG.S19`** — the image, verbatim from deramp.com. Decodes to `E000`–`E3FF`
  (1024 bytes). The reset/interrupt vectors live in-ROM at `E3F8`–`E3FF`
  (RESET → `START`); on the real board the 1 KB ROM is **mirror-decoded across the top
  8 K**, so the 6800's `FFF8`–`FFFF` vector fetch reads those same bytes. See the
  `relocate` region key in [`docs/manual/ref/boards.md`](../../docs/manual/ref/boards.md)
  and [`machines/swtpc.toml`](../../machines/swtpc.toml).
- **`SWTBUG.ASM`** — the SWTPC source, retained beside the image so the bytes can be
  reconciled against the listing. It equates the console control port at `$8004`
  (`CTLPOR`) and the scratchpad RAM at `$A000`.
- **`SWTBUG_Users_Guide.pdf`** — the SWTPC User's Guide, kept as documentation
  provenance.

Embedded as **`builtin:swtbug`** and used by
[`machines/swtpc.toml`](../../machines/swtpc.toml):

    swtpcsim swtpc

    $

That lone `$` is the prompt. The monitor talks to an MC6850 ACIA at `8004`/`8005`
(the `mps` board, SS-30 slot 1). Its command set is one letter each: `M` examine &
change memory, `G` go, `L` load a Motorola-S-record tape, `R` display registers,
`P`/`E` breakpoint control.

Provenance, size and CRC32 are recorded in [`docs/roms.md`](../../docs/roms.md); a unit
test checks the CRC at build time. Fetched from deramp.com
(`.../downloads/swtpc/`).
