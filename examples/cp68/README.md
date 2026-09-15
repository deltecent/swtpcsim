# CP/68 on the SWTPC 6800

The built-in `swtpc` machine is a Motorola 6800 with SWTBUG on its MP-S console and a DC-4
floppy controller (`dc4`). `cp68.toml` is that same machine with a **CP/68** boot disk
mounted on drive 0, so it comes up at SWTBUG's `$` prompt ready to boot:

```
cd examples/swtpc/cp68
swtpcsim cp68.toml
$                       (SWTBUG prompt)
D                       (SWTBUG disk boot)
```

`D` is SWTBUG's disk-boot command: it loads CP/68's cold-loader from track 0 and jumps into
it. CP/68 signs on and drops to its `.` prompt:

```
HEMENWAY ASSOCIATES CP/68-1.0
.
```

From there the usual CP/68 commands work.

## The disk

`CP68.DSK` is TSC's **CP/68 1.0**, single-density: 35 tracks × 18 sectors × 128 bytes =
80,640 bytes. Its track 0 is laid out `0,1,2,4..18` (sector ID 0 first, no sector 3) —
unlike FLEX, whose every track starts at sector 1. The DC-4 board probes this geometry from
the image **size** and stamps the track-0 sector-ID map on mount, so nothing in the `.toml`
configures tracks or sides. `docs/sources.md` records the provenance and the exact SHA-256.

## Read-only by default

Drive 0 is mounted `writeprotect = true`, so looking around CP/68 can never dirty the image —
a fresh clone stays a fresh clone, and there is no undo to need. To save files back, drop the
`writeprotect` line from the drive in `cp68.toml`, or copy the `.DSK` somewhere writable
first.

## Where the machine comes from

This file is the shipped `machines/swtpc.toml` with one disk mounted — see that file for why
each board and address is what it is. For the sibling FLEX boot examples, see
`examples/swtpc/flex/`.
