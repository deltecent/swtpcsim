<!-- GENERATED FROM THE PROGRAM ITSELF. Do not edit by hand.
     Every default, range and description below is printed from the same tables the
     monitor resolves against, so it cannot disagree with the program you are running. -->

# Boards and their parameters

Every key below is a key you may write in a machine file, and the *same* key you
may `SET` at the monitor prompt. That is not a coincidence and it is not a
convention: a board's properties **are** its TOML schema, so there is nothing here
that could disagree with the program.

Numbers follow the one rule: **on the wire → hex, never on the wire → decimal.**
A port is hex; a baud rate and a drive count are decimal. The defaults below are
printed in each property's own base.

The catalogue is **grouped by function** — CPU, memory, disk, serial, and so on —
and within a group the boards are in **alphabetical order**.

**CPU**

| Type | What it is |
|---|---|
| [`6800`](#6800) | Altair 680b / SWTPC CPU board: a Motorola 6800 |
| [`6809`](#6809) | CPU board: a Motorola 6809 |
| [`mp09`](#mp09) | SWTPC MP-09: a 6809 with the DAT and the S-BUG ROM |

**Memory**

| Type | What it is |
|---|---|
| [`memory`](#memory) | RAM/ROM board: plain, unbanked memory regions |

**Disk**

| Type | What it is |
|---|---|
| [`dc4`](#dc4) | SWTPC DC-4: WD179x 5.25" floppy controller |

**Serial**

| Type | What it is |
|---|---|
| [`680io`](#680io) | Altair 680b onboard I/O: 6850 console and strap port |
| [`mps`](#mps) | SWTPC MP-S: one 6850 serial port on an SS-30 slot |

**Tape**

| Type | What it is |
|---|---|
| [`680kcacr`](#680kcacr) | Altair 680b KCACR: Kansas City audio cassette |

**Parallel and printer**

| Type | What it is |
|---|---|
| [`680uio`](#680uio) | Altair 680b Universal I/O: 6850 serial port and 6820 PIA |

**Timers**

| Type | What it is |
|---|---|
| [`mpt`](#mpt) | SWTPC MP-T: a 6820 PIA interrupt timer on an SS-30 slot |


## CPU

### `6800`

Altair 680b / SWTPC CPU board: a Motorola 6800. Decodes nothing -- it drives the bus. Memory-mapped I/O

**Units:** `6800` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the board. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: cycles per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


### `6809`

CPU board: a Motorola MC6809. It decodes nothing. It drives the bus and takes IRQ from the bus. Its FIRQ input is not connected

**Units:** `6809` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | Crystal on the board. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `achieved_hz` | int | — | — | LIVE: cycles per real second the run loop last reached -- the crystal you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


### `mp09`

SWTPC MP-09 processor board: a Motorola MC6809, the DAT (a write-only 16 x 4 map at FFF0-FFFF that turns each logical 4K segment into a physical one; FF00-FFFF passes through) and IC4, the S-BUG monitor ROM at physical F800-FFFF. Takes IRQ from the bus; FIRQ is not connected

**Units:** `6809` (cpu)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `clock_hz` | int | `0` | `0` .. `100000000` | The CPU clock: a quarter of the crystal -- 1000000 for the 4 MHz crystal, 2000000 for 8 MHz. 0 runs flat out -- as fast as the host can. |
| `idle` | bool | `true` | `on` \| `off` | Stand down when the guest is only polling an empty keyboard. On by default -- the guest cannot tell, and a prompt stops burning a core. |
| `rom` | string | `builtin:sbug` | text | The monitor in IC4, at physical F800-FFFF: builtin:<name> or a file (relative to THIS FILE). Empty leaves the socket empty. |
| `dat` | string | — | — | LIVE: the physical 4K segment each logical segment 0-F maps to, as the DAT holds it. Read-only -- the guest loads it by writing FFF0-FFFF. **(read-only — not a key you may set)** |
| `achieved_hz` | int | — | — | LIVE: cycles per real second the run loop last reached -- the clock you got, beside the one you asked for. Read-only; 0 until it has run. **(read-only — not a key you may set)** |


## Memory

### `memory`

RAM/ROM board: a list of regions -- plain, unbanked memory

#### `[[board.region]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `type` | enum | `ram` \| `rom` | RAM, or ROM (which needs a `mount`, unless you want an empty socket) |
| `at` | int | `0x0` .. `0xFFFF` | Where it starts. An address: 0000, F800 |
| `size` | int | `1` .. `65536` | How much. Decimal, and it takes a suffix: 48K, 1024, 2M |
| `mount` | string | text | The ROM image. A file (relative to THIS FILE), or builtin:<name> |
| `relocate` | bool | `on` \| `off` | Move the image so its first record lands at `at` (like LOAD's AT), rather than requiring it to self-place there -- for a mirror-decoded ROM |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `fill` | enum | `random` | `zero` \| `random` | RAM contents at power-on: zero \| random (real RAM is not zeroed) |
| `seed` | int | `1` | any | Seed for fill=random. The same seed fills RAM the same way at every POWER, so a run is repeatable; change it for a different junk pattern |
| `pages` | string | — | — | the composite page map -- which pages this board answers for. Derived from the regions you declared **(read-only — not a key you may set)** |


## Disk

### `dc4`

SWTPC DC-4 floppy disk controller: a WD179x (1 MHz) with up to four 5.25" drives (drive0..3). Spans two SS-30 ports -- a true-sense drive/side select latch at $8014 and the WD179x registers at $8018-$801B (default). Memory-mapped, DRQ-polled. FLEX 2.0/3.0 boots from it via SWTBUG's 'D' command

**Units:** `drive0` (disk, MOUNT), `drive1` (disk, MOUNT), `drive2` (disk, MOUNT), `drive3` (disk, MOUNT)

#### `[[board.drive]]` — a list you may add

| Key | Kind | Legal | Meaning |
|---|---|---|---|
| `unit` | int | `0` .. `3` | Which drive (0..3) |
| `mount` | string | text | The disk image to put in it. Relative to THIS FILE. |
| `readonly` | bool | `on` \| `off` | Write-protect the disk. The drive senses it, so the guest is never told *(also `writeprotect`)* |
| `media` | enum | `flex35` \| `flex40` \| `flexds` \| `cp68` | Force the format instead of probing the image's size |

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0x8018` | 8008-801C \| E008-E01C, a multiple of 4 | WD179x block base ($8018 = SS-30 slot 6; $E018 on a 6809 motherboard); registers at base..base+3, drive-select latch four below (base-4) |
| `speed` | enum | `full` | `full` \| `real` | Drive timing. full: collapse seek and byte timing so the disk keeps up with the simulator at whatever speed it runs (the default -- FLEX polls DRQ/BUSY and cannot tell the difference). real: model the WD179x's actual timing -- 30ms/step seeks and per-byte data rate, and the Lost Data that a too-slow driver would hit |
| `drives` | int | `4` | `1` .. `4` | Drives on the controller (binary select D0..D1, 0-3) |


## Serial

### `680io`

Altair 680b onboard I/O: a 6850 ACIA console ('tty') at F000/F001 and the config-strap read port at F002. Memory-mapped

**Units:** `tty` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `straps` | int | `0x0` | `0x0` .. `0xFF` | Config straps read at F002: bit7 No-Terminal (0=terminal), bit2 stop bits |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


### `mps`

SWTPC MP-S serial interface: a 6850 ACIA console ('tty') on an SS-30 slot, control/status at the slot base and Rx/Tx data at base+1 (default $8004/$8005, the console slot). Memory-mapped; the ACIA IRQ pulls the 6800 IRQ. The board SWTBUG/MIKBUG's terminal routines assume

**Units:** `tty` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0x8004` | 8000-801C \| E000-E01C, a multiple of 4 | SS-30 slot base (window + slot*4: $8000 on a 6800 motherboard, $E000 on a 6809 one); control/status at base, Rx/Tx at base+1 |

#### Unit `tty` — `[board.unit.tty]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |


## Tape

### `680kcacr`

Altair 680b KCACR audio-cassette interface: a 1602-family UART recording Kansas City Standard FSK, memory-mapped at F010 (status/control) and F011 (data), active-LOW. Adds software motor control (control D7=on, D6=off) and interrupt-driven transfer (D0/D1 enables pull the 6800 IRQ). MOUNT a tape, WIND/REWIND it

**Units:** `tape` (tape, MOUNT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `300` | `50` .. `25000` | Line rate. A JUMPER on the real card -- software cannot change it |
| `data_bits` | int | `8` | `5` .. `8` | Data bits per character. The NDB1/NDB2 pads |
| `stop_bits` | int | `2` | `1` .. `2` | Stop bits. The NSB pad: GND = 1, +V = 2 |
| `parity` | enum | `none` | `none` \| `odd` \| `even` | The NPB/POE pads: none \| odd \| even |
| `motor` | enum | — | — | Tape-recorder motor relay (guest-driven: STA F010 7F = on, BF = off) **(read-only — not a key you may set)** |

#### Unit `tape` — `[board.unit.tape]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `mode` | enum | `play` | `play` \| `record` | Which way the bytes go: play loads from the file, record saves to it |
| `format` | enum | `auto` | `auto` \| `raw` \| `kcs300` | How to read the mounted file: auto \| raw \| fsk300 |
| `leader` | int | `15` | `0` .. `120` | Seconds of idle tone before recorded data, when writing audio |
| `trailer` | int | `5` | `0` .. `120` | Seconds of idle tone after recorded data, when writing audio |
| `waveform` | enum | `square` | `square` \| `sine` | Carrier shape when writing audio: square (like real hardware) \| sine |
| `level` | int | `36` | `1` .. `100` | Recording level as a percent of full scale, when writing audio |
| `rate` | enum | `full` | `full` \| `real` | Playback speed: full (as fast as the guest reads) \| real (wall-clock baud) |
| `detected` | string | — | — | What the mounted tape turned out to be (empty if nothing is mounted) **(read-only — not a key you may set)** |
| `position` | string | — | — | Where the tape head is now: mm:ss / total (percent) -- read-only **(read-only — not a key you may set)** |
| `counter` | enum | `on` | `on` \| `off` | Live tape counter on the console during a load: on \| off |
| `stop` | string | `off` | off \| end \| mm:ss | Auto-stop playback at this time: off \| end \| <mm:ss> |


## Parallel and printer

### `680uio`

Altair 680b Universal I/O: a second 6850 ACIA serial port ('serial') and a 6820 PIA parallel port (sections 'p1a/p1b', 'p2a/p2b' with pias=2) in an S9-relocatable window (default base F000: serial F006/F007, PIA F008-F00F), plus fixed switch inputs at F003 and a non-latched output at F010-F013. Memory-mapped, active-high

**Units:** `serial` (serial, CONNECT), `p1a` (serial, CONNECT), `p1b` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0xF000` | `0xF000` .. `0xF0F0` | S9 window base (F000 + position*0x10); serial at base+6, PIAs base+8..+F |
| `pias` | int | `1` | `1` .. `2` | 6820 PIAs populated: 1 (PIA-C only) or 2 (PIA-C + PIA-B) |
| `sense` | int | `0x0` | `0x0` .. `0xFF` | Switch inputs read at F003 (fixed, read-only tri-state) |
| `nlout` | bool | `true` | `on` \| `off` | Decode the F010-F013 non-latched output (off = IC A1 removed, KCACR owns F010/F011) |

#### Unit `serial` — `[board.unit.serial]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `baud` | int | `9600` | `50` .. `76800` | Line rate. A JUMPER on the real card -- software cannot change it, and there is no free-running setting: the rate paces the line |
| `dcd` | enum | `ground` | `ground` \| `wired` | /DCD pin: grounded on the card, or wired to the connector |
| `cts` | enum | `ground` | `ground` \| `wired` | /CTS pin: grounded on the card, or wired -- and then it gates the transmitter |
| `lines` | string | — | — | Live pin state (read-only). CAPITALS = asserted. in: DCD CTS, out: RTS BRK **(read-only — not a key you may set)** |
| `connect` | string | `null` | text | The endpoint on the other end of the line (CONNECT sets this) |

#### Unit `p1a` — `[board.unit.p1a]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this PIA section (CONNECT sets this) |

#### Unit `p1b` — `[board.unit.p1b]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint on the other end of this PIA section (CONNECT sets this) |


## Timers

### `mpt`

SWTPC MP-T interrupt timer: a 6820 PIA on an SS-30 slot (default $8010-$8013). Side B drives an MK5009 time base -- PB0-PB3 select 1 us to 1 hour, PB7 holds it in reset -- whose output interrupts on CB1; side A is a buffered 8-bit input port with a CA1 strobe. Memory-mapped; both PIA IRQs pull the 6800 IRQ

**Units:** `in` (serial, CONNECT)

#### Board properties

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `base` | int | `0x8010` | 8000-801C \| E000-E01C, a multiple of 4 | SS-30 slot base (window + slot*4: $8000 on a 6800 motherboard, $E000 on a 6809 one); PIA side A at base/base+1, side B (the timer) at base+2/base+3 |

#### Unit `in` — `[board.unit.in]`

| Key | Kind | Default | Legal | Meaning |
|---|---|---|---|---|
| `connect` | string | `null` | text | The endpoint feeding the side-A input port (CONNECT sets this) |

