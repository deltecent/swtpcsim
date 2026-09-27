#include "test.h"

#include "boards/s100-memory.h"
#include "core/machine.h"
#include "core/statefile.h"
#include "cpu/cpu6809.h"

using namespace swtpc;

// Every expectation below comes from reference/Motorola MC6809-MC6809E Programming
// Manual.md: its instruction tables for cycles, Table F-2 for the indexed modes,
// Figure 2-1 for the TFR/EXG and PSH/PUL post-bytes, and Figure 4-1 and the interrupt
// table for the stack frames.

namespace {

// A bare 6809 over 64K of RAM, driven directly -- these tests are about the CHIP.
struct Rig {
    Machine m;
    MemoryBoard* mem = nullptr;
    Cpu6809 cpu;

    Rig() {
        std::string err;
        Board* b = m.add("memory", "mem0", err);
        mem = dynamic_cast<MemoryBoard*>(b);
        Region r;
        r.kind = RegionKind::Ram;
        r.at = 0;
        r.size = 0x10000;
        mem->addRegion(r, err);
        setProperty(*mem, "fill", "zero", err);
        mem->power();
    }

    void load(std::initializer_list<uint8_t> code, uint16_t at) {
        uint16_t a = at;
        for (uint8_t byte : code) m.bus.memWrite(a++, byte);
    }
    void poke(uint16_t a, uint8_t v) { m.bus.memWrite(a, v); }
    void poke16(uint16_t a, uint16_t v) {
        poke(a, (uint8_t)(v >> 8));
        poke((uint16_t)(a + 1), (uint8_t)v);
    }
    uint8_t peek(uint16_t a) { return m.bus.peek(a); }

    // Steps n instructions; returns the cycles of the LAST one.
    uint32_t step(int n = 1) {
        uint32_t t = 0;
        for (int i = 0; i < n; ++i) t = cpu.step(m.bus).cycles;
        return t;
    }

    uint32_t reg(const char* name) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) return r.get();
        return 0xEEEEEEEE;
    }
    void setReg(const char* name, uint32_t v) {
        for (const RegDef& r : cpu.registers())
            if (r.name == name) r.set(v);
    }
    bool fl(const char* name) { return reg(name) != 0; }

    // Load a program at `at`, point PC there, run n instructions, and return the
    // cycles of the last one.
    uint32_t execAt(uint16_t at, std::initializer_list<uint8_t> code, int n) {
        load(code, at);
        cpu.setPc(at);
        return step(n);
    }
};

} // namespace

void test_cpu6809() {
    SECTION("the 6809 -- reflection, reset, DP and the deferred restart vector");
    {
        Rig g;
        CHECK(std::string(g.cpu.isa()) == "6809", "the core says which instruction set it speaks");

        std::string lamps;
        for (const RegDef& r : g.cpu.registers())
            if (r.show == RegShow::Flag) lamps += r.name;
        CHECK(lamps == "EFHINZVC", "eight lamps, in the order CC holds them");

        g.setReg("A", 0x12);
        g.setReg("B", 0x34);
        CHECK(g.reg("D") == 0x1234, "D is A:B, A the high byte");
        g.setReg("D", 0xBEEF);
        CHECK(g.reg("A") == 0xBE && g.reg("B") == 0xEF, "and writing D writes both halves");

        g.setReg("DP", 0x55);
        g.setReg("CC", 0x00);
        g.poke16(0xFFFE, 0x1234);
        g.cpu.reset(Reset::PowerOn);
        CHECK(g.reg("DP") == 0x00, "reset clears DP");
        CHECK(g.fl("I") && g.fl("F"), "reset sets both interrupt masks");
        CHECK(g.reg("A") == 0xBE, "and leaves A alone");
        CHECK(g.cpu.pc() == 0x0000, "PC is not yet the vector -- the fetch is deferred");
        g.load({0x12}, 0x1234);  // NOP
        g.step();
        CHECK(g.cpu.pc() == 0x1235, "the first step read FFFE/FFFF, then ran the NOP there");

        std::vector<VectorDef> v = g.cpu.vectors();
        CHECK(v.size() == 7 && v.front().at == 0xFFF2 && v.back().at == 0xFFFE,
              "seven vectors, SWI3 at FFF2 up to RESET at FFFE");
    }

    SECTION("loads, stores, and the direct page");
    {
        Rig g;
        // LDX #F000 ; STX >0050 -- big-endian, high byte at the lower address.
        CHECK(g.execAt(0x0100, {0x8E, 0xF0, 0x00}, 1) == 3, "LDX # is 3 cycles");
        CHECK(g.reg("X") == 0xF000, "LDX # took the word high byte first");
        CHECK(g.execAt(0x0100, {0xBF, 0x00, 0x50}, 1) == 6, "STX extended is 6 cycles");
        CHECK(g.peek(0x50) == 0xF0 && g.peek(0x51) == 0x00, "STX wrote hi at the lower address");

        // LDA #20 ; TFR A,DP ; LDA <40 -- a direct address is DP:nn.
        g.poke(0x2040, 0x77);
        g.poke(0x0040, 0x11);
        g.execAt(0x0100, {0x86, 0x20, 0x1F, 0x8B}, 2);
        CHECK(g.reg("DP") == 0x20, "TFR A,DP loaded the direct page");
        CHECK(g.execAt(0x0100, {0x96, 0x40}, 1) == 4, "LDA direct is 4 cycles");
        CHECK(g.reg("A") == 0x77, "and read DP:40 = 2040, not 0040");

        // The 16-bit loads for every register, with N, Z and V.
        g.execAt(0x0100, {0x1A, 0x02, 0xCC, 0x80, 0x00}, 2);  // ORCC #02 ; LDD #8000
        CHECK(g.reg("D") == 0x8000 && g.fl("N") && !g.fl("V"), "LDD sets N from bit 15 and clears V");
        CHECK(g.execAt(0x0100, {0x10, 0x8E, 0x12, 0x34}, 1) == 4, "LDY # is 4 cycles");
        CHECK(g.reg("Y") == 0x1234, "LDY loaded Y");
        g.execAt(0x0100, {0xCE, 0x56, 0x78, 0x10, 0xCE, 0x9A, 0xBC}, 2);  // LDU ; LDS
        CHECK(g.reg("U") == 0x5678 && g.reg("S") == 0x9ABC, "LDU and LDS loaded U and S");
        g.setReg("DP", 0x00);
        g.execAt(0x0100, {0x10, 0xDF, 0x60, 0xFD, 0x00, 0x62}, 2);  // STS <60 ; STD >0062
        CHECK(g.peek(0x60) == 0x9A && g.peek(0x61) == 0xBC, "STS direct");
        CHECK(g.peek(0x62) == 0x80 && g.peek(0x63) == 0x00, "STD extended");
    }

    SECTION("8-bit arithmetic -- H on ADD only; SUB/CMP leave H alone");
    {
        Rig g;
        g.execAt(0x0100, {0x86, 0x0F, 0x8B, 0x01}, 2);  // LDA #0F ; ADDA #01
        CHECK(g.reg("A") == 0x10 && g.fl("H"), "a carry out of bit 3 sets H");
        g.execAt(0x0100, {0x86, 0x7F, 0x8B, 0x01}, 2);
        CHECK(g.reg("A") == 0x80 && g.fl("V") && g.fl("N") && !g.fl("C"), "7F+01: V and N, no C");
        g.execAt(0x0100, {0x86, 0xFF, 0x8B, 0x01}, 2);
        CHECK(g.reg("A") == 0x00 && g.fl("Z") && g.fl("C"), "FF+01 wraps with C");

        g.execAt(0x0100, {0x1A, 0x20, 0x86, 0x10, 0x80, 0x01}, 3);  // ORCC #20 ; LDA #10 ; SUBA #01
        CHECK(g.reg("A") == 0x0F && g.fl("H"), "SUBA left H as it was");
        g.execAt(0x0100, {0x86, 0x00, 0x80, 0x01}, 2);
        CHECK(g.reg("A") == 0xFF && g.fl("C") && g.fl("N"), "00-01 borrows");
        g.execAt(0x0100, {0x86, 0x80, 0x81, 0x01}, 2);  // CMPA #01
        CHECK(g.reg("A") == 0x80 && g.fl("V") && !g.fl("C"), "CMPA 80-01: signed overflow, no write");
        g.execAt(0x0100, {0x1A, 0x01, 0x86, 0x05, 0xC6, 0x03, 0x82, 0x01}, 4);  // SEC-ish; SBCA #01
        CHECK(g.reg("A") == 0x03, "SBCA took the borrow: 05-01-1");
        g.execAt(0x0100, {0x86, 0xF0, 0x84, 0x3C, 0x8A, 0x01, 0x88, 0xFF}, 4);  // AND/OR/EOR
        CHECK(g.reg("A") == 0xCE && !g.fl("V"), "ANDA/ORA/EORA: F0&3C|01^FF = CE");
    }

    SECTION("16-bit arithmetic and compares -- H is never touched");
    {
        Rig g;
        CHECK(g.execAt(0x0100, {0x1A, 0x20, 0xCC, 0x7F, 0xFF, 0xC3, 0x00, 0x01}, 3) == 4, "ADDD # is 4 cycles");
        CHECK(g.reg("D") == 0x8000 && g.fl("V") && g.fl("N") && g.fl("H"), "7FFF+1: V and N; H still set");
        g.execAt(0x0100, {0xCC, 0x00, 0x00, 0x83, 0x00, 0x01}, 2);  // SUBD #1
        CHECK(g.reg("D") == 0xFFFF && g.fl("C") && g.fl("N"), "0000-0001 borrows");

        g.execAt(0x0100, {0x8E, 0x12, 0x34, 0x8C, 0x12, 0x34}, 2);  // CMPX #1234
        CHECK(g.fl("Z") && !g.fl("C"), "CMPX equal: Z, no C");
        g.execAt(0x0100, {0x8E, 0x10, 0x00, 0x8C, 0x20, 0x00}, 2);
        CHECK(g.fl("C") && g.fl("N"), "CMPX sets C on a 16-bit borrow -- unlike the 6800's CPX");
        CHECK(g.execAt(0x0100, {0xCC, 0x00, 0x05, 0x10, 0x83, 0x00, 0x05}, 2) == 5, "CMPD # is 5 cycles");
        CHECK(g.fl("Z"), "CMPD compared D");
        g.execAt(0x0100, {0x10, 0x8E, 0x00, 0x07, 0x10, 0x8C, 0x00, 0x07}, 2);
        CHECK(g.fl("Z"), "CMPY compared Y");
        g.execAt(0x0100, {0xCE, 0x00, 0x09, 0x11, 0x83, 0x00, 0x09}, 2);
        CHECK(g.fl("Z"), "CMPU compared U");
        g.execAt(0x0100, {0x10, 0xCE, 0x00, 0x0B, 0x11, 0x8C, 0x00, 0x0B}, 2);
        CHECK(g.fl("Z"), "CMPS compared S");
    }

    SECTION("the single-operand group -- the flag rules that moved from the 6800");
    {
        Rig g;
        g.execAt(0x0100, {0x1A, 0x01, 0x86, 0x00, 0x4D}, 3);  // ORCC #01 ; LDA #0 ; TSTA
        CHECK(g.fl("Z") && g.fl("C") && !g.fl("V"), "TST keeps C (the 6800 clears it)");

        g.execAt(0x0100, {0x86, 0x01, 0x1C, 0x00, 0x1A, 0x02, 0x44}, 4);  // LDA #1 ; V only ; LSRA
        CHECK(g.reg("A") == 0 && g.fl("C") && g.fl("Z") && !g.fl("N") && g.fl("V"), "LSR keeps V");
        g.execAt(0x0100, {0x86, 0x81, 0x1C, 0x00, 0x1A, 0x02, 0x47}, 4);  // ASRA
        CHECK(g.reg("A") == 0xC0 && g.fl("C") && g.fl("N") && g.fl("V"), "ASR holds the sign and keeps V");
        g.execAt(0x0100, {0x86, 0x02, 0x1C, 0x00, 0x1A, 0x03, 0x46}, 4);  // V,C only ; RORA
        CHECK(g.reg("A") == 0x81 && !g.fl("C") && g.fl("V"), "ROR brings C in at the top and keeps V");

        g.execAt(0x0100, {0x86, 0x40, 0x48}, 2);  // LDA #40 ; ASLA
        CHECK(g.reg("A") == 0x80 && g.fl("V") && !g.fl("C"), "ASL: V = b7^b6 of the operand (0^1)");
        g.execAt(0x0100, {0x86, 0xC0, 0x48}, 2);
        CHECK(g.reg("A") == 0x80 && !g.fl("V") && g.fl("C"), "ASL: 1^1 leaves V clear, C = old b7");
        g.execAt(0x0100, {0x1C, 0x00, 0x86, 0x40, 0x49}, 3);  // ROLA
        CHECK(g.reg("A") == 0x80 && g.fl("V"), "ROL: V = b7^b6 too");

        g.execAt(0x0100, {0x86, 0x80, 0x40}, 2);  // NEGA
        CHECK(g.reg("A") == 0x80 && g.fl("V") && g.fl("C"), "NEG 80: V, and C since it was not 0");
        g.execAt(0x0100, {0x86, 0x00, 0x40}, 2);
        CHECK(g.fl("Z") && !g.fl("C"), "NEG 00: no borrow");
        g.execAt(0x0100, {0x86, 0x0F, 0x43}, 2);  // COMA
        CHECK(g.reg("A") == 0xF0 && g.fl("C") && !g.fl("V"), "COM sets C, clears V");
        g.execAt(0x0100, {0x1C, 0x00, 0xC6, 0x7F, 0x5C}, 3);  // INCB
        CHECK(g.reg("B") == 0x80 && g.fl("V") && !g.fl("C"), "INC 7F sets V, leaves C");
        g.execAt(0x0100, {0xC6, 0x80, 0x5A}, 2);  // DECB
        CHECK(g.reg("B") == 0x7F && g.fl("V"), "DEC 80 sets V");
        g.execAt(0x0100, {0x1A, 0x0B, 0x5F}, 2);  // CLRB
        CHECK(g.reg("B") == 0 && g.fl("Z") && !g.fl("N") && !g.fl("V") && !g.fl("C"), "CLR");

        // Memory forms: direct 6, indexed 6+, extended 7.
        g.setReg("DP", 0);
        g.poke(0x50, 0x41);
        CHECK(g.execAt(0x0100, {0x0C, 0x50}, 1) == 6, "INC direct is 6 cycles");
        CHECK(g.peek(0x50) == 0x42, "and wrote back");
        g.poke(0x2000, 0x01);
        CHECK(g.execAt(0x0100, {0x74, 0x20, 0x00}, 1) == 7, "LSR extended is 7 cycles");
        CHECK(g.peek(0x2000) == 0x00 && g.fl("C"), "and shifted memory");
        g.setReg("X", 0x2000);
        g.poke(0x2000, 0x80);
        CHECK(g.execAt(0x0100, {0x6D, 0x84}, 1) == 6, "TST ,X is 6 cycles");
        CHECK(g.peek(0x2000) == 0x80 && g.fl("N"), "and wrote nothing back");
        g.execAt(0x0100, {0x7F, 0x20, 0x00}, 1);
        CHECK(g.peek(0x2000) == 0x00, "CLR extended cleared memory");
    }

    SECTION("indexed addressing -- every Table F-2 mode, and its cycles");
    {
        Rig g;
        // Each byte holds its own address's low byte, so a read says where it came from.
        for (int i = 0; i < 0x40; ++i) g.poke((uint16_t)(0x0FE0 + i), (uint8_t)(0xE0 + i));
        g.poke(0x1100, 0xAA);
        g.poke(0x0FF0, 0xF0);
        auto lda = [&](std::initializer_list<uint8_t> code) {
            g.setReg("X", 0x1000);
            return g.execAt(0x0100, code, 1);
        };
        // LDA's base indexed cost is 4; Table F-2 adds the rest.
        CHECK(lda({0xA6, 0x84}) == 4 && g.reg("A") == 0x00, ",X: 4 cycles, reads 1000");
        CHECK(lda({0xA6, 0x1F}) == 5 && g.reg("A") == 0xFF, "5-bit -1,X: +1, reads 0FFF");
        CHECK(lda({0xA6, 0x88, 0x10}) == 5 && g.reg("A") == 0x10, "8-bit 10,X: +1, reads 1010");
        CHECK(lda({0xA6, 0x89, 0x01, 0x00}) == 8 && g.reg("A") == 0xAA, "16-bit 0100,X: +4, reads 1100");
        CHECK(lda({0xA6, 0x89, 0xFF, 0xF0}) == 8 && g.reg("A") == 0xF0, "16-bit FFF0,X wraps to 0FF0");

        g.setReg("A", 0xF0);  // A,X: A is SIGNED
        g.cpu.setPc(0x0100);
        g.setReg("X", 0x1000);
        g.load({0xA6, 0x86}, 0x0100);
        CHECK(g.step() == 5 && g.reg("A") == 0xF0, "A,X with A=F0 reads 1000-10 = 0FF0");
        g.setReg("B", 0x10);
        CHECK(lda({0xA6, 0x85}) == 5 && g.reg("A") == 0x10, "B,X: +1, reads 1010");
        g.setReg("D", 0x0100);
        CHECK(lda({0xA6, 0x8B}) == 8 && g.reg("A") == 0xAA, "D,X: +4, reads 1100");

        CHECK(lda({0xA6, 0x80}) == 6 && g.reg("X") == 0x1001, ",X+: +2, reads then increments");
        CHECK(lda({0xA6, 0x81}) == 7 && g.reg("X") == 0x1002, ",X++: +3");
        CHECK(lda({0xA6, 0x82}) == 6 && g.reg("X") == 0x0FFF && g.reg("A") == 0xFF, ",-X: decrements first");
        CHECK(lda({0xA6, 0x83}) == 7 && g.reg("X") == 0x0FFE, ",--X: +3");

        g.setReg("Y", 0x1010);
        g.setReg("U", 0x1011);
        g.setReg("S", 0x1012);
        CHECK(g.execAt(0x0100, {0xA6, 0xA4}, 1) == 4 && g.reg("A") == 0x10, ",Y");
        CHECK(g.execAt(0x0100, {0xA6, 0xC4}, 1) == 4 && g.reg("A") == 0x11, ",U");
        CHECK(g.execAt(0x0100, {0xA6, 0xE4}, 1) == 4 && g.reg("A") == 0x12, ",S");

        // PC-relative, from the address after the whole instruction.
        g.poke(0x0113, 0x5A);
        CHECK(g.execAt(0x0100, {0xA6, 0x8C, 0x10}, 1) == 5 && g.reg("A") == 0x5A, "n,PCR 8-bit: +1");
        g.poke(0x0204, 0x6B);
        CHECK(g.execAt(0x0100, {0xA6, 0x8D, 0x01, 0x00}, 1) == 9 && g.reg("A") == 0x6B, "n,PCR 16-bit: +5");

        // Indirect: the computed address holds the final one.
        g.poke16(0x1000, 0x1100);
        g.poke16(0x1010, 0x1100);
        g.poke16(0x3000, 0x1100);
        CHECK(lda({0xA6, 0x94}) == 7 && g.reg("A") == 0xAA, "[,X]: +3");
        CHECK(lda({0xA6, 0x98, 0x10}) == 8 && g.reg("A") == 0xAA, "[n,X] 8-bit: +4");
        CHECK(lda({0xA6, 0x99, 0x00, 0x10}) == 11 && g.reg("A") == 0xAA, "[n,X] 16-bit: +7");
        CHECK(lda({0xA6, 0x91}) == 10 && g.reg("A") == 0xAA && g.reg("X") == 0x1002, "[,X++]: +6");
        CHECK(g.execAt(0x0100, {0xA6, 0x9F, 0x30, 0x00}, 1) == 9 && g.reg("A") == 0xAA, "[nnnn]: +5");
        g.poke16(0x0113, 0x1100);
        CHECK(g.execAt(0x0100, {0xA6, 0x9C, 0x10}, 1) == 8 && g.reg("A") == 0xAA, "[n,PCR]: +4");

        // An unlisted post-byte addresses ,R with no side effect.
        CHECK(lda({0xA6, 0x90}) == 4 && g.reg("X") == 0x1000 && g.cpu.pc() == 0x0102,
              "[,X+] is not allowed: it reads ,X, leaves X, and takes only the post-byte");
        CHECK(lda({0xA6, 0xFF, 0x30, 0x00}) == 4 && g.reg("A") == 0x12 && g.cpu.pc() == 0x0102,
              "extended indirect is 9F only: FF reads ,S (1012) and takes no address bytes");
    }

    SECTION("LEA -- only LEAX and LEAY touch Z");
    {
        Rig g;
        g.setReg("X", 0x0001);
        CHECK(g.execAt(0x0100, {0x30, 0x1F}, 1) == 5, "LEAX -1,X is 4+1 cycles");
        CHECK(g.reg("X") == 0 && g.fl("Z"), "and set Z on reaching zero");
        g.setReg("Y", 0x0010);
        g.execAt(0x0100, {0x31, 0x21}, 1);  // LEAY 1,Y
        CHECK(g.reg("Y") == 0x0011 && !g.fl("Z"), "LEAY clears Z on a non-zero result");
        g.setReg("CC", 0x04);
        g.setReg("S", 0x0002);
        g.execAt(0x0100, {0x32, 0x7E}, 1);  // LEAS -2,S
        CHECK(g.reg("S") == 0 && g.reg("CC") == 0x04, "LEAS reached zero and left every flag");
        g.setReg("U", 0x0100);
        g.execAt(0x0100, {0x33, 0xC9, 0x01, 0x00}, 1);  // LEAU 0100,U
        CHECK(g.reg("U") == 0x0200, "LEAU with a 16-bit offset");
    }

    SECTION("TFR / EXG -- same-size pairs only");
    {
        Rig g;
        g.setReg("X", 0x1234);
        CHECK(g.execAt(0x0100, {0x1F, 0x12}, 1) == 6 && g.reg("Y") == 0x1234, "TFR X,Y: 6 cycles");
        g.setReg("A", 1);
        g.setReg("B", 2);
        CHECK(g.execAt(0x0100, {0x1E, 0x89}, 1) == 8, "EXG is 8 cycles");
        CHECK(g.reg("A") == 2 && g.reg("B") == 1, "EXG A,B swapped them");
        g.setReg("D", 0xAAAA);
        g.setReg("X", 0x5555);
        g.execAt(0x0100, {0x1E, 0x01}, 1);  // EXG D,X
        CHECK(g.reg("D") == 0x5555 && g.reg("X") == 0xAAAA, "EXG D,X");
        g.setReg("A", 0x0F);
        g.execAt(0x0100, {0x1F, 0x8A}, 1);  // TFR A,CC
        CHECK(g.reg("CC") == 0x0F, "TFR A,CC wrote the flags");
        g.setReg("X", 0x7777);
        g.execAt(0x0100, {0x1F, 0x81}, 1);  // TFR A,X -- mixed sizes
        CHECK(g.reg("X") == 0x7777, "a mixed-size TFR (undefined) transfers nothing");
        g.setReg("X", 0x4000);
        g.execAt(0x0100, {0x1F, 0x15}, 1);  // TFR X,PC
        CHECK(g.cpu.pc() == 0x4000, "TFR X,PC jumps");
    }

    SECTION("PSH / PUL -- order, the other stack pointer, and 5 + 1 per byte");
    {
        Rig g;
        g.setReg("S", 0x0200);
        g.setReg("A", 0x11);
        g.setReg("B", 0x22);
        g.setReg("X", 0x3344);
        CHECK(g.execAt(0x0100, {0x34, 0x16}, 1) == 9, "PSHS A,B,X moves 4 bytes: 9 cycles");
        CHECK(g.reg("S") == 0x01FC, "S points AT the last byte pushed");
        CHECK(g.peek(0x01FC) == 0x11 && g.peek(0x01FD) == 0x22, "A lowest, then B");
        CHECK(g.peek(0x01FE) == 0x33 && g.peek(0x01FF) == 0x44, "X above them, big-endian");
        g.setReg("A", 0);
        g.setReg("B", 0);
        g.setReg("X", 0);
        g.execAt(0x0100, {0x35, 0x16}, 1);
        CHECK(g.reg("A") == 0x11 && g.reg("B") == 0x22 && g.reg("X") == 0x3344 && g.reg("S") == 0x0200,
              "PULS A,B,X restored them");

        g.setReg("U", 0x0300);
        g.execAt(0x0100, {0x36, 0x40}, 1);  // PSHU S
        CHECK(g.reg("U") == 0x02FE && g.peek(0x02FE) == 0x02 && g.peek(0x02FF) == 0x00,
              "PSHU puts S in bit 6");
        g.setReg("U", 0x1234);
        g.execAt(0x0100, {0x34, 0x40}, 1);  // PSHS U
        CHECK(g.peek(0x01FE) == 0x12 && g.peek(0x01FF) == 0x34, "PSHS puts U in bit 6");
        g.setReg("S", 0x0200);
        CHECK(g.execAt(0x0100, {0x34, 0xFF}, 1) == 17 && g.reg("S") == 0x01F4, "PSHS everything: 12 bytes");

        g.setReg("S", 0x01FE);
        g.poke16(0x01FE, 0x0456);
        g.execAt(0x0100, {0x35, 0x80}, 1);  // PULS PC
        CHECK(g.cpu.pc() == 0x0456, "PULS PC returns, like RTS");
    }

    SECTION("MUL, SEX, ABX, DAA");
    {
        Rig g;
        CHECK(g.execAt(0x0100, {0x86, 0x0C, 0xC6, 0x0A, 0x3D}, 3) == 11, "MUL is 11 cycles");
        CHECK(g.reg("D") == 0x0078 && !g.fl("C") && !g.fl("Z"), "0C*0A = 0078");
        g.execAt(0x0100, {0x86, 0x10, 0xC6, 0x08, 0x3D}, 3);
        CHECK(g.reg("D") == 0x0080 && g.fl("C"), "MUL: C is bit 7 of the result");
        g.execAt(0x0100, {0x86, 0xFF, 0xC6, 0xFF, 0x3D}, 3);
        CHECK(g.reg("D") == 0xFE01, "MUL is unsigned");
        g.execAt(0x0100, {0x86, 0x00, 0xC6, 0x55, 0x3D}, 3);
        CHECK(g.fl("Z"), "MUL sets Z on zero");

        g.execAt(0x0100, {0xC6, 0x80, 0x1D}, 2);
        CHECK(g.reg("D") == 0xFF80 && g.fl("N"), "SEX extends B's sign into A");
        g.execAt(0x0100, {0xC6, 0x7F, 0x1D}, 2);
        CHECK(g.reg("D") == 0x007F && !g.fl("N"), "and a positive B gives A=00");

        g.setReg("X", 0x1000);
        CHECK(g.execAt(0x0100, {0xC6, 0xFF, 0x3A}, 2) == 3, "ABX is 3 cycles");
        CHECK(g.reg("X") == 0x10FF, "ABX adds B UNSIGNED");

        g.execAt(0x0100, {0x86, 0x09, 0x8B, 0x01, 0x19}, 3);
        CHECK(g.reg("A") == 0x10, "BCD 09+01 = 10");
        g.execAt(0x0100, {0x86, 0x99, 0x8B, 0x01, 0x19}, 3);
        CHECK(g.reg("A") == 0x00 && g.fl("C"), "BCD 99+01 = 00 with carry");
    }

    SECTION("branches -- short 3 always; long 6 taken, 5 not");
    {
        Rig g;
        g.setReg("CC", 0x04);  // Z
        CHECK(g.execAt(0x0100, {0x27, 0x10}, 1) == 3 && g.cpu.pc() == 0x0112, "BEQ taken: 3");
        CHECK(g.execAt(0x0100, {0x26, 0x10}, 1) == 3 && g.cpu.pc() == 0x0102, "BNE not taken: 3");
        CHECK(g.execAt(0x0100, {0x10, 0x27, 0x10, 0x00}, 1) == 6 && g.cpu.pc() == 0x1104, "LBEQ taken: 6");
        CHECK(g.execAt(0x0100, {0x10, 0x26, 0x10, 0x00}, 1) == 5 && g.cpu.pc() == 0x0104, "LBNE not: 5");
        CHECK(g.execAt(0x0100, {0x10, 0x21, 0x10, 0x00}, 1) == 5 && g.cpu.pc() == 0x0104, "LBRN: 5");
        CHECK(g.execAt(0x0100, {0x16, 0xFF, 0x00}, 1) == 5 && g.cpu.pc() == 0x0003, "LBRA backward: 5");

        g.setReg("CC", 0x0A);  // N and V: signed >=
        CHECK(g.execAt(0x0100, {0x2C, 0x02}, 1) == 3 && g.cpu.pc() == 0x0104, "BGE with N=V");
        g.execAt(0x0100, {0x2E, 0x02}, 1);
        CHECK(g.cpu.pc() == 0x0104, "BGT with N=V and Z clear");
        g.execAt(0x0100, {0x2D, 0x02}, 1);
        CHECK(g.cpu.pc() == 0x0102, "BLT not taken");
        g.setReg("CC", 0x01);  // C
        g.execAt(0x0100, {0x22, 0x02}, 1);
        CHECK(g.cpu.pc() == 0x0102, "BHI not taken with C");
        g.execAt(0x0100, {0x23, 0x02}, 1);
        CHECK(g.cpu.pc() == 0x0104, "BLS taken with C");

        g.setReg("S", 0x0200);
        CHECK(g.execAt(0x0100, {0x8D, 0x10}, 1) == 7 && g.cpu.pc() == 0x0112, "BSR: 7");
        CHECK(g.peek(0x01FE) == 0x01 && g.peek(0x01FF) == 0x02, "BSR pushed 0102");
        g.load({0x39}, 0x0112);
        CHECK(g.step() == 5 && g.cpu.pc() == 0x0102 && g.reg("S") == 0x0200, "RTS: 5");
        CHECK(g.execAt(0x0100, {0x17, 0x01, 0x00}, 1) == 9 && g.cpu.pc() == 0x0203, "LBSR: 9");
        g.setReg("S", 0x0200);
        CHECK(g.execAt(0x0100, {0xBD, 0x30, 0x00}, 1) == 8 && g.cpu.pc() == 0x3000, "JSR extended: 8");
        g.setReg("X", 0x4000);
        CHECK(g.execAt(0x0100, {0xAD, 0x84}, 1) == 7 && g.cpu.pc() == 0x4000, "JSR ,X: 7");
        CHECK(g.execAt(0x0100, {0x7E, 0x50, 0x00}, 1) == 4 && g.cpu.pc() == 0x5000, "JMP extended: 4");
        CHECK(g.execAt(0x0100, {0x6E, 0x84}, 1) == 3 && g.cpu.pc() == 0x4000, "JMP ,X: 3");
        g.setReg("DP", 0x60);
        CHECK(g.execAt(0x0100, {0x0E, 0x10}, 1) == 3 && g.cpu.pc() == 0x6010, "JMP direct uses DP: 3");
    }

    SECTION("IRQ and FIRQ -- the entire frame against PC and CC only");
    {
        Rig g;
        g.poke16(0xFFF8, 0x0500);
        g.poke16(0xFFF6, 0x0600);
        g.load({0x3B}, 0x0500);  // RTI
        g.load({0x3B}, 0x0600);  // RTI
        g.setReg("S", 0x0400);
        g.setReg("CC", 0x00);

        g.m.bus.intWireChanged(true);
        g.cpu.setPc(0x0100);
        CHECK(g.step() == 19 && g.cpu.pc() == 0x0500, "IRQ vectored through FFF8: 19 cycles");
        CHECK(g.reg("S") == 0x0400 - 12, "stacking all twelve bytes");
        CHECK((g.peek(0x03F4) & 0x80) && !(g.peek(0x03F4) & 0x10), "the stacked CC has E set, I as it was");
        CHECK(g.fl("I") && !g.fl("F"), "IRQ sets I and leaves F");
        g.m.bus.intWireChanged(false);
        CHECK(g.step() == 15 && g.cpu.pc() == 0x0100 && g.reg("S") == 0x0400, "RTI pulled all of it: 15");
        CHECK(!g.fl("I"), "and restored I");

        g.cpu.setFirq(true);
        CHECK(g.step() == 10 && g.cpu.pc() == 0x0600, "FIRQ vectored through FFF6: 10 cycles");
        CHECK(g.reg("S") == 0x0400 - 3, "stacking only PC and CC");
        CHECK(!(g.peek(0x03FD) & 0x80), "with E clear in the stacked CC");
        CHECK(g.fl("I") && g.fl("F"), "FIRQ sets both masks");
        g.cpu.setFirq(false);
        CHECK(g.step() == 6 && g.cpu.pc() == 0x0100 && g.reg("S") == 0x0400, "RTI read E=0: 6 cycles");

        // F masks FIRQ; with both lines up and both open, FIRQ wins.
        g.setReg("CC", 0x40);
        g.cpu.setFirq(true);
        g.execAt(0x0100, {0x12}, 1);
        CHECK(g.cpu.pc() == 0x0101, "F masks FIRQ: the NOP ran");
        g.setReg("CC", 0x00);
        g.m.bus.intWireChanged(true);
        g.cpu.setPc(0x0100);
        g.step();
        CHECK(g.cpu.pc() == 0x0600, "FIRQ outranks IRQ");
        g.cpu.setFirq(false);
        g.m.bus.intWireChanged(false);
    }

    SECTION("NMI -- disarmed from reset until S is loaded");
    {
        Rig g;
        g.poke16(0xFFFE, 0x0100);
        g.poke16(0xFFFC, 0x0700);
        g.load({0x12, 0x10, 0xCE, 0x04, 0x00, 0x12}, 0x0100);  // NOP ; LDS #0400 ; NOP
        g.cpu.reset(Reset::PowerOn);
        g.cpu.signalNmi();
        g.step();
        CHECK(g.cpu.pc() == 0x0101, "an NMI before S is loaded is not taken");
        g.step();  // LDS
        g.cpu.signalNmi();
        CHECK(g.step() == 19 && g.cpu.pc() == 0x0700, "after LDS it is taken, through FFFC");
        CHECK(g.fl("I") && g.fl("F") && g.reg("S") == 0x0400 - 12, "with both masks set and the entire frame");
    }

    SECTION("SWI, SWI2, SWI3");
    {
        Rig g;
        g.poke16(0xFFFA, 0x0800);
        g.poke16(0xFFF4, 0x0900);
        g.poke16(0xFFF2, 0x0A00);
        g.setReg("S", 0x0400);
        g.setReg("CC", 0x00);
        CHECK(g.execAt(0x0100, {0x3F}, 1) == 19 && g.cpu.pc() == 0x0800, "SWI: FFFA, 19 cycles");
        CHECK(g.fl("I") && g.fl("F") && g.fl("E"), "SWI sets I and F");
        g.setReg("S", 0x0400);
        g.setReg("CC", 0x00);
        CHECK(g.execAt(0x0100, {0x10, 0x3F}, 1) == 20 && g.cpu.pc() == 0x0900, "SWI2: FFF4, 20 cycles");
        CHECK(!g.fl("I") && !g.fl("F"), "SWI2 masks nothing");
        g.setReg("CC", 0x00);
        CHECK(g.execAt(0x0100, {0x11, 0x3F}, 1) == 20 && g.cpu.pc() == 0x0A00, "SWI3: FFF2, 20 cycles");
        CHECK(!g.fl("I") && !g.fl("F"), "SWI3 masks nothing");
    }

    SECTION("CWAI and SYNC -- the two ways to wait");
    {
        Rig g;
        g.poke16(0xFFF8, 0x0500);
        g.poke16(0xFFF6, 0x0600);
        g.setReg("S", 0x0400);
        g.setReg("CC", 0x50);
        CHECK(g.execAt(0x0100, {0x3C, 0xEF}, 1) == 17, "CWAI stacks up front");
        CHECK(g.cpu.halted() && std::string(g.cpu.waitingOn()) == "CWAI", "and waits");
        CHECK(g.reg("S") == 0x0400 - 12 && !g.fl("I") && g.fl("F"), "CWAI #EF cleared I only");
        StepResult s = g.cpu.step(g.m.bus);
        CHECK(s.status == RunStatus::Halted && g.reg("S") == 0x0400 - 12, "nothing pending: still waiting");
        g.m.bus.intWireChanged(true);
        CHECK(g.step() == 3 && g.cpu.pc() == 0x0500 && !g.cpu.halted(), "IRQ ends the wait: 17+3 = 20");
        CHECK(g.reg("S") == 0x0400 - 12, "without stacking again");
        g.m.bus.intWireChanged(false);

        // SYNC: any line ends it; a masked one resumes at the next instruction.
        g.setReg("CC", 0x50);
        g.execAt(0x0100, {0x13, 0x12}, 1);
        CHECK(g.cpu.halted() && std::string(g.cpu.waitingOn()) == "SYNC", "SYNC waits");
        s = g.cpu.step(g.m.bus);
        CHECK(s.status == RunStatus::Halted, "with no line up");
        g.cpu.setFirq(true);
        CHECK(g.step() == 2 && !g.cpu.halted() && g.cpu.pc() == 0x0101, "a masked FIRQ ends SYNC");
        g.step();
        CHECK(g.cpu.pc() == 0x0102, "and the next instruction runs");
        g.cpu.setFirq(false);  // else FIRQ is taken before SYNC even runs
        g.setReg("CC", 0x00);
        g.execAt(0x0100, {0x13}, 1);
        g.cpu.setFirq(true);
        g.step();
        CHECK(g.cpu.pc() == 0x0600, "an unmasked FIRQ ends SYNC and is taken");
        g.cpu.setFirq(false);
    }

    SECTION("undefined opcodes are inert");
    {
        Rig g;
        g.setReg("A", 0x55);
        g.execAt(0x0100, {0x01}, 1);
        CHECK(g.cpu.pc() == 0x0101 && g.reg("A") == 0x55, "01: one byte, nothing changed");
        g.execAt(0x0100, {0x10, 0x00}, 1);
        CHECK(g.cpu.pc() == 0x0102, "10 00: the prefix and the byte after it");
        g.execAt(0x0100, {0x87}, 1);
        CHECK(g.cpu.pc() == 0x0101, "87 (no STA immediate): one byte");
    }

    SECTION("serialize / deserialize is a round trip, hidden state included");
    {
        Rig g;
        g.setReg("D", 0x1234);
        g.setReg("X", 0x5678);
        g.setReg("Y", 0x9ABC);
        g.setReg("U", 0xDEF0);
        g.setReg("S", 0x0400);
        g.setReg("DP", 0x42);
        g.setReg("CC", 0xFF);
        g.cpu.setPc(0x0100);
        g.load({0x13}, 0x0100);  // SYNC, so the hidden wait travels too
        g.step();

        StateWriter w;
        g.cpu.serialize(w);
        Cpu6809 clone;
        StateReader r(w.data());
        clone.deserialize(r);
        CHECK(r.ok(), "the reader consumed the frame");

        std::vector<uint32_t> a, b;
        g.cpu.captureRegs(a);
        clone.captureRegs(b);
        CHECK(a == b, "every register survived");
        CHECK(clone.halted() && std::string(clone.waitingOn()) == "SYNC", "and the SYNC wait");
    }

    SECTION("6809: captureRegs() IS registers(), value for value, in order");
    {
        Cpu6809               core;
        std::vector<RegDef>   defs = core.registers();
        std::vector<uint32_t> fast;
        uint32_t              seed = 0x13579BDFu;
        bool                  same = true;
        for (int round = 0; round < 64 && same; ++round) {
            for (const RegDef& d : defs) {
                seed = seed * 1664525u + 1013904223u;
                d.set(seed >> 8);
            }
            core.captureRegs(fast);
            same = fast.size() == defs.size();
            for (size_t i = 0; same && i < defs.size(); ++i) same = fast[i] == defs[i].get();
        }
        CHECK(same, "6809: captureRegs() matches registers() exactly");
    }
}
