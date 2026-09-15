#pragma once
//
// THE LAMP BOARD -- the board the Developer Guide teaches you to write.
//
// Eight LEDs and a latch. A store to its address lights them; that is the entire board. It
// is the smallest thing that is honestly a board: it decodes a bus cycle, it holds state, it
// has a setting, and it shows up in SHOW, in BOARDS, in a machine file and in CONFIG SAVE
// without a line of code anywhere else.
//
// IT IS NOT IN THE SHIPPING REGISTRY, AND THAT IS THE POINT.
//
// Every type in `BOARDS TYPES` is real SWTPC or Altair 680 hardware. A lamp latch is not --
// it does no work, it exists to be read about. Putting it in the registry would also spoil
// the tutorial's last step, which is "now add one line to src/boards/registry.cpp" -- a step
// the reader is meant to PERFORM, not to discover has already been done for them.
//
// So it lives here, out of the product, and is compiled into the TEST binary and driven by a
// real bus in tests/test_lamp.cpp. That is what stops the Developer Guide's code from
// rotting: it is not a listing in a Markdown file, it is a board that has to keep working.
//
// MEMORY-MAPPED, BECAUSE THE 6800 HAS NO OTHER KIND.
//
// There is no IN/OUT space on this machine -- the 6800 reaches every device by ADDRESS, the
// same instructions it uses for RAM. So the lamp answers at an address on the SS-30 I/O bus
// ($8000-$801F: eight slots of four addresses, slot N based at $8000 + 4*N). Slot 4, $8010,
// is empty in the stock swtpc machine, so the lamp can have it.
//
// WHY IT DECODES ONLY WRITES.
//
// A store lights the lamps; a load has nothing to read -- there is no "what are the lamps?"
// register on the real thing, just eight pins driving eight LEDs. So the board claims the
// WRITE at its address and leaves the READ alone, and a load from $8010 floats to $FF like
// any unclaimed address. That is not a shortcut. It is how 6800 I/O actually works: the bus
// routes by DIRECTION as well as address, which is the whole reason the 6850 ACIA next door
// can put its status register (a read) and its control register (a write) at one and the
// same address. A board that decoded on the address alone -- both directions -- would answer
// reads it has no answer for.
//
//     swtpcsim> WHO 8010
//     8010 read  nobody -- floats to FF
//     8010 write lamp0
//
// THE BUS ROUTES BY CYCLE TYPE, NOT JUST BY ADDRESS. That is the lesson the board is for.

#include "core/board.h"
#include "core/statefile.h"

namespace swtpc {

class LampBoard : public Board {
public:
    // The name a machine file calls this board. It is the CHIP or the common word, never a
    // catalog number -- nobody ever asked for an MP-A board, they asked for a 6800.
    std::string type() const override { return "lamp"; }

    // ---- The bus ----------------------------------------------------------------
    //
    // decodes() answers ONE question: "if this cycle happened, would I drive the bus?"
    //
    // IT MUST BE PURE AND COMBINATIONAL. The bus caches the answer and asks again only when a
    // board says its decode changed. A decodes() with a side effect in it is a bug that will
    // not show up for a month.
    bool decodes(const BusCycle& c) const override {
        if (!enabled_) return false;                  // a board switched off drives nothing
        if (c.type != Cycle::MemWrite) return false;  // a store lights them; a load is not ours
        return c.addr == addr_;
    }

    // One address, not a whole page -- so the bus must ask per address, not cache one answer
    // for the $80 page (where the console and the disk controller also live). See
    // Board::decodeIsPageUniform().
    bool decodeIsPageUniform() const override { return false; }

    // We claimed the cycle, so the byte is ours.
    void write(const BusCycle& c) override { latch_ = c.data; }

    // read() is not overridden. We never say yes to a read, so we are never asked one.

    // ---- Lifecycle --------------------------------------------------------------
    //
    // Two different events, and a board is entitled to treat them differently. RESET* is the
    // reset line; POC* is the power coming up. Here they happen to mean the same thing -- the
    // lamps go out -- but a memory board, for one, must not confuse them.
    void reset(Reset) override { latch_ = 0; }
    void power() override { latch_ = 0; }

    // ---- SNAPSHOT / RESTORE -----------------------------------------------------
    //
    // Runtime state, not configuration. The address strap is already correct in the machine
    // a snapshot is RESTOREd into, so we do not write it -- only the latch, which is the one
    // thing a running machine accumulated. Chain to the base first (it handles enabled_).
    void serialize(StateWriter& w) const override {
        Board::serialize(w);
        w.u8(latch_);
    }
    void deserialize(StateReader& r) override {
        Board::deserialize(r);
        latch_ = r.u8();
    }

    // ---- Reflection -------------------------------------------------------------
    //
    // THIS IS THE WHOLE CONFIGURATION LAYER. There is no schema file, no parser, and no
    // registration call. SET, SHOW, the TOML loader, CONFIG SAVE, the MCP tool schemas, tab
    // completion and the manual's generated board reference are all written once, against
    // this vector, and know nothing about any particular board.
    //
    // Which is why an `addr` here is an `addr = 8010` in a machine file, for free, today.
    std::vector<Property> properties() override {
        std::vector<Property> p;
        {
            Property x;
            x.name = "addr";
            x.help = "the SS-30 address this board latches. Write-only -- a load here is not ours";
            x.kind = Kind::Int;
            x.radix = 16;  // ON THE WIRE -> HEX. An address is a thing the 6800 sees.
            x.min = 0x8000;
            x.max = 0x801F;
            x.get = [this] { return Value::ofInt(addr_); };
            x.set = [this](const Value& v, std::string&) {
                addr_ = (uint16_t)v.i();
                // No decodeChanged() call here: the property layer calls it for us after any
                // successful set, precisely so that a board author cannot forget.
                return true;
            };
            p.push_back(std::move(x));
        }
        {
            Property x;
            x.name = "lamps";
            x.help = "what the guest last wrote -- the eight LEDs";
            x.kind = Kind::Int;
            x.radix = 16;
            x.get = [this] { return Value::ofInt(latch_); };
            // NO SETTER. That is how a consumer knows this is something the board KNOWS and
            // not something you CHOSE: SHOW prints "(read-only)", CONFIG SAVE leaves it out of
            // the file, and the manual's reference marks it. A setter that always failed would
            // stop a SET and fool all three.
            p.push_back(std::move(x));
        }
        return p;
    }

    // ---- Introspection ----------------------------------------------------------
    //
    // What BOARDS, SHOW BUS MAP and WHO print. It is documentation, not decode -- the bus
    // never consults it, and a board whose memMap() disagreed with its decodes() would be
    // lying to the operator while working perfectly.
    std::vector<MapEntry> memMap() const override {
        return {{addr_, addr_, "write", "lamp latch -- D0..D7 (write-only)"}};
    }

private:
    uint16_t addr_ = 0x8010;  // SS-30 slot 4 -- empty in the stock swtpc machine
    uint8_t latch_ = 0;
};

}  // namespace swtpc
