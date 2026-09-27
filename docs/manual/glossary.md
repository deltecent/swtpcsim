# Glossary

**ACIA** — Asynchronous Communications Interface Adapter. The Motorola 6850 chip at the heart
of the SWTPC MP-S console and the Altair 680b's serial ports. It has two registers a program can
see: a status register and a data register. Everything a program does with a serial port on this
machine, it does through one of those.

**ATTN** — See **STOP**. The former name for the STOP key; the `CONSOLE attn=` spelling still
works as an alias.

**backplane** — The PCB with the connectors on it that every board plugs into. It is not itself
one of them: nothing in a machine file fits a backplane, because the backplane is what a machine
file describes the contents of. On a SWTPC 6800 it is the **SS-50 bus** — the motherboard
carrying the CPU and memory — with the **SS-30** I/O bus hanging off it. The backplane *is* the
machine.

**board** — Anything that plugs into the backplane: memory, a serial port, a disk controller,
the processor itself. It is the word this program uses everywhere — `BOARDS` lists them,
`[[board]]` fits them in a machine file — and a board is the thing you add, remove, `SHOW` and
`SET`. See also **card**, which means the same thing.

**card** — The same object as a **board**. The period hardware and its manuals said "card", and
this manual keeps the word where the sentence is about the physical thing somebody bought,
socketed chips into and set jumpers on. Everywhere else it says board.

**contention** — Two boards answering the same address. On real hardware both would drive the
data bus at once and you would get a byte that is neither of theirs, intermittently, in a way
that would take you a week. `SHOW BUS CONTENTION` names them instead.

**cycle** — The unit of emulated time: one tick of the CPU's clock. Every 6800 instruction
costs a known number of them, and that is how this program knows what time it is. At 1 MHz a
cycle is one microsecond, and a cassette takes the time it takes because it costs a fixed
number of them.

**DC-4** — SWTPC's floppy-disk controller: a WD179x on the SS-30 bus, with up to four 5.25″
drives. It is what FLEX boots from. `dc4` in a machine file.

**decode** — What a board does when it recognises an address as its own and answers. A board
that does not decode an address stays silent and lets somebody else have it. Which board decodes
which address is the entire question of how a machine is put together.

**endpoint** — In this program, the thing on the far end of a serial unit's cable: `console`,
`null`, `loopback`, a TCP socket, or a real serial port. The serial chapter has the complete
list; there are no others.

**FLEX** — TSC's disk operating system for the 6800, and the reason the DC-4 is here. It boots
off a floppy to a `+++` prompt; `CAT` lists a disk, and files have names, extensions and a
`SAVE`.

**floating bus** — What the data bus reads when nothing is driving it. Read an address no board
decodes and you get whatever the bus floats to, not an error — that is the absence of a board,
and it looks like a fixed byte.

**front panel** — The controls a real operator used to examine memory, deposit bytes, and start
and stop the CPU. On these machines that job belongs to the ROM monitor — SWTBUG's and MON680's
`M`/`G` commands — and to the `swtpcsim>` prompt, which does EXAMINE, DEPOSIT, RUN, STEP and
RESET from the host with a much better view than a panel of switches gave.

**FSK** — Frequency Shift Keying. Encoding bits as two audible tones — one for a zero, one for a
one. It is how the KCACR gets data onto a cassette, and it is why a loading tape sounds the way
it does. The tones are **not** standard across machines: a board can only read the modulation
its own modem was built for, so mounting a recording in the wrong one is refused rather than
decoded. See **Kansas City standard** and the tapes chapter.

**Kansas City standard** — The 1975 agreement on how microcomputers should record data on
ordinary audio cassettes, named for the meeting that settled it. A one is eight cycles of
2400 Hz, a zero is four cycles of 1200 Hz, and both take the same time — which is the whole
point: a receiver counts cycles instead of trusting a clock, so a tape that plays 5% slow still
reads. 300 baud. The Altair 680b's KCACR reads and writes it. See also **FSK**.

**KCACR** — The Altair 680b's Kansas City audio-cassette interface: a UART recording Kansas City
FSK, memory-mapped at `F010`/`F011`, with a loader/punch PROM. `680kcacr` in a machine file.

**MCP** — Model Context Protocol. The protocol an AI assistant uses to call structured tools.
`swtpcsim --mcp` speaks it, so an assistant can drive the machine directly. See the MCP chapter.

**MON680** — the Altair 680b's ROM monitor. Prompt `.`; `M` examines and deposits a byte, `G`
goes, `J` jumps to an address.

**monitor** — Two things wear the name here. A **ROM monitor** (SWTBUG, MON680) is the program
burned into the machine's ROM that examines memory and runs code — the software a bare 6800 was
shipped with. The **`swtpcsim>` monitor** is the host prompt you reach with `^E`: not a program
running inside the machine, but you, standing in front of it, with a much better console than
the hardware ever had.

**MP-ID** — SWTPC's interface driver board for its 6809 systems. It carries a 6840 timer that
counts the power line, which is FLEX9's timer, and a PIA printer port. `mpid` in a machine file.

**MP-T** — SWTPC's interrupt timer board: a 6820 PIA and a time-base chip on the SS-30 bus,
interrupting the 6800 once every interval a program picks, from 1 µs to 1 hour. `mpt` in a
machine file.

**PROM** — Programmable Read-Only Memory. A chip with a program burned into it that survives
power-off. A 6800's ROM monitor lives in one, which is the only reason the machine can start at
all: something has to already be there to talk to.

**sector** — The smallest chunk of a disk you can read or write. FLEX disks use 256-byte
sectors.

**SS-50 / SS-30 bus** — SWTPC's two buses. The **SS-50** carries the CPU, memory and the system
signals — the motherboard everything plugs into. The **SS-30** is the I/O bus that hangs off it,
an eight-slot window at `$8000`–`$801F` where the MP-S console and the DC-4 floppy controller
live; slot *N* sits at `$8000 + 4·N`.

**STOP** — The STOP control, and the key that presses it: `^E` by default. It stops the running
CPU and returns you to the monitor without disturbing the machine — not RESET, not POWER — so a
bare `RUN` resumes at the instruction it was about to execute. The host intercepts it before the
guest sees the byte, so no guest program can take it from you. Move it with `CONSOLE stop=` (the
older `attn=` still works).

**SWTBUG** — the SWTPC 6800's ROM monitor, in a 2716 at `$E000`. Prompt `$`; `D` boots a disk,
`M` examines memory, `G` goes.

**TDRE** — Transmit Data Register Empty. The bit in the 6850's status register that means "the
board has room for another character". A program that wants to print polls it until it sets. A
serial port connected to `null` sets it forever, which is why writing to nothing works fine.

**track** — One concentric ring of sectors on a disk. The head steps in and out to reach a track
and does not move again to reach the sectors on it.

**UART** — Universal Asynchronous Receiver/Transmitter. The chip that turns a byte into a
sequence of bits on a wire and back again. The ACIA is one.

**unit** — One channel on a board that moves characters — the socket on the back. The MP-S
console has one (`tty`); the 680b Universal I/O board adds a second serial line (`serial`).
`CONNECT uio0:serial` names the board and then the unit.
