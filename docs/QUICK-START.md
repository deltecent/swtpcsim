# Quick start

**Boot a machine with one command.** From the folder you unzipped this into:

```
$ ./swtpcsim examples/flex/flex2-40.toml
```

On **Windows**, the path is spelled with backslashes and the program is `swtpcsim.exe`:

```
> swtpcsim.exe examples\swtpc\flex\flex2-40.toml
```

That is the whole of it. The machine comes up in **SWTBUG**, the SWTPC 6800's ROM monitor,
and prints its prompt:

```
$
```

Type **`D`** — SWTBUG's disk-boot command — and it loads **FLEX 2.0** off the DC-4 floppy.
FLEX signs on, asks the date, and drops you at its `+++` prompt:

```
$D
DATE (MM,DD,YY)? 1,15,80
+++
```

It is 1980. Type `CAT` to see what is on the disk:

```
+++CAT
```

An assembler, an editor, and the FLEX utilities are on there.

## The three keys to remember

You are talking to FLEX, the software the CPU is running. Two prompts share the window: `+++`
is FLEX (and `$` is SWTBUG before it boots), while `swtpcsim>` is the simulator's own monitor.
You move between them with these:

| | |
|---|---|
| **`^E`** (Control-E) | Stop the CPU and return to the `swtpcsim>` monitor. The machine stops exactly where it stood and loses nothing. |
| **`RUN`** | Start the CPU running again. It picks up where it left off. |
| **`QUIT`** | Done. (`Q` will do. There is no `EXIT`.)|

## The disk

This example mounts the FLEX disk **read-only**, so looking around can never change the image —
type freely and nothing on your host is touched. When you mean to *save* files back, drop the
write-protect (the machine file sets `writeprotect = true` on the drive), and copy the folder
first so you keep a clean original. It is self-contained and boots from anywhere:

```
$ cp -R examples/flex my-flex
$ ./swtpcsim my-flex/flex2-40.toml
```

## Where the files come from

You named **one** path — the machine file — and the simulator found the disk on its own. One
rule covers all of it:

- **The machine file you name on the command line** is found from the folder you are in — your
  shell. That is the one path you hand to the shell, before any machine exists.
- **Everything after resolves against the machine's own folder** — the directory that `.toml`
  lives in. The disk it mounts, and the `MOUNT`, `LOAD`, `SAVE` and `DO` you type at the
  `swtpcsim>` prompt, all come from there. `flex2-40.toml` mounts `FLEX2-40.DSK` from
  *beside itself* — and if you type `MOUNT dc40:drive0 FLEX2-40.DSK` you get that same file,
  from that same folder.

For example, from the folder you unzipped into:

```
$ pwd
/home/you/swtpcsim
$ ./swtpcsim examples/flex/flex2-40.toml
```

`examples/flex/flex2-40.toml` is found from where you are; the `FLEX2-40.DSK` it mounts
— and anything you later type at the prompt — is found in `examples/flex/`, beside the
machine.

That is why every folder under `examples/` boots wherever you copy it: the disk travels with the
`.toml` that names it, and so does everything you do to that machine. `swtpcsim` never changes
your working directory.

To see it, ask the machine: **`SHOW PATHS`** at the `swtpcsim>` prompt prints the base
directory — and the sandbox root, the one folder host file access is confined to:

- **the base directory** — the machine's own folder; what a machine file mounts and what you type both resolve against it;
- **the sandbox root** — the one folder host file access may reach, and cannot escape.

The leading `./` means *the `swtpcsim` in this folder* — a fresh unzip is not on your `PATH`, so
you point at it. Copy the program into a directory on your `PATH` (`/usr/local/bin` on macOS or
Linux) and you can drop the `./` and type just `swtpcsim` from anywhere, including from inside an
example folder: `cd examples/flex && swtpcsim flex2-40.toml`.

## No disk? Boot a cassette instead

Every folder under `examples/` is a complete machine with its media already inside it, so any
of them comes up the moment you name it:

```
$ ls examples/
$ ./swtpcsim examples/altair680/altair680-kcacr.toml
```

`examples/altair680/` is a **MITS Altair 680b** with MON680 in ROM and a Kansas City cassette in
the deck — the machine that shows you what a 6800 in a home was in 1976. Each folder carries its
own `README.pdf` describing what it is and what to type.

## Where to go next

`swtpcsim-manual.pdf` is the full User Manual — the same quick start above at more length,
then the machines, the disks and tapes, and the boards. `swtpcsim-monitor.pdf` and
`swtpcsim-debugger.pdf` cover the `swtpcsim>` prompt and the built-in debugger.
