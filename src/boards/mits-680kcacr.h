#pragma once
//
// Altair 680b KCACR -- the Kansas City Audio Cassette Record interface.
// reference/Altair 680b KCACR.md.
//
// THE CASSETTE BOARD IS A CASSETTE INTERFACE THAT GREW A MOTOR. Its serial engine is
// the 1602-family UART, its tape machinery is the same MOUNT / WIND / REWIND / WAV
// codec, and its recording is Kansas City FSK. So this board DERIVES from
// Cassette1602Board and inherits every one of those (cassette1602.h): the tape unit,
// the live counter, the auto-stop, SNAPSHOT of the head position. What the KCACR adds
// is exactly the three things its manual documents that a plain audio cassette
// interface does not have -- and all three are the 6800's world:
//
//   * IT IS MEMORY-MAPPED, NOT PORTED. The 6800 has no IN/OUT space, so the two
//     registers live in ordinary memory and are reached with LDA/STA. F010 is
//     Status(read)/Control(write); F011 is Read/Write Data. decodes()/read()/
//     write() answer MemRead/MemWrite at those two addresses, not I/O ports.
//
//   * EVERY BIT IS ACTIVE-LOW -- "True = Logic 0" (reference section 2). RDA, TBE,
//     both interrupt enables and both motor bits assert as 0. So the status byte
//     is built here, in the KCACR's own active-low convention.
//
//   * IT HAS MOTOR CONTROL AND INTERRUPTS. Control D7=0 turns the recorder motor
//     on, D6=0 off; D0=0 enables the Read-Data interrupt, D1=0 the Transmit-Buffer
//     interrupt, and either enabled condition pulls the 6800 IRQ. The interrupt
//     enables auto-clear on any register access or a Motor-Off write (reference
//     section 4), which is how the handler acknowledges. The two enable flip-flops
//     are the inherited inIntEnabled_/outIntEnabled_ (read/write here); the motor
//     relay is this board's one added latch.
//
// The loader/punch PROM at FD00 is NOT on this board -- it is a ROM in the machine's
// memory map (like MON680 at FF00), so nothing about it is here. This board is the
// F010/F011 register facade plus the tape transport underneath it.

#include "boards/cassette1602.h"

#include <cstdint>
#include <string>
#include <vector>

namespace swtpc {

class KcacrBoard : public Cassette1602Board {
public:
    KcacrBoard();

    std::string type() const override { return "680kcacr"; }

    // ---- bus (memory-mapped, active-low) ----
    bool    decodes(const BusCycle& c) const override;
    uint8_t read(const BusCycle& c) override;
    void    write(const BusCycle& c) override;

    // Two scattered addresses in page F0, never a whole page -- ask per address.
    bool decodeIsPageUniform() const override { return false; }

    // ---- interrupts: an enabled RDA/TBE pulls the 6800 IRQ (reference section 4) ----
    bool    assertsInt() const override;

    // ---- lifecycle: Serial1602Board's, plus the motor relay's power-up state ----
    void reset(Reset r) override;

    // ---- reflection: the tape's, minus the serial base's electrical straps, plus motor ----
    std::vector<Property> properties() override;
    std::vector<MapEntry> memMap() const override;

    // ---- SNAPSHOT / RESTORE: Cassette1602Board (UART + int-enables + tape) then the motor ----
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // For the tests, so they can read the motor relay without going through SHOW.
    bool motorOn() const { return motorOn_; }

protected:
    // THE ONE MODEM THIS BOARD HAS: Kansas City Standard (2400/1200, 300 baud),
    // reference section 6. Cassette1602Board's own modem() is 2400/1850 and refuses a
    // KC tape; the KCACR is the board that reads and writes KC, so it selects that
    // format.
    std::vector<TapeFormat> modem() const override;

private:
    static constexpr uint16_t kStatusCtrl = 0xF010;  // read = status, write = control
    static constexpr uint16_t kData       = 0xF011;  // read = RX data, write = TX data

    // The status byte in the KCACR's active-low convention: not-asserted bits read 1,
    // an asserted condition reads 0. D0 = Read Data Available, D7 = Transmit Buffer
    // Empty; D1-D6 are unused and read 1.
    uint8_t statusByte() const;

    // THE TAPE-RECORDER MOTOR RELAY. Driven by control D7 (on) / D6 (off); normally
    // closed at power-up. Runtime
    // state, so it travels in a snapshot. Tape motion is byte-driven (host/tape.h),
    // so at the default rate=full the relay is cosmetic -- but the register is real
    // and must be swallowed without disturbing the UART underneath it.
    bool motorOn_ = true;
};

} // namespace swtpc
