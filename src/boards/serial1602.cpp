#include "boards/serial1602.h"

#include "core/statefile.h"
#include "host/endpoint.h"
#include "host/stream.h"

#include <utility>
#include <vector>

namespace swtpc {

namespace {

Serial1602Board::EndpointResolver g_resolver;

// A card in a backplane always has a clock, but Bus::attach() is public, so a
// board CAN be wired up without a machine around it. A UART with no clock is a
// chip with no crystal: it cannot receive and it cannot time a character. It reads
// as a dead card rather than dereferencing a null pointer.
Clock& deadCard() {
    static Clock stopped;
    return stopped;
}

} // namespace

void Serial1602Board::setResolver(EndpointResolver r) { g_resolver = std::move(r); }

Serial1602Board::Serial1602Board() {
    // -> NullStream. There is no null pointer in the stream path, ever: a card with
    // nothing plugged into it is a card with a DEAD line, not a dangling one.
    u_.disconnect();
}

Serial1602Board::~Serial1602Board() {
    // The queue is holding a lambda with `this` inside it, and a card can be pulled
    // out of a RUNNING machine (`BOARDS REMOVE`). A deadline that fires into a
    // destroyed board is a use-after-free with a two-week fuse on it.
    if (clock_) clock_->cancel(wake_);
}

bool Serial1602Board::txBufferEmpty() const {
    return u_.txBufferEmpty(clock_ ? *clock_ : deadCard());
}

// ---------------------------------------------------------------------------
// THE CARD'S OWN CLOCK (DESIGN.md 4.4.1, 7.5).
//
// The receiver is advanced HERE, and that is the load-bearing line. An
// interrupt-driven driver NEVER reads the status port -- being interrupt-driven is
// precisely so that it does not have to -- so a UART that only ingested a character
// when the guest looked at a register would never ingest one, never raise the
// request, and the operator could type forever with nothing happening.
//
// And the transmitter drains on the UART's clock with nobody touching the card at
// all: a guest that enables the output interrupt, sends a character and HALTs is an
// entirely ordinary driver, and the only thing that can wake it is a deadline this
// card set for itself.
// ---------------------------------------------------------------------------
void Serial1602Board::serialize(StateWriter& w) const {
    Board::serialize(w);
    u_.serialize(w);
    w.boolean(inIntEnabled_);
    w.boolean(outIntEnabled_);
}

void Serial1602Board::deserialize(StateReader& r) {
    Board::deserialize(r);
    u_.deserialize(r);
    inIntEnabled_  = r.boolean();
    outIntEnabled_ = r.boolean();
    refresh();  // re-drive the interrupt pin and re-arm the deadline from the restored state
}

void Serial1602Board::refresh() {
    if (!clock_) return;

    u_.poll(*clock_);
    intChanged();  // drive the interrupt pin -- the bus is not going to come and ask

    clock_->cancel(wake_);
    wake_ = Clock::kNone;

    // Usually there is no edge coming and we set no timer at all: a quiet line with
    // an idle transmitter has nothing whatever to do next, and that is the commonest
    // state in the machine.
    if (uint64_t next = nextEdge()) wake_ = clock_->at(next, [this] { refresh(); });
}

// WHEN COULD THE REQUEST MOVE ON ITS OWN? Zero means never.
//
// THE CHIP REPORTS THE DEADLINE; THE CARD DECIDES IF ANYONE IS LISTENING. The COM2502
// has no interrupt pin, so unlike the 6850 it cannot answer this question itself: it
// knows when TBMT rises and when the next character lands, and it knows nothing about
// the two enable flip-flops or the wire to the CPU, both of which are out here.
uint64_t Serial1602Board::nextEdge() const {
    const Clock& clk = clock_ ? *clock_ : deadCard();

    uint64_t best = 0;
    auto consider = [&](uint64_t when) {
        // STRICTLY FUTURE. A deadline already past is already showing in the pin;
        // arming a timer for now() would fire inside the drain loop that is running
        // us, and arm it again, and never stop.
        if (when <= clk.now()) return;
        if (!best || when < best) best = when;
    };

    // The transmitter drains on its own clock, and the request rises with it.
    if (outIntEnabled_) consider(u_.txFreeAt());

    // The receiver fills on its own too -- but ONLY if there is actually a character
    // on the line to fill it with (u_.rxWaiting()). If the host has sent nothing, there
    // is no edge coming and no timer to set: a byte appearing out of nowhere is not a
    // deadline, it is an event in the OUTSIDE WORLD, and pump() is the one door the
    // outside world comes through.
    if (inIntEnabled_ && u_.rxWaiting()) consider(u_.rxNextAt());

    return best;
}

// ---------------------------------------------------------------------------
// RESET. Unlike the 6850 (which has no reset pin at all), the COM2502 HAS one: MR,
// pin 21. So a card CAN reset this UART from the backplane, and the data sheet says
// what happens when it does -- "sets TSO, TEOC and TBMT high, and clears RDA, RPE,
// RFE, ROR", which is exactly Uart1602::masterReset().
// ---------------------------------------------------------------------------
void Serial1602Board::reset(Reset) {
    if (!clock_) return;

    u_.masterReset(*clock_);

    // POC* clears the interrupt-enable flip-flops. We do the same on RESET*.
    inIntEnabled_  = false;
    outIntEnabled_ = false;

    // The endpoint STAYS CONNECTED -- Uart1602::masterReset() is careful about that.
    // A warm reset does not unplug the terminal, and a guest that reset its UART and
    // found the console gone would be a baffling thing to debug.

    // refresh() CANCELS the outstanding deadline before re-arming, which is why
    // wake_ must not be cleared here: POWER empties the queue under us, but RESET*
    // does not. A character was going out when the switch was hit, and its alarm is
    // still on the books. Zeroing the handle first would ORPHAN it.
    refresh();
}

void Serial1602Board::power() { reset(Reset::PowerOn); }

// THE ONE DOOR THE OUTSIDE WORLD COMES THROUGH (DESIGN.md 7.1).
void Serial1602Board::pump() {
    u_.pump();
    refresh();
}

// A pad moved: `baud` or a word-format pad (which changes how long a character
// takes, so every deadline this card has set is now aimed at the wrong cycle), or
// `connect` (a new line, possibly with something already on it).
void Serial1602Board::configChanged() {
    // A STRAP THAT MOVED MAY HAVE MOVED THE FRAME. `baud`, `data_bits`, `stop_bits`
    // and `parity` are the wire's format, so a real serial port on the far end has to
    // be reopened at the new one -- resoldering NDB1 and leaving the cable at 8N1
    // would be a card whose jumpers mean nothing outside the simulator. Every other
    // endpoint ignores it. We do not try to work out WHICH property moved, for the
    // same reason configChanged() itself does not (core/board.cpp).
    u_.programLine();

    refresh();
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

std::vector<Property> Serial1602Board::properties() {
    std::vector<Property> p;

    // ---- THE FORMAT PADS. They live ON THE CHIP (they are pins on it), and the card
    // presents them, because a pad is what an operator with a soldering iron sees.
    {
        Property x;
        x.name  = "baud";
        x.help  = "Line rate. A JUMPER on the real card -- software cannot change it";
        x.kind  = Kind::Int;
        x.radix = 10;  // never on the wire: decimal (DESIGN.md 10.0.1)
        x.min   = 50;
        x.max   = 25000;  // "The maximum BAUD rate is (400K/16) 25,000 BAUD"
        x.unit  = "baud";
        x.get   = [this] { return Value::ofInt(u_.baud); };
        x.set   = [this](const Value& v, std::string&) {
            u_.baud = v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "data_bits";
        x.help  = "Data bits per character. The NDB1/NDB2 pads";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 5;
        x.max   = 8;
        x.get   = [this] { return Value::ofInt(u_.dataBits); };
        x.set   = [this](const Value& v, std::string&) {
            u_.dataBits = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name  = "stop_bits";
        x.help  = "Stop bits. The NSB pad: GND = 1, +V = 2";
        x.kind  = Kind::Int;
        x.radix = 10;
        x.min   = 1;
        x.max   = 2;
        x.get   = [this] { return Value::ofInt(u_.stopBits); };
        x.set   = [this](const Value& v, std::string&) {
            u_.stopBits = (int)v.i();
            return true;
        };
        p.push_back(std::move(x));
    }
    {
        Property x;
        x.name    = "parity";
        x.help    = "The NPB/POE pads: none | odd | even";
        x.kind    = Kind::Enum;
        x.choices = {"none", "odd", "even"};
        x.get     = [this] {
            switch (u_.parity) {
            case LineParity::Odd:  return Value::ofStr("odd");
            case LineParity::Even: return Value::ofStr("even");
            default:               return Value::ofStr("none");
            }
        };
        x.set = [this](const Value& v, std::string&) {
            const std::string& s = v.s();
            u_.parity = (s == "odd")    ? LineParity::Odd
                        : (s == "even") ? LineParity::Even
                                        : LineParity::None;
            return true;
        };
        p.push_back(std::move(x));
    }

    {
        Property x;
        x.name = "connect";
        x.help = "The endpoint on the other end of the line (CONNECT sets this)";
        x.kind = Kind::Str;
        x.get  = [this] { return Value::ofStr(u_.endpoint()); };
        // Route through connect() so a declarative `unit.tty connect = "in:tape.tap"`
        // rebases its PATH the same way the CONNECT command does -- one path, one rule.
        x.set  = [this](const Value& v, std::string& err) { return connect("tty", v.s(), err); };
        p.push_back(std::move(x));
    }

    // NO TRANSFORM CHAIN. `upper`, `strip7out` and the rest are the CONSOLE's
    // (DESIGN.md 7.2, host/console.h) -- `SET CONSOLE STRIP7OUT=ON`. This card's
    // connector goes to a Teletype, a modem or a socket, and the ONLY one of those
    // that has any business rewriting a byte is the one with a human behind it.
    return p;
}

// ONE serial unit. The card has one UART and one connector. Every jumper on it is a
// BOARD property, which is exactly what it is on the PCB. The unit exists because
// CONNECT names one.
std::vector<UnitDef> Serial1602Board::units() const {
    return {{"tty", UnitKind::Serial, u_.endpoint()}};
}

bool Serial1602Board::connect(const std::string& unit, const std::string& ep, std::string& err) {
    if (unit != "tty") {
        err = "this board has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    if (!g_resolver) {
        err = "no endpoint resolver installed";
        return false;
    }
    // A machine-file in:/out: PATH is relative to the machine file; rebase the copy the
    // resolver opens. The `connect` unit property routes here too, so both the CONNECT
    // command and a declarative connect are covered.
    std::vector<std::string> paths;
    std::string              spec = rebaseEndpointPaths(ep, [&](const std::string& p) {
        paths.push_back(p);
        return resolvePath(p);
    });
    auto s = g_resolver(spec, err);
    if (!s) {
        for (const std::string& p : paths) err += pathNote(p);
        return false;
    }
    attachStream(std::move(s));

    refresh();  // a new line, and it may already have something waiting on it
    return true;
}

bool Serial1602Board::disconnect(const std::string& unit, std::string& err) {
    if (unit != "tty") {
        err = "this board has no unit '" + unit + "' -- it has one, and it is called 'tty'";
        return false;
    }
    attachStream(std::make_unique<NullStream>());

    refresh();  // the line went dead: no more characters are coming off it
    return true;
}

} // namespace swtpc
