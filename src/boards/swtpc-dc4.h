#pragma once
//
// The SWTPC DC-4 floppy disk controller -- reference/DC-4 Floppy Disk Controller.md.
//
// THE SWTPC 6800's 5.25" MINI-FLOPPY CONTROLLER. The DC-4 is a Western Digital
// WD179x soft-sector floppy controller on an SS-30 I/O card. It is what FLEX 2.0/3.0
// boot from on the SWTPC 6800, and SWTBUG's built-in `D` (disk boot) command is
// hardwired to it. Up to four drives daisy-chain off the one controller.
//
// MEMORY-MAPPED, NOT PORT-MAPPED (the 6800 has no IN/OUT space). Unlike every other
// SS-30 card here, the DC-4 spans TWO adjacent SS-30 ports -- it needs five addresses
// and a port is only four:
//
//     8014   drive-select latch (write) -- D0..D1 drive 0-3, D6 side (1=top)   [port 5]
//     8018   WD179x Command (write) / Status (read)                            [port 6]
//     8019   WD179x Track
//     801A   WD179x Sector
//     801B   WD179x Data
//
// The two addresses are what BOTH consumers hard-code: SWTBUG.ASM's DISK routine
// (CLR $8014; command/status at $8018) and the FLEX disk driver (EDSKDRV.ASM:
// DRVREG=$8014, COMREG=$8018..DATREG=$801B). They are the reason the board is here
// at all, so they are fixed defaults; `base` moves the WD block and the latch tracks
// it four below (the DC-4's two ports are physically adjacent).
//
// THE DRIVE-SELECT LATCH IS TRUE-SENSE. The FLEX driver writes `side|drive` straight
// to $8014 (STAA DRVREG, no complement) -- drive number is BINARY in D0..D1 (not the
// one-hot the VersaFloppy uses), side is D6 ($40 = side 1 / top). See selectFromLatch().
//
// THE PART IS A WD179x AT 1 MHz. The DC-4 clocks the FDC at 1 MHz (EDSKDRV's own
// comment: step-rate code $03 "gives a 40ms step interval" -- the 1771's r1r0=11 rate
// is 40 ms only at CLK=1 MHz). We model it with the family's Wd1791 leaf (FM/MFM, side
// select), which shares the WD179x register file, command set, and step table; a
// separate Wd1797 leaf would differ in nothing this card can observe (side compare is
// not modeled for a synthesized-ID raw .DSK anyway -- see wd17xx.h). fdcClockHz = 1 MHz.
//
// DRQ-POLLED. The FLEX driver polls DRQ then BUSY in a tight loop (EDSKDRV READ2/WRITE2,
// FLEXLOAD rdLoop) -- there is no CPU wait state on the 6800 bus. Because it only ever
// loops on BUSY, it cannot observe whether the WD179x's seek and byte timing are real or
// collapsed, so the card runs the chip WAIT-SYNCED by default (the `speed=full` knob):
// every register access resolves the command as if the guest had waited, and the disk
// keeps up with the simulator at whatever speed it runs. `speed=real` flips the chip to
// byte-timed -- real 30ms/step seeks and per-byte data rate, Lost Data and all -- and the
// card then arms a Clock deadline for each autonomous edge, the 2SIO/VersaFloppy idiom.
//
// The disk on the far end of each drive is a DiskImageDrive (boards/floppy-drive.h)
// over a mounted DiskImage (host/disk.h); the card owns the images and probes their
// geometry against the FLEX format table on mount.

#include "boards/floppy-drive.h"
#include "chips/wd17xx.h"
#include "core/board.h"
#include "core/clock.h"
#include "host/disk.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace swtpc {

class Dc4Board : public Board {
public:
    Dc4Board();
    ~Dc4Board() override;

    std::string type() const override { return "dc4"; }

    // ---- bus (memory-mapped) ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // Five addresses across two SS-30 ports -- not a uniform page, so the bus must ask
    // per address rather than cache one answer for the page (Board::decodeIsPageUniform()).
    bool decodeIsPageUniform() const override { return false; }

    // ---- lifecycle ----
    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // ---- reflection ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // ---- disk units: drive0..drive(n-1), a [[board.drive]] sub-unit table ----
    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    std::vector<std::string> subUnitTables() const override { return {"drive"}; }
    std::vector<Property>    subUnitProperties(const std::string& table) const override;
    std::vector<SubUnit>     subUnits() const override;

    std::vector<std::string> drainLog() override;

    // ---- SNAPSHOT / RESTORE (DESIGN.md 13) ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For tests, without going through the bus.
    Wd17xx& chip() { return *chip_; }

protected:
    bool addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) override;

private:
    // One physical drive on the daisy chain: the mounted image (the board OWNS it) and the
    // adapter that presents it to the chip as a FloppyDrive. The adapter carries the head
    // position, which is runtime state the board serializes.
    struct Drive {
        std::unique_ptr<DiskImage> img;
        std::string    path;    // as WRITTEN (round-trips through SAVE); see mount()
        std::string    forced;  // the `media` property: "" means "probe it"
        DiskImageDrive drv;
    };

    void     selectFromLatch();  // decode $8014: drive select + side -> attach + set side
    void     refresh();          // advance the chip, re-arm the one wake deadline
    Drive*   selected();
    bool     currentTrackHasSector0();  // does the selected drive's current track carry an ID 0?
    Clock&   clk() const;
    uint16_t latchAddr() const { return (uint16_t)(base_ - 4); }  // $8014 when base_ = $8018

    uint16_t base_   = 0x8018;  // WD179x block: base_ .. base_+3; latch at base_-4
    int      drives_ = 4;

    std::unique_ptr<Wd17xx> chip_;
    std::vector<Drive>      drive_;

    uint8_t latch_ = 0;   // last byte written to the drive-select register ($8014), true-sense
    int     sel_   = -1;  // the selected drive (D0..D1), or -1 for none
    bool    fullSpeed_ = true;  // `speed` property: true = wait-synced (collapse timing)

    std::vector<std::string> log_;
    Clock::Handle wake_ = Clock::kNone;
};

} // namespace swtpc
