# What is in the package

```
swtpcsim                the program. One file, nothing to install.
QUICK-START.pdf          boot a machine in one command. Start here.
swtpcsim-manual.pdf     this.
swtpcsim-changelog.pdf  what changed in this release, and the ones before it.
swtpcsim-cheatsheet.pdf every command and option, rendered for reading.
swtpcsim-monitor.pdf    the swtpcsim> prompt: driving the machine from the console.
swtpcsim-debugger.pdf   breakpoints, stepping, and looking at the bus itself.
DRIVING-WITH-AI.md       for an AI assistant driving the machine; see below.
cheatsheet.md            the same reference as plain text, for the AI to read.
LICENSE                  the MIT licence this is published under.
LICENSE-SDL3             the licence of SDL3, which is built into the program.
examples/                machines that boot, media included.
```

That is the whole archive. There is no library to install, no runtime, and no configuration
file you must write before the program will start.

**Start with `QUICK-START.pdf`.** It boots a machine in a single command and shows you the three
keys — `^E`, `RUN`, `QUIT` — that move you between the CPU and the monitor. This manual is
the long version of it; that one page is enough to get a machine running.

`swtpcsim-changelog.pdf` is the release history — what this version does that the last one did
not, and the same for the versions before it. It is a separate document from this manual on
purpose: the manual describes the program as it is *now*, and a record of what changed reads
better on its own than as a chapter that would have to grow one section per release.

`swtpcsim-monitor.pdf` and `swtpcsim-debugger.pdf` are two more documents beside this one.
They are about driving the program itself — the `swtpcsim>` prompt where you start and stop
the machine, and the debugger you reach for at that prompt when something has gone wrong —
rather than the emulated hardware this manual describes. Open them the way you open this one;
each says at the top where to start if it is the first one you picked up.

`swtpcsim` is a single self-contained program. The one outside library it uses — **SDL3**,
which opens the window the video boards draw into — is compiled *into* it rather than shipped
beside it, so there is nothing to install and nothing that can go missing. `LICENSE-SDL3` is
that library's licence, and it is in the package because its code is in the program.

The **Developer Guide** is not in here — it is a separate download from the same release page
this came from, and you want it only if you intend to build a board of your own.

### `DRIVING-WITH-AI.md`

This one is not for you, exactly. It is a briefing document for an **AI assistant**: drop it in
a working directory, start an assistant there, and say *"using swtpcsim, boot FLEX and show me
what is on the disk."* It tells the assistant how to drive the machine over the program's MCP
interface. Ignore it if that is not how you work — nothing else depends on it.

The quick reference travels beside it: the whole command surface — every option, every monitor
command, every board and machine — generated from this very program so it matches the binary you
have. It ships in two forms of the same content. `swtpcsim-cheatsheet.pdf` is the one for you —
open it the way you open this manual. `cheatsheet.md` is the same thing as plain text, there for
the assistant to read.

## The machines are in the program

You do not need any files to get a running machine — the machine descriptions are compiled into
the binary, and naming one boots it:

```
$ swtpcsim --list                what the built-in names are
$ swtpcsim altair680             a MITS Altair 680b, MON680 in ROM, on a terminal
$ swtpcsim swtpc                 a SWTPC 6800, SWTBUG in ROM, ready for a FLEX disk
$ swtpcsim swtpc09               a SWTPC 6809, S-BUG in ROM, with a DC-4 floppy
```

A built-in is an ordinary machine file that happens to live inside the executable — the same
TOML format you would write yourself, and `CONFIG SAVE mine.toml` writes any running machine out
as one you can edit. **`altair680` carries its monitor in ROM and needs nothing else**; `swtpc`
carries SWTBUG in ROM and comes up ready for a floppy, wanting media — which the next section
is about. The machines chapter has the full story.

## The examples, media included

`examples/` holds complete machines. **Each is a folder with the media in it**, so every
one of them comes up the moment you unzip the archive — nothing to fetch, nothing to mount.

**Every folder carries its own README**, in Markdown and as a PDF beside it, and that README
is the description of that example: what the machine is, what is in the drive or the deck,
what to type, and what it should print back. So the list of examples is not written down in
this manual — look in `examples/`, and read the README of whichever one you want.

```
$ ls examples/
$ swtpcsim examples/flex/flex2-40.toml
```

**The folder is the unit, and you may move it anywhere.** A path written *inside* a machine
file resolves against **that file**, not against wherever you were standing when you ran the
program — so `examples/flex/flex2-40.toml` names its disk as plain `FLEX2-40.DSK`, the one lying next
to it, and the folder still boots after you copy it to your desktop, rename it, or mail it to
somebody.

(The other half of that rule matters just as much: a path *you type* at the prompt is relative
to **your shell**, because you are the one who can see your own directory. The machines chapter
covers both halves.)

The examples chapter walks through some of them at length. Where an example carries period
documentation of its own — a game's own printed manual, say — that travels in the folder too,
and its README says so.

## What is *not* in the package: everything else to run

**What is in `examples/` is the whole of the shipped media.** A built-in that wants a
disk or a tape — `swtpc`, say, which comes up with an empty DC-4 drive — starts up perfectly
well without one:

```
$ swtpcsim -x "SHOW MOUNTS" swtpc
swtpcsim> SHOW MOUNTS
  UNIT         KIND  HOLDS
  dc40:drive0  disk  (empty)

  Paths are AS WRITTEN.  SHOW PATHS says what they are relative to.
```

You supply the media and `MOUNT` it. The disks and tapes chapters describe how — and where
those chapters name an image that is not in `examples/`, they are showing you the shape of
the command, not a file you already have.

> **Where the rest will come from.** A separate **`swtpcsim-packages`** repository is planned
> to hold the wider collection of disks, tapes and machine files, packaged the same way — each
> example a self-contained folder you can drop anywhere. **It is not published yet**, and
> exactly which images go in it has not been settled, so there is nothing to link to here yet.

The bulk of the media is kept out of the program's own archive on purpose: an image is large,
most of the good ones are not ours to redistribute, and the simulator's version and the
software's have no reason to move together. The ones that ship are the ones that make the
manual's first chapters true.

## What is *not* in the package: the source

The **source code** is not here — the simulator's, that is. `swtpcsim` is an open project under
the MIT licence, and the source is a separate thing to fetch:

**<https://github.com/deltecent/swtpcsim>**

Nothing in this manual requires it. The one exception worth naming: **if you want to build a
board of your own** — which is what the simulator is really for — you need the source, and
you want the *Developer Guide*, which is a different document. This one is about driving the
machine, not extending it.

## Reporting a bug, or asking for something

Both go in the same place — the **Issues** tab of that repository:

**<https://github.com/deltecent/swtpcsim/issues>**

Search it first; if nobody has raised your problem, open a new issue. You need a GitHub
account, and nothing else.

**What makes a bug report useful** is enough for somebody else to see what you saw:

- The **version** — the line `swtpcsim` prints at startup, or `swtpcsim --version` — and
  which operating system. Paste it whole: the commit in the parentheses is what says which
  source built your copy, and between releases the number alone names them all the same.
  `SHOW VERSION` prints that from inside the monitor, plus a `video` row saying whether this
  copy can open a window — worth including in anything about the video boards.
- The **machine**: the built-in's name, or the machine file itself, which is a small text
  file you can paste.
- **What you typed and what happened.** Paste the terminal, prompt and all. The monitor
  echoes every command, so a pasted session is a complete record of what was asked of it.
- What you expected instead, when that is not obvious.

If the guest software misbehaved rather than the simulator, say which software and where you
got it — a period program failing on real hardware in 1976 is a fair thing for it to do here
too, and knowing the image is how that gets untangled.

**A feature request is an issue as well**, and does not need an apology. Say what you are
trying to do rather than only which knob you want, because the machine often has a way in
already; and if it does not, the shape of the problem is what decides the shape of the
answer. A missing board is a particularly good request when you can name the manual it
was documented in — every board here was modelled from its own documentation, and a board
with no surviving source is one nobody can build honestly.
