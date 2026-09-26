# Disks

A disk on these machines is really three things: a **controller** board on the bus, some number of
**drives** hanging off it, and a **disk image** sitting in one of those drives. The three are
separate, and the manual keeps them separate, because the machine did.

This chapter is about the last two — the **image** and the **drive it goes in** — and the `MOUNT`
workflow that puts one in the other.

The controller is a board. It goes in the machine file. The drives are part of the controller —
the SWTPC **DC-4** addresses up to four of them. The disk goes in a drive, and you may put it there
either way: name it in the machine file, or `MOUNT` it at the prompt. They do the same thing.

> **A disk is not a cassette, and the difference is real.** A machine file *may* name the floppy
> that is in drive 0 — the FLEX example does exactly that — but it never names the tape in the
> recorder. A floppy drive is wired to its controller and the guest can tell what is in it. **A
> cassette is something a human puts in and presses PLAY on**, and it stays at the prompt where
> humans are. See the tapes chapter.

## The controller

`swtpcsim`'s disk controller is the SWTPC **DC-4**, board type `dc4`, and this chapter's examples
use it. Its hardware detail lives in the **Boards** chapter; here it is just the thing the drives
hang off:

| Board | What it is | Drives |
|---|---|---|
| `dc4` | **SWTPC DC-4** — a WD179x floppy controller. The board FLEX boots from. | up to 4 |

The DC-4 is **memory-mapped**, not port-addressed: its drive-select latch and the WD179x registers
answer addresses on the SS-30 bus. Drives are units on the board: `drive0`, `drive1`, and so on up
to four.

## Putting a disk in — `MOUNT`

```
MOUNT <id>[:<unit>] <file> [WP] [CREATE]
```

```
swtpcsim> MOUNT dc40:drive1 games.dsk
```

That is a floppy going into drive 1 of the controller called `dc40`. The socket was empty before;
it isn't now.

**The file has to be there already.** A name that does not exist is not a new disk, it is a typo,
and you are told so rather than handed a disk you did not mean to make:

```
swtpcsim> MOUNT dc40:drive1 my-scratch.dsk
dc40: 'my-scratch.dsk': no such file
dc40: to make a blank one, add CREATE: MOUNT dc40:drive1 my-scratch.dsk CREATE
```

`CREATE` is how you make one on purpose, and "Making a scratch disk" below is what to do with it
once you have.

### An `.imd` is converted on the way in

`MOUNT` on a file whose name ends `.imd` (an **ImageDisk** file, a format much archived floppy
software is distributed in) does not mount the `.imd` itself. It **reads it, converts it to a raw
sector image, writes that beside it as `foo.dsk`, and mounts the `.dsk`** — because a controller
reads raw sectors, not ImageDisk's track records. The conversion is reported so you can check it
against the disk you expected:

```
swtpcsim> MOUNT dc40:drive0 flex.imd
dc40: converted flex.imd -> flex.dsk
dc40:   IMD: FLEX 2.0 system disk
dc40:   102400 bytes
dc40:drive0: mounted flex.dsk
```

It **never overwrites a `.dsk` that is already there** — the same caution as `CREATE`. If
`flex.dsk` already exists you are told to remove it or mount it directly, so a re-mount cannot
silently discard edits you made to the converted disk. From then on the `.dsk` is the disk; the
`.imd` is just where it came from.

`WP` **write-protects the disk**, and it does what a real diskette's write-protect notch did: the
guest may read the disk, and a write is refused at the controller and never reaches the file on
your host.

```
swtpcsim> MOUNT dc40:drive2 golden-master.dsk WP
```

`RO` is accepted and means exactly the same thing. It is the right word for a ROM socket, which is
read-only because of what it is — for a floppy, `WP` is the thing you are actually doing.

A protected disk says so wherever it is listed, so you never have to remember which one you did it
to:

```
swtpcsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dc40:drive2  disk  golden-master.dsk  (write-protected)
```

And if the **host** would not let us write the file — the permissions say no — the disk still
mounts, protected, and says that it was not your idea:

```
  dc40:drive2  disk  master.dsk  (write-protected -- THE HOST WON'T LET US WRITE IT; you did not ask for this)
```

That one is worth reading closely. You did not ask for the protection, so the guest is about to
bounce every write off a disk you believe is writable.

And taking it out:

```
swtpcsim> UNMOUNT dc40:drive1
dc40:drive1: unmounted (the drive is now empty)
```

The socket is empty again. The guest sees a drive with no disk in it, which is a thing a real drive
could be.

### Names, and what you may leave out

Board names are **case-blind everywhere** — `dc40`, `DC40` and `Dc40` are the same board.

Beyond that you may omit **what carries no information**:

- the **trailing index**, when only one board of that type is in the machine. One floppy controller
  means `dc4` finds `dc40`.
- the **unit**, when the board has only one thing you could possibly mount into. A cassette has one
  slot, so it needs no unit.

A floppy controller does not qualify for the second one. It has several drives — up to four on a
DC-4 — and which drive you meant is real information, so you must say it. **Anything genuinely
plural, you must name.**

## …or put it in the machine file

`MOUNT` at the prompt is for the disk you are dealing with *now*. A disk that belongs to a machine
belongs in the machine file, and that is how the shipped examples do it:

```toml
[[board]]
id = "dc40"                    # no `type`: the controller is already there

  [[board.drive]]
  unit         = 0
  mount        = "FLEX2-40.DSK"  # relative to THIS FILE
  # writeprotect = true          # refuse every write; your file cannot change
  # create       = true          # make it blank if it isn't there -- CREATE, in a file
  # media        = "flex40"      # what a blank one is; see "The geometry is probed" below
```

`readonly = true` says the same thing as `writeprotect` — both refuse every write; `MOUNT` takes
`WP` for it, and `SHOW MOUNTS` prints `(write-protected)`.

The two forms do the same thing. The difference is only *when*: one is the drive as the machine
ships, the other is you, at the prompt, changing your mind.

Note the path. It names the file lying **beside the machine file**, with no directory at all —
which is what lets the whole folder be copied somewhere else and still boot. A path inside a
machine file is resolved against **that file**; a path you type is resolved against **your shell**.
The machines chapter has the rest of that rule.

## …or fetch it over the network

Everything so far assumed the image is a file on your host. It need not be. Point `MOUNT` at a
**TNFS server** — the network file system the FujiNet project speaks — and the disk is fetched
across the network instead of read off your disk:

```
swtpcsim> MOUNT dc40:drive0 tnfs://fileserver/flex/games.dsk
swtpcsim> MOUNT dc40:drive1 tnfs://fileserver:16384/scratch.dsk WP
```

The form is `tnfs://<host>[:<port>]/<path>`. The port may be left off and defaults to **16384**,
the TNFS port; the path is where the image lives on the server. A machine file may name one the
same way — `mount = "tnfs://fileserver/flex/games.dsk"` on the drive — and because a `tnfs://` name
carries its own server with it, it is **not** re-based against the machine file or your shell the
way a bare filename is. It means the same thing wherever you write it.

What happens after that is what happens with any disk. The image is pulled in **once, at mount**,
and from then on it behaves exactly like a local one: the guest reads and writes it, the geometry
is probed from its size (below), `SHOW MOUNTS` lists it, and your changes are written back to the
server when you `UNMOUNT` or the guest flushes. `WP` write-protects it just as it does a local
disk, and if the **server** will not let us write, the disk still mounts, protected, and says the
protection was not your idea.

A network can go away in the middle of a session, and a local disk cannot — so this one case is
worth watching for. If the server stops accepting writes while you are using the disk, swtpcsim
**tells you**: it prints a line saying it can no longer save changes to that mount, and that they
are being held in memory only. The guest keeps running, and swtpcsim keeps trying; when the server
comes back it says so and the held changes are saved. But until then, treat those changes as not
yet safe — if you quit or the server never returns, what was written after the warning is lost.

## The geometry is probed, not declared

You do not tell `swtpcsim` what kind of disk you just mounted. It **looks at the file's byte count**
and works it out. The DC-4 recognises the FLEX formats:

| Format | Tracks | Sides | Sectors | Bytes/sector | File size |
|---|---|---|---|---|---|
| `flex35` | 35 | 1 | 10 | 256 | **89,600** |
| `flex40` | 40 | 1 | 10 | 256 | **102,400** |
| `flexds` | 40 | 2 | 10 | 256 | **204,800** |

A file of 102,400 bytes is a 40-track single-sided FLEX floppy. There is nothing else it could be.
This is why the quick start never mentions a format: there was nothing to mention. FLEX numbers its
sectors from 1, and its system information record lives on track 0 — the probe reads the size, and
the driver takes it from there.

**Only the 40-track image ships.** The FLEX example carries a `flex40` disk; a `flex35` or a
`flexds` image is one **you supply**. The board reads all three.

### And a file that matches nothing at all

A blank disk is a file of no size, and a size of zero is not in the table. It mounts anyway, because
refusing it would leave you no way to make a disk:

**A file whose size matches no row is mounted UNFORMATTED, at the widest format the board can
reach**, and the guest's own formatter fills it in. The file **grows as it is written**, out to as
far as the head can step. That is what makes `CREATE` and a period format program add up to a disk.

If you want a blank disk of a particular geometry — because you are about to format it with
something that expects a certain number of tracks — say so with `media`:

```toml
  [[board.drive]]
  unit   = 1
  mount  = "scratch.dsk"
  create = true
  media  = "flex40"        # not "as far as the controller can step"
```

`media` is a machine-file key on the drive, not something you can set at the prompt. It also settles
a truncated image, or a format you are inventing, where the byte count genuinely cannot decide.

## Get back to the prompt before you stop

A mounted disk is mounted **read/write**, and every write goes through to the file on your host as
it happens. A disk operating system buffers, though — a directory update or the tail of a file may
still be in memory when your command returns — so the rule is simple and worth keeping:

> **Get back to FLEX's `+++` prompt before you `UNMOUNT`, copy, or stop the machine.**

At the prompt the guest has finished writing, and the disk on your host is what you think it is.
`^E` back to the monitor from `+++` is safe; `^E` one instant after a program says it has saved your
file, and before the prompt returns, may catch the disk mid-write.

## The disk is real, and there is no undo

Every write goes through to the file on your host, and `SNAPSHOT` will not save you: it captures the
machine's *state*, not your host files, so the disk image is not in it. A guest cannot save your
work onto a disk it is not allowed to write, so read/write is the only default that lets the machine
be a machine — and the price of it is that you can destroy the example disk with one mistyped
command.

**Copy the folder before you experiment.** It is self-contained and boots from anywhere. Use `WP`
(or mount read-only in the machine file, as the shipped FLEX example does) when you want the guest
to look and not touch.

## Making a scratch disk

It is two steps, not two ways, and the second one is the guest's — which is exactly how it was in
1976. **`CREATE` gets you the blank medium; a formatter makes it a disk.**

**Step one, from the monitor.** `CREATE` makes the file and puts it in the drive:

```
swtpcsim> MOUNT dc40:drive1 my-scratch.dsk CREATE
dc40:drive1: created my-scratch.dsk (empty)
dc40:drive1: mounted my-scratch.dsk
```

Zero bytes, and mounted — an unformatted disk, which is a thing you can hold in your hand and a
thing FLEX will refuse to read. That is the correct state for a disk nobody has formatted yet.

**Step two, from inside FLEX.** Boot a system disk on drive 0, put the blank in another drive, and
format it the way the period did — FLEX's own **`NEWDISK`** lays down the track structure and the
system information record. When it finishes, the file on your host has grown from nothing to a
formatted FLEX disk, and the drive is one FLEX will `CAT`, copy to, and put files on.

If you want a blank disk of a particular geometry rather than the widest the controller can step,
say `media = "flex40"` (or `flex35`, `flexds`) on the drive — see "The geometry is probed" above.

Either way, remember which chapter you are in: get back to `+++` before you go looking at the file.

## Looking at what is in the machine

`SHOW` on the controller lists its drives and what is in each one:

```
swtpcsim> SHOW dc40
```

`BOARDS` shows the backplane — every board, where it decodes, and who is fighting whom:

```
swtpcsim> BOARDS
```

Between them they answer nearly every "why is it not booting" question there is, and the first one
is almost always *the disk is in the wrong drive*.
