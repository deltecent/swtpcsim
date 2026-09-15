# Built-in ROMs

The common ROMs are **compiled into the simulator**. There is no portable place to keep ROM images across macOS, Linux, and Windows — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the binary" are four different answers and every one of them becomes a support question. ROMs are small, so they are embedded and the question disappears: a fresh checkout boots on any OS with nothing to download.

Use one from a `memory` board region:

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xE000
  mount = "builtin:swtbug"
```

`SHOW ROMS` lists what is compiled in. A bare path (`mount = "roms/mine.bin"`) loads a host file instead — **built-ins are a convenience, never a lock-in.**

## The provenance rule

`DESIGN.md` §0.1 says hardware facts come from period manuals and first-hand artifacts, never from another emulator. **A ROM image is a hardware fact**, and an embedded blob with no recorded source is exactly the second-hand fact that rule exists to prevent — worse than a wrong bit in a document, because every piece of software above it would then be debugged against the wrong ground truth, and it would look like a software bug for a very long time.

So **every built-in ROM has a row in the table below**, and no ROM is embedded without one:

| Field | Why |
|---|---|
| **Source** | A specific dump, part, or listing. "From the internet" is not a source. |
| **Size** | Exact bytes. A ROM padded to the next power of two is a *different artifact*. |
| **CRC32** | Verified by a unit test at build time, so a corrupted embed **fails the build**, not a user. |
| **Verified against** | The listing, manual, or hardware the dump was checked against — if it was. Say so honestly when it wasn't. |

## The ROMs

| `builtin:` name | Part | Size | CRC32 | Source | Verified against |
|---|---|---|---|---|---|
| `mon680` | Altair **680b** PROM Monitor (ACIA v1.0) | **256 B** (`FF00`–`FFFF`) | **`397E717F`** | `roms/MON680/` — `MON680.S19` / `MON680.ASM` / `MON680.LST`. **MITS**, the 680b System Monitor for the onboard 6850 ACIA console — a single 256-byte PROM (PROM 1, the highest) holding the monitor and the reset/interrupt vectors (`reference/Altair 680b Theory of Operation.md` §3). Motorola **6800** code in Motorola **S-record** form, decoded by `loadSrec`. Fetched 2026-08-08 from deramp.com (`.../altair/software/altair_680/PROM Monitor/`). | Its own `MON680.LST` listing, byte-for-byte (the CRC is over the decoded `FF00`–`FFFF` image). **And it boots**: `swtpcsim altair680` reaches the `.` prompt and examines its own PROM over the onboard ACIA ([`machines/altair680.toml`](../machines/altair680.toml); `tests/acceptance/altair680.exp`). |
| `swimon` | Altair **680b** PROM Monitor (SWI-breakpoint variant) | **256 B** (`FF00`–`FFFF`) | **`2ABE348F`** | `roms/SWIMON/` — `SWIMON.S19` / `SWIMON.ASM` / `SWIMON.LST`. **MITS**, the same 680b monitor as `mon680` customized to vector `SWI` through `$0010` (so the user can install a `JMP $FFEE` there and use SWI breakpoints); same `FF00`–`FFFF` window, a different image, a different CRC. Fetched 2026-08-08 from deramp.com (`.../altair/software/altair_680/PROM Monitor/`). | Its own `SWIMON.LST` listing, byte-for-byte (CRC over the decoded image). Console path identical to `mon680`, which boots. |
| `kcacr` | Altair **680b** KCACR cassette loader/punch PROM | **256 B** (`FD00`–`FDFF`) | **`A89ADB57`** | `roms/KCACR/` — `KCACR.S19` / `KCACR.ASM` / `KCACR.LST`. The optional 680b PROM (socket V, one below the monitor) that loads and dumps memory over the **KCACR** audio-cassette interface in Motorola S-record form: `.J FD00` loads, `.J FD74` punches. Motorola **6800** code (`.S19`, decoded by `loadSrec`) that calls the MON680 console routines, so it runs with `mon680` present; its listing's equates name the `680kcacr` board's registers — `SIOSR $F010`, `SIODR $F011`. Fetched 2026-08-08 from deramp.com (`.../altair/software/altair_680/PROM for KCACR/`). | Its own `KCACR.LST` (Mike Douglas's July-2022 disassembly), byte-for-byte (CRC over the decoded `FD00`–`FDFF` image). Registers match the `680kcacr` board's unit tests (`tests/test_680kcacr.cpp`). |
| `swtbug` | **SWTPC 6800** SWTBUG monitor v1.0 (1977, MIKBUG replacement) | **1024 B** (`E000`–`E3FF`), contiguous | **`F9130EF4`** | `roms/SWTBUG/` — `SWTBUG.S19` / `SWTBUG.ASM` + `SWTBUG_Users_Guide.pdf`. **Southwest Technical Products Corp.**, the 1 KB 2716 System Monitor that replaced MIKBUG on the SWTPC 6800 (© 1977, prints the `$` prompt). Motorola **6800** code in Motorola **S-record** form, decoded by `loadSrec`. Its console equate is `CTLPOR $8004` and its scratchpad is at `$A000`; the reset/interrupt vectors are in-ROM at `E3F8`–`E3FF` (`ORG $E3F8`: IRQ / SWI / NMI / RESET→`START`), and the board mirror-decodes the 1 KB part across the top 8 K so the 6800's `FFF8`–`FFFF` fetch reads them (the `relocate` region key; see `machines/swtpc.toml`). Fetched from deramp.com (`.../downloads/swtpc/`). | Its own `SWTBUG.ASM` listing, byte-for-byte (CRC over the decoded `E000`–`E3FF` image). **And it boots**: `swtpcsim swtpc` reaches the `$` prompt over the `mps` console, and boots FLEX 2.0 off the DC-4 with the `D` command (`tests/acceptance/flex.exp`). |

## Licensing

These parts are from companies that no longer exist (MITS folded in 1979; Southwest Technical Products is long gone), and vintage ROM images circulate freely — but "freely circulating" is not the same as "licensed to redistribute," and **embedding a ROM in the binary is redistribution** in a way that asking a user to supply a file is not.

This is Patrick's call, not the simulator's, and it is recorded here so it is a decision rather than an accident. If any ROM turns out to be one we should not ship, the fallback costs nothing: it stays a path (`mount = "roms/x.bin"`), the user supplies it once, and `docs/roms.md` says where to get it.

`mon680`, `swimon` and `kcacr` are original **MITS** 680b parts; `swtbug` is **Southwest Technical Products**'. Both companies are long defunct and their images circulate freely in the vintage archives (deramp.com). They ship here on that basis — a decision, made here, reversible to a path if any should not have been.

**Each ROM directory carries its manual and a `README.md`.** The vendor manual PDFs are retained in `roms/<NAME>/` as downloaded, and each `README.md` is distilled from that manual (plus the ROM's own `.ASM` header) — what the ROM is, its load address and entry points, its I/O and RAM needs, and the `mount =` line to use it. These are the one exception to the repo's "no vendor PDFs" rule (`.gitignore`): they are small, they are the primary-source manuals for parts we embed, and keeping them beside the image is the same provenance discipline the table above exists to enforce.

## How it works

At build time, CMake turns each file in `roms/` into a byte array in a generated translation unit, and a registry maps `builtin:<name>` → `{span<const uint8_t>, crc32, description}`. Consequences worth stating:

- **No filesystem access at runtime** for a built-in. It is `.rodata`. Nothing to find, nothing to permission, nothing to ship alongside.
- **The board does not care.** A region takes a `span<const uint8_t>`; whether that came from `.rodata` or a file the host service read is not its business (§7). The `builtin:` scheme is resolved by the config loader, above the board.
- **`CONFIG SAVE` round-trips the name, not the bytes** — `mount = "builtin:swtbug"` in, `mount = "builtin:swtbug"` out.
- **A CRC test per ROM.** Cheap, and it turns "someone's editor mangled a binary" from a mystery into a build failure.
- **The `SHOW ROMS` description is one hand-written line per ROM**, in `roms/<NAME>/DESC`. Every built-in has one, so the column is uniform and each is visible and editable in the tree — not derived by a heuristic no one can see. Editing a `DESC` re-runs the embed; a ROM with no `DESC` still builds (blank column) but warns.

## References

- `DESIGN.md` §0.1 (where hardware facts come from), §7 (host services), §10.2 (`RAW` — how you get bytes into a ROM at runtime).
- `docs/boards/s100-memory.md` — the board that holds them.
