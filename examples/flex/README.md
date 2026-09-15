# FLEX on the SWTPC 6800

The built-in `swtpc` machine is a Motorola 6800 with SWTBUG on its MP-S console and a DC-4
floppy controller (`dc4`) — the hardware TSC's **FLEX** operating system ran on. Each machine
file here is that same machine with a FLEX boot disk mounted on drive 0, so it comes up at
SWTBUG's `$` prompt ready to boot:

```
cd examples/swtpc/flex
swtpcsim flex2-40.toml
$                       (SWTBUG prompt)
D                       (SWTBUG disk boot)
```

`D` is SWTBUG's disk-boot command: it loads FLEX's cold-loader from track 0 and jumps into
it. FLEX signs on and drops to its `+++` prompt. From there `CAT` lists the disk, and the
usual FLEX commands work.

## The six variants

Two FLEX releases, each in the three geometries the DC-4 supports. The board probes the
geometry from the image **size**, so nothing in the `.toml` configures tracks or sides:

| Machine file | Disk | FLEX | Geometry |
|---|---|---|---|
| `flex2-35.toml` | `FLEX2-35.DSK` | 2.0 | 35-track single-sided, 88K |
| `flex2-40.toml` | `FLEX2-40.DSK` | 2.0 | 40-track single-sided, 100K |
| `flex2-DS.toml` | `FLEX2-DS.DSK` | 2.0 | 40-track double-sided, 200K |
| `flex3-35.toml` | `FLEX3-35.DSK` | 3.0 | 35-track single-sided, 88K |
| `flex3-40.toml` | `FLEX3-40.DSK` | 3.0 | 40-track single-sided, 100K |
| `flex3-DS.toml` | `FLEX3-DS.DSK` | 3.0 | 40-track double-sided, 200K |

## Only one disk ships; the rest are one download away

`FLEX2-40.DSK` is the single image kept in the repository — it is what the `swtpc` quick
start and the FLEX acceptance test boot. The other five machine files name sibling images
that are **not** in the tree. They come from Mike Douglas's *FLEX Disk Images for the SWTPC
6800 Computer* on deramp.com — a folder of 6800-compatible FLEX 2.0/3.0 boot disks, with its
own `-ReadMe.pdf` (kept here, also untracked):

  https://deramp.com/downloads/swtpc/software/FLEX/

Download the ones you want and drop each `.DSK` beside its `.toml` — the names above are the
names in that folder. `docs/sources.md` records the provenance and the exact SHA-256 of the
shipped `FLEX2-40.DSK`.

## Read-only by default

Every drive here is mounted `writeprotect = true`, so looking around FLEX can never dirty the
image — a fresh clone stays a fresh clone, and there is no undo to need. To save files back
(FLEX's `SAVE`, `NEWDISK`, PCGET/PCPUT, a file copy), drop the `writeprotect` line from the
drive in the `.toml`, or copy the `.DSK` somewhere writable first.

## Where the machine comes from

These files are the shipped `machines/swtpc.toml` with one disk mounted — see that file for
why each board and address is what it is. For the sibling CP/68 boot example, see
`examples/swtpc/cp68/`.
