# Tapes

Before the floppy, there was a **cassette recorder**. Not a special one — a home audio cassette
deck from a department store, with a microphone jack and an earphone jack, and a card that turned
bytes into a noise it could record and turned the noise back into bytes. On the Altair 680b that
card is the **KCACR**, and it is in this simulator.

The chapter ends with the thing this is all for: loading a program off a cassette the way it was
actually loaded — nothing on a disk, a loader in a small ROM, and a tape.

## The KCACR

`680kcacr` is the Altair 680b's **audio-cassette interface**. Underneath it is a 1602-family UART
with a **Kansas City Standard** FSK modem soldered to it: the guest reads and writes a serial
register, and the modem on the other side turns the bits into tones. It is memory-mapped and
active-low — status/control at `F010`, data at `F011`.

It has one unit: **`tape`**. There is one slot in a cassette recorder.

### You cannot `CONNECT` it to anything

The KCACR takes no endpoint. It cannot be wired to your terminal, to a socket, or to a real serial
port, and the reason is simple: **the line is soldered to the modem.** The signal goes to a cassette
deck and nowhere else. **It is a cassette interface, not a serial port**, and the fact that it is
built out of a serial part does not make it one. The serial chapter is about the boards that *do*
take endpoints.

### It can start and stop the tape, but not wind it

The KCACR has **software motor control**: a write to the control register with bit D7 set starts
the transport and D6 stops it, so a guest program can run the recorder while it reads or writes. It
can also transfer under interrupt (the D0/D1 enables pull the 6800 IRQ). What a motor line cannot
do is say *which way* — so winding the tape to a position is still something you type, below.

## Working a tape

### The tape is not hardware

**The tape is NOT in the machine file, deliberately.**

A machine file describes the **hardware** — what boards are in the backplane, what they decode, how
much memory is on the bus. Which cassette is sitting in the recorder is not hardware. It is a thing
you did with your hands, this morning, and you can do a different thing with your hands this
afternoon without unscrewing the lid.

**You put the tape in, and you press PLAY.** That is `MOUNT`, and it is a thing you type.

### Putting a tape in

```
swtpcsim> MOUNT kc0:tape "kcacr-demo.tap"
```

The KCACR has one unit, so the unit carries no information, so you may drop it:

```
swtpcsim> MOUNT kc0 tape.bin
```

Names are case-blind. The rules are the ones in the disks chapter, and they are the same rules.

### `mode = play | record`

```
swtpcsim> SET kc0:tape mode=record
```

**`play` loads from the file; `record` saves to it.** Which way the bytes are going is the whole of
the setting.

The two are **mutually exclusive** — one tape, one head, one direction at a time — so `mode` is one
setting with two values, and there is no third.

### Where the head is — the tape counter

`SHOW MOUNTS`, and `SHOW <id>`, report where the head is sitting as a position on the tape:

```
swtpcsim> SHOW MOUNTS
  kc0:tape  tape  demo.wav  00:15 / 01:28 (17%)  301/2048 bytes
```

The counter is a **time and a percentage** — minutes and seconds into the tape, and how far along
it you are. For a `.WAV` that time is the recording's *own*: it counts the leader and any silent
gaps between programs, exactly as a real cassette counter would. For a byte `.TAP`, which carries no
audio, the time is estimated from the baud. The byte count is still there beside it.

### `WIND` — move the head to a time

A tape unit brings a verb of its own:

```
WIND <id>:<unit> <mm:ss | START | END>
```

`WI` will do. It winds the head to a time on the tape — so a cassette holding several programs one
after another is **reachable**: read the counter (or a manual) for where the next one begins, and
wind there.

```
swtpcsim> WIND kc0:tape 2:05
kc0:tape: wound to 02:05 / 08:40 (24%) -- demo.wav (...)
```

The position is a time — `mm:ss`, or a bare number of seconds — or the words `START` and `END`. A
time past the end lands at the end.

### `REWIND` — wind to the start

`REWIND <id>:<unit>` (`REW`) is `WIND … START`: the common case, kept as its own verb.

**You need it to load the same tape twice.** A tape that has been read is a tape whose head is at
the end of the tape, and playing it again gets you silence. This surprises people exactly once.

```
swtpcsim> REW kc0:tape
```

### Watching a load — the live counter

When a tape plays in real time (`rate = real`, below) the counter **ticks up on the console while
it loads**, so you can watch a long tape's progress. It is on by default; turn it off for a machine
whose guest is writing to the same terminal:

```
swtpcsim> MOUNT kc0:tape "tape.wav" counter=off
```

or `SET kc0:tape counter=off` at any time. Turning it off changes nothing about `SHOW` — the
position is always there to ask for.

### `stop` — halt the tape at a time

A multi-program tape runs one program straight into the next. The `stop` mark is your finger on the
recorder's **STOP button at a counter mark**: set it to a time and the tape goes quiet there instead
of running on. Set it at `MOUNT`, or with `SET` any time after:

```
swtpcsim> MOUNT kc0:tape "tape.wav" stop=2:05
swtpcsim> SET  kc0:tape stop=2:05
```

Load program 1 and the line falls silent at 2:05 — the same quiet the end of the tape gives — so the
loader stops there rather than reading into program 2. To carry on, move the mark forward (or clear
it) and go again:

```
swtpcsim> SET kc0:tape stop=5:30      (the next boundary)
swtpcsim> SET kc0:tape stop=off       (play to the physical end)
```

It is `off` (play to the end) unless you set it. `SHOW` shows an armed mark as `stop @ 02:05`. It
halts **playback only** — a recording writes straight through it — and it is independent of `WIND`.

### `rate = full | real`

**The cassette carries its own clock, and by default it runs flat out.** `rate = full` — the
default — hands the guest each byte the moment it is ready to read the next one, so a tape loads in
about a second whatever speed the CPU is set to. This is almost always what you want: a program you
are trying to run should not make you wait for a recorder that has been gone for forty years.

```
swtpcsim> SET kc0:tape rate=real
```

`rate = real` paces playback in **real wall-clock time** at the tape's baud, so a load takes as long
as it took on the day. Set it when the *wait itself* is the point: a demo, a screen recording, the
sound of the thing. It is playback only; recording takes as long as the guest drives it either way.

**What this is *not* is the CPU clock.** The tape's clock and the processor's are separate crystals
on the real hardware, and they are separate here. A faster or slower CPU no longer drags the tape
with it; `rate` is the only thing that governs how fast a tape plays.

## Audio tapes — `.WAV`

Most surviving cassettes are not files of bytes. They are **audio**: somebody put a cassette in a
deck, played it into a sound card, and saved a `.WAV`. You can mount one.

The recording is demodulated **once, when you mount it** — never while the machine is running — and
from that moment everything above it, including `SHOW`'s byte count and `WIND`/`REWIND`, means
exactly what it meant for a `.TAP`. The one thing the audio adds is a *real* clock: the tape counter
reads the recording's own minutes and seconds, gaps and leader included, where a byte tape can only
estimate them. The guest cannot tell.

```
swtpcsim> MOUNT kc0:tape "program.wav"
kc0:tape: mounted program.wav
program.wav: kcs300, 4439 bytes, 0 framing errors (100.0% of frames intact)
```

**Read that first line.** A mount always says what it found, and the framing-error count is the
number that matters: a tape that decoded at 60% is noise, not a program, and you want to know that
now rather than after the loader has crashed. A decode below 90% is refused outright.

**But it is a framing rate, not a clean bill of health.** The percentage counts frames whose start
and stop bits landed where they belonged; it cannot tell you the eight bits between them are the
ones that were recorded. A worn or poorly dubbed recording can keep its framing and still hand the
loader wrong bytes. If a tape mounts well and the program still will not run, a worn recording is
the likely answer.

**What decides is the file's magic, never its name.** A `.TAP` somebody renamed `.WAV` is still read
as bytes, and a recording renamed `.TAP` is still demodulated.

### `extract` — split a WAV into per-program `.TAP` files

A cassette WAV often holds several programs one after another, separated by a few seconds of silence.
**`extract` writes each program out as its own `.TAP`** — so you can keep, mount or load them one at
a time instead of winding through the whole tape. Ask for it at `MOUNT`:

```
swtpcsim> MOUNT kc0:tape "games.wav" extract
kc0:tape: mounted games.wav
  games-1.tap  2048 bytes
  games-2.tap  3120 bytes
2 programs extracted
```

The files land **beside the WAV**, named from it: `games.wav` becomes `games-1.tap`, `games-2.tap`,
… with a 1-to-N index (a single-program tape is just `games.tap`, no index). `extract=<base>` names
them yourself. It only **reads** the tape and **writes** the files — nothing in the machine changes.

The same thing has a verb, so you can split a WAV that is already in the deck without re-mounting:

```
swtpcsim> EXTRACT kc0:tape
```

Only a `.WAV` can be extracted — a `.TAP` is already the bytes, and has no gaps left to split on. The
boundary is a second or more of silence, far longer than the gaps *inside* a program, so programs
come apart cleanly and none is cut in half.

### A board will refuse audio it could not really have heard

The KCACR's modem reads the **Kansas City Standard** (`kcs300`). The demodulator here measures the
tones actually on the tape, so it *could* read another modulation perfectly well — but a real KCACR
could not, and a tape whose tones are outside the modem's range is refused rather than decoded into
bytes no real board could have produced:

```
swtpcsim> MOUNT kc0:tape "othertape.wav"
kc0: othertape.wav: this board's modem cannot hear that tape -- it carries ..., and this board reads kcs300
```

The frequencies in that message are a **measurement, not the tape's specification** — read it as
*"not mine, and here is roughly what is there"* — and you go find a machine that reads it.

### Recording back out to a `.WAV`

Put the recorder in `record`, and when the transport stops the tape is re-modulated and written back
over the file — in the format and at the sample rate it was mounted with:

```
swtpcsim> SET kc0:tape mode=record
swtpcsim> RUN                          (the guest records)
swtpcsim> SET kc0:tape mode=play       (...and the WAV is written here)
```

**You can also make a blank tape from scratch — `MOUNT … CREATE`.** A blank file has no recording to
sniff, so its *name* decides what it becomes: a `.wav` name makes a blank **audio** tape that records
a real, playable WAV; any other name makes a blank **byte** tape.

```
swtpcsim> MOUNT kc0:tape new.wav CREATE mode=record    (a fresh Kansas City audio cassette)
swtpcsim> MOUNT kc0:tape new.tap CREATE mode=record    (a fresh byte cassette)
```

The mount line tells you which you got, so there is no guessing. `mode=record` is there because the
deck comes up in PLAY, one head and one direction: it is what lets the tape *record*, not what makes
it audio — the name does that. Without `CREATE`, `MOUNT` needs a file that already exists, so a
mistyped name is caught as a mistake rather than turned into a blank tape.

The stop is what writes the audio. `UNMOUNT` and any `WIND` (`REWIND` included) are stops too. The
whole file is rewritten each time, not patched: the audio for a byte starts at an offset that depends
on every byte before it, so there is no cheaper splice.

**Time and level are the things that do not survive the round trip.** A byte image holds no
durations, so the leader a real transport needs has to be put back by whoever writes the audio — the
`leader` and `trailer` properties (in seconds) do that. And the `level` property (percent of full
scale) decides whether the tape reads back cleanly on real hardware: too hot overdrives a real deck's
input and the tape reads its header and then fails. A `waveform` property chooses whether the tone is
drawn square (what the real modem lays down) or sine (smoother, quieter). Their defaults are set for
the board — `SHOW kc0` lists them, and the board reference at the back records the numbers.

Recording to a `.TAP` works as it always has, and is unaffected by any of this.

### `format`, when you need to overrule the sniff

Each tape unit has a `format` property. `auto` is the default and is almost always right.

```
swtpcsim> SET kc0:tape format=raw
swtpcsim> SHOW kc0
```

(`SET` addresses the unit; `SHOW` addresses the **board**, and lists every unit on it with its
properties underneath. There is no `SHOW <id>:<unit>`.)

| Value | What it does |
|---|---|
| `auto` | Sniff for RIFF magic; demodulate a recording, read anything else as bytes |
| `raw` | Read the file's own bytes **even if it is a WAV** — how you inspect a tape that decodes badly |
| a modulation | Force the board's own — `kcs300` on the KCACR |

It selects a *reading*; it never widens the hardware. A companion read-only `detected` property
reports what the mounted tape turned out to be. `format` takes effect at the **next** `MOUNT`,
because a tape is decoded once, when you put it in.

## Loading a program off cassette — the ritual

This is the whole point of the chapter. On the Altair 680b, a small ROM holds the KCACR loader, and
loading a program is: put the cassette in, then jump to the loader. The `altair680` cassette example
is that machine, with a demo S-record tape already in the recorder.

**1. Put the cassette in, and start the machine.**

```
$ swtpcsim examples/altair680/altair680-kcacr.toml
```

The example's machine file mounts `kcacr-demo.tap` in the recorder and boots MON680 to its `.`
prompt. (You could type the `MOUNT` yourself instead — it gets no special powers.)

**2. Run the loader.**

```
.J FD00
```

MON680's `J` jumps to the KCACR loader/punch ROM at `FD00`. The loader starts the recorder reading,
the tones come off the tape, and it deposits the program into memory — verifying every byte as it
writes — then returns to the `.` prompt at the tape's terminating record.

**3. Look at what landed.**

```
.M 0200
```

The demo tape is a tiny S-record that writes four bytes at `0200`; `M` examines memory, and the byte
the loader wrote is there. To load real 680b software instead, mount its cassette in place of the
demo tape — the loader path is identical. The companion entry `.J FD74` **punches** memory back out,
recording it to a tape in `record` mode.

### Speed: the tape and the CPU are on separate clocks

**The machine runs flat out by default, and so does the tape.** The two are on separate clocks, as
they were in the hardware: the processor's speed is `clock_hz` on the CPU board, the tape's is `rate`
on the deck (above), and neither drags the other. `SET cpu0 clock_hz=1000000` buys back the period
feel of the *processor*, while `SET kc0:tape rate=real` is the separate switch that makes the *load*
take its real wall-clock time. In `rate = full` the guest cannot tell the difference at any CPU
speed, because a polled loader only ever asks for the next byte and the byte is always ready.
