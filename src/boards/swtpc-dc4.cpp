#include "boards/swtpc-dc4.h"

#include "core/bus.h"
#include "core/clock.h"
#include "core/statefile.h"
#include "core/value.h"
#include "host/media.h"

#include <cstdio>
#include <string>

namespace swtpc {

// An unclocked card is a chip with no crystal: it cannot time a byte, so it reads dead
// rather than dereferencing a null Clock. (Same idiom as the 2SIO / VersaFloppy.)
static Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

Clock& Dc4Board::clk() const { return clock_ ? *clock_ : deadCard(); }

// ---------------------------------------------------------------------------
// THE MEDIA THIS CARD KNOWS -- SWTPC 5.25" single-density formats. All are 256-byte soft
// sectors numbering from 1; the FLEX formats put ten per side. FLEX reserves track 0 (boot
// at sector 1, SIR at sector 3, directory from sector 5) and files start at track 1
// (NEWDISK.ASM: FIRST=$0101, DIRSEC=5, MAXS0=10). The sizes are the whole recorded disk:
//
//   flex35   35 trk x 1 x 10 x 256 =  89,600   (34 usable file tracks, NEWDISK TOT35=340)
//   flex40   40 trk x 1 x 10 x 256 = 102,400   (39 usable,             TOT40=390)
//   flexds   40 trk x 2 x 10 x 256 = 204,800   (39 usable x 2 sides,   TOTDS=780)
//
// CP/68 (TSC) is the same MF-68 SD hardware and boots through SWTBUG's `D` too, but it is a
// CP/M-style disk: 128-byte sectors, eighteen per track, so it needs its own geometry. (The
// sector SIZE is what matters to the WD179x -- CP/68's boot reads exactly 128 bytes per
// sector and then polls BUSY, so a 256-byte layout hangs it with the command still busy.)
//
//   cp68     35 trk x 1 x 18 x 128 =  80,640   (deramp.com CP68.DSK)
//
// DOUBLE-SIDED SECTOR NUMBERING: FLEX numbers a cylinder's sectors 1..10 on side 0 and
// 11..20 on side 1 (NEWDISK MAXS1=20; the driver picks the side from the sector number:
// EDSKDRV SEEK `CMPB #10 / BLS side 0`). So side 1's media carries ID fields 11..20 --
// laid down here with a second initFormat range whose startSector is 11.
struct FlexFormat {
    const char* name;
    int         tracks;
    int         heads;
    int         sectors;
    int         sectorSize;
    Density     density;
    bool        interleaved;  // DS is cylinder-major (T0H0, T0H1, T1H0, ...)
    uint64_t    bytes;
    bool        track0Zero = false;  // CP/68: track 0 is 0,1,2,4..18 (no 3), rest are 1..N
};

static const std::vector<FlexFormat>& flexFormats() {
    static const std::vector<FlexFormat> f = {
        {"flex35", 35, 1, 10, 256, Density::SD, false, 35ull * 1 * 10 * 256},  //  89,600
        {"flex40", 40, 1, 10, 256, Density::SD, false, 40ull * 1 * 10 * 256},  // 102,400
        {"flexds", 40, 2, 10, 256, Density::SD, true,  40ull * 2 * 10 * 256},  // 204,800
        {"cp68",   35, 1, 18, 128, Density::SD, false, 35ull * 1 * 18 * 128, true},  //  80,640
    };
    return f;
}

// ---------------------------------------------------------------------------
// Construction. The part is a WD179x (Wd1791 leaf: FM/MFM, side select), clocked at
// 1 MHz on the DC-4, and DRQ-polled (NOT wait-synced) -- FLEX polls DRQ/BUSY in software.
// ---------------------------------------------------------------------------
Dc4Board::Dc4Board() {
    drive_.resize((size_t)drives_);
    chip_ = std::make_unique<Wd1791>("fdc");
    chip_->fdcClockHz  = 1000000;  // the DC-4 halves a 2 MHz can? no -- a 1 MHz FDC clock:
                                   // EDSKDRV's $03 step code is "40ms", the 1771 r=11 @ 1 MHz
    chip_->dataRateBits = 250000;  // 5.25" MFM; SD media resets this on mount (see mount())
    chip_->setWaitSynced(fullSpeed_);  // full speed by default (see the `speed` property)
}

Dc4Board::~Dc4Board() {
    // The Clock holds a lambda with `this`; a card can be pulled from a running machine, and
    // a deadline firing into a freed board is a use-after-free.
    if (clock_) clock_->cancel(wake_);
}

Dc4Board::Drive* Dc4Board::selected() {
    return (sel_ < 0 || sel_ >= (int)drive_.size()) ? nullptr : &drive_[(size_t)sel_];
}

// Does the selected drive's CURRENT track carry a sector whose ID is 0? Asked when the guest
// writes 0 to the sector register, to tell a CP/68 disk (track 0 really has a sector 0) from
// a FLEX one (numbered from 1, where 0 is the boot's shorthand for sector 1). Reflects the
// track the head is on -- sectorIdAt() synthesizes IDs for the current (head, side).
bool Dc4Board::currentTrackHasSector0() {
    Drive* d = selected();
    if (!d || !d->img) return false;
    const int n = d->drv.sectorCount();
    for (int i = 0; i < n; ++i) {
        FloppyDrive::SectorId id{};
        if (d->drv.sectorIdAt(i, id) && id.sector == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The $8014 drive-select latch -- TRUE-SENSE (the FLEX driver writes it uncomplemented):
//
//   D0..D1   drive number, BINARY 0-3 (EDSKDRV ORAA CURDRV; not the VersaFloppy's one-hot)
//   D6       side: 1 = top/side 1, 0 = bottom/side 0 (SIDEMSK $40)
//
// Point the chip at the selected drive and set the side on both the drive (the DiskImage
// head index) and the chip (the WD179x side-select output).
// ---------------------------------------------------------------------------
void Dc4Board::selectFromLatch() {
    const int drv  = latch_ & 0x03;
    const int side = (latch_ >> 6) & 1;
    sel_ = (drv < drives_) ? drv : -1;

    FloppyDrive* fd = nullptr;
    if (Drive* d = selected()) {
        d->drv.setSide(side);
        fd = &d->drv;
    }
    chip_->attach(fd);
    chip_->setSide(side);
}

// ---------------------------------------------------------------------------
// The bus. Five addresses: the latch at base_-4 ($8014) and the WD179x block base_..base_+3
// ($8018-$801B). Everything else in the page is not ours.
// ---------------------------------------------------------------------------
bool Dc4Board::decodes(const BusCycle& c) const {
    if (!enabled_) return false;
    if (c.type != Cycle::MemRead && c.type != Cycle::MemWrite) return false;
    if (c.addr == latchAddr()) return true;
    return c.addr >= base_ && c.addr <= (uint16_t)(base_ + 3);
}

uint8_t Dc4Board::read(const BusCycle& c) {
    if (c.type != Cycle::MemRead) return 0xFF;
    Clock&  k = clk();
    uint8_t v = 0xFF;
    if (c.addr == latchAddr()) {
        v = 0xFF;  // the drive-select latch is write-only; a read floats the bus
    } else if (c.addr >= base_ && c.addr <= (uint16_t)(base_ + 3)) {
        switch (c.addr - base_) {
            case 0: v = chip_->readStatus(k);   break;  // $8018 command(w) / status(r)
            case 1: v = chip_->readTrackReg();  break;  // $8019 track
            case 2: v = chip_->readSectorReg(); break;  // $801A sector
            case 3: v = chip_->readData(k);     break;  // $801B data
        }
    }
    refresh();
    return v;
}

void Dc4Board::write(const BusCycle& c) {
    if (c.type != Cycle::MemWrite) return;
    Clock& k = clk();
    if (c.addr == latchAddr()) {
        latch_ = c.data;       // $8014 drive/side latch (true-sense)
        selectFromLatch();
    } else if (c.addr >= base_ && c.addr <= (uint16_t)(base_ + 3)) {
        switch (c.addr - base_) {
            case 0: chip_->writeCommand(c.data, k);  break;
            case 1: chip_->writeTrackReg(c.data);    break;
            // SECTOR 0 MEANS THE FIRST SECTOR. SWTBUG's disk boot (and the mholley $0300
            // loader) clear the sector register and read, relying on the SWTPC controller
            // treating a register value of 0 as "the first sector" (EXORsim does the same,
            // as a blanket `if (!data) data = 1`). But the first sector is not always 1:
            // FLEX numbers a track from 1, so its boot IS sector 1 -- while a CP/68 disk
            // numbers track 0 from ZERO (BOOT.ASM: 0,1,2,4..18), so its boot really is
            // sector 0 and a blind 0->1 would skip it. So honor a real sector 0 when the
            // current track HAS one, and only substitute 1 when it does not.
            case 2: chip_->writeSectorReg(c.data == 0 && !currentTrackHasSector0() ? 1
                                                                                   : c.data);
                    break;
            case 3: chip_->writeData(c.data, k);     break;
        }
    }
    refresh();
}

// ---------------------------------------------------------------------------
// The card's clock discipline (the 2SIO/VersaFloppy idiom): advance the chip and re-arm
// the one deadline for its next autonomous edge. Byte-timed (not wait-synced), so the
// chip genuinely has edges between register accesses -- a seek finishing, the next byte
// of a transfer -- and the deadline is what makes them happen with nobody polling.
// ---------------------------------------------------------------------------
void Dc4Board::refresh() {
    if (!clock_) return;
    chip_->poll(*clock_);
    clock_->cancel(wake_);
    wake_ = Clock::kNone;
    uint64_t e = chip_->nextEdge(*clock_);
    if (e) wake_ = clock_->at(e, [this] { refresh(); });
}

void Dc4Board::reset(Reset r) {
    if (!clock_) return;
    if (r == Reset::PowerOn) chip_->powerOn(*clock_);
    refresh();
}

void Dc4Board::power() { reset(Reset::PowerOn); }

void Dc4Board::pump() { refresh(); }

void Dc4Board::configChanged() {
    decodeChanged();  // `base` may have moved the card (and the latch four below it)
    refresh();
}

std::vector<std::string> Dc4Board::drainLog() {
    std::vector<std::string> out = std::move(log_);
    log_.clear();
    if (chip_)
        for (auto& s : chip_->drainLog()) out.push_back(std::move(s));
    for (auto& s : out) s = id + ":" + s;
    return out;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
std::vector<Property> Dc4Board::properties() {
    std::vector<Property> p;
    {
        Property x;
        x.name  = "base";
        x.help  = "WD179x block base ($8018 = SS-30 slot 6); registers at base..base+3, "
                  "drive-select latch four below (base-4 = $8014)";
        x.kind  = Kind::Int;
        x.radix = 16;
        x.min   = 0x8008;  // latch would be $8004 (the console slot) -- keep clear below that
        x.max   = 0x801C;
        x.get   = [this] { return Value::ofInt(base_); };
        x.set   = [this](const Value& v, std::string& err) {
            long long b = v.i();
            if ((b & 0xFFE0) != 0x8000 || (b & 0x0003) != 0) {
                err = "the DC-4's WD179x block is an SS-30 slot: $8000 + slot*4, a multiple "
                      "of 4 in the $8000-$801C I/O window";
                return false;
            }
            if (b < 0x8008) {
                err = "base too low: the drive-select latch (base-4) must clear the console "
                      "slot at $8004";
                return false;
            }
            base_ = (uint16_t)b;
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "speed";
        x.help    = "Drive timing. full: collapse seek and byte timing so the disk keeps up "
                    "with the simulator at whatever speed it runs (the default -- FLEX polls "
                    "DRQ/BUSY and cannot tell the difference). real: model the WD179x's actual "
                    "timing -- 30ms/step seeks and per-byte data rate, and the Lost Data that "
                    "a too-slow driver would hit";
        x.kind    = Kind::Enum;
        x.choices = {"full", "real"};
        x.get     = [this] { return Value::ofStr(fullSpeed_ ? "full" : "real"); };
        x.set     = [this](const Value& v, std::string&) {
            fullSpeed_ = (v.s() == "full");
            chip_->setWaitSynced(fullSpeed_);
            refresh();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "drives";
        x.help  = "Drives on the controller (binary select D0..D1, 0-3)";
        x.kind  = Kind::Int;
        x.radix = 10;  // a count, never on the wire -> decimal
        x.min   = 1;
        x.max   = 4;
        x.get   = [this] { return Value::ofInt(drives_); };
        x.set   = [this](const Value& v, std::string& err) {
            int n = (int)v.i();
            for (int i = n; i < (int)drive_.size(); ++i) {
                if (drive_[(size_t)i].img) {
                    err = "drive" + std::to_string(i) + " still has a disk in it";
                    return false;
                }
            }
            drives_ = n;
            drive_.resize((size_t)n);
            selectFromLatch();  // the vector may have moved -- re-attach
            return true;
        };
        p.push_back(std::move(x));
    }
    return p;
}

std::vector<MapEntry> Dc4Board::memMap() const {
    return {
        {(uint32_t)latchAddr(), (uint32_t)latchAddr(), "drive select",
         "D0-D1 drive 0-3, D6 side (write-only, true-sense)"},
        {(uint32_t)base_ + 0, (uint32_t)base_ + 0, "command/status", "WD179x"},
        {(uint32_t)base_ + 1, (uint32_t)base_ + 1, "track",          "WD179x"},
        {(uint32_t)base_ + 2, (uint32_t)base_ + 2, "sector",         "WD179x"},
        {(uint32_t)base_ + 3, (uint32_t)base_ + 3, "data",           "WD179x"},
    };
}

// ---------------------------------------------------------------------------
// Units, MOUNT, UNMOUNT, and the [[board.drive]] sub-unit table.
// ---------------------------------------------------------------------------
std::vector<UnitDef> Dc4Board::units() const {
    std::vector<UnitDef> u;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        UnitDef x;
        x.name  = "drive" + std::to_string(i);
        x.kind  = UnitKind::Disk;
        x.state = d.img ? d.path : "(empty)";
        if (d.img) {
            x.readOnly       = d.img->readOnly();
            x.readOnlyForced = d.img->readOnlyForced();
        }
        u.push_back(std::move(x));
    }
    return u;
}

static int driveIndex(const std::string& unit, int count) {
    if (unit.rfind("drive", 0) != 0) return -1;
    const std::string n = unit.substr(5);
    if (n.empty()) return -1;
    for (char ch : n)
        if (ch < '0' || ch > '9') return -1;
    int i = std::stoi(n);
    return (i >= 0 && i < count) ? i : -1;
}

static const FlexFormat* findFormat(const std::string& name) {
    for (const auto& f : flexFormats())
        if (name == f.name) return &f;
    return nullptr;
}

// Probe the image's size against the FLEX format table (or a forced `media`). Every FLEX
// size here is distinct, so an unforced probe is unambiguous. A blank/short image with a
// forced `media` is laid out but left unformatted for a guest FORMAT; anything else that
// does not match is an error, with the leftover disk untouched.
static const FlexFormat* probe(uint64_t got, const std::string& forced, std::string& err) {
    if (!forced.empty()) {
        const FlexFormat* f = findFormat(forced);
        if (!f) { err = "unknown media `" + forced + "`"; return nullptr; }  // schema-validated
        if (sizeMatches(got, f->bytes)) return f;
        if (got < f->bytes)             return f;  // blank / short -> format it
        err = std::to_string(got) + " bytes is too large for media=" + forced + " (" +
              std::to_string(f->bytes) + ").";
        return nullptr;
    }

    for (const auto& f : flexFormats())
        if (sizeMatches(got, f.bytes)) return &f;

    std::string sizes;
    for (const auto& f : flexFormats()) {
        if (!sizes.empty()) sizes += ", ";
        sizes += std::string(f.name) + "=" + std::to_string(f.bytes);
    }
    err = std::to_string(got) + " bytes matches no FLEX format (" + sizes +
          "). Set `media` to force one.";
    return nullptr;
}

bool Dc4Board::mount(const std::string& unit, const std::string& path, bool ro, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) {
        err = "no unit `" + unit + "` on " + id + " (it has drive0.." +
              std::to_string(drives_ - 1) + ")";
        return false;
    }

    // WHERE WE LOOK is resolvePath(); WHAT WE REMEMBER is `path`, as written (core/board.h),
    // so SHOW and CONFIG SAVE round-trip and the file still loads from its own directory.
    auto media = openMedia(resolvePath(path), ro, err);
    if (!media) { err += pathNote(path); return false; }

    auto img = std::make_unique<DiskImage>(std::move(media));
    const FlexFormat* fmt = probe(img->size(), drive_[(size_t)i].forced, err);
    if (!fmt) return false;  // a failed probe leaves the old disk in place
    const bool full = sizeMatches(img->size(), fmt->bytes);

    img->init(fmt->tracks, fmt->heads, fmt->interleaved);
    if (full) {
        // Side 0: sectors 1..10. Side 1 (DS): sectors 11..20 (FLEX's cylinder numbering).
        img->initFormat(0, fmt->tracks - 1, 0, 0, fmt->density, fmt->sectors,
                        fmt->sectorSize, /*startSector=*/1);
        if (fmt->heads > 1)
            img->initFormat(0, fmt->tracks - 1, 1, 1, fmt->density, fmt->sectors,
                            fmt->sectorSize, /*startSector=*/fmt->sectors + 1);

        // CP/68's track 0 is numbered 0,1,2,4..18 -- no sector 3, and the first sector is
        // zero (BOOT.ASM / PC2FLOP.ASM: track 0 sectors 1,2,3 renumbered to 0,1,2). The
        // rest of the disk is the plain 1..18 laid down above. Restamp track 0 with the
        // explicit ID list so the file's slots (0,128,256,384,...) carry those numbers.
        if (fmt->track0Zero) {
            TrackFormat t0{fmt->density, fmt->sectors, fmt->sectorSize, 0, {}};
            for (int k = 0; k < fmt->sectors; ++k) t0.sectorIds.push_back(k <= 2 ? k : k + 1);
            img->setTrackFormat(0, 0, t0);
        }
    }
    img->setExtendsOnWrite(true);  // a blank/short image formats as the guest streams tracks

    const bool forcedRo = img->readOnlyForced();

    Drive& d = drive_[(size_t)i];
    d.img    = std::move(img);
    d.path   = path;
    d.drv.mount(d.img.get(), ro);
    d.drv.setHeadTrack(0);
    d.drv.setFormatting(true);       // FLEX FORMAT (NEWDISK.ASM) writes tracks via Write Track
    d.drv.setRevsPerSecond(5);       // 5.25" mini spins at 300 RPM = 5 rev/s
    // Byte timing follows the recorded density (single source of truth for a polled transfer).
    chip_->dataRateBits = (fmt->density == Density::DD) ? 250000 : 125000;
    if (sel_ == i) selectFromLatch();  // re-point the chip at the new medium

    if (forcedRo) {
        char m[192];
        std::snprintf(m, sizeof m,
                      "drive%d mounted WRITE-PROTECTED -- the host will not let us write %s",
                      i, path.c_str());
        log_.push_back(m);
    }
    return true;
}

bool Dc4Board::unmount(const std::string& unit, std::string& err) {
    int i = driveIndex(unit, drives_);
    if (i < 0) { err = "no unit `" + unit + "` on " + id; return false; }

    Drive& d = drive_[(size_t)i];
    if (!d.img) { err = id + ":" + unit + " is empty"; return false; }

    d.img->sync();
    d.img.reset();
    d.path.clear();
    d.drv.eject();
    if (sel_ == i) selectFromLatch();  // the drive is empty now: NOT READY
    return true;
}

std::vector<Property> Dc4Board::subUnitProperties(const std::string& table) const {
    if (table != "drive") return {};
    std::vector<Property> p;
    {
        Property x;
        x.name  = "unit";
        x.help  = "Which drive (0..3)";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 0;
        x.max   = drives_ - 1;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "mount";
        x.help = "The disk image to put in it. Relative to THIS FILE.";
        x.kind = Kind::Str;
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "readonly";
        x.help    = "Write-protect the disk. The drive senses it, so the guest is never told";
        x.kind    = Kind::Bool;
        x.aliases = {"writeprotect"};
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name = "media";
        x.help = "Force the format instead of probing the image's size";
        x.kind = Kind::Enum;
        for (const auto& f : flexFormats()) x.choices.push_back(f.name);
        p.push_back(std::move(x));
    }
    return p;
}

bool Dc4Board::addSubUnit(const std::string& table, const KeyValues& kv, std::string& err) {
    if (table != "drive") {
        err = type() + " has no [[board." + table + "]] table";
        return false;
    }

    int         unit = -1;
    std::string path, media;
    bool        ro = false;

    // loadSubUnit() has already refused an undeclared key, a `media` not in flexFormats(), and
    // a `unit` outside 0..drives-1, and VALIDATED every value against the schema. It hands us the
    // reader's original text, though -- so `readonly` must be parsed the SAME way the validator
    // accepted it (parseValue, case-insensitively).
    for (const auto& [k, v] : kv) {
        if (k == "unit") unit = std::stoi(v);
        else if (k == "mount") path = v;
        else if (k == "readonly") {
            Value bv;
            std::string e;
            if (parseValue(v, Kind::Bool, bv, e)) ro = bv.b();
        }
        else if (k == "media") media = v;
    }
    if (unit < 0) {
        err = "[[board.drive]] needs a `unit`";
        return false;
    }
    if (unit >= drives_) {
        err = "[[board.drive]] unit " + std::to_string(unit) + " but the card has " +
              std::to_string(drives_) + " drives";
        return false;
    }

    drive_[(size_t)unit].forced = media;
    if (path.empty()) return true;
    return mount("drive" + std::to_string(unit), path, ro, err);
}

std::vector<Board::SubUnit> Dc4Board::subUnits() const {
    std::vector<SubUnit> out;
    for (int i = 0; i < drives_; ++i) {
        const Drive& d = drive_[(size_t)i];
        if (!d.img && d.forced.empty()) continue;  // an empty, unforced drive says nothing

        SubUnit su;
        su.table = "drive";
        su.fields.push_back({"unit", std::to_string(i), false});  // decimal: a count
        if (!d.forced.empty()) su.fields.push_back({"media", d.forced, true});
        if (d.img) {
            su.fields.push_back({"mount", d.path, true});
            if (d.img->readOnly()) su.fields.push_back({"readonly", "true", false});
        }
        out.push_back(std::move(su));
    }
    return out;
}

// ---------------------------------------------------------------------------
// SNAPSHOT / RESTORE (DESIGN.md 13). The controller state that is NOT host-backed: the
// chip's whole register file and any command in flight, the drive-select latch, and each
// drive's head position. The disk IMAGES are host-backed and do not travel; the chip
// straps (clock, data rate, wait-sync) are the card's and are re-applied on construction.
// ---------------------------------------------------------------------------
void Dc4Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    w.u8(latch_);
    w.u32((uint32_t)drive_.size());
    for (const Drive& d : drive_) w.u32((uint32_t)d.drv.headTrackRaw());
    chip_->serialize(w);
}

void Dc4Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    latch_ = r.u8();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        int head = (int)r.u32();
        if (i < drive_.size()) drive_[i].drv.setHeadTrack(head);
    }
    chip_->deserialize(r);
    selectFromLatch();  // re-attach the selected drive and re-apply side
    refresh();          // re-arm the wake from the restored chip state
}

} // namespace swtpc
