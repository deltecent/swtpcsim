# Running it

`swtpcsim` is a single executable. Unzip the package and run it from a terminal.

```
$ ./swtpcsim
swtpcsim 1.0.0 -- 6800, full speed.
machine: swtpc.  HELP for commands.
swtpcsim>
```

The `./` says **run the program in this directory** — on macOS and Linux a bare name is
searched for on your `PATH` and not in the current directory, so the prefix is how the shell
finds it. On Windows use `.\swtpcsim` in PowerShell (`cmd` finds it either way). Omit the
prefix entirely once `swtpcsim` is on your `PATH` — then its bare name works from anywhere.

A release built straight from its tag prints just the version, as above. A build made *between*
releases appends **the commit it was built from** — `swtpcsim 1.0.0-12-gabc1234` — because the
version number alone names every build after a release alike, and it is the commit that says which
source produced the program in front of you. Quote whichever you have in a bug report. `SHOW
VERSION` prints the full identity on its own, and says whether the tree had uncommitted edits in it
at the time.

That prompt is **the monitor**. The machine exists — it has memory, a processor, a console
board and a floppy controller in it — but it is not running. Nothing has been started. This
is the equivalent of standing in front of the real machine with the power on, before you
have hit RESET and let its ROM monitor take over.

## macOS: the first run

macOS marks anything that arrives from the internet and refuses to run it until you say
otherwise. If you see *"cannot be opened because the developer cannot be verified"*, clear
the mark:

```
$ xattr -dr com.apple.quarantine ./swtpcsim
```

Do that once. It is not a comment on the program; it is what macOS does to every unsigned
binary that arrives in a zip, whoever wrote it.

The mark is put there by whatever *fetched* the file — a browser, a mail client — so if you
pulled the zip down with `curl` or `scp` there may be nothing to clear. The command says
nothing and succeeds either way, which is why it is `-dr` and not `-d`: plain `-d` reports an
error when the flag is already absent, and that error is not a problem.

## Getting help, and getting out

| Type | To |
|---|---|
| `HELP` | list every command |
| `HELP DUMP` | the usage and worked examples for one command |
| `QUIT` | leave |

**There is no `EXIT`.** `QUIT` is the word, and `Q` is enough of it.

Commands resolve by **prefix**, so you type as much as it takes to be unambiguous and no
more — `HELP` shows each command with its shortest form in brackets: `D[UMP]`, `DE[POSIT]`,
`RES[ET]`. Type the part before the bracket. And **commands are not case-sensitive**, nor
are the names of the boards in the machine; this manual writes them in capitals only because
it is easier to read.

## Which machine you get

Running `swtpcsim` with no arguments gives you the default machine, `swtpc` — a SWTPC 6800
with a console and a DC-4 floppy controller, ready for a FLEX disk.

Naming something gets you something else:

```
$ ./swtpcsim examples/flex/flex2-40.toml   a machine file: this one boots FLEX
$ ./swtpcsim altair680                   a BUILT-IN machine, by name
$ ./swtpcsim --list                      what the built-in names are
```

A **built-in** is a machine file that lives inside the program. There is nothing special
about it — it is written in the same format as the ones under `examples/`. To see what
is actually in one, boot it and look:

```
$ ./swtpcsim -x BOARDS altair680
```

…and if you want it as a file you can edit, `CONFIG SAVE mine.toml` writes out the machine
you are actually running, and what it writes will boot.

The full story — every command-line option, and how `swtpcsim` decides whether a word is a
built-in name or a filename — is in the machines chapter.
