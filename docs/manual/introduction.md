# What swtpcsim is

`swtpcsim` simulates the **SWTPC 6800** and the **MITS Altair 680b** — the Motorola 6800 machines
of the mid-1970s and the buses they were built around.

It boots real software — not software written to work with it, but the actual artifacts, byte for
byte, as they were shipped: SWTBUG and MIKBUG in ROM, TSC's FLEX off a floppy, MON680 on the
680b. None of it has been patched, and none of it knows it is not running on a real machine.

The bus is a real object in the program, not a wiring diagram implied by the code. Boards plug
into it, answer addresses, pull the interrupt line, and float the data bus when nobody is driving
it — getting the answer wrong in exactly the ways real boards did. That makes the machine a
**bench**: a board you have not built yet can be fitted here, and the driver you have not finished
can be run against it, before either exists in copper. And it makes bugs findable — you can stop
the machine mid-instruction, ask which board answered and which stayed silent, and run the same
thing again and get the same answer, because nothing here is intermittent and nothing is hidden.

## What it does

- **A Motorola 6800**, faithful down to the flags and the cycle counts, and checked against a
  processor reference before any board is built on it. It drives two machines: the SWTPC 6800 on
  its SS-50/SS-30 bus, and the MITS Altair 680b.
- **A Motorola 6809**, checked the same way. It drives the SWTPC 6809: the MP-09 processor board
  and its S-BUG monitor, in the same SS-50 system.
- **A board for most of the machine**, each modelled from its own manual: the CPU board, RAM/ROM,
  the SWTPC MP-S serial console, the Altair 680b's onboard and Universal I/O, the KCACR
  audio-cassette interface, and the DC-4 floppy controller that FLEX boots from. The boards
  chapter has the whole list.
- **A monitor** — the prompt you get when the machine is not running — with breakpoints (plain or
  conditional), single-stepping, disassembly, memory examine and deposit, a bus-cycle trace and a
  history ring, and a view of the bus itself: who decodes what, who is pulling the interrupt line,
  and where two boards are fighting.
- **Real I/O.** A serial board can be wired to your terminal, to a TCP socket (so you can telnet
  into the guest), or to an actual serial port on your machine, with the modem control lines wired
  through.

## What it does not do

This section is here because a manual that only lists strengths is an advertisement.

- **You can save state, but not replay.** `SNAPSHOT` writes the machine's whole state to a file
  and `RESTORE` reads it back. What you cannot do is *record* a session and step backwards through
  it — there is no rewind.
- **These machines had no video and no audio.** A terminal on a serial port is the display, as it
  was. **There is no audio output.** Cassette `.WAV` files are read and written as *files*, which
  the tapes chapter covers.
- **Not every 6800 board is here**, and timing, while honest, is not a circuit simulation:
  instructions cost the right number of cycles and a cassette takes the right number of them to
  load, but propagation delays and analogue behaviour are not modelled — and no software from the
  period could tell.

## What is in the box

You have the `swtpcsim` program, this manual, and a folder of worked examples with their media —
complete machines that boot, each with a README of its own. More machines are built into the
program itself. It boots something the moment you unzip it; the next chapter says what, and where
further disks and tapes come from.

There is no installer and nothing to set up. Unzip it and run it.
