#include "test.h"

#include "../examples/boards/lamp/lamp.h"
#include "core/machine.h"
#include "core/statefile.h"

#include <string>

using namespace swtpc;

// The board the Developer Guide's "Writing a board" chapter builds. This is what keeps that
// chapter honest: every claim it makes about the lamp is asserted here against a REAL bus, so
// the code in the Markdown cannot rot without a test going red.
//
// The lamp is NOT in the shipping registry (that is the tutorial's point -- the reader adds
// it), so we cannot Machine::add("lamp",...). We construct it, attach it to a machine's bus,
// and drive the same cycles a CPU would. Lifecycle (power/reset) we call by hand, because a
// board only on the bus -- not owned by the Machine -- never hears those events; that trap is
// the subject of a callout box in the chapter itself.

namespace {

int lamps(LampBoard& b) {
    for (Property& p : b.properties())
        if (p.name == "lamps") return (int)p.get().i();
    return -1;
}

} // namespace

void test_lamp() {
    SECTION("lamp -- decodes the store at its address, and ONLY the store");
    {
        LampBoard lamp;
        BusCycle  c;

        c.type = Cycle::MemWrite;
        c.addr = 0x8010;
        CHECK(lamp.decodes(c), "a store to 8010 is ours (the lamp latch)");

        c.type = Cycle::MemRead;
        CHECK(!lamp.decodes(c), "a LOAD from 8010 is NOT ours -- the bus routes by direction");

        c.type = Cycle::MemWrite;
        c.addr = 0x8011;
        CHECK(!lamp.decodes(c), "8011 is not ours -- one address, not the whole slot");
        c.addr = 0x800C;
        CHECK(!lamp.decodes(c), "and nothing at another slot");

        lamp.setEnabled(false);
        c.addr = 0x8010;
        CHECK(!lamp.decodes(c), "a board switched off drives nothing");
    }

    SECTION("lamp -- a store latches the byte; a load floats (write-only)");
    {
        Machine   m;
        LampBoard lamp;
        m.bus.setVerify(true);
        m.bus.attach(&lamp);
        lamp.power();

        // A REAL WRITE CYCLE on the bus -- not a method call on the board.
        m.bus.memWrite(0x8010, 0x55);
        CHECK(lamps(lamp) == 0x55, "the store latched the byte, read back through reflection");

        // The board is WRITE-ONLY, and the proof is that the load FLOATS.
        CHECK(m.bus.memRead(0x8010) == 0xFF, "a load from 8010 is not the lamp's -- the bus floats");
    }

    SECTION("lamp -- the bus routes by direction, and it is NOT contention");
    {
        Machine   m;
        LampBoard lamp;
        m.bus.setVerify(true);
        m.bus.attach(&lamp);
        lamp.power();

        BusCycle store; store.type = Cycle::MemWrite; store.addr = 0x8010;
        BusCycle load;  load.type  = Cycle::MemRead;   load.addr  = 0x8010;

        auto onStore = m.bus.respondersTo(store);
        auto onLoad  = m.bus.respondersTo(load);
        CHECK(onStore.size() == 1, "a store at 8010: the lamp, and only the lamp");
        CHECK(onStore.size() == 1 && onStore[0] == &lamp, "and it is the lamp itself");
        CHECK(onLoad.empty(), "a load at 8010: nobody -- the board never claimed the read");
        CHECK(m.bus.drain().empty(), "...and the bus logs NOTHING. It is not contention.");
    }

    SECTION("lamp -- reset and power turn the lamps off");
    {
        Machine   m;
        LampBoard lamp;
        m.bus.attach(&lamp);

        m.bus.memWrite(0x8010, 0xFF);
        CHECK(lamps(lamp) == 0xFF, "all eight lit");
        lamp.reset(Reset::Bus);
        CHECK(lamps(lamp) == 0x00, "RESET* clears the latch");

        m.bus.memWrite(0x8010, 0xAA);
        lamp.power();
        CHECK(lamps(lamp) == 0x00, "power-on clears it too");
    }

    SECTION("lamp -- the addr strap moves the decode");
    {
        Machine   m;
        LampBoard lamp;
        m.bus.setVerify(true);
        m.bus.attach(&lamp);
        lamp.power();

        std::string err;
        CHECK(setProperty(lamp, "addr", "800c", err), "addr is a hex strap in the SS-30 window");

        m.bus.memWrite(0x800C, 0x22);
        CHECK(lamps(lamp) == 0x22, "the store at the NEW address latches");

        BusCycle old; old.type = Cycle::MemWrite; old.addr = 0x8010;
        CHECK(m.bus.respondersTo(old).empty(), "and the old address answers nobody -- the decode followed");

        // lamps has no setter: SET must be refused, not silently accepted.
        CHECK(!setProperty(lamp, "lamps", "01", err), "lamps is read-only -- it has no setter");
    }

    SECTION("lamp -- snapshot carries the latch, not the strap");
    {
        LampBoard lamp;
        std::string err;
        setProperty(lamp, "addr", "8018", err);
        lamp.write([] { BusCycle c; c.type = Cycle::MemWrite; c.addr = 0x8018; c.data = 0x3C; return c; }());

        StateWriter w;
        lamp.serialize(w);

        LampBoard   fresh;             // built from config: strap already correct
        StateReader rd(w.data());
        fresh.deserialize(rd);
        CHECK(lamps(fresh) == 0x3C, "the latch survives serialize/deserialize");
    }
}
