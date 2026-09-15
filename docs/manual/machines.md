# Machines

A **machine** is a backplane with boards in it. Which boards, with what settings, in what
state — that is all a machine is, and it is the only thing `swtpcsim` needs to be told.

You tell it in one of three ways: name a **built-in**, name a **file**, or say **nothing** and
take the default. This chapter is about how that choice is made, and about the one rule that
decides what a path means.

## The command line

```
swtpcsim [options] [machine]
```

| Option | |
|---|---|
| `machine` | a built-in name, **or** a config file if it contains a `/` or ends in `.toml` |
| `-m, --machine <name>` | **always** a built-in name — never a file |
| `-f, --file <path>` | **always** a file — never a built-in name |
| `-n, --none` | an empty backplane. No boards, no memory, nothing |
| `-l, --list` | list the built-in machines and exit |
| `-s, --script <file>` | run a command script, then exit with its status |
| `-x, --exec <cmd>` | run one monitor command, then exit. Repeatable |
| `-i, --interactive` | after `--script`/`--exec`, stay in the monitor |
| `--mcp` | run as an MCP server on stdio |
| `-v, --version` | print the version and exit |
| `-h, --help` | print this help and exit |

**Give exactly one machine.** A positional name *and* a `-m`, or a `-f` *and* a `-n`, is an
error — the program says *give ONE machine* and stops. It does not guess which one you meant.

## How a bare word resolves — and why it never looks at your disk

```
$ swtpcsim swtpc                       a BUILT-IN, by name
$ swtpcsim examples/flex/flex2-40.toml           a FILE, by path
```

The rule is **purely syntactic**. The filesystem is **never probed**:

| The word | What it is |
|---|---|
| contains `/` or `\` | a **file** |
| ends in `.toml` | a **file** |
| anything else | a **built-in name** |

That is deliberate, and it is worth being clear about why, because a simulator that guessed
would be more convenient exactly until the day it was not.

**`swtpcsim swtpc` means `swtpc` in every directory on earth.** It means the same thing on
your machine and on mine. It cannot be hijacked by a file called `swtpc` that happens to be
lying next to you — because the program never asks whether such a file exists. A command in a
script, a line in a README, a habit in your fingers: all of them keep meaning what they meant.

The cost is that `swtpcsim mymachine.toml` needs the extension, and `swtpcsim ./mymachine`
needs the `./`. That is a small price, and if you want the question settled explicitly, settle
it:

```
$ swtpcsim -m swtpc                    a built-in. Stop looking for a file
$ swtpcsim -f ./swtpc                  a file called `swtpc`, no extension, right here
```

`-m` and `-f` are how you say what you mean when the syntax will not say it for you.

## The one file the simulator *finds*

If you name **nothing at all**, and the working directory contains a file called
`swtpcsim.toml`, that machine is loaded — and it says so:

```
$ swtpcsim
swtpcsim: no machine named -- using ./swtpcsim.toml (`-m default` for the built-in).
swtpcsim 1.0.0 -- 6800, full speed.
machine: bench.  HELP for commands.
swtpcsim>
```

**The first line is the announcement**, and it is printed before anything else so you cannot
miss it. The `machine:` line after it names the machine *the file* declares — `bench` here,
not the file it came out of — so the two lines together say both halves: where it came from,
and what it turned out to be.

This is the **only** file the simulator finds rather than is given, and it only happens when
the command line names nothing whatsoever. Name a built-in, a file, or `-n`, and `./swtpcsim.toml`
is ignored — you asked for something, so you get it.

Put one in a project directory and `swtpcsim`, bare, is your machine. **It announces itself
when it does**, so you are never running something you did not know about.

With no `swtpcsim.toml` and no arguments, you get the built-in `default`.

## The built-in machines

A **built-in is a TOML machine file compiled into the binary.** There is nothing privileged
about it: same format, same keys, same rules as one you write yourself. It is in the program
only so that it is always there.

```
$ swtpcsim --list
```

names them, one to a line, each with a sentence saying what it is — and it is the live list,
so it cannot be short of a machine the way a list typed into a chapter can. The machine
reference at the back of this manual is that same table. From the `swtpcsim>` prompt the
list is `SHOW MACHINES`.

To see what is in one — its backplane and its startup — name it:

```
$ swtpcsim -x 'SHOW MACHINE' swtpc
```

or, at the prompt, `SHOW MACHINE swtpc`. A bare `SHOW MACHINE` there is the machine you are
running now.

And to get it as **text you can edit** — the actual machine file, every board, every setting:

```
$ swtpcsim -x 'CONFIG SAVE mine.toml' swtpc
$ swtpcsim mine.toml
```

`CONFIG SAVE` writes the machine you are actually running, and it round-trips. **Which makes
every built-in a worked example.** Find the one closest to what you want, save it out, and edit
it.

Or better, do not copy it at all — start *from* it with `base`, and write down only what is
different. The configuring chapter is about that.

## The empty backplane — `-n`

```
$ swtpcsim -n
```

No boards. No memory. No processor. `-n` is a bare chassis, and every `BOARDS ADD` from there
is yours. It is the honest starting point when you are building a machine up board by board,
and it is the one way to be certain nothing is in there that you did not put there.

## The path rule: one base directory

This is the rule that lets an example directory be copied anywhere and still boot. It is one
sentence:

> **A relative path resolves against the machine's directory** — the folder the machine file
> was loaded from.

That folder is the base for *everything*: the disks and PROMs the machine file itself mounts,
**and** the `MOUNT`, `LOAD`, `SAVE`, `DO` and `-s` paths you type at the prompt. One directory,
one answer, whether the path was written by the file's author or by you.

When `examples/flex/flex2-40.toml` says `mount = "FLEX2-40.DSK"`, it means *the disk in
this folder* — and it goes on meaning that after you copy the folder to your desktop, rename it,
or mail it to someone. That is why the examples are self-contained directories, and why the
quick start's `cp -R` actually works. And when you then type

```
swtpcsim> MOUNT dc40:drive1 FLEX2-40.DSK
```

you get the **same file**, from the **same folder** — the one the machine came from — no matter
which directory you launched `swtpcsim` from. Typed paths used to resolve against your shell
instead, which is how the identical disk could show up under two different names; that split is
gone.

A **built-in** machine has no directory of its own, so its base is the directory you launched
from — the only anchor it has.

### When it bites, and what it looks like

The rule is invisible until a file is missing, and then it can look like a typo that is not one.
Keep your machine files in a `machines/` folder, write a path meaning *the folder you launched
from*, and you get the one confusing case:

```toml
[[board.drive]]
unit  = 0
mount = "disks/flex/FLEX2-40.DSK"   # meant: the disks/ up beside machines/
```

`swtpcsim -f ./machines/swtpc.toml` then says:

```
./machines/swtpc.toml: dc40: 'machines/disks/flex/FLEX2-40.DSK': no such file
  ('disks/flex/FLEX2-40.DSK' is relative to the machine's directory, ./machines/)
```

**The disk is not missing.** It was looked for beside the machine file, because that is the
machine's directory and that is where relative paths point. Write it the way the machine sees it:

```toml
mount = "../disks/flex/FLEX2-40.DSK" # up out of machines/, then down into disks/
```

…or keep the machine file next to what it mounts, which is what every shipped example does. The
same `../` applies whether the path is in the file or you type it — because both resolve against
the one base.

### Ask the machine, rather than working it out

You do not have to hold this in your head. `SHOW PATHS` prints the base, for the machine you are
actually running:

```
swtpcsim> SHOW PATHS
  base directory     /home/you/swtpc/flex
                     Everything resolves against this -- what a machine file
                     mounts, and the MOUNT / LOAD / SAVE / DO / -s you type.
                     It is the directory the machine was loaded from.
```

The base is where paths point — what a machine file mounts and what you type both resolve
against it.

Boot a **built-in** machine and the base is the directory you launched from, because a built-in
carries no folder of its own:

```
  base directory     /home/you/swtpc
                     ...
                     This machine is built in, so it is the directory you
                     launched from.
```

`SHOW MOUNTS` is the companion: every disk, tape and ROM in the machine and what is in each,
across all the boards at once.

```
swtpcsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dc40:drive0  disk  FLEX2-40.DSK
  dc40:drive1  disk  (empty)
  dc40:drive2  disk  (empty)
  dc40:drive3  disk  (empty)
  mem0:rom0    rom   builtin:swtbug  (read-only)

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

**Empty drives are listed, not hidden.** The DC-4 has four, one disk is in it, and the other
three doors are open — which is the machine, and worth seeing.

That last line is the command telling you what the middle column is worth, and it is why the two
belong together: `SHOW MOUNTS` tells you what the machine was told, and `SHOW PATHS` tells you
what that meant.

### None of this is a sandbox

The path rule decides **where a path points**, and confines nothing. A machine file may mount any
file on your disk — with `..`, or with an absolute path — and it will be opened. It is a rule
about resolution, not about permission.

## Running a command and leaving — `-x` and `-s`

`swtpcsim` does not have to be interactive.

```
$ swtpcsim -x 'SHOW MACHINE' default
$ swtpcsim -x 'DUMP 0 F' examples/flex/flex2-40.toml
```

`-x` runs one monitor command against the machine and exits. It is **repeatable**, and the
commands run in the order you gave them:

```
$ swtpcsim -x 'MOUNT dc40:drive0 mine.dsk' -x 'RUN' -i examples/flex/flex2-40.toml
```

`-i` is the difference between a query and a start-up: without it the program exits when the
commands are done; with it you are dropped into the monitor with the machine exactly as your
commands left it. `-i` alone, with no `-x` or `-s`, does nothing.

`-s` runs a **script** — a file of monitor commands, one per line, the same ones you type:

```
$ swtpcsim -s boot.cmd examples/flex/flex2-40.toml
```

**The exit status is non-zero if any command failed.** That is the whole point: `swtpcsim -s`
is a program you can put in a shell script, a Makefile, or a build, and test the result of.

```sh
if swtpcsim -s check.cmd mine.toml; then
    echo "machine is sane"
fi
```

## Which chapter next

The **configuring** chapter is the machine file itself: every table, every key, and the four
things a `[[board]]` entry can mean. The **boards** chapter is what the boards *are*.
