# examples

**Machines that boot.** Each directory here is self-contained: a `.toml` that describes the
machine, the media it needs lying beside it, and a note saying what you will see. Copy any one of
them anywhere and it still runs — a path inside a machine file resolves against **that file**, not
against the directory you launched from.

## Machines

```
swtpcsim examples/altair680/altair680-uio.toml      # Altair 680b + Universal I/O board
swtpcsim examples/altair680/altair680-kcacr.toml    # Altair 680b + KCACR audio-cassette interface
swtpcsim examples/flex/flex2-40.toml                # SWTPC 6800 + DC-4, boots FLEX 2.0  ($ D)
swtpcsim examples/cp68/cp68.toml                    # SWTPC 6800 + DC-4, boots CP/68 1.0 ($ D)
swtpcsim examples/debugger/debugger.toml            # a bare 6800 for learning the symbolic debugger
```

| | What it is |
|---|---|
| [`altair680/`](altair680/) | The built-in **`altair680`** machine — a Motorola 6800 with MON680 on its onboard 6850 console, to the `.` prompt — with a period expansion board added. `altair680-uio.toml` fits the **Universal I/O board** (`680uio`): a second 6850 serial port and a 6820 PIA. `altair680-kcacr.toml` fits the **KCACR audio-cassette interface** (`680kcacr`) and its loader PROM at `FD00`; `.J FD00` reads an S-record cassette into memory. See the directory's own README. |
| [`flex/`](flex/) | The built-in **`swtpc`** machine — a Motorola 6800 with SWTBUG on its MP-S console and a DC-4 floppy (`dc4`) — with a **FLEX** boot disk mounted. Six machine files cover FLEX 2.0 and 3.0 in the DC-4's three geometries (35-track/40-track single-sided, 40-track double-sided); `$ D` boots FLEX to its `+++` prompt. `FLEX2-40.DSK` ships; the rest are a download away. See the directory's own README. |
| [`cp68/`](cp68/) | The same `swtpc` machine booting **TSC's CP/68 1.0** (`cp68.toml`) from `CP68.DSK`. `$ D` boots to the `HEMENWAY ASSOCIATES CP/68-1.0` sign-on and its `.` prompt — a CP/M-style DOS on a single-density 128-byte-sector disk whose track 0 is numbered `0,1,2,4..18`. |

## Learning the tools

| | What it is |
|---|---|
| [`debugger/`](debugger/) | A bare 6800 — 32K of RAM and an MP-S console, **no ROM and no disk** — for learning the monitor's symbolic debugger. A 44-byte `HELLO` program (`HELLO.ASM`/`.LST`/`.S19`) you load, disassemble by name, single-step, and break on a label by hand. See the directory's own README. |

## For developers

| | What it is |
|---|---|
| [`boards/lamp/`](boards/lamp/) | A minimal example board (`lamp.h`) — the worked source for [`docs/devguide/adding-a-board.md`](../docs/devguide/adding-a-board.md), which walks through writing a `Board` from scratch. |
