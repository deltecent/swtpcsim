#pragma once
//
// Serial1602Board -- the reusable serial engine around a 1602-family UART.
//
// THIS IS A BASE, NOT A BOARD YOU CAN ADD. Nothing registers it; nothing
// instantiates it directly. It carries the parts a memory-mapped 6800 serial or
// cassette card needs and NONE of the wiring that would make it one particular
// card: no address decode, no status-word layout, no interrupt straps. A concrete
// board derives from it, decodes its own addresses, builds its own status byte, and
// drives its own interrupt pin -- the 680b KCACR cassette (mits-680kcacr.h) reaches
// it through Cassette1602Board (cassette1602.h).
//
// What lives here is exactly the machinery that is the SAME on every card that hangs
// a 1602-family UART (the COM2502, src/chips/uart1602.h) off the bus:
//
//   * THE UART ITSELF, and its receive/transmit timing (u_).
//   * THE TWO INTERRUPT-ENABLE FLIP-FLOPS. The COM2502 has no interrupt pin and no
//     interrupt enables of its own -- a card derives its two requests from RDA and
//     TBMT and gates them with a pair of flip-flops that are a SEPARATE IC. Those
//     flip-flops, and the wire to the CPU, are the card's, not the chip's, which is
//     why they live on this board base and not in Uart1602. See DESIGN.md 7.8.
//   * THE CARD'S OWN CLOCK (refresh()/nextEdge()). An interrupt-driven driver never
//     reads the status port, so the receiver has to be advanced and the pin re-driven
//     on the UART's clock with nobody touching the card (DESIGN.md 4.4.1, 7.5).
//   * THE CONNECTOR, snapshot, and lifecycle.
//
// A derived board that decodes I/O ports or memory addresses does so itself; a
// derived board that has interrupts drives its pin with its own assertsInt(). This
// base drives neither, so a bare instance decodes nothing and asks for no interrupt.

#include "chips/uart1602.h"  // the COM2502 itself -- A CHIP IS NOT A CARD (DESIGN.md 7.8)
#include "core/board.h"
#include "host/filter.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace swtpc {

class Serial1602Board : public Board {
public:
    Serial1602Board();
    ~Serial1602Board() override;

    std::string type() const override { return "serial1602"; }

    void reset(Reset) override;
    void power() override;
    void pump() override;
    void configChanged() override;

    // SNAPSHOT/RESTORE (DESIGN.md 13). The UART's state plus the two interrupt-enable
    // flip-flops. The port/connector is config. A derived board extends this.
    // deserialize() re-arms via refresh() from the restored state.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // One UART. A derived cassette board inherits this override with it -- a cassette
    // arriving is traffic too. (Board::rxBytes.)
    uint64_t rxBytes() const override { return u_.rxBytes(); }

    // What the real serial port said when the card tried to program its straps into
    // it. A cable that cannot do 7E2 is a fact about the world, and it is said out
    // loud rather than swallowed.
    std::vector<std::string> drainLog() override { return u_.drainLog(); }

    std::vector<Property> properties() override;
    std::vector<UnitDef>  units() const override;

    bool connect(const std::string& unit, const std::string& endpoint,
                 std::string& err) override;
    // Install a PRE-BUILT stream (the MCP console's filtered scripted line). The chip
    // takes it via attachStream; refresh() re-arms in case a byte is already waiting.
    bool connectStream(const std::string& unit, std::unique_ptr<ByteStream> s,
                       std::string& err) override {
        if (unit != "tty") {
            err = "this board has no unit '" + unit + "' -- it has one, and it is called 'tty'";
            return false;
        }
        attachStream(std::move(s));
        refresh();
        return true;
    }
    bool disconnect(const std::string& unit, std::string& err) override;

    // The monitor resolves an endpoint string to a stream; the BOARD is not allowed
    // to know what a socket is (DESIGN.md 7.7).
    using EndpointResolver =
        std::function<std::unique_ptr<ByteStream>(const std::string&, std::string&)>;
    static void setResolver(EndpointResolver r);

    // PLUG A LINE INTO THE CONNECTOR. The chip owns the stream (and wraps it in the
    // transform chain, DESIGN.md 7.2) -- this is the card's connector, handing it down.
    void attachStream(std::unique_ptr<ByteStream> s) { u_.connect(std::move(s)); }
    std::string endpoint() const { return u_.endpoint(); }

    // The connector, for an operator that owns the endpoint (the MCP console).
    // Non-owning; the UART owns the stream. See Board::unitStream.
    ByteStream* unitStream(const std::string& unit) override {
        return unit == "tty" ? &u_.stream() : nullptr;
    }

    // ---- The pins, so a test can look at them without going through the bus. ----
    bool dataAvailable() const { return u_.dataAvailable(); }
    bool txBufferEmpty() const;

    // ---- PROTECTED, FOR CASSETTE1602BOARD AND ITS DESCENDANTS ------------------
protected:
    // Everything that could have moved the interrupt pin has just happened: advance
    // the receiver, re-drive the pin, and set the alarm clock for the next moment the
    // UART could move it with nobody touching the card (DESIGN.md 7.5).
    void refresh();

    // The next cycle at which THE CARD's interrupt request could move on its own.
    // Zero means never. ALWAYS STRICTLY IN THE FUTURE (see Mc6850::nextEdge).
    //
    // This is the card's and not the chip's, and the reason is the whole chip/board
    // split in one function: the COM2502 has no interrupt pin and no interrupt
    // enables. It just reports when its registers next change (u_.txFreeAt(),
    // u_.rxNextAt()); the two enable flip-flops that decide whether anybody CARES are
    // a separate IC on this card, and so is the wire to the CPU.
    uint64_t nextEdge() const;

    // The two conditions, before the interrupt enables.
    bool rxReady() const { return u_.dataAvailable(); }
    bool txReady() const { return txBufferEmpty(); }

    // ---- THE UART. One COM2502 (src/chips/uart1602.h). ----
    //
    // It owns the line, the receive register, Data Available, the transmit deadline
    // and the word-format pins. What it does NOT own is anything on the following
    // list, and every item on it is a fact about the CARD rather than about the chip:
    // where the status bits sit and how they read, the interrupt enables, the wire to
    // the CPU, and the address decode.
    Uart1602 u_{"tty"};

    // ---- Software state: the interrupt-enable flip-flops. ----
    //
    // These are a separate IC on the card, not anything inside the UART. The COM2502
    // has no interrupt pin at all -- the card derives its two requests from RDA and
    // TBMT and gates them with these. A derived board reads and writes them from its
    // own control register.
    bool inIntEnabled_  = false;
    bool outIntEnabled_ = false;

    Clock::Handle wake_ = Clock::kNone;
};

} // namespace swtpc
