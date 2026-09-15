# Adding a built-in ROM (`.S19` → embedded image)

A built-in ROM is a directory under `roms/<NAME>/` that the build compiles into the
binary, reachable from a machine file as `mount = "builtin:<name>"`. Every 6800 monitor
this simulator ships — SWTBUG, MON680, the KCACR cassette loader — is one, and **none of
them is assembled here.** They arrive as a **Motorola S-record** image, verbatim from the
period source (deramp.com), and are embedded byte-for-byte. The developer's job is to place
the image, reconcile it against the listing, and wire it in — not to run an assembler.

## What goes in the directory

`roms/SWTBUG/` is the pattern:

| File | |
|---|---|
| `SWTBUG.S19` | **The image.** A Motorola S-record file — the bytes, verbatim from the source archive. This is the only file the build reads. |
| `SWTBUG.ASM` | The period source, kept beside the image so the bytes can be reconciled against the listing. Provenance, not an input. |
| `SWTBUG.LST` | The assembler listing, where the archive has one. Provenance. |
| `DESC` | One hand-written line, shown in `SHOW ROMS`. |
| `README.md` | What the ROM is, where it decodes, and where it came from. |

The build never assembles `SWTBUG.ASM`. It reads `SWTBUG.S19` and nothing else; the source
sits beside the image so a human can check the bytes against it, and so the provenance
travels with them.

## How the embed works

`cmake/embed_roms.cmake` looks in each `roms/<NAME>/` for an image — `*.HEX`, `*.BIN`,
`*.S19` or `*.SREC` — takes the **first** it finds, and writes it into `roms_generated.cpp`
**byte-for-byte**. The extension picks the loader format (`.S19`/`.SREC` → `Format::Srec`,
`.HEX` → `Format::Hex`, otherwise raw `Format::Bin`); the C++ side then parses it with the
**same loader that backs the `LOAD` command and the monitor's `L`** (DESIGN.md 10.3.1). The
CMake script deliberately does *not* understand any of these formats and must not learn to —
the one parser in the binary is the only one there is.

Because it takes the first match, keep **one image per directory.** Ship the `.ASM`/`.LST`
for provenance, but exactly one `.S19`.

## Where it lands in memory

A Motorola S-record carries its own load addresses, so the image **self-places** —
`builtin:swtbug` decodes to `E000`–`E3FF` because that is where its records say to go. Two
consequences are worth knowing:

- A ROM region in the machine file gives the *decode window*; the image places itself inside
  it. `machines/swtpc.toml` mounts `builtin:swtbug` at `E000`, and the records fill
  `E000`–`E3FF`.
- When the same chip is **mirror-decoded** elsewhere — SWTBUG's 1 KB ROM answers the 6800's
  `FFF8`–`FFFF` vector fetch on the real board — the region carries `relocate = true`, which
  shifts the image so its first record lands at the region's `at` (see the `relocate` key in
  [`docs/manual/ref/boards.md`](../manual/ref/boards.md) and the second ROM region in
  `machines/swtpc.toml`).

## Then wire it in

1. Drop the `.S19` (and the `.ASM`/`.LST` provenance) in `roms/<NAME>/` with a one-line
   `DESC` and a `README.md`. The directory becomes a built-in automatically.
2. Add its CRC32 row to [`docs/roms.md`](../roms.md) and a case to `tests/test_roms.cpp` — a
   built-in ROM is a hardware fact, so a mangled embed must fail the **build**, not a user.
   The test CRC is `crc32(Image::flat())`, i.e. over the `FF`-filled span `lo`–`hi`, so a
   part with gaps is `contiguous == false`.
3. Prove it runs. Mount it in a machine and boot: a monitor should reach its prompt and
   respond to a command over the console. Drive it with `--mcp` (see [the MCP
   chapter](../manual/mcp.md)), never a hand-rolled `expect` script.

A worked example, end to end, is [`roms/SWTBUG/`](../../roms/SWTBUG/README.md) — image,
source, `DESC`, and the `docs/roms.md` row and `test_roms.cpp` case that guard it.

## If you have source but no image

It has not come up: every built-in so far arrived as a Motorola S-record. If it ever does —
an OCR'd 6800 listing with no surviving image — the 6800 path is a period 6800 assembler
(TSC's, or FLEX's `ASMB`) emitting an S-record, which then drops in exactly as above. That
workflow is **not** mechanised here, and this guide will not pretend it is until someone has
actually run it and can write down what bit them. And never commit an image a modern
cross-assembler produced without reconciling it, byte-for-byte, against the period listing —
the whole point of a built-in ROM is that it is the real bytes.
