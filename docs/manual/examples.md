# Worked examples

Complete sessions. **Every transcript below was captured from the program**, not typed out
from memory — if it says the machine printed something, the machine printed it.

These are a few of the machines in `examples/`, gone through at length because each one teaches
something about how the machine works rather than merely how to start it. **They are not the
whole of what is there.** Every folder under `examples/` carries its own README — as Markdown
and as a PDF beside it — saying what that machine is and what to type, so the place to see what
you have is the directory itself, not a list in here.

---

## 1. FLEX 2.0 from a floppy

```
$ swtpcsim examples/flex/flex2-40.toml
```

```
startup> RESET
startup> RUN
[console -- ^E returns to the monitor]

$
```

You land at SWTBUG's `$` prompt — the SWTPC's ROM monitor, with a FLEX 2.0 disk sitting in
drive 0. **There is no `BOOT` command.** On a real SWTPC you told SWTBUG to boot the disk with
its **`D`** command, and that is what you do here:

```
$ D
FLEX 2.0
DATE (MM,DD,YY)? 1,15,80

+++
```

`D` reads FLEX's cold-loader off track 0 and jumps into it; FLEX signs on, asks the date, and
drops to its `+++` prompt.

### What is on the disk

```
+++CAT

DRIVE 0  ...
 ...file list...

  ...SECTORS LEFT
+++
```

`CAT` is FLEX's directory command. The disk carries the FLEX utilities and whatever else the
image was built with; `+++` is the FLEX prompt, and the usual FLEX commands work from it.

### Out, and back in

`^E` stops the machine and gives you the monitor. Nothing is lost — a bare `RUN` resumes at the
instruction it was about to execute.

```
+++
STOP -- the machine is still at ADEE. RUN resumes.
H0I1N0Z1V0C0 A=0D B=00 X=8004 SP=A03D PC=ADEE  ...
swtpcsim>
```

Look at the machine while it sits there, then hand the keyboard back with `RUN`:

```
swtpcsim> BOARDS
swtpcsim> SHOW dc40
swtpcsim> DUMP A000
swtpcsim> RUN
```

Your FLEX prompt is exactly where you left it.

### The disk is read-only, and that is on purpose

The example mounts drive 0 with `writeprotect = true`, so looking around FLEX can never dirty
the image — a fresh clone stays a fresh clone, and there is no undo to need. To save files back
(FLEX's `SAVE`, `NEWDISK`, a file copy), drop the `writeprotect` line from the drive in the
`.toml`, or copy the `.DSK` somewhere writable first.

### The board figured out the disk's shape by itself

Nothing in the machine file says how many tracks or sides the disk has. The DC-4 board probes
the geometry from the image's **size** on mount — 100K here is a 40-track single-sided FLEX
disk. Drop a differently-sized FLEX image beside a matching `.toml` and it boots the same way;
the folder holds five sibling machine files for the other geometries FLEX shipped in.

---

## 2. MON680 on a bare Altair 680b

```
$ swtpcsim altair680
```

```
startup> RESET
startup> RUN
[console -- ^E returns to the monitor]

.
```

The `altair680` machine is a MITS Altair 680b: a 6800 with 1K of RAM and its onboard console —
a single 6850 ACIA — and nothing else. It comes up straight into **MON680**, the 680b's ROM
monitor, at a `.` prompt. There is no disk and no operating system; the monitor *is* the
machine's software, and it is how you examine memory, deposit bytes, and run a program.

`M` examines and changes memory a byte at a time:

```
.M 0000 00 01
.M 0001 00 02
```

Type an address after `M` and the monitor prints the byte there and waits; type a new value to
change it, or Return to move on. That — plus `G` to go and the register display — is the whole
of driving a 680b, because in 1976 the monitor was the whole of the software you were given.

`^E` takes the keyboard back to the `swtpcsim>` monitor at any point, and `RUN` resumes:

```
.
STOP -- the machine is still at FF85. RUN resumes.
H0I1N1Z0V1C0 A=51 B=E0 X=0000 SP=00EE PC=FF85  BSR FF24
swtpcsim>
```

---

## 3. A cassette on the 680b's KCACR

```
$ swtpcsim examples/altair680/altair680-kcacr.toml
```

This is the same 680b with a **KCACR** audio-cassette interface added — the 680b's tape board,
a UART recording **Kansas City Standard** FSK, with a loader/punch PROM at `FD00`. A cassette
(`kcacr-demo.tap`) is already in the recorder: a tiny Motorola S-record tape that deposits four
bytes at `0200`. From the `.` prompt:

```
.J FD00
.M 0200 86
```

1. `J FD00` runs the loader PROM. It reads the tape's S-records into memory and returns to the
   monitor at the terminating `S9` record. (On a bad tape it prints one letter and stops: `C` =
   checksum/non-hex, `M` = memory error.)
2. `M 0200` examines `0200` and reads back `86`, the first byte the tape carried — proof the
   tape was read, byte by byte, the way a real KCACR read it.

### The tape is not in the machine file

A machine file describes **hardware** — the processor, the console, the cassette *interface*.
Which cassette is in the recorder is not hardware, so it is not in the file: you `MOUNT` it,
the same verb an operator used to press PLAY.

```
swtpcsim> MOUNT kc0:tape "kcacr-demo.tap"
swtpcsim> REWIND kc0:tape
```

`REWIND` is a verb the **cassette board brings with it** — it exists only because there is a
KCACR in the machine. You need it to load the same tape twice, for exactly the reason you would
have needed it in 1976.

### It loaded in an instant, and a real one took time

The machine runs **flat out** by default, so a tape that took a real 680b the better part of a
minute comes off at once. `SET kc0:tape rate=real` plays it at the true Kansas City speed if you
want period *feel* — what the guest sees is identical either way. The `680kcacr` reads and
writes Kansas City modulation only.

---

## 4. FLEX9 on the SWTPC 6809

```
$ swtpcsim examples/flex9/flex9-40.toml
```

```
S-BUG 1.8 - 56K
>
```

This is the SWTPC 6809: the MP-09 processor board with its **S-BUG** monitor, and a FLEX9 disk
in drive 0. S-BUG sets up the board's address translator, counts the RAM, and prints the total.
Its disk-boot command is **`U`**:

```
>U
Timer not available.

FLEX - Version 2.8:3 - 56K

Date (MM,DD,YY)? 1,15,80

+++CAT

CATALOG OF DRIVE NUMBER 0
DISK: FLEX9-2  #0

 NAME   TYPE    SIZE  PRT  SUR

CAT     .CMD       3
CATF    .CMD       5
CATW    .CMD       2
FLEX    .SYS      25
...
```

`Timer not available.` is FLEX's own message: this machine has no timer board, and FLEX runs
without one. The disk is mounted read-only, as in the FLEX 2.0 example. The folder also has
`flex9-35.toml` and `flex9-DS.toml`, the same FLEX9 on a 35-track disk and on a double-sided
one, and both disks are included.

To see where S-BUG put everything, stop with `^E` and type `SHOW cpu0`. The `dat` line is the
address translator's map, and in a 56K system it maps every 4K block to itself.

---

## Where to go next

- **The examples this chapter did not walk through** — `examples/`, and the README in each folder.
- **The other boards on the 680b** — `examples/altair680/` also has a Universal I/O board example.
- **Telnet into the guest, or wire it to a real serial port** — the serial chapter.
- **Look at the bus while it runs** — the *Debugger* document.
