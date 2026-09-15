# Troubleshooting

Most of what goes wrong here is not a fault. It is the machine doing exactly what a real one
did, in a way nobody warned you about. This chapter is the warning, after the fact.

## `^C` does not stop the machine

It is not supposed to.

**`^C` belongs to the guest.** FLEX reads it, and so do plenty of programs — it is a common
way to interrupt. A stop key the guest also wants is a stop key the guest will eat, and then
you are trapped inside your own simulator.

**`^E` is the stop key.** It stops the CPU and hands you the monitor; the host intercepts it
before the guest is ever offered the byte, and **no program running inside the machine can
disable it, ignore it, or take it from you.**

If a guest genuinely needs `^E` for something of its own, move STOP out of its way:

```
swtpcsim> CONSOLE stop=1D
```

That makes it `^]`. It must be a control character.

## I pressed STOP and now the machine seems stuck

It is not stuck. It is **stopped**, which is what STOP is for, and nothing has been lost.

STOP halts the processor and hands you the monitor. It is not RESET and not POWER: the
registers, the memory and the disk are precisely as the CPU left them, and the monitor told
you where to pick it up when it printed *"still at ...".*

```
swtpcsim> RUN
```

A bare `RUN` — no address — resumes at that exact instruction, and your prompt (SWTBUG's `$`,
FLEX's `+++`, MON680's `.`) is where you left it. (`RUN <addr>` is a different thing: it loads
the PC first, so it *restarts* rather than resumes.)

And while you are stopped, you can look: `REGS`, `EXAMINE`, `DUMP`, `DISASM` and `STEP` all
work at this prompt, on a machine that is not moving under you. See the *Debugger* document.
`BREAK` is for stopping at a place you cannot reach by hand.

## My file was not written / the disk lost my changes

The shipped disk is **read-only.**

The FLEX example mounts its drive with `writeprotect = true`, so looking around FLEX can never
dirty the image — which is what you want until you mean to save. But it also means FLEX cannot
write to it: a `SAVE` or a file copy is refused at the controller. To keep your changes, drop the
`writeprotect` line from the drive in the `.toml`, or copy the disk somewhere writable first.

## The disk image changed and I wanted it not to

There is no undo once a drive is writable. **A read/write image is mounted like a real machine** —
a guest that cannot write its disk cannot save your work.

Two answers, and take one *before* you experiment:

```
swtpcsim> MOUNT dc40:drive0 FLEX2-40.DSK RO
```

`RO` refuses every write at the controller, so your file is safe whatever the guest does. It is
for a disk you mean to **read**: the guest is never told the disk is protected — the controller
has no bit that says so — so a program that sets out to write to an `RO` disk will not fail
gracefully. The disks chapter explains why.

or copy the folder. It is a directory with a machine file and an image in it, and it boots
from anywhere:

```
$ cp -R examples/flex my-flex
```

## `MOUNT` says "no such file" and the file is right there

Look at *which machine you are running*.

**A relative path resolves against the machine's directory — the folder the machine file was
loaded from — not the folder you launched from.** If you `cd` somewhere with a disk, start a
machine from elsewhere, and type `MOUNT dc40:drive0 that.dsk` expecting your current folder,
it is looked for beside the *machine* instead, and that is the directory you were not thinking
about. `SHOW PATHS` prints the one it uses.

This is deliberate, and it is the reason an example folder boots from anywhere: the machine
file says `FLEX2-40.DSK` and means *the image next to me*, so you can move the folder, or run it
from three levels up, and it still finds its disk — and a name you type finds that same folder,
so the disk the machine came with and the disk you mount by hand live in one place, not two.

## Every prompt ends in a garbage character

Some period software **sets bit 7 of the last character of every message** as a string
terminator, and your terminal prints the result — so `SIZE?` comes out `SIZ?` with a garbage
character where the `E` should be. The `E` is there; it arrived as `C5` instead of `45`.

```
swtpcsim> CONSOLE strip7out=on
```

The real Teletype ignored bit 7 and printed the `E`. `strip7out` is your terminal doing the
same. The serial chapter explains why the fix belongs on the console and **never** on the
board.

## Nothing appears when I type / the guest does not see my keys

Ask who has the keyboard.

```
swtpcsim> SHOW CONSOLE
```

**Exactly one unit may hold the console.** If the console is on a unit the guest is not
reading, you are typing into a board nobody is listening to. Connect the right one — and note
that connecting a second unit to `console` *steals* it from the first, and says so.

## The machine runs impossibly fast — a cassette loads in one second

It is running **flat out**, which is the default. `clock_hz = 0` means "no divisor, go".

```
swtpcsim> SET cpu0 clock_hz=1000000
```

That is the real 1 MHz crystal, and it will give you the real cassette-load time. Both
behaviours are correct; one of them is just a much longer lunch.

## A file transfer keeps timing out — XMODEM, a serial upload

**Slow the machine down. Transfers with the outside world want the real crystal.**

```
swtpcsim> SET cpu0 clock_hz=1000000
```

Here is why, because it is worth understanding once and it explains a whole class of symptom.

**A guest program has no clock. It counts instructions.** A period transfer program times a
second the way every program of the era did — by spinning in a loop and counting the trips —
and that arithmetic is *only* a second at the real crystal speed. It waits a few of them for a
block header. Run the machine flat out and those seconds — seconds of **cycles** — are retired
by your host in a few tens of **milliseconds**, while the sending program on the other end of
the wire is still living in seconds of the ordinary kind. The transfer concludes the sender is
dead, NAKs, purges the line, and gives up. Nothing is broken. The two ends are simply no longer
using the same clock.

**And that is the boundary, exactly:** timing inside the machine is consistent at any speed,
because everything in there counts the same cycles — which is why a cassette loads correctly
flat out, the KCACR and the tape agreeing with each other at whatever speed the pair of them run.
A transfer to your host has **one end inside the machine and one end outside it**, and only the
crystal makes those two agree.

So: flat out for everything the machine does to itself. The real crystal for anything it does
with you. Set it back afterwards if you like the speed.

## Two boards are fighting over an address

```
swtpcsim> SHOW BUS CONTENTION
```

names them. To see the whole map of who decodes what:

```
swtpcsim> SHOW BUS MAP
```

On real hardware this was two boards strapped to the same address and a bus you could not
trust. Here it is a list.

## A read from an address returns `FF` and I expected something

**Nothing decodes that address.** The bus floats high when no board is driving it, and `FF` is
what a floating bus reads. It is not an error, and the machine will not tell you — a real one
did not either.

To find out who *would* have answered, without running a cycle:

```
swtpcsim> WHO F400
```

And if the symptom is a **hang** — a machine you assembled yourself starts, prints nothing, and
never comes back — the cause is often exactly this: it is polling an address for a board that is
not there, reading `FF` forever. Arm the bus to say so:

```
swtpcsim> SET BUS UNCLAIMED=WARN
swtpcsim> RUN
```

`WARN` prints the address and the PC that reached for it and keeps running; `HALT` stops the
machine right there, so `SHOW REG` and a `DUMP` show it exactly as it wedged. It names each
absent address once per run. The default is `SILENT`; you turn it on when you need it.

## `RESET` did not clear memory

It is not supposed to.

**`RESET` is the bus's RESET\* line.** It does what pulling that line did: the processor goes
to zero, boards return to their power-on state. RAM is RAM; it was not cleared on the real
machine and it is not cleared here.

**`POWER` is the only thing that loses memory.** That is the difference between pressing a
button and pulling a plug, and the manual keeps it.

## macOS refuses to run it

*"cannot be opened because the developer cannot be verified"*.

```
$ xattr -dr com.apple.quarantine ./swtpcsim
```

Once. It is not a comment on the program; it is what macOS does to every unsigned binary that
arrives in a zip.

If that prints nothing, it worked — and if the flag was never there (a zip fetched with `curl`
or `scp` is not marked; only one a browser or a mail client downloaded is), it also prints
nothing and succeeds. That is the whole reason for `-dr` over a plain `-d`, which announces an
error when there is nothing to remove.
