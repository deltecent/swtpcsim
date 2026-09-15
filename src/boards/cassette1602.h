#pragma once
//
// Cassette1602Board -- the reusable audio-cassette engine.
//
// THIS IS A BASE, NOT A BOARD YOU CAN ADD. It is a Serial1602Board (the 1602-family
// UART engine, serial1602.h) with a cassette recorder soldered where the connector
// would be. Nothing registers it; the 680b KCACR (mits-680kcacr.h) derives from it.
//
// The serial half is inherited whole: same UART, same two interrupt-enable
// flip-flops, same refresh()/nextEdge() clock. What this base adds is everything a
// cassette needs and a bare serial line does not:
//
//   * THE LINE IS SOLDERED TO A CASSETTE. There is no connector to CONNECT to. The
//     UART's serial pins go to a modem, the modem's audio goes to the recorder, and
//     so the unit is a TAPE you MOUNT -- UnitKind::Tape rather than UnitKind::Serial.
//
//   * A TAPE HAS A POSITION, so the card brings verbs: WIND / REWIND (and EXTRACT for
//     a multi-program WAV). A disk does not need them -- you can seek a disk. That is
//     the whole reason board-injected commands exist (Board::commands()).
//
//   * A MODEM. It is analog -- an FSK tone pair, derived by dividing the clock, and a
//     phase-locked loop to get it back -- and the guest cannot observe one bit of it.
//     Which modulation the card can hear is modem(): VIRTUAL, because a descendant may
//     carry a different one (the KCACR reads Kansas City Standard where this base's
//     own answer is the 2400/1850 FSK a plain audio-cassette modem hears).

#include "boards/serial1602.h"
#include "host/tape.h"
#include "host/tapecodec.h"

#include <memory>
#include <string>
#include <vector>

namespace swtpc {

class Cassette1602Board : public Serial1602Board {
public:
    Cassette1602Board();

    std::string type() const override { return "cassette1602"; }

    // The serial base's, minus `connect`. THE CARD HAS NO CONNECTOR: its UART's serial
    // pins are soldered to the modem board. Offering an endpoint would advertise a
    // socket where the hardware has a cassette.
    std::vector<Property> properties() override;

    // THE BUTTONS ON THE RECORDER -- `mode = play | record`, and a UNIT property
    // rather than a board one because it is not on the card. The operator pressed the
    // button; here, the operator types it.
    std::vector<Property> unitProperties(const std::string& unit) override;

    // ONE TAPE. MOUNT puts a cassette in the recorder; UNMOUNT takes it out.
    std::vector<UnitDef> units() const override;
    bool mount(const std::string& unit, const std::string& path, bool ro, std::string& err) override;
    bool unmount(const std::string& unit, std::string& err) override;

    // ...and CONNECT is refused, with the reason, rather than silently inherited.
    bool connect(const std::string& unit, const std::string& endpoint, std::string& err) override;
    bool disconnect(const std::string& unit, std::string& err) override;

    // WIND (and REWIND, its wind-to-start alias). The reason Board::commands() exists.
    std::vector<CommandDef> commands() const override;
    bool runCommand(const std::string& name, const std::vector<std::string>& args,
                    std::ostream& out, std::string& err) override;

    // A LIVE TAPE COUNTER while a tape loads. Non-empty only when a cassette is actually
    // playing (0 < head < end) and the operator did not turn the counter off -- the run
    // loop prints it and knows nothing about tapes (core/board.h). Empty at full speed in
    // practice, because a full-rate load empties the tape inside one repaint.
    std::string activityLabel() const override;

    // BREAK TAPE STOP: report the rising edge of the cassette reaching its auto-stop mark
    // (core/board.h). Read-and-clear -- the debugger polls it at the instruction boundary
    // while such a breakpoint is armed, and the run loop drains it once at the start of a
    // run so a stop from a PREVIOUS load does not fire the moment this run begins. The
    // edge, not the level, is what fires: a tape parked at its stop mark across several
    // RUNs fires once, and a REWIND (which moves the head off the mark) re-arms it.
    bool takeAutoStop() override;

    // For the tests, so they can watch the head move without a filesystem.
    const TapeImage* tape() const { return tape_.get(); }

    // SNAPSHOT/RESTORE (DESIGN.md 13). The serial half (UART + interrupt enables) plus
    // this card's two runtime facts: which button is down (mode_) and where the head
    // is (the tape's position). The tape's bytes are host-backed and do not travel;
    // deserialize() relines the stream so it runs in the restored mode from the
    // restored position.
    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

    // What a mount had to say for itself. Merged with the UART's, because the serial
    // base's drainLog() is the UART's alone and a mount that demodulated a WAV has its
    // own news (host/tapecodec.h -- report, do not hide).
    std::vector<std::string> drainLog() override;

protected:
    // WHAT THIS CARD'S MODEM CAN PHYSICALLY HEAR -- one modulation, because the card
    // has one modem: continuous FSK at 2400/1850, 300 baud. A Kansas City tape is
    // REFUSED rather than decoded, because the real PLL sits at 2125 Hz and takes
    // about +/-100 Hz, and a 1200 Hz space tone is some 925 Hz outside it. See
    // host/tapecodec.h for why decoding it anyway would be inventing hardware.
    //
    // VIRTUAL, because a DESCENDANT card can carry a different modem -- the KCACR
    // (mits-680kcacr.h) reads and writes Kansas City Standard, so its override returns
    // that. That is not inventing hardware: it is the modem the physical board has.
    virtual std::vector<TapeFormat> modem() const;

private:
    // Hand the UART a fresh line onto the tape in whatever mode the recorder is in
    // now. Called on MOUNT and whenever a button is pressed.
    void reline();

    // Push `leader`/`trailer` down onto an audio tape. A no-op on a byte tape.
    void applyEncoding();

    // The transport stopped: an audio tape re-encodes itself and goes to the host.
    // Called wherever an OPERATOR action ends a recording -- UNMOUNT, REWIND, releasing
    // RECORD. See MediaFile::commit() for why this is not sync().
    void commitTape();

    // Move the head to `pos` (clamped to the tape), sharing REWIND's and WIND's staging:
    // commit any recording, seek, drop the byte the UART is still holding from where the
    // head used to be, and reline. REWIND is stageAt(0).
    void stageAt(uint64_t pos);

    // Where the head is, in seconds into the recording, and the recording's length.
    // Real audio time for a WAV (audio_->secondsAt); an estimate from the 300-baud strap
    // for a byte tape, which carries no audio to measure.
    double   tapeSeconds(uint64_t bytePos) const;
    double   tapeTotalSeconds() const;
    uint64_t secondsToByte(double secs) const;  // the inverse, for WIND to a time

    // THE BOARD OWNS THE TAPE; THE UART OWNS ONLY A STREAM ONTO IT (host/tape.h).
    // That split is what keeps REWIND out of the chip's reach: a UART that could
    // rewind its own line is not a UART.
    std::unique_ptr<TapeImage> tape_;
    std::string                path_;

    std::string format_ = "auto";  // the `format` unit property
    std::string detected_;         // ...and what the mounted tape turned out to be

    // THE TAPE, IF IT IS AUDIO -- non-owning, and null when it is a byte tape or when
    // nothing is mounted. tape_ owns the medium; this is how the board reaches the one
    // thing only an audio tape has, which is an encoding to write back with.
    AudioTapeMedia* audio_ = nullptr;

    // Seconds of idle tone either side of a recording. At least ~15 s of steady tone
    // before data, and at least 5 s between batches -- so a trailer of 5 makes two
    // recordings laid end to end into a tape a real machine would accept. See the
    // `leader` property for why they are integers and why they exist at all.
    long long leader_  = 15;
    long long trailer_ = 5;

    // The CARRIER SHAPE this card writes audio with: "square" (default -- what a real modem
    // lays down) or "sine". Audible only; a re-mount decodes either the same (tapemodem.h).
    std::string wave_ = "square";

    // THE RECORDING LEVEL, percent of full scale, when this card writes audio. 36% is a
    // realistic cassette level; the old 80% ran more than twice as hot as a genuine dub.
    long long level_ = 36;   // percent of full scale, 1..100

    // HOW FAST THE TAPE PLAYS BACK, and it is the tape's clock, not the CPU's. "full"
    // (default) empties the cassette as fast as the loader reads it, at any clock_hz --
    // no waiting for a machine that never had to wait to read its own memory. "real"
    // paces playback in wall time at the card's 300-baud strap, the way a recorder does,
    // whatever the crystal is set to. See host/tape.h; it is playback only -- recording
    // is the operator's finger on the button and takes as long as it takes.
    std::string rate_ = "full";

    // Show the live counter on the console while a tape loads. Default ON; the operator
    // turns it off at MOUNT (`counter=off`) or with SET on a serial-console machine where
    // the guest owns stdout. On-demand SHOW works either way. See activityLabel().
    bool liveCounter_ = true;

    std::vector<std::string> log_;

    // PLAY, until somebody says otherwise. It is what you do with a cassette 99 times
    // out of 100, and it is the safe default in the one way that matters: a tape that
    // is playing cannot be written over.
    TapeStream::Mode mode_ = TapeStream::Mode::Play;

    // The last auto-stop level takeAutoStop() saw, so it can report the RISING edge. Only
    // ever touched inside takeAutoStop(); the pre-run drain in the debugger keeps it in
    // step with reality at the start of every run. See takeAutoStop().
    bool wasAtStop_ = false;
};

} // namespace swtpc
