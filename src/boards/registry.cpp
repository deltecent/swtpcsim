#include "boards/registry.h"

#include "boards/s100-memory.h"
#include "boards/mits-680cpu.h"
#include "boards/cpu6809card.h"
#include "boards/swtpc-mp09.h"
#include "boards/mits-680io.h"
#include "boards/mits-680kcacr.h"
#include "boards/mits-680uio.h"
#include "boards/swtpc-mps.h"
#include "boards/swtpc-dc4.h"
#include "boards/swtpc-mpid.h"
#include "boards/swtpc-mpt.h"

namespace swtpc {

// The type name is the CHIP or the board's common name, because that is the word
// an operator reaches for -- `BOARDS ADD 6800 cpu0`. The card's identity lives in
// its .md, where it belongs.
//
// swtpcsim is the Motorola 6800/6809 machine. Its board set is the 6800 world
// it inherited (the Altair 680b boards) plus the SWTPC SS-50/SS-30
// boards: `mps` (MP-S serial console, landed Stage 2) and `dc4` (WD1797 floppy,
// Stage 3) -- see /Users/patrick/.claude/plans.
std::vector<BoardType> boardTypes() {
    return {
        {"memory", "RAM/ROM board: plain, unbanked memory regions", "RAM/ROM board: a list of regions -- plain, unbanked memory"},
        {"6800", "Altair 680b / SWTPC CPU board: a Motorola 6800", "Altair 680b / SWTPC CPU board: a Motorola 6800. Decodes nothing -- it drives the bus. Memory-mapped I/O"},
        {"6809", "CPU board: a Motorola 6809", "CPU board: a Motorola MC6809. It decodes nothing. It drives the bus and takes IRQ from the bus. Its FIRQ input is not connected"},
        {"mp09", "SWTPC MP-09: a 6809 with the DAT and the S-BUG ROM", "SWTPC MP-09 processor board: a Motorola MC6809, the DAT (a write-only 16 x 4 map at FFF0-FFFF that turns each logical 4K segment into a physical one; FF00-FFFF passes through) and IC4, the S-BUG monitor ROM at physical F800-FFFF. Takes IRQ from the bus; FIRQ is not connected"},
        {"680io", "Altair 680b onboard I/O: 6850 console and strap port", "Altair 680b onboard I/O: a 6850 ACIA console ('tty') at F000/F001 and the config-strap read port at F002. Memory-mapped"},
        {"680uio", "Altair 680b Universal I/O: 6850 serial port and 6820 PIA", "Altair 680b Universal I/O: a second 6850 ACIA serial port ('serial') and a 6820 PIA parallel port (sections 'p1a/p1b', 'p2a/p2b' with pias=2) in an S9-relocatable window (default base F000: serial F006/F007, PIA F008-F00F), plus fixed switch inputs at F003 and a non-latched output at F010-F013. Memory-mapped, active-high"},
        {"680kcacr", "Altair 680b KCACR: Kansas City audio cassette", "Altair 680b KCACR audio-cassette interface: a 1602-family UART recording Kansas City Standard FSK, memory-mapped at F010 (status/control) and F011 (data), active-LOW. Adds software motor control (control D7=on, D6=off) and interrupt-driven transfer (D0/D1 enables pull the 6800 IRQ). MOUNT a tape, WIND/REWIND it"},
        {"mps", "SWTPC MP-S: one 6850 serial port on an SS-30 slot", "SWTPC MP-S serial interface: a 6850 ACIA console ('tty') on an SS-30 slot, control/status at the slot base and Rx/Tx data at base+1 (default $8004/$8005, the console slot). Memory-mapped; the ACIA IRQ pulls the 6800 IRQ. The board SWTBUG/MIKBUG's terminal routines assume"},
        {"dc4", "SWTPC DC-4: WD179x 5.25\" floppy controller", "SWTPC DC-4 floppy disk controller: a WD179x (1 MHz) with up to four 5.25\" drives (drive0..3). Spans two SS-30 ports -- a true-sense drive/side select latch at $8014 and the WD179x registers at $8018-$801B (default). Memory-mapped, DRQ-polled. FLEX 2.0/3.0 boots from it via SWTBUG's 'D' command"},
        {"mpid", "SWTPC MP-ID: 6840 line-clock timer and PIA printer port (S/09)", "SWTPC MP-ID interface driver board (S/09): a 6820 PIA at base (default $E080) and an MC6840 timer at base+$10 ($E090). The 6840 counts the power line -- 2 x line_hz pulses a second into C1 and C3, O3 into C2 -- and a 74LS393 counts O1 onto PIA side A, the clock FLEX9's TIME reads. PIA side B is a printer port ('lpt'). The 6840 and both PIA IRQs pull the bus IRQ"},
        {"mpt", "SWTPC MP-T: a 6820 PIA interrupt timer on an SS-30 slot", "SWTPC MP-T interrupt timer: a 6820 PIA on an SS-30 slot (default $8010-$8013). Side B drives an MK5009 time base -- PB0-PB3 select 1 us to 1 hour, PB7 holds it in reset -- whose output interrupts on CB1; side A is a buffered 8-bit input port with a CA1 strobe. Memory-mapped; both PIA IRQs pull the 6800 IRQ"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "6800") return std::make_unique<Cpu6800Board>();
    if (type == "6809") return std::make_unique<Cpu6809Board>();
    if (type == "mp09") return std::make_unique<Mp09Board>();
    if (type == "680io") return std::make_unique<Io680Board>();
    if (type == "680uio") return std::make_unique<Uio680Board>();
    if (type == "680kcacr") return std::make_unique<KcacrBoard>();
    if (type == "mps") return std::make_unique<MpsBoard>();
    if (type == "dc4") return std::make_unique<Dc4Board>();
    if (type == "mpid") return std::make_unique<MpidBoard>();
    if (type == "mpt") return std::make_unique<MptBoard>();
    return nullptr;
}

} // namespace swtpc
