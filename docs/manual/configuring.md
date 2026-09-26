# The machine file

A machine file is **TOML**. It lists the boards in the backplane, what is set on each of them,
and what to do once the power is on. That is all it does, and there is nothing else it can do.

This chapter is the definitive description of the format.

## What TOML is

The machine file is written in **TOML** — a plain configuration format meant to be written by
hand and read back a year later without a manual. You need only a handful of rules to read every
example below:

- **`key = value`** — one setting per line: `clock_hz = 2000000`.
- **Quotes are for text.** A string is quoted (`name = "flex"`); a number or a
  true/false is bare (`size = 256`, `idle = true`).
- **`[table]` appears once** — `[machine]` is the machine, `[console]` is your terminal.
- **`[[table]]` — doubled brackets — repeats.** Each `[[board]]` starts another board.
- **A nested `[board.unit.x]` belongs to the block above it**, by name — the indentation
  in these examples is only for the eye.
- **`#` starts a comment** to the end of the line. A comment that begins **`#>`** is a
  *note* — the file prints it to you when it loads (see below).

That is the whole of the syntax. Everything below is which keys go where.

## The one thing to know first

**Anything you can type, a config can do — and nothing more.** A machine file has no special
powers. It cannot boot a disk, because there is no `BOOT` verb; what it can do is *type
`RESET` and `RUN` for you*, which is what the operator did to bring the ROM monitor up. Every
board setting in a machine file is a setting you could have made with `SET` at the prompt, and
every `SET` you make at the prompt is a key you could have written in the file. **A board's
properties *are* its TOML keys.**

There is no separate config schema anywhere in the program. That is why the board reference at
the back of this manual is exhaustive, and why it cannot drift out of date: it is printed from
the same table the monitor resolves against.

## Nothing is silently ignored

**Any unknown table or key is a hard error, with a sentence saying so.** The machine does not
load.

```
mine.toml: unknown [machine] key 'widget'
mine.toml: [[board]] cpu0: cpu0 has no property 'frobnicate'. Known: clock_hz idle achieved_hz
```

This is not fussiness. **A configuration that looks like it set something and did not is worse
than one that will not load**, because you will spend the afternoon debugging the machine
instead of the typo. A misconfiguration in this program cannot be silent.

## Notes the operator sees — `#>`

An ordinary `#` comment is for whoever *reads the file*. A comment that begins **`#>`** is for
whoever *loads it*: its text is printed to you when the machine comes up, right after the
`loaded …` line and before the `startup` commands run. It is where the file's author leaves the
one or two sentences you need to actually use the machine.

```toml
#> Boots FLEX 2.0 from drive 0.
#> At SWTBUG's $ prompt type D to boot the disk, then CAT at the +++ prompt.

[machine]
name = "flex"
base = "swtpc"
startup = ["RESET", "RUN"]
```

- **One `#>` line is one printed line.** Write as many as you like, in a row or scattered
  through the file — they print in the order they appear.
- **`#>` on its own prints a blank line**, so you can space a note into a short paragraph.
- **A `#>` can trail a setting**, too: `name = "flex"  #> the 40-track variant`.
- **Under `--mcp` the notes go to stderr.** There stdout carries only the MCP messages, so a
  client reading it sees nothing else; you still see the notes in the terminal.
- It is still a comment. It **sets nothing**, and `CONFIG SAVE` does not write it back — a
  saved machine is the backplane, not the prose around it. If a note is worth keeping, keep it
  in the file you wrote by hand.

An ordinary `#` comment, as always, is seen by no one but the reader of the file.

## The tables

| Table | What it is |
|---|---|
| `[machine]` | the machine's identity. Three keys, no more |
| `[[board]]` | a board. One entry per board |
| `[board.unit.<name>]` | one unit *on* the board above — a serial channel, a tape deck |
| `[[board.region]]` | a memory region on a `memory` board |
| `[[board.drive]]` | a drive on a disk controller |
| `[console]` | **your terminal.** Not a board — see below |

## `[machine]` — and it has exactly three keys

```toml
[machine]
name    = "flex"
base    = "swtpc"
startup = ["RESET", "RUN"]
```

### `name`

What the machine is called. That is the whole of it.

### `base` — start from a machine, and say what is *different*

```toml
base = "swtpc"            # a built-in
base = "../flex/flex2-40.toml"  # or a file
```

The value resolves by **the same syntactic rule as the command line**: contains a `/` or ends
in `.toml` → a file; otherwise → a built-in name. (The machines chapter explains why the
filesystem is never probed.) A file path is relative to *this* file.

Two rules about where it goes:

- **`base` is processed before every other key**, whatever order the file is written in.
- **`base` must appear before the first `[[board]]`.** You cannot inherit a backplane you have
  already started modifying.

`base` nests **up to 8 deep**. A machine built on a machine built on `swtpc` is fine.

This is the key that makes the format worth using. Without it, every variant of a machine is a
copy of a hundred lines, and the day you change one of them you change it in six files. With
it, a machine file says only **what is different**, and reads as the diff it actually is.

### `startup` — the operator's keystrokes, written down

```toml
startup = ["RESET", "RUN"]
```

An array of **ordinary monitor commands**, run once the machine is built. Any command. They run
in order, and you see them run — that is the `startup>` line in the quick start.

**Paths inside a `startup` command are relative to the machine file** — the machine's own
directory, which is the base every relative path uses, whether written in the file or typed at
the prompt. So a `startup` line and the same command typed by hand find the same file.

### What `[machine]` will *not* take

```toml
[machine]
clock_hz = 1000000        # ERROR
```

It is **rejected, with an explanation**:

```
mine.toml: clock_hz belongs to the CPU BOARD, not to [machine] --
  the crystal is on the board. Put it in the CPU's [[board]]:
      [[board]]
      type     = "6800"
      id       = "cpu0"
      clock_hz = 1000000
```

The crystal is soldered to the **CPU board**. It is not a property of "the machine" — the
machine is just the box the board is plugged into. If you pull the CPU board out, the crystal
goes with it.

It gets a custom error rather than the generic *unknown key* because it is the one people reach
for first, and being told *where the thing actually lives* is more use than being told it isn't
here.

## `[[board]]` — and it has four forms

This is the heart of the format. **What a `[[board]]` entry means depends on whether it has a
`type`, and whether its `id` is one the base already used.**

| Write | And it means |
|---|---|
| `type` + a **new** `id` | **ADD** the board |
| `type` + an id **from the base** | **REPLACE** the board outright |
| **no** `type` + an id | **MODIFY IN PLACE** |
| `remove = true` + an id | **PULL THE BOARD OUT** |

**`id` is always mandatory.** It is how you refer to the board at the prompt, and how a later
file refers to it here.

### ADD — `type` + a new id

```toml
[[board]]
type = "680uio"
id   = "uio0"
```

A board that was not there is now there. **In a file with no `base`, this is the only form** —
there is nothing to modify, replace or remove.

### REPLACE — `type` + an id the base already used

```toml
[[board]]
type = "mps"
id   = "mps0"
base = 0x8008
```

If the base had a board called `mps0`, it is **gone** — pulled out and thrown away — and a fresh
`mps` is fitted in its place. **Everything the base set on that board is lost**, including the
settings you did not mention. You get the type's defaults, plus whatever you write here.

That is what "replace" means, and it is almost never what you want. You want:

### MODIFY IN PLACE — no `type`

```toml
[[board]]
id   = "cpu0"
clock_hz = 1000000
```

**Leave the `type` out and you are reaching into the board that is already there.** Everything
the base set on `cpu0` stays set; you change the crystal and nothing else.

The absence of `type` is the whole signal. It reads oddly for about a day and then reads as
exactly what it is: *I am not fitting a board, I am adjusting one.*

### REMOVE — `remove = true`

```toml
[[board]]
id     = "uio0"
remove = true
```

The board is pulled out of the backplane. Its ports stop being decoded. Nothing else in the file
may mention it.

### The error that catches a copy-paste

**`type` + an id that *this same file* has already declared is an error.** Not a replace — an
error. Within one file, declaring the same board twice is never something you meant; it is a
block you copied and forgot to rename. The file will not load, and it will tell you which id.

(Across files it is different: an id from your *base* is a board you inherited, and replacing it
is a legitimate thing to want.)

## Everything else on a `[[board]]` is a property

`type`, `id` and `remove` are the only keys the config layer understands. **Every other key is
handed straight to the board.**

```toml
[[board]]
type = "mps"
id   = "mps0"
base = 0x8004        # the mps knows what a slot base is. The config layer does not.
```

The config layer knows nothing about addresses, baud rates or drive counts. It
cannot, and it does not try. It routes the key to the board and the board accepts it or
rejects it by name:

```
mine.toml: [[board]] cpu0: cpu0 has no property 'frobnicate'. Known: clock_hz idle achieved_hz
```

**The full key list for every board is the board reference at the back of this manual.** The
boards chapter says what the boards *are*.

## `[board.unit.<name>]` — settings that belong to one unit

Some boards carry more than one independent thing. A 680b Universal I/O board carries a **6850
serial port** *and* a **6820 PIA** — different chips, not two channels of one: the serial unit
and the PIA section have their own settings, their own endpoint, and they share nothing at all.
So they get their own tables.

```toml
[[board]]
type = "680uio"
id   = "uio0"
base = 0xF000                  # the BOARD's property -- the window both chips live in

  [board.unit.serial]
  baud    = 9600               # the serial port's property
  connect = "console"

  [board.unit.p1a]
  connect = "out:pia.log"      # the PIA's section A is a different chip. It does not care.
```

A key in `[board.unit.serial]` is exactly the key `SET uio0:serial baud=9600` takes at the
prompt. It is the same property, reached two ways.

The board reference lists which boards have units, and what each unit takes.

## `[[board.region]]` — memory

A `memory` board is **a list of regions**, which is why one physical card can carry 32K of RAM
and the monitor PROM up in high memory. The regions are the board.

```toml
[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000            # HEX -- it is an address
  size = "32K"             # DECIMAL -- it is a count

  [[board.region]]
  type  = "rom"
  at    = 0xE000
  mount = "builtin:swtbug" # a file path, or builtin:<name>
```

| Key | |
|---|---|
| `type` | **required.** `ram` or `rom` |
| `at` | the address where the region starts. **Hex** |
| `size` | the size of a `ram` region. **Decimal**. You can use the `K` and `M` suffixes. A `rom` region takes its size from its image, rounded up to a page, so it ignores `size` |
| `mount` | a ROM image: a file path, or `builtin:<name>` |

(A size with a suffix is written as a string — `size = "56K"` — because `56K` is not a number
TOML will accept bare. A plain count needs no quotes: `size = 256`.)

### An empty socket

**A `rom` region with no `mount` is an empty socket**, even when it has a `size`. It decodes
nothing, and reads there float to `FF` — because that is what a bus with nobody driving it does.
It is not zeros, and it is not an error. It is an unpopulated socket on a board that has one,
which is a thing a real machine could be, and software that reads it gets `FF`.

## `[[board.drive]]` — disks

A disk controller addresses drives; a drive holds an image.

```toml
[[board]]
id = "dc40"

  [[board.drive]]
  unit     = 0             # DECIMAL -- it is a drive number
  mount    = "FLEX2-40.DSK"  # relative to THIS FILE
  readonly = false
```

| Key | |
|---|---|
| `unit` | the drive number. **Decimal** |
| `mount` | the image file |
| `readonly` | refuse every write at the controller, so the host file cannot change. For a disk you mean to read — see the disks chapter. `writeprotect` is the same key under the name the rest of the program uses; write either |
| `media` | force a format instead of probing the image |
| `create` | make the file, empty, if it is not there — then mount it. `MOUNT … CREATE`, written down |

`media` is the escape hatch. The controller normally works out the format from the image, and
normally it is right; when it is not — a headerless image, an unusual geometry — you say so.
It is also what says how big a **blank** disk is, since a blank one matches no format at all.
The disks chapter covers the formats, and `create`.

Without `create`, a `mount` naming a file that is not there is an **error and the machine does
not load** — the same rule as everywhere else here, that a thing which looks like it worked
and did not is the worst outcome available.

## Numbers: hex on the wire, decimal for counts

The machine file follows the same number rule as the monitor (the *Monitor* document states it
in full): **anything the 6800 sees on the bus is hex; anything that never reaches the bus is
decimal.**

```toml
base  = 8004      # 0x8004 -- an address is on the wire, so it is HEX
at    = 0xE000    # an address
baud  = 9600      # a rate  -- DECIMAL
size  = 56        # a count -- DECIMAL
```

**`base = 8004` is address `0x8004`, not eight thousand and four.** That is the line that
catches people: a base address is on the wire, the wire is hex, and every SWTPC listing wrote
the console slot as 8004. In your own files, be explicit — the forcing markers all work here:
`0x8004`/`$8004`/`8004h` for hex, `0o...`/`...q` for octal, `0b...` for binary, `#...` for
decimal, and a `K`/`M` suffix is always a decimal count. To read and print in octal throughout,
set `[console] base = octal` (below) — that changes the base, not the rule. Board settings keep
their own base regardless, so `SHOW` prints an address as `0x8004` however you wrote it.

## `[console]` — your terminal, which is not a board

```toml
[console]
stop      = 0x05      # STOP: the key that gets you back to the monitor. ^E  (attn= still accepted)
base      = hex       # hex | octal -- how you read/write addresses, ports, bytes
upper     = false
strip7in  = false
strip7out = false
crlf      = false
echo      = false
bell      = true
bsdel     = "off"
```

`[console]` is **not a `[[board]]`** and it is not in the backplane. It describes *the terminal
you are sitting at* — a piece of equipment on your desk, on the far end of a cable, in 2026. The
machine never knew anything about it.

| Key | |
|---|---|
| `stop` | the STOP key's byte. **Hex.** Default `05` = `^E`. (`attn` is an accepted alias.) |
| `base` | `hex` \| `octal` — how the monitor reads and prints the wire class (addresses, bytes). `octal` is split octal |
| `upper` | fold input to upper case |
| `strip7in` | clear bit 7 of everything the guest receives |
| `strip7out` | clear bit 7 of everything the guest sends |
| `crlf` | translate line endings |
| `echo` | echo typed characters locally |
| `bell` | let the guest ring your terminal's bell |
| `bsdel` | `off` \| `bs` \| `del` — what your Backspace key sends |

Because they are the terminal's, they reach as far as the terminal does and no further: send a
board's console unit somewhere else — `CONNECT mps0:tty socket:2323`, or out a real serial port —
and the far end gets the bytes exactly as the guest wrote them, all eight bits. The settings are
not undone; the byte just no longer passes through the console, and every line in the machine is
8-bit clean. The serial chapter has the full story, and what to set instead.

## The transform chain belongs to the console

The `[console]` keys above (`strip7out`, `upper`, `crlf` …) are the **only** thing in the
simulator that alters a byte, and they belong to the console because that is where a human is
reading text. **Every serial line is 8-bit clean** — there is no bit-masking strap on any board,
because a line may carry XMODEM and a line that eats bit 7 is a line you cannot trust with a
file. So the fix for a guest whose output arrives garbled with high-bit characters is
`strip7out` on the console, never a 7-bit strap on the board. The serial chapter explains why in
full.

## A complete machine file

Small, whole, and it works. No `base` — so every board is an ADD:

```toml
[machine]
name    = "tiny"
startup = ["RESET", "RUN"]

[[board]]
type     = "6800"          # the CPU is a board. The crystal is on it.
id       = "cpu0"
clock_hz = 0               # 0 = flat out. This is the default.

[[board]]
type = "mps"               # the console board
id   = "mps0"
base = 8004                # HEX. Address 0x8004, the SS-30 console slot.

  [board.unit.tty]
  baud    = 9600           # DECIMAL. Nine thousand six hundred.
  connect = "console"

[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "32K"

  [[board.region]]
  type  = "rom"
  at    = 0xE000
  size  = 1024
  mount = "builtin:swtbug"
```

## A `base` delta — and this is the one you will actually write

This is genuine. It is how the FLEX example is built, and it is nine lines:

```toml
[machine]
name    = "flex"
base    = "swtpc"          # a 6800 CPU, an MP-S console, a DC-4 floppy
                           # controller, RAM and SWTBUG in ROM
startup = ["RESET", "RUN"] # the operator's own keystrokes, written down

[[board]]
id = "dc40"                # NO type: modify the controller the base already has

  [[board.drive]]
  unit  = 0
  mount = "FLEX2-40.DSK"   # relative to THIS FILE
```

Everything a SWTPC 6800 is, `swtpc` already was. The only thing this file has to say is *which
floppy is in drive 0*, and *press RESET and RUN* to reach SWTBUG (from there `D` boots the
disk). **That is the whole of the difference, and so that is the whole of the file.**

Note what is not here: no `type` on the `[[board]]`, because `dc40` already exists and we are
adjusting it, not fitting it. Had we written `type = "dc4"`, we would have thrown the base's
controller away and got a fresh one with default settings — and it would still have worked, and
we would never have known we had done it. Leave the `type` out.

## Saving and loading at the prompt

```
swtpcsim> CONFIG SAVE mine.toml
swtpcsim> CONFIG LOAD mine.toml
```

**`CONFIG SAVE` writes the machine you are actually running** — every board, every property, as
it stands right now, including everything you changed with `SET` since you started. It
**round-trips**: load what it wrote and you get the machine back.

**`CONFIG LOAD` is the whole machine, so it replaces the one you have** — the same thing that
naming the file on the command line does, and there is no undo but the file you saved it to.
It is also **all or nothing**: the machine is built off to one side first, so a file that will
not load leaves you exactly where you were rather than halfway between two machines.

Which makes it the fastest way to write a machine file. Build the machine at the prompt with
`BOARDS ADD` and `SET` until it is what you want, then save it, then edit the file down to the
parts you care about — or give it a `base` and delete the rest.

The one part of the file that is not a board is the **`startup` list** — the commands the machine
runs on load, `MOUNT` the disk, `RESET`, `RUN`. You can build that at the prompt too, with
`STARTUP`:

```
swtpcsim> STARTUP ADD MOUNT dc40:drive0 "FLEX2-40.DSK"
swtpcsim> STARTUP ADD RESET
swtpcsim> STARTUP ADD RUN
swtpcsim> STARTUP
  1  MOUNT dc40:drive0 "FLEX2-40.DSK"
  2  RESET
  3  RUN
```

`STARTUP ADD` appends a line exactly as you typed it — quotes and spaces and all, because a
startup entry is just a command line; `STARTUP REMOVE <n>` drops one and `STARTUP CLEAR` empties
the list. Whatever you assemble is what `CONFIG SAVE` writes as `startup = [...]`, so the boot
sequence you tried at the prompt is the boot sequence the file carries.
