# SBUG — SWTPC 6809 S-BUG monitor v1.8

The monitor in the **SWTPC MP-09** CPU board's ROM socket (IC4). It is a **Motorola 6809**
program in **Motorola S-record** form (`SBUG.S19`) — a 2 KB 2716 EPROM at `F800`–`FFFF`,
decoded on the board's **physical** address.

- **`SBUG.S19`** — the image, from deramp.com's `SBUG_S1.TXT` with its first line removed.
  That line (`:SWTPC S-BUG 1.8`) is a title, not an S-record; every other line is kept
  verbatim. Decodes to `F800`–`FFFF` (2048 bytes). The 6809 vectors are in-ROM at
  `FFF2`–`FFFF` (RESET → `FF00`, S-BUG's `START`).
- **`SBUG.ASM`** — the source (`sbug_src.txt`), commented by Allen Clark and Wallace Watson
  and modified to 1.8 by Randy Jarrett. Kept beside the image so the bytes can be reconciled
  against it. ⚠ Its line 383 holds two instructions on one line
  (`LBSR OUT4H … LBSR OUT2S …`); an assembler would read the second as a comment. **The
  image contains both calls** — trust `SBUG.S19`.

Embedded as **`builtin:sbug`**, the default `rom` of the `mp09` board. S-BUG loads the DAT
for an identity map, searches for RAM (the `$55AA` test, one 4K block at a time), and signs
on over the MP-S console at `E004`:

    S-BUG 1.8 - 56K
    >

The `>` is the prompt. Its commands are one letter each; `U` boots a minifloppy through the
DC-x controller at `E014`/`E018` (track 0, sector 1, into `C000`). See
`reference/S-BUG Monitor.md` for the full walk-through.

This is the unpatched 1.8 image, for the standard system with I/O at `E000`. SWTPC's guide
patches `FF79` from `F1` to `F7` to run it in a 6800 mainframe with I/O at `8000`; that
patch is not applied here.
