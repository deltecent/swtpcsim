#include "test.h"

#include "boards/s100-memory.h"
#include "boards/swtpc-dc4.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "host/media.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace swtpc;

namespace {

// THE FLEX 40-track single-sided format, the size the SWTPC 6800 shipped: 40 x 1 x 10 x 256
// = 102,400 bytes. A raw .DSK is exactly the sector data end to end, so a sector's bytes on
// the disk are the image bytes at (track*10 + sector-1) * 256 -- which is what lets these
// tests read a sector through the whole board and check it against the buffer with no
// intermediate layer to trust. (See src/host/disk.h: startSector = 1, non-interleaved.)
constexpr uint64_t kFlex40 = 40ull * 1 * 10 * 256;
constexpr int      kSectorsPerTrack = 10;

// CP/68: SSSD 35 x 1 x 18 x 128 = 80,640. Its track 0 is numbered 0,1,2,4..18 (no sector 3,
// first sector ZERO); every other track is the plain 1..18. mount() stamps that ID map on
// track 0, so a sector's file offset is its SLOT in the image order 0,1,2,4..18 -- the ID 4
// lands in slot 3 (128*3 = 384), because there is no slot for the missing 3.
constexpr uint64_t kCp68 = 35ull * 1 * 18 * 128;

uint64_t sectorOffset(int track, int sector) {
    return (uint64_t)(track * kSectorsPerTrack + (sector - 1)) * 256;
}

// A deterministic, position-dependent fill so a byte read back proves it came from the
// offset we asked for and not a neighbouring sector.
uint8_t pat(uint64_t i) { return (uint8_t)(i * 31u + 7u); }

// The MemoryMedia the resolver hands out (test_media.cpp / test_680kcacr.cpp pattern) --
// captured so a write test can assert on what actually reached the "disk".
MemoryMedia* g_media = nullptr;

// Install a resolver that returns a MemoryMedia of `bytes`, filled per pat(). Read-write
// unless `ro`. No filesystem is touched -- a board test never should (host/media.h).
std::vector<uint8_t> withDisk(uint64_t size, bool ro = false) {
    std::vector<uint8_t> image((size_t)size);
    for (size_t i = 0; i < image.size(); ++i) image[i] = pat(i);
    setMediaResolver([image, ro](const std::string& path, bool wantRo, std::string&) {
        auto m  = std::make_unique<MemoryMedia>(path, image, ro || wantRo);
        g_media = m.get();
        return m;
    });
    return image;
}

void restoreResolver() {
    setMediaResolver(openHostFile);
    g_media = nullptr;
}

// A machine with just the DC-4 on it and a scrap of RAM low down (clear of the SS-30 I/O
// window), driven through the real bus so a register access runs the same decode/refresh an
// operator's poke would. The default DC-4 base leaves the WD179x at $8018 and the latch at
// $8014.
struct Rig {
    Machine    m;
    Dc4Board*  dc4 = nullptr;

    Rig() {
        std::string err;
        m.bus.setVerify(true);

        MemoryBoard* mem = dynamic_cast<MemoryBoard*>(m.add("memory", "mem0", err));
        Region       ram;
        ram.kind = RegionKind::Ram;
        ram.at   = 0x0000;
        ram.size = 0x1000;  // 4K -- well clear of the $8000 I/O window
        mem->addRegion(ram, err);

        dc4 = dynamic_cast<Dc4Board*>(m.add("dc4", "dc40", err));
        m.add("6800", "cpu0", err);
        m.power();
    }

    // Register addresses at the default base ($8018 block, latch $8014).
    static constexpr uint16_t kLatch  = 0x8014;
    static constexpr uint16_t kCmd    = 0x8018;  // command (w) / status (r)
    static constexpr uint16_t kTrack  = 0x8019;
    static constexpr uint16_t kSector = 0x801A;
    static constexpr uint16_t kData   = 0x801B;

    uint8_t status() { return m.bus.memRead(kCmd); }

    // Run a Type I (seek/restore) to quiescence. In full-speed (wait-synced) mode the
    // command resolves the instant it is issued, but polling status the way a driver does is
    // the honest exercise and costs nothing.
    void waitIdle() {
        for (int g = 0; g < 100000; ++g)
            if ((status() & 0x01) == 0) return;  // S0 BUSY clear
    }

    void selectDrive(int drv, int side) {
        m.bus.memWrite(kLatch, (uint8_t)((drv & 0x03) | (side ? 0x40 : 0x00)));
    }

    void seek(int track) {
        m.bus.memWrite(kData, (uint8_t)track);
        m.bus.memWrite(kCmd, 0x10);  // Seek, no verify
        waitIdle();
    }

    // A WD179x Read Sector driven the way the FLEX driver does: poll status, take a byte on
    // every DRQ, stop when BUSY drops. `sectorReg` is written verbatim so a test can pass 0
    // and prove the board's sector-0 -> 1 coercion.
    std::vector<uint8_t> readSector(int sectorReg) {
        m.bus.memWrite(kSector, (uint8_t)sectorReg);
        m.bus.memWrite(kCmd, 0x88);  // Read Sector, m=0
        std::vector<uint8_t> out;
        for (int g = 0; g < 100000; ++g) {
            uint8_t st = status();
            if (st & 0x02) out.push_back(m.bus.memRead(kData));  // S1 DRQ
            if ((st & 0x01) == 0) break;                         // S0 BUSY clear
        }
        return out;
    }

    // Symmetric Write Sector.
    void writeSector(int sectorReg, const std::vector<uint8_t>& bytes) {
        m.bus.memWrite(kSector, (uint8_t)sectorReg);
        m.bus.memWrite(kCmd, 0xA8);  // Write Sector, m=0, a1a0=00 (FB)
        size_t i = 0;
        for (int g = 0; g < 100000; ++g) {
            uint8_t st = status();
            if ((st & 0x02) && i < bytes.size()) m.bus.memWrite(kData, bytes[i++]);
            if ((st & 0x01) == 0) break;
        }
    }
};

bool decodesMem(Dc4Board& b, uint16_t addr, Cycle type = Cycle::MemRead) {
    BusCycle c;
    c.type = type;
    c.addr = addr;
    return b.decodes(c);
}

std::string prop(Dc4Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().s();
    return "<none>";
}

long long propInt(Dc4Board& b, const std::string& name) {
    for (Property& p : b.properties())
        if (p.name == name) return p.get().i();
    return -1;
}

} // namespace

void test_swtpc_dc4() {
    SECTION("dc4 -- the decode is five fixed addresses across two SS-30 ports");
    {
        Rig g;
        CHECK(decodesMem(*g.dc4, 0x8014), "$8014 (drive-select latch) is ours");
        CHECK(decodesMem(*g.dc4, 0x8018), "$8018 (WD179x command/status) is ours");
        CHECK(decodesMem(*g.dc4, 0x8019), "$8019 (track) is ours");
        CHECK(decodesMem(*g.dc4, 0x801A), "$801A (sector) is ours");
        CHECK(decodesMem(*g.dc4, 0x801B), "$801B (data) is ours");

        CHECK(!decodesMem(*g.dc4, 0x8015), "$8015 is NOT ours -- the latch is one address");
        CHECK(!decodesMem(*g.dc4, 0x8016), "...nor the rest of the latch's port");
        CHECK(!decodesMem(*g.dc4, 0x8017), "...nor $8017, the byte just below the WD block");
        CHECK(!decodesMem(*g.dc4, 0x801C), "...nor $801C, just above it");

        // The card is memory-mapped: it answers reads AND writes at the same addresses (the
        // 6800 has no I/O space), unlike the 680io strap buffer that only decodes reads.
        CHECK(decodesMem(*g.dc4, 0x8018, Cycle::MemWrite), "$8018 decodes a write (command)");
        CHECK(decodesMem(*g.dc4, 0x8014, Cycle::MemWrite), "$8014 decodes a write (the latch)");
    }

    SECTION("dc4 -- the drive-select latch is write-only");
    {
        Rig g;
        g.m.bus.memWrite(Rig::kLatch, 0x03);
        CHECK(g.m.bus.memRead(Rig::kLatch) == 0xFF, "a read of the latch floats the bus (0xFF)");
    }

    SECTION("dc4 -- properties: speed, base, drives");
    {
        Rig         g;
        std::string err;

        CHECK(prop(*g.dc4, "speed") == "full", "the default timing is full speed (wait-synced)");
        CHECK(setProperty(*g.dc4, "speed", "real", err), "speed accepts `real`");
        CHECK(prop(*g.dc4, "speed") == "real", "...and reports it back");
        CHECK(setProperty(*g.dc4, "speed", "full", err), "speed accepts `full` again");

        CHECK(propInt(*g.dc4, "base") == 0x8018, "base defaults to $8018 (SS-30 slot 6)");
        CHECK(!setProperty(*g.dc4, "base", "8002", err),
              "base below $8008 is rejected -- the latch would collide with the console slot");
        CHECK(!setProperty(*g.dc4, "base", "8019", err),
              "a base that is not a multiple of 4 is not an SS-30 slot");
        CHECK(setProperty(*g.dc4, "base", "8010", err), "$8010 (slot 4) is a legal base");
        CHECK(setProperty(*g.dc4, "base", "E018", err),
              "$E018 is slot 6 of the 6809 window -- S-BUG's Comreg, latch at $E014");
        CHECK(!setProperty(*g.dc4, "base", "E004", err),
              "in the 6809 window too, the latch may not land on the console slot");

        CHECK(propInt(*g.dc4, "drives") == 4, "four drives by default");
        CHECK(!setProperty(*g.dc4, "drives", "0", err), "zero drives is rejected");
        CHECK(!setProperty(*g.dc4, "drives", "5", err), "five drives is rejected (2-bit select)");
        CHECK(setProperty(*g.dc4, "drives", "2", err), "two drives is fine");
    }

    SECTION("dc4 -- mount probes the image size against the FLEX format table");
    {
        withDisk(kFlex40);
        Rig         g;
        std::string err;
        CHECK(g.dc4->mount("drive0", "flex40.dsk", false, err),
              "a 102,400-byte image mounts as flex40");

        withDisk(kFlex40 + 10000);  // no FLEX format is this size
        Rig         g2;
        std::string err2;
        CHECK(!g2.dc4->mount("drive0", "junk.dsk", false, err2),
              "an image matching no FLEX format is refused");
        CHECK(!err2.empty(), "...with a message saying so");
        restoreResolver();
    }

    SECTION("dc4 -- read a sector through the whole board (latch, seek, WD179x)");
    {
        std::vector<uint8_t> image = withDisk(kFlex40);
        Rig g;
        std::string err;
        CHECK(g.dc4->mount("drive0", "flex40.dsk", false, err), "mount flex40 on drive0");

        g.selectDrive(0, 0);
        g.m.bus.memWrite(Rig::kCmd, 0x03);  // Restore to track 0
        g.waitIdle();
        g.seek(1);
        CHECK(g.m.bus.memRead(Rig::kTrack) == 1, "the track register followed the seek to track 1");

        std::vector<uint8_t> got = g.readSector(1);
        CHECK(got.size() == 256, "a read delivers exactly one 256-byte sector");
        bool match = got.size() == 256;
        for (size_t k = 0; k < got.size(); ++k)
            match = match && got[k] == pat(sectorOffset(1, 1) + k);
        CHECK(match, "...and it is track 1 sector 1's bytes, from the right offset");
        restoreResolver();
    }

    SECTION("dc4 -- sector register 0 is coerced to sector 1 (SWTBUG boot reads sector 0)");
    {
        withDisk(kFlex40);
        Rig g;
        std::string err;
        g.dc4->mount("drive0", "flex40.dsk", false, err);

        g.selectDrive(0, 0);
        g.m.bus.memWrite(Rig::kCmd, 0x03);
        g.waitIdle();
        g.seek(3);

        std::vector<uint8_t> asZero = g.readSector(0);  // the boot ROM writes SECREG = 0
        CHECK(asZero.size() == 256, "reading `sector 0` still returns a sector");
        bool isSectorOne = asZero.size() == 256;
        for (size_t k = 0; k < asZero.size(); ++k)
            isSectorOne = isSectorOne && asZero[k] == pat(sectorOffset(3, 1) + k);
        CHECK(isSectorOne, "...and it is track 3 SECTOR 1, not some phantom sector 0");
        restoreResolver();
    }

    SECTION("dc4 -- CP/68 track 0 is an ID map (0,1,2,4..18), not sector arithmetic");
    {
        withDisk(kCp68);
        Rig g;
        std::string err;
        CHECK(g.dc4->mount("drive0", "cp68.dsk", false, err),
              "an 80,640-byte image mounts as cp68");

        g.selectDrive(0, 0);
        g.m.bus.memWrite(Rig::kCmd, 0x03);  // Restore to track 0
        g.waitIdle();
        CHECK(g.m.bus.memRead(Rig::kTrack) == 0, "the head is on track 0");

        // Reads a whole 128-byte sector and proves every byte came from `wantOff`.
        auto readsFrom = [&](int sectorReg, uint64_t wantOff) {
            std::vector<uint8_t> got = g.readSector(sectorReg);
            if (got.size() != 128) return false;
            for (size_t k = 0; k < got.size(); ++k)
                if (got[k] != pat(wantOff + k)) return false;
            return true;
        };

        // Sector ID 0 is REAL on CP/68's track 0, so it must NOT be coerced to 1 (as it is on
        // a FLEX track that has no sector 0) -- it is the first slot, file offset 0.
        CHECK(readsFrom(0, 0),
              "sector 0 reads the first slot (offset 0), NOT a coerced sector 1");
        CHECK(readsFrom(2, 256), "sector 2 reads slot 2 (offset 256)");
        // The ID map skips the missing 3: sector 4 is the FOURTH slot, offset 3*128 = 384.
        CHECK(readsFrom(4, 384), "sector 4 reads slot 3 (offset 384), past the absent 3");

        // Sector 3 is not on track 0. A real WD179x finds no matching ID and raises Record
        // Not Found -- it must not silently read a neighbour.
        std::vector<uint8_t> s3 = g.readSector(3);
        CHECK(s3.empty(), "sector 3 delivers no data -- it is not on the track");
        CHECK((g.status() & 0x10) != 0, "...and the WD179x flags Record Not Found (S4)");
        restoreResolver();
    }

    SECTION("dc4 -- write a sector, and it reaches the disk at the right offset");
    {
        withDisk(kFlex40);
        Rig g;
        std::string err;
        g.dc4->mount("drive0", "flex40.dsk", false, err);

        g.selectDrive(0, 0);
        g.m.bus.memWrite(Rig::kCmd, 0x03);
        g.waitIdle();
        g.seek(2);

        std::vector<uint8_t> payload(256);
        for (size_t k = 0; k < payload.size(); ++k) payload[k] = (uint8_t)(0xC0 ^ k);
        g.writeSector(3, payload);

        // The board syncs per sector into the MemoryMedia buffer; check it landed at
        // (track 2, sector 3)'s offset and nowhere else.
        const uint64_t off = sectorOffset(2, 3);
        bool wrote = g_media != nullptr;
        for (size_t k = 0; wrote && k < payload.size(); ++k)
            wrote = wrote && g_media->bytes()[off + k] == payload[k];
        CHECK(wrote, "the written sector's bytes are on the disk at track 2 sector 3");
        CHECK(g_media && g_media->bytes()[off - 1] == pat(off - 1),
              "...and the byte just before it is untouched");
        restoreResolver();
    }

    SECTION("dc4 -- a write-protected mount refuses to change the disk");
    {
        withDisk(kFlex40, /*ro=*/true);
        Rig g;
        std::string err;
        g.dc4->mount("drive0", "flex40.dsk", false, err);

        g.selectDrive(0, 0);
        g.m.bus.memWrite(Rig::kCmd, 0x03);
        g.waitIdle();
        g.seek(2);

        std::vector<uint8_t> payload(256, 0x99);
        g.writeSector(1, payload);
        CHECK((g.status() & 0x40) != 0, "S6 WRITE PROTECT is set on a write to a protected disk");

        const uint64_t off = sectorOffset(2, 1);
        CHECK(g_media && g_media->bytes()[off] == pat(off), "...and the disk is unchanged");
        restoreResolver();
    }

    SECTION("dc4 -- snapshot carries the chip register file and the drive-select latch");
    {
        withDisk(kFlex40);
        Rig g;
        std::string err;
        g.dc4->mount("drive0", "flex40.dsk", false, err);

        g.selectDrive(1, 1);  // drive 1, side 1 -- into the latch
        g.m.bus.memWrite(Rig::kCmd, 0x03);
        g.waitIdle();
        g.seek(7);
        g.m.bus.memWrite(Rig::kSector, 4);

        StateWriter w;
        g.dc4->serialize(w);

        Dc4Board    fresh;
        StateReader rd(w.data());
        fresh.deserialize(rd);
        CHECK(fresh.chip().readTrackReg() == 7, "the track register survives serialize/deserialize");
        CHECK(fresh.chip().readSectorReg() == 4, "...and the sector register with it");
        restoreResolver();
    }
}
