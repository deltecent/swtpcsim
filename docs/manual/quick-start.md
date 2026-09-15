# Quick start: boot a machine in one command

```
$ ./swtpcsim swtpc
```

On **Windows** the program is `swtpcsim.exe`:

```
> swtpcsim.exe swtpc
```

That is the whole of it.

```
startup> RESET
RESET* pulsed. (Memory is UNTOUCHED -- only POWER loses RAM.)
startup> RUN
[console -- ^E returns to the monitor]

$
```

You are at **SWTBUG**, the SWTPC 6800's ROM monitor. The `$` is its prompt, and it is 1977.
The bare `swtpc` machine comes up here with an empty floppy drive — from SWTBUG you examine
memory, load a tape, or boot a disk.

## Boot FLEX

To bring up an operating system, give the machine a disk and tell SWTBUG to boot it. The
shipped FLEX example is that same machine with a FLEX 2.0 disk already in drive 0:

```
$ ./swtpcsim examples/flex/flex2-40.toml
```

At the `$` prompt, type **`D`** — SWTBUG's disk-boot command:

```
$ D
FLEX 2.0
DATE (MM,DD,YY)? 1,15,80

+++
```

`D` reads FLEX's cold-loader off track 0 and jumps into it; FLEX signs on, asks the date, and
drops to its `+++` prompt. Type `CAT` to see what is on the disk:

```
+++CAT
...directory listing...
+++
```

## What actually happened

Nothing was faked, and it is worth knowing what the one command did, because the rest of the
manual is built on it.

The machine file named a **SWTPC 6800 with SWTBUG in ROM, an MP-S serial console, and a DC-4
floppy controller**, put the FLEX image in drive 0, and then did one more thing: it typed
`RESET` and `RUN` for you. That is what the `startup>` lines are telling you. **There is no
`BOOT` command in this program** — on a real SWTPC you pressed RESET, which jumped into SWTBUG,
and then typed `D` to boot the disk, so that is what the machine file says. Anything you can
type, a machine file can do; it gets no special powers.

From there it is all real: SWTBUG's `D` read the boot sector off track 0, that loader pulled
FLEX into memory and jumped into it, and FLEX printed its banner.

## Where it found the disk — one base directory

You typed **one** path — the machine file — and the simulator found the rest itself: the disk
image, named *inside* that file. Both point at the same place, and knowing where is the whole of
how the program handles directories.

- **The machine file on the command line** is found from **your shell** — the directory you were
  in when you ran the command — because at that instant no machine exists yet.
  `examples/flex/flex2-40.toml` is walked down from there.
- **Everything after resolves against the machine's own directory** — the folder that `.toml`
  lives in. The disk it mounts, a PROM it loads, and anything you type at the `swtpcsim>`
  prompt (`MOUNT`, `LOAD`, `SYMBOLS LOAD`) all come from there. `flex2-40.toml` mounts
  `FLEX2-40.DSK` with no directory at all, and the simulator looks for it *beside the `.toml`*
  — and so would a `MOUNT FLEX2-40.DSK` you type.

Here it is concretely. Say you unzipped the package into `~/swtpcsim` and launched it from
there:

```
$ pwd
/home/you/swtpcsim
$ ./swtpcsim examples/flex/flex2-40.toml
```

- `examples/flex/flex2-40.toml` — **you** named it to the shell, so it is found from where
  you stand: `~/swtpcsim/examples/flex/flex2-40.toml`.
- `FLEX2-40.DSK` — the machine mounts it, so it is found in the machine's directory:
  `~/swtpcsim/examples/flex/FLEX2-40.DSK`. Type `MOUNT dc40:drive0 FLEX2-40.DSK` and you
  get that same file, from that same folder.

Copy that folder somewhere else and it still boots, because the disk's path is tied to the
machine, not to you:

```
$ cp -R examples/flex /tmp/myflex
$ ./swtpcsim /tmp/myflex/flex2-40.toml
```

Your shell never left `~/swtpcsim` — the simulator does not change your working directory — yet
the disk is now read from `/tmp/myflex/FLEX2-40.DSK`, beside the machine file you named, and
anything you type at the prompt is found there too. That is the whole reason every example is a
self-contained folder you can copy anywhere and still boot: the disk, and everything you do to
the machine, travels with the machine file.

To see it, ask — **`SHOW PATHS`** at the `swtpcsim>` prompt prints the base directory: the
machine's own folder. What a machine file mounts, and what you *type* (`MOUNT`, `LOAD`, `SAVE`,
`DO`, or a `-s` script), both resolve against it. For a built-in machine, which has no folder of
its own, it is the directory you launched from.

### The `./`, and running from anywhere

Every command so far began with `./swtpcsim` — which means *the `swtpcsim` in **this** folder*.
A fresh unzip drops the program into a directory your shell does not search for commands, so you
have to point at it, and `./` is how you say *look right here*.

You can make that unnecessary by **installing** the program — copying it into one of the
directories your shell already searches, its **`PATH`** (on macOS and Linux, `/usr/local/bin` is
the usual one). Once it is on your `PATH` you drop the `./` and type just `swtpcsim`, from any
directory at all — including from *inside* an example folder:

```
$ cp swtpcsim /usr/local/bin/          # once: put it on your PATH
$ cd examples/flex
$ pwd
/home/you/swtpcsim/examples/flex
$ swtpcsim flex2-40.toml
```

Now your shell is *inside* `examples/flex`, so the machine file is simply `flex2-40.toml`
with no directory — found from where you are — and the disk it mounts is found beside the file,
as always. The rule did not change; only where the program lives, and which directory you ran it
from, did.

## Getting back out — `^E`

Press **`^E`** (Control-E). This is the **STOP** switch, and it is how you take the keyboard back
from a running program:

```
$
STOP -- the machine is still at E216. RUN resumes.
H0I1N0Z1V0C0 A=0D B=00 X=8004 SP=A03D PC=E216  LDAB 00,X
swtpcsim>
```

You are back at the monitor, and **the machine is stopped exactly where it stood**. The
processor executes nothing while this prompt is up: the `PC=E216` above is where it will still
be in an hour. That is what makes the prompt useful — you can read memory, single-step, and set
a breakpoint, and none of it is a moving target.

Stopped is not **lost**. STOP is not RESET and it is not POWER: every register, every byte of
memory, the disk in the drive and the CPU's place in its own program are all exactly as they
were. That is the whole content of *"the machine is still at E216"* — it is telling you the
machine is intact and says where to pick it up.

Why `^E` and not `^C`? Because **`^C` belongs to the software running on the machine** — so the
host intercepts `^E` before the running program is ever offered the byte, and no program inside
the machine can take it from you. Everything else, `^C` included, goes straight through. (If
`^E` collides with something you need, `CONSOLE stop=1D` moves it to `^]`.)

## Going back in — `RUN`

```
swtpcsim> RUN
```

That is all. The machine never stopped, so it simply picks up where it was, and your `$` (or
FLEX `+++`) prompt is where you left it.

## Leaving — `QUIT`

```
swtpcsim> QUIT
```

There is no `EXIT`. `Q` will do.

## The three things to remember

| | |
|---|---|
| **`^E`** | stop the CPU, back to the monitor. The machine stops where it stands, and loses nothing. |
| **`RUN`** | start the CPU running again. |
| **`QUIT`** | done. |

## Careful: the disk is real, and there is no undo

The FLEX example mounts its disk **read-only** (`writeprotect = true`) for exactly this reason,
so looking around can never dirty the image. When you want to save files back — FLEX's `SAVE`,
`NEWDISK`, a file copy — drop that line, and then anything FLEX writes happens to the file on
your host with nothing keeping a copy. Two ways to be safe:

- **Write-protect it.** `MOUNT dc40:drive0 examples/flex/FLEX2-40.DSK RO` refuses every
  write at the controller, so the file cannot change however the program behaves.
- **Copy the folder** when you actually intend to write. It is self-contained and boots from
  anywhere:

  ```
  $ cp -R examples/flex my-flex
  $ ./swtpcsim my-flex/flex2-40.toml
  ```

## No disk? Start with the 680b instead

If you would rather see something simpler than a whole operating system, try the **`altair680`**
machine: a MITS Altair 680b that comes up straight into its ROM monitor, MON680, at a `.`
prompt — no disk, no boot, just a 6800 and a terminal. The examples chapter walks it through,
along with loading a cassette on the 680b's KCACR interface.
