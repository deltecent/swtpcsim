// ONE CARD, FOUR KINDS OF THING (DESIGN.md 3, 9).
//
// Patrick asked: can a single board contain a block device, a character device,
// RAM and ROM? This file is the answer, and it is a card, not an argument.
//
// `Combo` below is a fixture, NOT a shipped board -- it is deliberately in tests/
// rather than src/boards/, because we do not ship a disk controller that cannot
// read a disk, and we do not model a real card without its manual (DESIGN.md 0.1).
// What it proves is that the BUS AND BOARD MODEL has no hidden assumption that a
// card is one kind of thing:
//
//   - it answers MEMORY cycles (a RAM region and a ROM region), and
//   - it answers an I/O PORT in the same decodes(), and
//   - it carries a disk drive AND a serial port as named, typed units.
//
// Such cards were completely ordinary: a floppy controller with its own boot PROM,
// some scratch RAM and a serial port on the same board is a 1977 product, not an
// exotic one. The Tarbell we already model is two of the four (PROM + FDC).
//
// THE FAILURE THIS CATCHES is the one the old integer unit scheme guaranteed.
// `MOUNT combo:2` on a serial port could only ever FAIL -- the board had nothing
// left to tell it apart from drive 2 -- so the user got a shrug. With named, typed
// units the same mistake gets a sentence that says what to do instead.

#include "core/board.h"
#include "core/bus.h"
#include "test.h"

using namespace swtpc;

namespace {

// A floppy controller with a boot PROM, a page of scratch RAM, and a serial port.
class Combo : public Board {
public:
    std::string type() const override { return "combo"; }

    static constexpr uint16_t kRom = 0xF000;  // 256 bytes of boot PROM
    static constexpr uint16_t kRam = 0xF100;  // 256 bytes of scratch RAM
    static constexpr uint16_t kReg = 0xF200;  // the FDC's command/status register (memory-mapped)

    // ONE decodes() ANSWERS EVERY REGION THE CARD OWNS. On the memory-mapped 6800 a
    // card's PROM, its scratch RAM and its device registers are all just addresses; it
    // says yes to the cycles it drives and the bus asks nothing else.
    bool decodes(const BusCycle& c) const override {
        switch (c.type) {
        case Cycle::MemRead:
            return (c.addr & 0xFF00) == kRom || (c.addr & 0xFF00) == kRam ||
                   (c.addr & 0xFF00) == kReg;
        case Cycle::MemWrite:
            return (c.addr & 0xFF00) == kRam || (c.addr & 0xFF00) == kReg;  // the PROM ignores writes
        }
        return false;
    }

    uint8_t read(const BusCycle& c) override {
        if ((c.addr & 0xFF00) == kReg) return status_;
        if ((c.addr & 0xFF00) == kRom) return rom_[c.addr & 0xFF];
        return ram_[c.addr & 0xFF];
    }
    void write(const BusCycle& c) override {
        if ((c.addr & 0xFF00) == kReg) {
            cmd_ = c.data;
            return;
        }
        if ((c.addr & 0xFF00) == kRam) ram_[c.addr & 0xFF] = c.data;
    }

    std::vector<Property> properties() override { return {}; }

    // FOUR UNITS, THREE KINDS, ONE CARD.
    std::vector<UnitDef> units() const override {
        return {
            {"drive0", UnitKind::Disk, disk0_.empty() ? "(empty)" : disk0_},
            {"drive1", UnitKind::Disk, disk1_.empty() ? "(empty)" : disk1_},
            {"rom0", UnitKind::Rom, "boot.bin"},
            {"tty", UnitKind::Serial, tty_.empty() ? "(not connected)" : tty_},
        };
    }

    bool mount(const std::string& u, const std::string& path, bool, std::string& e) override {
        if (u == "drive0") { disk0_ = path; return true; }
        if (u == "drive1") { disk1_ = path; return true; }
        if (u == "rom0") return true;
        e = "no such unit";
        return false;
    }
    bool connect(const std::string& u, const std::string& ep, std::string& e) override {
        if (u == "tty") { tty_ = ep; return true; }
        e = "no such unit";
        return false;
    }

    uint8_t status_ = 0x80, cmd_ = 0;
    uint8_t rom_[256] = {0xC3}, ram_[256] = {0};
    std::string disk0_, disk1_, tty_;
};

} // namespace

void test_units() {
    SECTION("one card: a block device, a character device, RAM, ROM -- and a register");

    Bus bus;
    Combo c;
    c.id = "combo";
    bus.attach(&c);

    // ---- PROM, RAM and a device register, all from the same card, one decodes() ----
    CHECK(bus.memRead(0xF000) == 0xC3, "the boot PROM answers a memory read");
    bus.memWrite(0xF100, 0x42);
    CHECK(bus.memRead(0xF100) == 0x42, "the scratch RAM stores a write");
    CHECK(bus.memRead(0xF200) == 0x80, "and the FDC's status register answers a read -- same board");
    bus.memWrite(0xF200, 0x0B);
    CHECK(c.cmd_ == 0x0B, "and its command register takes a write");

    // A ROM region does not decode a write. It does not "reject" it -- it never
    // answers, so nobody drives the cycle and the byte is simply gone.
    bus.memWrite(0xF000, 0x99);
    // Ask THIS cycle's question before running another one -- lastUnclaimed() is
    // about the last cycle, and a read here would answer for the read.
    CHECK(bus.lastUnclaimed(), "NOBODY decoded the write -- the board never answered");
    CHECK(bus.memRead(0xF000) == 0xC3, "so the write did not stick, and the PROM is intact");

    // ---- four units, three kinds, all on the one card ----
    auto us = c.units();
    CHECK(us.size() == 4, "the card declares four units");

    UnitDef u;
    CHECK(c.findUnit("drive0", u) && u.kind == UnitKind::Disk, "drive0 is a disk");
    CHECK(c.findUnit("rom0", u) && u.kind == UnitKind::Rom, "rom0 is a socket");
    CHECK(c.findUnit("tty", u) && u.kind == UnitKind::Serial, "tty is a serial port");
    CHECK(c.findUnit("TTY", u), "and a unit name is not case-sensitive");
    CHECK(!c.findUnit("drive9", u), "a unit the card does not have is simply not there");

    // ---- the kind is CHECKED, which is the entire point of naming them ----
    // Under the old integer scheme `MOUNT combo:3` on the serial port could only
    // fail. It could never say WHY, because nothing distinguished unit 3 the port
    // from unit 3 the drive. Now the mistake has a cause, and the cause is legible.
    CHECK(c.findUnit("tty", u) && !isMountable(u.kind),
          "you cannot MOUNT a disk image onto a serial port");
    CHECK(c.findUnit("drive0", u) && isMountable(u.kind), "but a drive takes one");

    std::string err;
    CHECK(c.mount("drive0", "disks/cpm.dsk", false, err), "MOUNT combo:drive0");
    CHECK(c.connect("tty", "tcp:2323", err), "CONNECT combo:tty");
    CHECK(c.units()[0].state == "disks/cpm.dsk", "and the card reports what is in drive0");
    CHECK(c.units()[3].state == "tcp:2323", "and what tty is connected to");
    CHECK(c.units()[1].state == "(empty)", "drive1 is still empty, and says so");
}
