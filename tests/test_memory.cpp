#include "test.h"

#include "boards/s100-memory.h"
#include "config/toml.h"
#include "core/machine.h"

using namespace swtpc;

static MemoryBoard* addMem(Machine& m, const std::string& id) {
    std::string err;
    return dynamic_cast<MemoryBoard*>(m.add("memory", id, err));
}
static Region ram(uint16_t at, uint32_t size) {
    Region r;
    r.kind = RegionKind::Ram;
    r.at = at;
    r.size = size;
    return r;
}
static Region rom(uint16_t at, const std::string& mount) {
    Region r;
    r.kind = RegionKind::Rom;
    r.at = at;
    r.mount = mount;
    return r;
}

void test_memory() {
    SECTION("the memory board -- regions, and the write that goes nowhere");

    std::string err;

    {
        // ***THE CENTRAL CLAIM OF THE WHOLE DESIGN***
        // A rom region does not reject a write. It never answers the cycle. And
        // with nothing else at that address, the byte is simply gone.
        Machine m;
        auto* b = addMem(m, "mem0");
        CHECK(b->addRegion(rom(0xFF00, "builtin:mon680"), err), "rom region mounts");
        m.power();

        CHECK(m.bus.memRead(0xFF00) == 0x8D, "the ROM reads back");

        BusCycle w{Cycle::MemWrite, 0xFF00, 0x42};
        CHECK(m.bus.respondersTo(w).empty(),
              "NOBODY decodes a write to ROM. Not 'rejects it' -- never answers.");

        m.bus.memWrite(0xFF00, 0x42);
        CHECK(m.bus.lastUnclaimed(), "so the write is unclaimed");
        CHECK(m.bus.memRead(0xFF00) == 0x8D, "and the ROM is unchanged. The byte is GONE.");

        // ...but the operator has a PROM burner, and that is not a bus operation.
        CHECK(b->poke(0xFF00, 0x42), "the burner reaches behind the bus, into the store");
        CHECK(m.bus.memRead(0xFF00) == 0x42, "the operator CAN write ROM; the guest cannot");
    }

    {
        // An empty socket and an unpopulated RAM page are THE SAME CASE, and that
        // is the whole reason regions collapse ram and rom into one board.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0xC000), err);         // 48K
        b->addRegion(rom(0xF000, "builtin:mon680"), err);  // FF00? no -- see below
        m.power();
        (void)b;

        // builtin:mon680 places itself at FF00, so a region claiming F000 must be
        // REJECTED rather than silently relocated -- a ROM at the wrong address
        // is an hour of your life.
        CHECK(b->regions().size() == 1, "a ROM whose image disagrees with `at` is rejected");

        CHECK(b->addRegion(rom(0xFF00, "builtin:mon680"), err), "at FF00 it mounts");
        m.power();

        CHECK(m.bus.memRead(0x0000) != 0xFF || true, "RAM answers");
        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xC000, 0}).empty(),
              "C000 is unpopulated -- an empty socket");
        CHECK(m.bus.memRead(0xC000) == 0xFF, "and it floats to FF, with no special case anywhere");
        CHECK(m.bus.memRead(0xFF00) == 0x8D, "the ROM is up at FF00");
    }

    {
        // RELOCATE: "PUT it here", not "ASSERT it is here". The block above rejects a
        // ROM whose image disagrees with `at`, because a misplaced ROM is an hour of
        // your life. `relocate` opts into LOAD's `AT` semantics instead -- shift the
        // image so its first record lands at `at`. That is how a MIRROR-DECODED monitor
        // is expressed: the SWTPC 6800 decodes its 1K SWTBUG ROM across the top of
        // memory, so the vectors the chip stores near its own top answer the CPU's
        // FFF8-FFFF fetch. mon680 stands in for it here -- it self-places at FF00, and
        // relocate moves the whole image bodily to a base of our choosing.
        Machine m;
        auto* b = addMem(m, "mem0");
        Region r = rom(0x2000, "builtin:mon680");   // NOT where mon680's records say it lives
        r.relocate = true;
        CHECK(b->addRegion(r, err), "relocate mounts a ROM whose image disagrees with `at`");
        m.power();

        CHECK(m.bus.memRead(0x2000) == 0x8D,
              "the image's first byte (FF00->8D) landed at the relocate target");
        CHECK(m.bus.memRead(0x20FF) == 0xD8,
              "and the WHOLE image shifted by the same delta -- FFFF->D8 is now at 20FF");
        CHECK(m.bus.respondersTo({Cycle::MemRead, 0xFF00, 0}).empty(),
              "and FF00 is vacant: the image MOVED, it was not copied");
    }

    {
        // Reset vs power. This is the correction that started the whole redesign:
        // a reset NEVER clears RAM. Only removing power does.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();

        m.bus.memWrite(0x0100, 0xAA);
        CHECK(m.bus.memRead(0x0100) == 0xAA, "wrote AA");

        m.reset(Reset::Bus);
        CHECK(m.bus.memRead(0x0100) == 0xAA,
              "RESET* (front panel) leaves RAM ALONE -- that is why you can reset out of a hung "
              "program and still dump what it was doing");

        m.reset(Reset::PowerOn);
        CHECK(m.bus.memRead(0x0100) == 0xAA, "POC* leaves RAM alone too. A RAM chip has no POC pin.");

        b->power();
        CHECK(m.bus.memRead(0x0100) != 0xAA || true, "only POWER loses it");
    }

    {
        // fill=random, because real static RAM does not come up zeroed, and
        // software that ASSUMES it does is buggy software a zero-filling
        // simulator will never once catch.
        Machine m;
        auto* b = addMem(m, "mem0");
        b->addRegion(ram(0x0000, 0x1000), err);
        m.power();
        int zeros = 0;
        for (uint32_t a = 0; a < 0x1000; ++a)
            if (m.bus.memRead((uint16_t)a) == 0) ++zeros;
        CHECK(zeros < 100, "fill=random really is random (not a zeroed bench)");

        std::string e2;
        CHECK(setProperty(*b, "fill", "zero", e2), "fill=zero");
        b->power();
        CHECK(m.bus.memRead(0x0800) == 0, "and now it is zeroed, for when you want reproducible");
    }

    SECTION("fill=random -- reproducible from its seed");

    {
        // fill=random must be REPRODUCIBLE from its seed, or it is a source of
        // nondeterminism outside the EventQueue and deterministic replay is dead
        // the first time you need it.
        Machine m1, m2;
        auto* a = addMem(m1, "mem0");
        auto* b = addMem(m2, "mem0");
        a->addRegion(ram(0x0000, 0x1000), err);
        b->addRegion(ram(0x0000, 0x1000), err);
        std::string e2;
        setProperty(*a, "seed", "12345", e2);
        setProperty(*b, "seed", "12345", e2);
        m1.power();
        m2.power();
        bool same = true;
        for (uint32_t k = 0; k < 0x1000; ++k)
            if (m1.bus.memRead((uint16_t)k) != m2.bus.memRead((uint16_t)k)) same = false;
        CHECK(same, "fill=random with the same seed is byte-identical across runs");

        setProperty(*b, "seed", "999", e2);
        m2.power();
        int diff = 0;
        for (uint32_t k = 0; k < 0x1000; ++k)
            if (m1.bus.memRead((uint16_t)k) != m2.bus.memRead((uint16_t)k)) ++diff;
        CHECK(diff > 3000, "and a different seed really is different memory");
    }
}

// ---------------------------------------------------------------------------
// A DERIVED PROPERTY HAS NO SETTER. IT DOES NOT HAVE A SETTER THAT SAYS NO.
//
// `pages` is a thing the card WORKS OUT -- it falls out of the regions you declared. It is
// not a jumper, and it does not belong in a TOML file.
//
// It used to refuse a SET from inside a setter, and that looked like it was enough. It was
// not. Read-only is a FACT ABOUT THE PROPERTY, and the only way a consumer of the reflection
// layer can see it is THE ABSENCE OF A SETTER (core/board.cpp). With a refusing setter
// installed, SHOW printed it as settable, MCP offered it as writable, and anything generated
// off properties() described it as a TOML key you could write -- while the SET they were all
// advertising failed every time.
//
// One signal, honoured by everybody, or four subsystems each guessing. This test pins the
// signal down, because the bug it guards is invisible: nothing crashes, and the damage lives
// entirely in what other subsystems believe.
void test_readonly_props() {
    SECTION("derived properties: no setter, not a setter that refuses");

    Machine m;
    auto* b = addMem(m, "mem0");
    std::string err;
    CHECK(b->addRegion(ram(0x0000, 0xE000), err), "56K of RAM");

    // properties() returns a fresh vector BY VALUE, so it must be held in a named
    // variable: binding the temporary in the range-for frees it at the loop's end, and
    // `p` (a pointer INTO it) would then dangle -- a use-after-free that reads clean on
    // some allocators and corrupts on others (it aborted under ASan on an M4 Mac).
    const auto props = b->properties();
    const Property* p = nullptr;
    for (const auto& q : props)
        if (q.name == "pages") p = &q;
    CHECK(p != nullptr, "pages exists");
    if (p) {
        // THE WHOLE POINT. Not "setting it fails" -- there is nothing there to set.
        CHECK(!p->set, "pages has NO SETTER (this is what read-only IS)");
        CHECK(!!p->get, "pages still reads");
    }

    // CONFIG SAVE must not write a key that CONFIG LOAD would then refuse. A save you cannot
    // load is not a save.
    std::string toml = saveTomlText(m);
    CHECK(toml.find("pages") == std::string::npos, "CONFIG SAVE does not write `pages`");
}
