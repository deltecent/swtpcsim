# `memory` — Static memory board (RAM and/or ROM)

**Status:** the generic RAM/ROM board every machine uses. It is the `memory` board type the
`swtpc` and `altair680` machines fit for their RAM and their monitor PROM.

## The real hardware

Period memory boards are a **base address** and a set of **populated areas**, both set by jumpers, DIP switches, and which sockets you actually filled. Several boards live in one machine and together they make up the memory map.

Three facts about real cards drive this design. The first two are commonly missed; the third is the one that shapes the whole board.

1. **A card was frequently only partly populated.** You bought a 16K card and soldered in 4K of chips, meaning to fill it later. The card decoded its full range but **only answered where the chips actually were.** So "size" and "which addresses respond" are *not* the same thing.
2. **RAM contents are lost when power is removed — and only then.** Pressing the reset button does not clear memory, and a board that clears its store on RESET\* is modeling a machine nobody ever built.
3. **One card could carry both RAM and ROM, in several areas at once.** A PROM card is a row of sockets — four 2708s at F000/F400/F800/FC00 — and any of them may be empty. Combo RAM+ROM cards existed too.

## Why one board and not two

An earlier draft had a `ram` board and planned a separate `rom` board. That is wrong, and fact 3 is why: a real card may hold **two ROM areas and two RAM areas**, and modeling it as four boards would put four lines in `BOARDS` for one physical card — breaking the premise the whole bus model rests on, that a board *is* a card.

So there is **one `memory` board, holding a list of regions.** A region is *an area of the card that is populated with something*:

| Region `type` | Reads | Writes |
|---|---|---|
| `ram` | from the store | **stored** |
| `rom` | from the store | **not decoded** |

That is the only difference between them. A ROM socket and a RAM chip-range are otherwise the same thing, and collapsing them makes the board *smaller*, not bigger: an unpopulated ROM socket and an unpopulated RAM page are now the same case, handled by the same page map, floating to the same `0xFF`.

### There is no write-protect on this board

An earlier draft also modeled the Altair front panel's **PROTECT / UNPROTECT** here. It is gone, at Patrick's direction (2026-07-11), and the reason is worth keeping because someone will otherwise re-add it.

Protect was standing in for *"load a HEX file and treat it as ROM."* It is the wrong tool for that, and the reason is fact 2: **RAM is lost at power-off.** So the image has to be re-loaded on every power cycle — and the protect map *survives* while the bytes do not, leaving a region that is confidently marked protected and full of garbage. A `rom` region re-reads its file on power-up, which is what a PROM does.

> If the front-panel feature is ever wanted as a *replica*, it comes back — but it needs a manual first (its granularity is unsourced; `DESIGN.md` §0.1), and it must not be reintroduced merely because "ROM ought to be write-protectable." **ROM is not protected RAM. It is a region that does not decode writes.**

## Sources

| Source | Path | Authority |
|---|---|---|
| Page granularity, size shortcuts | Patrick (2026-07-11) | 256-byte (100H) pages; `48K`/`64K` shortcuts. |
| **A reset never clears RAM; only power-off does** | Patrick (2026-07-11) | See *Reset*, below. |
| **One card may hold several RAM and ROM areas** | Patrick (2026-07-11) | Hence regions, and hence no separate `rom` board. |
| No write-protect | Patrick (2026-07-11) | See above. |

> This card is a *generic* memory board, not a replica of a specific part number — it is not claiming to be an 88-16MCS and should not pretend to be.

## Register reference

**No registers at all.** The board decodes memory cycles only — no device register the guest can read or write — and the guest cannot reconfigure it. Regions are *configuration*.

## Regions

```toml
[[board]]
type = "memory"
id   = "mem0"

  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "48K"                 # 0000-BFFF

  [[board.region]]
  type = "rom"
  at   = 0xF000
  mount = "roms/monitor.bin"   # 2048 bytes -> decodes F000-F7FF

  [[board.region]]
  type = "rom"
  at   = 0xFF00
  mount = "roms/dbl.hex"       # 256 bytes  -> decodes FF00-FFFF
```

**A `rom` region's size comes from its image**, rounded up to a 100H page. A short image decodes a short range — drop a 256-byte boot PROM anywhere and it occupies one page.

### Built-in ROMs — `mount = "builtin:<name>"`

**The common ROMs are compiled into the simulator.** There is no portable place to put ROM images across macOS, Linux, and Windows — `/usr/share`, `~/Library`, `%APPDATA%`, and "next to the binary" are four different answers, and every one of them is a support question. ROMs are small (a boot PROM is 256 bytes; a monitor is 2K), so they are embedded and the question disappears:

```toml
  [[board.region]]
  type  = "rom"
  at    = 0xE000
  mount = "builtin:swtbug"     # compiled in. Works on a fresh checkout, on any OS,
                               #   with no download and nothing to install.

  [[board.region]]
  type  = "rom"
  at    = 0xF000
  mount = "roms/my-monitor.hex" # a bare path is still a host file. Yours wins.
```

The `builtin:` prefix follows the same scheme idiom the design already uses for `connect` (`socket:`, `serial:`, `in:`/`out:`, `console`) — so it needs no new grammar, and `CONFIG SAVE` round-trips the string `builtin:swtbug` rather than a wad of bytes.

**Every built-in ROM must have a provenance row in `docs/roms.md`** — what it is, where the dump came from, its exact size, and its CRC32. This is `DESIGN.md` §0.1 applied to binaries: *an embedded blob with no source is a second-hand fact*, and a ROM that silently differs from the real part is the single most expensive bug this project could ship, because every piece of software above it would be debugged against the wrong ground truth. A unit test verifies each CRC at build time, so a corrupted embed fails the build instead of failing a user.

```
swtpcsim> SHOW ROMS
  name       size   crc32     description
  ---------------------------------------------------------------------
  swtbug     1024   ????????  SWTPC SWTBUG monitor
  mon680     ...    ????????  MITS Altair 680b MON680 monitor
  ...

  Built in. Use  mount = "builtin:<name>"  in a [[board.region]].
  A bare path loads a host file instead — see docs/roms.md for provenance.
```

Built-ins are a **convenience, never a lock-in**: a path always overrides, so anyone with a different dump of the same part can use it without patching the simulator.

**`F800`–`FEFF` above is an empty socket.** The board does not decode it. If nothing else in the machine does either, it floats to `0xFF` (see below). This needed no new mechanism: an empty ROM socket and an unpopulated RAM page are the same case.

**Regions are sub-units**, exactly like the DC-4's drives and the MP-S's units, so the existing `id:unit` addressing already reloads a socket with no new monitor syntax:

```
swtpcsim> MOUNT mem0:rom0 newdbl.hex
mem0:2 — rom, FF00-FFFF (256 bytes, 1 page), from newdbl.hex
```

### An empty socket is still a socket

A `rom` region with **no `mount`** is a socket with no chip in it, and that is an ordinary thing for a card to be — a four-socket PROM board with two chips in it is a machine somebody actually owned. It decodes nothing, so those pages float; it still has a unit name, so you can `MOUNT` a chip into it.

**`UNMOUNT` pulls the chip. It does not unsolder the socket.** The region stays, empty, and keeps its name. This is not fussiness: the sockets are **numbered**, so erasing the region would renumber every socket behind it — pull the chip out of `rom0` and the chip sitting in `rom1` would silently *become* `rom0`, and `MOUNT mem0:rom0` would then put it in the wrong socket. You cannot unsolder a socket by pulling its chip.

`BOARDS` shows it as `rom1(empty)` in UNITS, and it is **absent from the memory column** — because the memory map is a map of what is decoded, and an empty socket is not.

## How it is simulated

**The page is the unit of everything: 256 bytes (100H).** A 64K space is 256 pages, `00`–`FF`. The board keeps one page map — *which region, if any, owns this page?* — and it drives `decodes()`:

```cpp
bool MemoryBoard::decodes(const BusCycle& c) const {
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    const Region* r = owner(c.addr);
    if (!r) return false;                            // empty socket / unpopulated page
    if (c.type == Cycle::MemWrite && r->kind == RegionKind::Rom) return false;  // <- ROM never answers a write
    return true;
}

uint8_t MemoryBoard::read (const BusCycle& c) { return store_[plane(c.addr)]; }
void    MemoryBoard::write(const BusCycle& c) { store_[plane(c.addr)] = c.data; }
```

That is the whole board. Note what `write()` does **not** contain: any check at all. **A write that reaches the board is stored, unconditionally** — because a real static RAM chip that is selected with `WE` asserted *stores the byte*; it has no opinion. And a write can only reach the board if `decodes()` let it, which a ROM region never does.

### Writes to ROM — the board does not decode them

**A `rom` region does not "reject" a write, or "ignore" it, or log it. It never answers the cycle.** This is the single most important line in this document, because everything else falls out of it and nothing needs a special case.

What happens *next* is emergent, decided by what else is in the machine — **the bus arbitrates nothing** (`DESIGN.md` §4.2):

| What else covers that address | A guest write does |
|---|---|
| Nothing | **Nothing latches it. The byte is gone.** The write half of the floating bus. |
| Another board's RAM (write only) | **Lands in that RAM** — the ROM never answered the write, so the RAM is the only board that did. |

A `read` at an address two boards both decode is a different case: they both drive, and that is **contention** (§4.6), reported and naming both — exactly the bug a real backplane would have handed you.

> **Heritage note.** On the S-100 machines the framework came from, a boot ROM could *shadow* the RAM beneath it — read as ROM while letting writes fall through to the RAM — by pulling `PHANTOM*` (`DESIGN.md` §4.2). No SS-50 board here does that; the line and its `phantom`/`honors_phantom` straps were removed, so an overlap of two decoding boards is contention, not a silent shadow.

### Getting bytes *into* a ROM region

The guest cannot write ROM. **The operator can**, and the mechanism already exists — `RAW <id>` (`DESIGN.md` §10.2) reaches behind the bus, straight into the board's store:

```
swtpcsim> LOAD dbl.hex RAW mem0        ; the PROM burner. Not a bus cycle.
mem0: loaded 256 bytes from dbl.hex (FF00-FFFF, region 2)

swtpcsim> DEPOSIT FF00 41              ; a bus cycle. mem0 doesn't decode it.
FF00: no board decodes writes here (mem0 region 2 is rom). byte discarded.
                                        ; ^ not silence. see Quirks.
```

That distinction is not a simulator convenience — it is physically what happens. Burning a PROM is *not a bus operation*; you pull the chip and put it in a programmer. Modeling it as a bus write would require the bus to know who originated a cycle, which a real backplane cannot know and which no board should ever have to ask.

Intel HEX carries its own load addresses, so the file places the bytes; a `.bin` needs `AT`. The loader is already specified (§10.3), checksums every record, and fails loudly with the record number.

### The floating bus

**An unpopulated page needs no special case.** The board just doesn't decode it. If no *other* board does either, nobody drives the bus, it floats high, and **the CPU reads `0xFF`** — the same rule that answers every unmapped read with `0xFF` (`DESIGN.md` §4.6.1). One mechanism; unpopulated memory and empty ROM sockets both fall out of it for free.

A **write** to an unclaimed address is the mirror image: nobody latches it, and it is simply gone.

## Banking

This board has **no** banking and **no** device register — it is plain, unbanked RAM and ROM, which is
what the `swtpc` and `altair680` machines want. Bank switching would be a different board with its
own decoder; there is no banked-memory board in swtpcsim today.

## Fill on power-up — real RAM does not come up zeroed

`fill = zero | random`, applied to `ram` regions. **`random` is the honest default for a bench**, because real static RAM powers up in an indeterminate state, and software that *assumes* zeroed memory is buggy software that a zero-filling simulator will never catch. `zero` is available because it makes failures reproducible when you are chasing something else.

> **`random` must take a `seed`, and the seed must go in the snapshot** — otherwise it is a source of nondeterminism outside the `EventQueue`, and deterministic replay (§13) is dead the first time you need it. Exactly the class of thing §7.5 exists to prevent.

`rom` regions are not filled. They are **re-read from their files** on power-up.

## Properties

| Property | Type | Runtime? | Meaning |
|---|---|---|---|
| `region` | Region[] | config | The populated areas. Each is `type` (`ram`\|`rom`), `at` (page-aligned), and either `size` (ram) or `mount` (rom). |
| `pages` | PageMap | **read-only** | The composite page map, as a range list — which pages this board answers for. **Derived from the regions**, and not a key you may set: the regions are the truth, and a page map you could edit behind them would be a second, disagreeing one. |
| `fill` | Enum | config | `random` (default) or `zero`. Applies to `ram` regions. |
| `seed` | Int | config | RNG seed for `fill=random`. **Snapshotted**, so replay stays deterministic. |

There are **no board-specific commands**, and the monitor learns nothing about memory. What a
card decodes is decided by the regions you declare, in the machine file — there is no runtime
verb that punches a hole in a populated card.

`SHOW mem0` prints the regions, the sockets, and the properties:

```
swtpcsim> SHOW mem0
mem0  (memory)
  regions:
    0  ram  0000-DFFF  56K
    1  rom  FF00-FFFF  builtin:mon680

  unit     kind    holds
    rom0    rom     builtin:mon680  (read-only)

  property         value            legal
  fill             random           zero|random
  seed             1                
  pages            0000-DFFF,FF00-FFFF (read-only)
```

**`pages` is the hole, stated as a range list.** This card answers for `0000-DFFF` and
`FF00-FFFF` and nothing else, so `E000-FEFF` is unpopulated and reads there float to `FF`. That
is the difference between a user understanding their machine and filing a bug — a hole in memory
is invisible otherwise. And a `rom` region does not decode writes at all, which is the single
most confusing thing this board can do to you: the write does not fail, it simply never happened.

## Multiple cards

Several `memory` boards coexist, each with its own regions and maps — which is what a real backplane looks like, and it exercises multi-board decode and contention (§4.6) from the first milestone:

```toml
# A plain 16K RAM card at the bottom of memory.
[[board]]
type = "memory"
id   = "mem0"
fill = "random"           # real RAM powers up indeterminate
seed = 12345              # ...but reproducibly so. Snapshotted; replay stays deterministic.
  [[board.region]]
  type = "ram"
  at   = 0x0000
  size = "16K"

# A second, static-strapped board above it -- plain, unbanked memory too,
# with no device register of its own.
[[board]]
type = "memory"
id   = "mem1"
  [[board.region]]
  type = "ram"
  at   = 0x4000
  size = "48K"
```

Overlap two cards and the bus reports it, naming both.

## Reset

**A reset never changes memory. RAM is lost only when the machine is powered off.** (Patrick, 2026-07-11.) Say it out loud, because the code will be tempted to conflate the two.

| Event | Monitor | `ram` contents | `rom` contents | `pages` |
|---|---|---|---|---|
| **Power applied** | `POWER` | **indeterminate** — filled per `fill` | **re-read from their files** | configured |
| **`Reset::PowerOn`** (POC\*) | *(part of `POWER`)* | untouched | untouched | untouched |
| **`Reset::Bus`** (RESET\*, the front-panel button) | `RESET` | untouched | untouched | untouched |

POC\* and power coming up coincide on real hardware, which is why they share the `POWER` command — but they are **different things**, and the memory array is the proof: a RAM chip has no POC\* pin. Its contents are indeterminate because *the chips just powered up*, not because a signal arrived. Model the fill as belonging to power, and let both resets leave the store alone.

**A reset on this board touches nothing at all** — there is no bank latch here to clear.

And the trap in the other direction: *"my program vanished when I hit reset"* reads like a memory-model bug rather than a reset bug, and will cost you a day.

## Quirks reproduced

| Quirk | If you get it wrong |
|---|---|
| **A `rom` region does not decode writes** — it does not reject them | Reject them *in the board* and "writes fall through to the RAM underneath" becomes a special case in the bus, which is the mistake §4.2 exists to prevent. |
| **Unpopulated pages read `0xFF`**, not `0x00` | Memory-sizing routines find 64K on every machine and size themselves wrong. A zero-filled hole also disassembles as a field of `NOP`s, which is a uniquely confusing thing to stare at. |
| **A write to ROM with nothing beneath is silently gone** | It has to be — nothing latched it. But **report it at the monitor** when the operator does it by hand, or `DEPOSIT` looks broken. Do *not* report it for guest writes: period software scans memory by writing and reading back, and it would spew. |
| **A reset preserves memory; only power-off clears it** | Clear on reset and the user's program vanishes on a reset-button press — which reads like a memory-model bug, not a reset bug. |
| **A partially-populated card decodes only where chips are** | Model `size` as a plain contiguous range and holes become inexpressible — a real 16K card with 4K fitted cannot be represented at all. |
| **RAM powers up with indeterminate contents** | Zero-fill and you will never catch guest code that assumes zeroed memory — until it runs on real hardware. |

## Limitations and deliberate departures

- **This card is generic, not a specific period part.** Its jumper semantics are ours. A modeled MITS 88-16MCS would get its own doc.
- **A `rom` region's size comes from its image file**, rounded up to a page. Real cards decode a whole chip's worth (a 2708 covers 1K whether or not the image fills it), and `at` would have to be chip-aligned. We take the file's word instead — simpler, and it lets a 256-byte boot PROM sit at FF00, which a strict 1K-chip model could not express.
- **POC\*'s 200 ns minimum pulse width is not modeled.** It is an analog property of the reset circuit (and on real machines, an RC network that drifts — many owners fit a dedicated supervisor IC to get a clean edge). We assume a clean POC\*. Nothing in the digital model depends on the width.
- **No parity, no error detection, no wait states.** Period cards had none worth modeling.

## Verification (milestone 1a acceptance)

No CPU exists in milestone 1a, so **the monitor is the bus master** — `DEPOSIT` and `DUMP` originate real bus cycles with nothing else in the machine, exactly as the Altair front panel did. That is not a workaround; it is a free early test of the `BusMaster` abstraction (§3).

**RAM and the floating bus**

1. A 48K `ram` region; `DUMP C000-C0FF` returns **all `FF`**. The hole is real and reads float.
2. `LOAD test.bin AT 0100`, `DUMP`, `SAVE`, `COMPARE` — byte-identical round trip. Same for Intel HEX.
3. `FILL 0000-BFFF 00`, then `RESET` → contents **survive**. `POWER` → contents **re-filled** per `fill`.
4. `fill=random` with a fixed `seed` → **byte-identical memory across two runs**, and the seed survives `SNAPSHOT`/`RESTORE`.

**ROM regions**

5. A `rom` region at FF00 from a 256-byte file decodes **FF00–FFFF and nothing else**; `DUMP FF00` shows the image.
6. `DEPOSIT FF00 41` (a bus write) is **not decoded** — `DUMP FF00` is unchanged, and the monitor **says so** rather than silently succeeding.
7. `LOAD other.hex RAW mem0` **does** change it. The operator has a PROM burner; the guest does not.
8. An **empty socket** between two ROM regions (F800–FEFF above) reads `FF`, exactly like unpopulated RAM. One mechanism.
9. A short image decodes a short range: a 2048-byte file at F000 owns F000–F7FF, and F800 is not this board's problem.

**Overlap is contention, not a silent shadow**

10. A `rom` region over another card's `ram` at the same address: a **read** has both boards driving → **contention reported** (§4.6), naming both. This is a bug the real backplane would have handed you, and it must not be silently resolved.
11. Same setup, a **write**: the ROM does not decode it, so it lands in the RAM beneath — the only board that answered. `DUMP` (a read) then still contends; `RAW` on the RAM card reads back `41`.

**Round trip**

12. Two `memory` cards at overlapping bases → **contention reported**, naming both.
13. `CONFIG SAVE` then `CONFIG LOAD` reproduces the regions and page map exactly.

## References

- `DESIGN.md` §4.1 (decode), §4.2 (`PHANTOM*` — a heritage note), §4.6 (contention, the floating bus), §5 (properties), §6 (reset), §10.2 (`RAW`), §10.3 (Intel HEX), §10.3.1 (built-in ROMs).
- `docs/roms.md` — the built-in ROM registry and its provenance rule.
