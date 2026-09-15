#include "boards/registry.h"

#include "boards/s100-memory.h"
#include "boards/mits-680cpu.h"
#include "boards/mits-680io.h"
#include "boards/mits-680kcacr.h"
#include "boards/mits-680uio.h"
#include "boards/swtpc-mps.h"
#include "boards/swtpc-dc4.h"

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
        {"memory", "RAM/ROM board: a list of regions -- plain, unbanked memory"},
        {"6800", "Altair 680b / SWTPC CPU board: a Motorola 6800. Decodes nothing -- it drives the bus. Memory-mapped I/O"},
        {"680io", "Altair 680b onboard I/O: a 6850 ACIA console ('tty') at F000/F001 and the config-strap read port at F002. Memory-mapped"},
        {"680uio", "Altair 680b Universal I/O: a second 6850 ACIA serial port ('serial') and a 6820 PIA parallel port (sections 'p1a/p1b', 'p2a/p2b' with pias=2) in an S9-relocatable window (default base F000: serial F006/F007, PIA F008-F00F), plus fixed switch inputs at F003 and a non-latched output at F010-F013. Memory-mapped, active-high"},
        {"680kcacr", "Altair 680b KCACR audio-cassette interface: a 1602-family UART recording Kansas City Standard FSK, memory-mapped at F010 (status/control) and F011 (data), active-LOW. Adds software motor control (control D7=on, D6=off) and interrupt-driven transfer (D0/D1 enables pull the 6800 IRQ). MOUNT a tape, WIND/REWIND it"},
        {"mps", "SWTPC MP-S serial interface: a 6850 ACIA console ('tty') on an SS-30 slot, control/status at the slot base and Rx/Tx data at base+1 (default $8004/$8005, the console slot). Memory-mapped; the ACIA IRQ pulls the 6800 IRQ. The board SWTBUG/MIKBUG's terminal routines assume"},
        {"dc4", "SWTPC DC-4 floppy disk controller: a WD179x (1 MHz) with up to four 5.25\" drives (drive0..3). Spans two SS-30 ports -- a true-sense drive/side select latch at $8014 and the WD179x registers at $8018-$801B (default). Memory-mapped, DRQ-polled. FLEX 2.0/3.0 boots from it via SWTBUG's 'D' command"},
    };
}

std::unique_ptr<Board> makeBoard(const std::string& type) {
    if (type == "memory") return std::make_unique<MemoryBoard>();
    if (type == "6800") return std::make_unique<Cpu6800Board>();
    if (type == "680io") return std::make_unique<Io680Board>();
    if (type == "680uio") return std::make_unique<Uio680Board>();
    if (type == "680kcacr") return std::make_unique<KcacrBoard>();
    if (type == "mps") return std::make_unique<MpsBoard>();
    if (type == "dc4") return std::make_unique<Dc4Board>();
    return nullptr;
}

} // namespace swtpc
