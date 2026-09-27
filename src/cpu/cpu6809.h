#pragma once
//
// The Motorola MC6809 core -- the chip of the SWTPC MP-09, not the card (DESIGN.md 3).
//
// Source: reference/Motorola MC6809-MC6809E Programming Manual.md, and nothing else
// (DESIGN.md 0.1). Opcodes, cycle counts, the indexed post-byte table (Table F-2), the
// stacking order and the vectors all come from there.
//
// It is a 6800 at the SOURCE level only. The binary is different, and so are the
// parts that bite:
//
//   * TWO STACKS AND A DIRECT PAGE. S is the hardware stack; U is the user's. A
//     direct address is DP:nn, not 00:nn. Reset clears DP so old code still works.
//
//   * A STACK POINTER POINTS AT THE LAST BYTE PUSHED. A push decrements first and
//     then writes; a pull reads and then increments. (The 6800 is the other way.)
//
//   * FIRQ STACKS ONLY PC AND CC, with E=0, and RTI reads E back to decide how many
//     bytes to pull. A core that always pulls twelve bytes corrupts the stack.
//
//   * NMI IS DISARMED FROM RESET UNTIL S IS FIRST LOADED, so an NMI that arrives
//     before the program has a stack is not taken.
//
//   * THE FLAG RULES MOVED. TST keeps C; LSR, ROR and ASR keep V; ASL and ROL set V
//     to b7^b6 of the operand. None of those is what the 6800 does.
//
// Where the manual calls a result UNDEFINED -- H after a subtract or a shift, V after
// DAA, an illegal opcode, a TFR between registers of different sizes -- this core
// changes nothing. That is a choice, and it is stated at each place it is made.

#include "core/bus.h"
#include "cpu/cpu.h"

#include <cstdint>

namespace swtpc {

class Cpu6809 : public CpuCore {
public:
    const char* isa() const override { return "6809"; }

    std::vector<RegDef> registers() override;
    void captureRegs(std::vector<uint32_t>& out) override;
    void reset(Reset) override;
    StepResult step(Bus& bus) override;

    uint16_t pc() const override { return pc_; }
    // A deposit cancels a pending reset-vector fetch, as on the 6800 (cpu6800.h).
    void setPc(uint16_t v) override { pc_ = v; fetchResetVector_ = false; }

    // CWAI and SYNC both hold the processor until an interrupt line moves.
    bool halted() const override { return waiting_ || syncing_; }
    bool interruptsEnabled() const override { return !if_; }
    const char* waitingOn() const override { return syncing_ ? "SYNC" : "CWAI"; }
    std::vector<VectorDef> vectors() const override {
        return {{0xFFF2, "SWI3"}, {0xFFF4, "SWI2"}, {0xFFF6, "FIRQ"}, {0xFFF8, "IRQ"},
                {0xFFFA, "SWI"},  {0xFFFC, "NMI"},  {0xFFFE, "RESET"}};
    }

    // NMI is an edge on its own pin. The core latches it and takes it at the next
    // instruction boundary -- but only once S has been loaded (see nmiArmed_).
    void signalNmi() {
        if (nmiArmed_) nmiPending_ = true;
    }

    // FIRQ is a level on its own pin, gated by F. The card that carries this core
    // drives it; the plain `6809` card leaves it unconnected.
    void setFirq(bool asserted) { firqLine_ = asserted; }

    // The CC byte: E F H I N Z V C.
    uint8_t cc() const;
    void setCc(uint8_t v);

    void serialize(StateWriter& w) const override;
    void deserialize(StateReader& r) override;

private:
    // ---- fetch/store. EVERYTHING goes through the Bus (no back door to RAM) ----
    uint8_t fetch(Bus& bus) { return bus.memRead(pc_++); }
    uint16_t fetch16(Bus& bus);                  // big-endian: hi first
    uint16_t read16(Bus& bus, uint16_t a) const;
    void write16(Bus& bus, uint16_t a, uint16_t v) const;
    void push8(Bus& bus, uint16_t& sp, uint8_t v);
    uint8_t pull8(Bus& bus, uint16_t& sp);
    void push16(Bus& bus, uint16_t& sp, uint16_t v);
    uint16_t pull16(Bus& bus, uint16_t& sp);

    uint16_t d() const { return (uint16_t)((a_ << 8) | b_); }
    void setD(uint16_t v) { a_ = (uint8_t)(v >> 8); b_ = (uint8_t)v; }
    void setS(uint16_t v) { s_ = v; nmiArmed_ = true; }  // a load of S arms NMI

    // ---- flags ----
    void setNZ8(uint8_t r) { nf_ = (r & 0x80) != 0; zf_ = (r == 0); }
    void setNZ16(uint16_t r) { nf_ = (r & 0x8000) != 0; zf_ = (r == 0); }

    uint8_t add8(uint8_t a, uint8_t m, bool carry);
    uint8_t sub8(uint8_t a, uint8_t m, bool borrow);
    uint16_t add16(uint16_t a, uint16_t m);
    uint16_t sub16(uint16_t a, uint16_t m);
    uint8_t logic8(uint8_t r);                   // AND/OR/EOR/BIT/LD/ST: N Z, V=0
    uint16_t ld16(uint16_t r);                   // 16-bit load/store: N Z, V=0
    uint8_t rmw(uint8_t col, uint8_t m);         // NEG..CLR by the low opcode nibble
    void daa();

    // ---- decode helpers ----
    uint16_t indexedEa(Bus& bus, uint32_t& extra);
    uint16_t eaFor(Bus& bus, int mode, uint32_t& extra);  // 1 dir, 2 idx, 3 ext
    static int regBits(int code);                 // TFR/EXG code -> 16, 8, or 0 (undefined)
    uint16_t getRegCode(int code) const;
    void setRegCode(int code, uint16_t v);
    uint32_t tfrExg(uint8_t post, bool exchange);
    uint32_t pushRegs(Bus& bus, uint8_t mask, bool userStack);
    uint32_t pullRegs(Bus& bus, uint8_t mask, bool userStack);
    void pushEntire(Bus& bus);                    // the 12-byte frame, E already set
    bool cond(uint8_t op) const;                  // branch condition, by low nibble

    uint32_t page1(Bus& bus, uint8_t op);
    uint32_t page2(Bus& bus, uint8_t op);
    uint32_t page3(Bus& bus, uint8_t op);
    uint32_t aluBlock(Bus& bus, uint8_t op);
    uint32_t swi(Bus& bus, uint16_t vector, bool mask, uint32_t cycles);
    uint32_t takeInterrupt(Bus& bus, uint16_t vector, bool entire, bool setF);

    uint8_t a_ = 0, b_ = 0, dp_ = 0;
    uint16_t x_ = 0, y_ = 0, u_ = 0, s_ = 0, pc_ = 0;
    bool ef_ = false, ff_ = false, hf_ = false, if_ = false;
    bool nf_ = false, zf_ = false, vf_ = false, cf_ = false;

    // CWAI has stacked the entire state and is waiting; the interrupt that ends the
    // wait needs no further stacking.
    bool waiting_ = false;
    // SYNC is waiting for ANY interrupt line, masked or not.
    bool syncing_ = false;

    // Reset arms this; the first step() reads FFFE/FFFF (DESIGN.md 6).
    bool fetchResetVector_ = false;

    // NMI: armed by the first load of S after reset, then latched per edge.
    bool nmiArmed_ = false;
    bool nmiPending_ = false;

    // The FIRQ pin's level, as the card drives it.
    bool firqLine_ = false;
};

} // namespace swtpc
