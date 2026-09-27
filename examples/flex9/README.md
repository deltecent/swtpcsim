# FLEX9 on the SWTPC 6809

The built-in `swtpc09` machine is an SWTPC 6809: the MP-09 processor board with the S-BUG
monitor, 56K of RAM, an MP-S console and a DC-4 floppy controller (`dc4`). Each machine file here
is that machine with a TSC **FLEX9 2.8:3** boot disk mounted on drive 0, so it comes up at
S-BUG's `>` prompt ready to boot:

```
swtpcsim examples/flex9/flex9-40.toml
S-BUG 1.8 - 56K
>U                      (S-BUG mini-floppy boot)
```

`U` is S-BUG's mini-floppy boot command. It restores drive 0, reads track 0 sector 1 into
`$C000` and jumps there. FLEX signs on, asks the date, and drops to its `+++` prompt:

```
Timer not available.

FLEX - Version 2.8:3 - 56K

Date (MM,DD,YY)? 1,15,80

+++CAT
```

`Timer not available.` is FLEX's own message: the machine has no timer board, and FLEX runs
without one. From `+++`, `CAT` lists the disk and the usual FLEX commands work.

## The three disks

The same FLEX9 in the three geometries the DC-4 supports. The board finds the geometry from
the image's **size**, so nothing in a `.toml` sets tracks or sides:

| Machine file | Disk | Geometry |
|---|---|---|
| `flex9-35.toml` | `FLEX9-2.83-35.DSK` | 35-track single-sided, 88K |
| `flex9-40.toml` | `FLEX9-2.83-40.DSK` | 40-track single-sided, 100K |
| `flex9-DS.toml` | `FLEX9-2.83-DS.DSK` | 40-track double-sided, 200K |

All three come from Mike Douglas's *PC2Flop and Flop2PC (for SWTPC DC-x Controllers in SWTPC
6809)* on deramp.com:

  https://deramp.com/downloads/swtpc/software/FLEX/6809%20FLEX/Disk%20Image%20Transfer/

## Read-only by default

Drive 0 is mounted `writeprotect = true`, so looking around FLEX can never change the image.
To save files back (FLEX's `SAVE`, `NEWDISK`, a file copy), remove the `writeprotect` line
from the drive in the `.toml`, or copy the `.DSK` somewhere writable first.
