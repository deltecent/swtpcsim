#include "cpu/cpu6809.h"

#include "core/statefile.h"

namespace swtpc {

// Every section cites reference/Motorola MC6809-MC6809E Programming Manual.md as
// "the manual".

// ---------------------------------------------------------------------------
// Fetch and store. Every one is a REAL bus cycle, and every 16-bit access is
// BIG-ENDIAN -- high byte at the lower address.
// ---------------------------------------------------------------------------
uint16_t Cpu6809::fetch16(Bus& bus) {
    uint8_t hi = fetch(bus);
    uint8_t lo = fetch(bus);
    return (uint16_t)((hi << 8) | lo);
}

uint16_t Cpu6809::read16(Bus& bus, uint16_t a) const {
    uint8_t hi = bus.memRead(a);
    uint8_t lo = bus.memRead((uint16_t)(a + 1));
    return (uint16_t)((hi << 8) | lo);
}

void Cpu6809::write16(Bus& bus, uint16_t a, uint16_t v) const {
    bus.memWrite(a, (uint8_t)(v >> 8));
    bus.memWrite((uint16_t)(a + 1), (uint8_t)v);
}

// Both stacks grow DOWN and point AT the last byte pushed: a push decrements and
// then writes, a pull reads and then increments. A word goes low byte first, so it
// lands big-endian in memory -- the high byte at the lower address.
void Cpu6809::push8(Bus& bus, uint16_t& sp, uint8_t v) {
    sp = (uint16_t)(sp - 1);
    bus.memWrite(sp, v);
}

uint8_t Cpu6809::pull8(Bus& bus, uint16_t& sp) {
    uint8_t v = bus.memRead(sp);
    sp = (uint16_t)(sp + 1);
    return v;
}

void Cpu6809::push16(Bus& bus, uint16_t& sp, uint16_t v) {
    push8(bus, sp, (uint8_t)v);
    push8(bus, sp, (uint8_t)(v >> 8));
}

uint16_t Cpu6809::pull16(Bus& bus, uint16_t& sp) {
    uint8_t hi = pull8(bus, sp);
    uint8_t lo = pull8(bus, sp);
    return (uint16_t)((hi << 8) | lo);
}

// ---------------------------------------------------------------------------
// The condition code register: E F H I N Z V C (the manual, Figure 1-2). All eight
// bits are real, so unlike the 6800 nothing reads back as a constant 1.
// ---------------------------------------------------------------------------
uint8_t Cpu6809::cc() const {
    uint8_t v = 0;
    if (ef_) v |= 0x80;
    if (ff_) v |= 0x40;
    if (hf_) v |= 0x20;
    if (if_) v |= 0x10;
    if (nf_) v |= 0x08;
    if (zf_) v |= 0x04;
    if (vf_) v |= 0x02;
    if (cf_) v |= 0x01;
    return v;
}

void Cpu6809::setCc(uint8_t v) {
    ef_ = (v & 0x80) != 0;
    ff_ = (v & 0x40) != 0;
    hf_ = (v & 0x20) != 0;
    if_ = (v & 0x10) != 0;
    nf_ = (v & 0x08) != 0;
    zf_ = (v & 0x04) != 0;
    vf_ = (v & 0x02) != 0;
    cf_ = (v & 0x01) != 0;
}

// ---------------------------------------------------------------------------
// ALU primitives. The manual marks H UNDEFINED after a subtract and V undefined
// after DAA; this core leaves an undefined flag as it was.
// ---------------------------------------------------------------------------

// ADD/ADC: H is the carry out of bit 3 (only DAA reads it), V the signed overflow.
uint8_t Cpu6809::add8(uint8_t a, uint8_t m, bool carry) {
    unsigned ci = carry ? 1 : 0;
    unsigned r = (unsigned)a + m + ci;
    hf_ = ((a & 0x0F) + (m & 0x0F) + ci) > 0x0F;
    cf_ = r > 0xFF;
    uint8_t res = (uint8_t)r;
    vf_ = ((a ^ res) & (m ^ res) & 0x80) != 0;
    setNZ8(res);
    return res;
}

// SUB/SBC/CMP: C is a borrow, V the signed overflow. H is undefined -- left alone.
uint8_t Cpu6809::sub8(uint8_t a, uint8_t m, bool borrow) {
    unsigned bi = borrow ? 1 : 0;
    unsigned r = (unsigned)a - m - bi;
    cf_ = (r & 0x100) != 0;
    uint8_t res = (uint8_t)r;
    vf_ = ((a ^ m) & (a ^ res) & 0x80) != 0;
    setNZ8(res);
    return res;
}

// The 16-bit arithmetic (ADDD, SUBD, and every 16-bit compare) leaves H alone.
uint16_t Cpu6809::add16(uint16_t a, uint16_t m) {
    uint32_t r = (uint32_t)a + m;
    cf_ = r > 0xFFFF;
    uint16_t res = (uint16_t)r;
    vf_ = ((a ^ res) & (m ^ res) & 0x8000) != 0;
    setNZ16(res);
    return res;
}

uint16_t Cpu6809::sub16(uint16_t a, uint16_t m) {
    uint32_t r = (uint32_t)a - m;
    cf_ = (r & 0x10000) != 0;
    uint16_t res = (uint16_t)r;
    vf_ = ((a ^ m) & (a ^ res) & 0x8000) != 0;
    setNZ16(res);
    return res;
}

uint8_t Cpu6809::logic8(uint8_t r) {
    setNZ8(r);
    vf_ = false;
    return r;
}

uint16_t Cpu6809::ld16(uint16_t r) {
    setNZ16(r);
    vf_ = false;
    return r;
}

// The single-operand group, by the low opcode nibble (the manual's RMW table and
// its Appendix-A shift detail). The column is the same for the accumulator,
// direct, indexed and extended forms.
uint8_t Cpu6809::rmw(uint8_t col, uint8_t m) {
    uint8_t r = m;
    switch (col) {
    case 0x0:  // NEG: V only for 80 (its own negative); C unless the operand was 0
        r = (uint8_t)(0 - m);
        vf_ = (m == 0x80);
        cf_ = (m != 0);
        setNZ8(r);
        break;
    case 0x3:  // COM: C always set, V cleared
        r = (uint8_t)~m;
        setNZ8(r);
        vf_ = false;
        cf_ = true;
        break;
    case 0x4:  // LSR: N forced 0, C = old b0, V UNCHANGED (not the 6800's N^C)
        cf_ = (m & 0x01) != 0;
        r = (uint8_t)(m >> 1);
        setNZ8(r);
        break;
    case 0x6: {  // ROR: C = old b0, V unchanged
        bool oldC = cf_;
        cf_ = (m & 0x01) != 0;
        r = (uint8_t)((m >> 1) | (oldC ? 0x80 : 0));
        setNZ8(r);
        break;
    }
    case 0x7:  // ASR: sign held, C = old b0, V unchanged
        cf_ = (m & 0x01) != 0;
        r = (uint8_t)((m >> 1) | (m & 0x80));
        setNZ8(r);
        break;
    case 0x8:  // ASL/LSL: C = old b7, V = b7^b6 of the OPERAND
        cf_ = (m & 0x80) != 0;
        vf_ = (((m >> 7) ^ (m >> 6)) & 1) != 0;
        r = (uint8_t)(m << 1);
        setNZ8(r);
        break;
    case 0x9: {  // ROL: C = old b7, V = b7^b6 of the operand
        bool oldC = cf_;
        cf_ = (m & 0x80) != 0;
        vf_ = (((m >> 7) ^ (m >> 6)) & 1) != 0;
        r = (uint8_t)((m << 1) | (oldC ? 1 : 0));
        setNZ8(r);
        break;
    }
    case 0xA:  // DEC: V on 80 -> 7F, C untouched
        r = (uint8_t)(m - 1);
        vf_ = (m == 0x80);
        setNZ8(r);
        break;
    case 0xC:  // INC: V on 7F -> 80, C untouched
        r = (uint8_t)(m + 1);
        vf_ = (m == 0x7F);
        setNZ8(r);
        break;
    case 0xD:  // TST: V cleared, C UNTOUCHED (the 6800 clears it)
        setNZ8(m);
        vf_ = false;
        break;
    case 0xF:  // CLR
        r = 0;
        nf_ = false;
        zf_ = true;
        vf_ = false;
        cf_ = false;
        break;
    default:
        break;  // the undefined columns never reach here
    }
    return r;
}

// DAA: the low correction (+06) on a half-carry or a low nibble above 9; the high
// correction (+60) on carry, a high nibble above 9, or a 9 that the low correction
// will carry out of. C is set if the high correction fired or C was already set --
// and C already set forces the high correction, so both reduce to one test.
void Cpu6809::daa() {
    uint8_t hi = a_ >> 4;
    uint8_t lo = a_ & 0x0F;
    uint8_t add = 0;
    if (hf_ || lo > 9) add |= 0x06;
    if (cf_ || hi > 9 || (hi == 9 && lo > 9)) add |= 0x60;
    a_ = (uint8_t)(a_ + add);
    cf_ = (add & 0x60) != 0;
    setNZ8(a_);
}

// ---------------------------------------------------------------------------
// Reflection (DESIGN.md 3.0.3). Eight lamps in the order they sit in CC, then the
// accumulators, the four pointer registers, DP and PC. D (= A:B) and CC are
// reachable by name but stay off the line: each is a second view of bits already
// printed.
// ---------------------------------------------------------------------------
std::vector<RegDef> Cpu6809::registers() {
    auto flag = [](const char* n, const char* help, bool* p) {
        return RegDef{n, 1, n, RegShow::Flag, help, [p] { return (uint32_t)(*p ? 1 : 0); },
                      [p](uint32_t v) { *p = v != 0; }};
    };
    auto r16 = [](const char* n, const char* help, uint16_t* p) {
        return RegDef{n, 16, n, RegShow::Field, help, [p] { return (uint32_t)*p; },
                      [p](uint32_t v) { *p = (uint16_t)v; }};
    };

    return {
        flag("E", "entire state stacked", &ef_),
        flag("F", "FIRQ mask", &ff_),
        flag("H", "half carry", &hf_),
        flag("I", "IRQ mask", &if_),
        flag("N", "negative", &nf_),
        flag("Z", "zero", &zf_),
        flag("V", "overflow", &vf_),
        flag("C", "carry", &cf_),

        {"A", 8, "", RegShow::Field, "accumulator A", [this] { return (uint32_t)a_; },
         [this](uint32_t v) { a_ = (uint8_t)v; }},
        {"B", 8, "", RegShow::Field, "accumulator B", [this] { return (uint32_t)b_; },
         [this](uint32_t v) { b_ = (uint8_t)v; }},
        r16("X", "index register X", &x_),
        r16("Y", "index register Y", &y_),
        r16("U", "user stack pointer", &u_),
        // A deposit into S from the monitor is not the CPU loading S, so it does not
        // arm NMI; only an instruction does (see setS).
        r16("S", "hardware stack pointer", &s_),
        {"DP", 8, "", RegShow::Field, "direct page", [this] { return (uint32_t)dp_; },
         [this](uint32_t v) { dp_ = (uint8_t)v; }},
        {"PC", 16, "PC", RegShow::Field, "program counter", [this] { return (uint32_t)pc_; },
         [this](uint32_t v) { setPc((uint16_t)v); }},

        {"D", 16, "", RegShow::Off, "accumulator D = A:B", [this] { return (uint32_t)d(); },
         [this](uint32_t v) { setD((uint16_t)v); }},
        {"CC", 8, "", RegShow::Off, "condition codes: E F H I N Z V C",
         [this] { return (uint32_t)cc(); }, [this](uint32_t v) { setCc((uint8_t)v); }},
    };
}

// The list above, as plain loads. SAME ORDER, entry for entry -- tests/test_cpu6809.cpp
// holds it to registers() so the two cannot drift.
void Cpu6809::captureRegs(std::vector<uint32_t>& out) {
    out.resize(18);
    uint32_t* o = out.data();
    o[0] = ef_; o[1] = ff_; o[2] = hf_; o[3] = if_;
    o[4] = nf_; o[5] = zf_; o[6] = vf_; o[7] = cf_;
    o[8] = a_; o[9] = b_; o[10] = x_; o[11] = y_; o[12] = u_; o[13] = s_;
    o[14] = dp_; o[15] = pc_; o[16] = d(); o[17] = cc();
}

// Reset (the manual's interrupt table): DP=0, I=1, F=1, NMI disarmed, and PC from
// FFFE -- deferred to the first step() so reset touches no memory (DESIGN.md 6).
// Nothing else is changed.
void Cpu6809::reset(Reset) {
    dp_ = 0;
    if_ = true;
    ff_ = true;
    waiting_ = false;
    syncing_ = false;
    nmiArmed_ = false;
    nmiPending_ = false;
    fetchResetVector_ = true;
}

// ---------------------------------------------------------------------------
// Indexed addressing (the manual, Table F-2). Returns the effective address and
// adds the table's extra cycles to `extra`. The register field is bits 6-5: X Y U S.
//
// A post-byte the table does not list -- low nibble 7, A or E, low nibble F other
// than 9F, or an indirect ,R+ / ,-R -- has no published effect. It addresses ,R,
// with no offset, no extra cycle and no change to R, and the disassembler marks it.
// ---------------------------------------------------------------------------
uint16_t Cpu6809::indexedEa(Bus& bus, uint32_t& extra) {
    uint8_t post = fetch(bus);
    uint16_t* regs[4] = {&x_, &y_, &u_, &s_};
    uint16_t& r = *regs[(post >> 5) & 3];

    // 0RRnnnnn: a 5-bit signed offset inside the post-byte. No indirect form.
    if (!(post & 0x80)) {
        int off = post & 0x1F;
        if (off & 0x10) off -= 32;
        extra += 1;
        return (uint16_t)(r + off);
    }

    bool indirect = (post & 0x10) != 0;
    uint16_t ea;
    uint32_t plain, ind;  // Table F-2's +~, without and with indirection
    switch (post & 0x0F) {
    case 0x0:  // ,R+  (post-increment)
        if (indirect) return r;
        ea = r; r = (uint16_t)(r + 1); plain = 2; ind = 2;
        break;
    case 0x1:  // ,R++
        ea = r; r = (uint16_t)(r + 2); plain = 3; ind = 6;
        break;
    case 0x2:  // ,-R  (pre-decrement)
        if (indirect) return r;
        r = (uint16_t)(r - 1); ea = r; plain = 2; ind = 2;
        break;
    case 0x3:  // ,--R
        r = (uint16_t)(r - 2); ea = r; plain = 3; ind = 6;
        break;
    case 0x4:  // ,R
        ea = r; plain = 0; ind = 3;
        break;
    case 0x5:  // B,R  (B signed)
        ea = (uint16_t)(r + (int8_t)b_); plain = 1; ind = 4;
        break;
    case 0x6:  // A,R
        ea = (uint16_t)(r + (int8_t)a_); plain = 1; ind = 4;
        break;
    case 0x8: {  // n,R  8-bit signed
        int8_t off = (int8_t)fetch(bus);
        ea = (uint16_t)(r + off); plain = 1; ind = 4;
        break;
    }
    case 0x9: {  // n,R  16-bit
        uint16_t off = fetch16(bus);
        ea = (uint16_t)(r + off); plain = 4; ind = 7;
        break;
    }
    case 0xB:  // D,R
        ea = (uint16_t)(r + d()); plain = 4; ind = 7;
        break;
    case 0xC: {  // n,PCR  8-bit -- from the PC after the offset byte
        int8_t off = (int8_t)fetch(bus);
        ea = (uint16_t)(pc_ + off); plain = 1; ind = 4;
        break;
    }
    case 0xD: {  // n,PCR  16-bit
        uint16_t off = fetch16(bus);
        ea = (uint16_t)(pc_ + off); plain = 5; ind = 8;
        break;
    }
    case 0xF:  // [n] -- extended indirect: the table lists 9F and no other
        if (post != 0x9F) return r;
        ea = fetch16(bus); plain = 5; ind = 5;
        break;
    default:  // 7, A, E
        return r;
    }

    if (indirect) {
        extra += ind;
        return read16(bus, ea);
    }
    extra += plain;
    return ea;
}

// The effective address for a memory mode: 1 direct (DP:nn), 2 indexed, 3 extended.
uint16_t Cpu6809::eaFor(Bus& bus, int mode, uint32_t& extra) {
    switch (mode) {
    case 1: return (uint16_t)((dp_ << 8) | fetch(bus));
    case 2: return indexedEa(bus, extra);
    default: return fetch16(bus);
    }
}

// ---------------------------------------------------------------------------
// TFR / EXG (the manual, Figure 2-1A). Codes 0-5 are D X Y U S PC; 8-B are A B CC
// DP. The manual calls any other code, or a pair of different sizes, UNDEFINED; this
// core then transfers nothing.
// ---------------------------------------------------------------------------
int Cpu6809::regBits(int code) {
    if (code <= 5) return 16;
    if (code >= 8 && code <= 0xB) return 8;
    return 0;
}

uint16_t Cpu6809::getRegCode(int code) const {
    switch (code) {
    case 0x0: return d();
    case 0x1: return x_;
    case 0x2: return y_;
    case 0x3: return u_;
    case 0x4: return s_;
    case 0x5: return pc_;
    case 0x8: return a_;
    case 0x9: return b_;
    case 0xA: return cc();
    default:  return dp_;  // 0xB
    }
}

void Cpu6809::setRegCode(int code, uint16_t v) {
    switch (code) {
    case 0x0: setD(v); break;
    case 0x1: x_ = v; break;
    case 0x2: y_ = v; break;
    case 0x3: u_ = v; break;
    case 0x4: setS(v); break;
    case 0x5: pc_ = v; break;
    case 0x8: a_ = (uint8_t)v; break;
    case 0x9: b_ = (uint8_t)v; break;
    case 0xA: setCc((uint8_t)v); break;
    default:  dp_ = (uint8_t)v; break;  // 0xB
    }
}

uint32_t Cpu6809::tfrExg(uint8_t post, bool exchange) {
    int src = post >> 4, dst = post & 0x0F;
    int bits = regBits(src);
    if (bits != 0 && bits == regBits(dst)) {
        uint16_t sv = getRegCode(src);
        if (exchange) {
            uint16_t dv = getRegCode(dst);
            setRegCode(src, dv);
        }
        setRegCode(dst, sv);
    }
    return exchange ? 8 : 6;
}

// ---------------------------------------------------------------------------
// PSH / PUL (the manual, Figure 2-1B). One bit per register; a push goes PC first
// (to the highest address) and CC last, a pull the reverse. The "other" stack
// pointer occupies bit 6: U on the S stack, S on the U stack. 5 cycles plus one per
// byte moved.
// ---------------------------------------------------------------------------
uint32_t Cpu6809::pushRegs(Bus& bus, uint8_t mask, bool userStack) {
    uint16_t& sp = userStack ? u_ : s_;
    uint32_t n = 0;
    if (mask & 0x80) { push16(bus, sp, pc_); n += 2; }
    if (mask & 0x40) { push16(bus, sp, userStack ? s_ : u_); n += 2; }
    if (mask & 0x20) { push16(bus, sp, y_); n += 2; }
    if (mask & 0x10) { push16(bus, sp, x_); n += 2; }
    if (mask & 0x08) { push8(bus, sp, dp_); n += 1; }
    if (mask & 0x04) { push8(bus, sp, b_); n += 1; }
    if (mask & 0x02) { push8(bus, sp, a_); n += 1; }
    if (mask & 0x01) { push8(bus, sp, cc()); n += 1; }
    return 5 + n;
}

uint32_t Cpu6809::pullRegs(Bus& bus, uint8_t mask, bool userStack) {
    uint16_t& sp = userStack ? u_ : s_;
    uint32_t n = 0;
    if (mask & 0x01) { setCc(pull8(bus, sp)); n += 1; }
    if (mask & 0x02) { a_ = pull8(bus, sp); n += 1; }
    if (mask & 0x04) { b_ = pull8(bus, sp); n += 1; }
    if (mask & 0x08) { dp_ = pull8(bus, sp); n += 1; }
    if (mask & 0x10) { x_ = pull16(bus, sp); n += 2; }
    if (mask & 0x20) { y_ = pull16(bus, sp); n += 2; }
    if (mask & 0x40) {
        uint16_t v = pull16(bus, sp);
        if (userStack) setS(v); else u_ = v;
        n += 2;
    }
    if (mask & 0x80) { pc_ = pull16(bus, sp); n += 2; }
    return 5 + n;
}

// The entire-state frame on S (the manual, Figure 4-1): PC, U, Y, X, DP, B, A, CC,
// twelve bytes, CC at the lowest address. The caller sets E first, so the CC that
// is stacked says "entire".
void Cpu6809::pushEntire(Bus& bus) {
    pushRegs(bus, 0xFF, false);
}

// ---------------------------------------------------------------------------
// Interrupts. The manual gives SWI as 19 cycles; IRQ and NMI run the same sequence
// (stack twelve bytes, fetch the vector), so they cost the same. FIRQ stacks nine
// bytes fewer: 10. A CWAI has already stacked the frame, so ending its wait costs
// only the vector fetch -- see CWAI below for the split.
// ---------------------------------------------------------------------------
uint32_t Cpu6809::takeInterrupt(Bus& bus, uint16_t vector, bool entire, bool setF) {
    uint32_t t;
    if (waiting_) {
        waiting_ = false;  // CWAI stacked the entire state, with E=1
        t = 3;
    } else if (entire) {
        ef_ = true;
        pushEntire(bus);
        t = 19;
    } else {
        ef_ = false;  // FIRQ: PC and CC only
        push16(bus, s_, pc_);
        push8(bus, s_, cc());
        t = 10;
    }
    if_ = true;
    if (setF) ff_ = true;
    pc_ = read16(bus, vector);
    return t;
}

// SWI sets I and F; SWI2 and SWI3 mask nothing.
uint32_t Cpu6809::swi(Bus& bus, uint16_t vector, bool mask, uint32_t cycles) {
    ef_ = true;
    pushEntire(bus);
    if (mask) {
        if_ = true;
        ff_ = true;
    }
    pc_ = read16(bus, vector);
    return cycles;
}

// Branch conditions by the low nibble -- the same for short (2x) and long (10 2x).
bool Cpu6809::cond(uint8_t op) const {
    switch (op & 0x0F) {
    case 0x0: return true;                      // BRA
    case 0x1: return false;                     // BRN
    case 0x2: return !cf_ && !zf_;              // BHI
    case 0x3: return cf_ || zf_;                // BLS
    case 0x4: return !cf_;                      // BCC/BHS
    case 0x5: return cf_;                       // BCS/BLO
    case 0x6: return !zf_;                      // BNE
    case 0x7: return zf_;                       // BEQ
    case 0x8: return !vf_;                      // BVC
    case 0x9: return vf_;                       // BVS
    case 0xA: return !nf_;                      // BPL
    case 0xB: return nf_;                       // BMI
    case 0xC: return nf_ == vf_;                // BGE
    case 0xD: return nf_ != vf_;                // BLT
    case 0xE: return !zf_ && nf_ == vf_;        // BGT
    default:  return zf_ || nf_ != vf_;         // BLE
    }
}

// ---------------------------------------------------------------------------
// 80-FF on page 1: the accumulator ALU, the 16-bit loads/stores/arithmetic, and
// BSR/JSR. The A side (80-BF) and B side (C0-FF) differ only in the irregular
// columns 3, C, D, E and F. Bits 5-4 pick the mode: immediate, direct, indexed,
// extended. Cycle counts are the manual's instruction tables.
// ---------------------------------------------------------------------------
uint32_t Cpu6809::aluBlock(Bus& bus, uint8_t op) {
    bool accB = (op & 0x40) != 0;
    int mode = (op >> 4) & 3;
    int col = op & 0x0F;
    uint8_t& acc = accB ? b_ : a_;
    uint32_t extra = 0;

    static const uint32_t k8[4]     = {2, 4, 4, 5};  // 8-bit ALU, LD, ST
    static const uint32_t kLd16[4]  = {3, 5, 5, 6};  // LDD/LDX/LDU, STD/STX/STU
    static const uint32_t kAr16[4]  = {4, 6, 6, 7};  // ADDD/SUBD/CMPX
    static const uint32_t kJsr[4]   = {7, 7, 7, 8};  // BSR (imm slot) / JSR

    auto operand16 = [&]() -> uint16_t {
        if (mode == 0) return fetch16(bus);
        return read16(bus, eaFor(bus, mode, extra));
    };

    switch (col) {
    case 0x3: {  // SUBD (A side) / ADDD (B side)
        uint16_t m = operand16();
        setD(accB ? add16(d(), m) : sub16(d(), m));
        return kAr16[mode] + extra;
    }
    case 0xC: {  // CMPX (A side) / LDD (B side)
        uint16_t m = operand16();
        if (accB) {
            setD(ld16(m));
            return kLd16[mode] + extra;
        }
        sub16(x_, m);
        return kAr16[mode] + extra;
    }
    case 0xD:  // BSR / JSR (A side) ; STD (B side)
        if (!accB) {
            if (mode == 0) {  // BSR
                int8_t off = (int8_t)fetch(bus);
                push16(bus, s_, pc_);
                pc_ = (uint16_t)(pc_ + off);
                return kJsr[0];
            }
            uint16_t ea = eaFor(bus, mode, extra);
            push16(bus, s_, pc_);
            pc_ = ea;
            return kJsr[mode] + extra;
        }
        [[fallthrough]];
    case 0xF: {  // STD (B, col D) ; STX (A) / STU (B)
        if (mode == 0) return 2;  // no immediate store: undefined, inert
        uint16_t v = col == 0xD ? d() : (accB ? u_ : x_);
        write16(bus, eaFor(bus, mode, extra), ld16(v));
        return kLd16[mode] + extra;
    }
    case 0xE: {  // LDX (A) / LDU (B)
        uint16_t m = ld16(operand16());
        if (accB) u_ = m; else x_ = m;
        return kLd16[mode] + extra;
    }
    case 0x7:  // STA / STB
        if (mode == 0) return 2;  // no immediate store: undefined, inert
        bus.memWrite(eaFor(bus, mode, extra), logic8(acc));
        return k8[mode] + extra;
    default:
        break;
    }

    uint8_t m = mode == 0 ? fetch(bus) : bus.memRead(eaFor(bus, mode, extra));
    switch (col) {
    case 0x0: acc = sub8(acc, m, false); break;  // SUB
    case 0x1: sub8(acc, m, false); break;        // CMP
    case 0x2: acc = sub8(acc, m, cf_); break;    // SBC
    case 0x4: acc = logic8(acc & m); break;      // AND
    case 0x5: logic8(acc & m); break;            // BIT
    case 0x6: acc = logic8(m); break;            // LD
    case 0x8: acc = logic8(acc ^ m); break;      // EOR
    case 0x9: acc = add8(acc, m, cf_); break;    // ADC
    case 0xA: acc = logic8(acc | m); break;      // OR
    default:  acc = add8(acc, m, false); break;  // 0xB ADD
    }
    return k8[mode] + extra;
}

// ---------------------------------------------------------------------------
// Page 2 (after a 10 prefix): long conditional branches, SWI2, and the Y/S and
// CMPD/CMPY forms. Each costs one cycle more than its page-1 counterpart. An
// opcode the manual does not list here has consumed both bytes and does nothing.
// ---------------------------------------------------------------------------
uint32_t Cpu6809::page2(Bus& bus, uint8_t op) {
    if (op >= 0x21 && op <= 0x2F) {  // LBcc: 6 taken, 5 not
        uint16_t off = fetch16(bus);
        if (!cond(op)) return 5;
        pc_ = (uint16_t)(pc_ + off);
        return 6;
    }
    if (op == 0x3F) return swi(bus, 0xFFF4, false, 20);  // SWI2
    if (op < 0x80) return 2;

    int mode = (op >> 4) & 3;
    bool accB = (op & 0x40) != 0;
    int col = op & 0x0F;
    uint32_t extra = 0;
    static const uint32_t kLd[4] = {4, 6, 6, 7};  // LDY/LDS, STY/STS
    static const uint32_t kCp[4] = {5, 7, 7, 8};  // CMPD/CMPY

    auto operand16 = [&]() -> uint16_t {
        if (mode == 0) return fetch16(bus);
        return read16(bus, eaFor(bus, mode, extra));
    };

    if (!accB && (col == 0x3 || col == 0xC)) {  // CMPD / CMPY
        uint16_t m = operand16();
        sub16(col == 0x3 ? d() : y_, m);
        return kCp[mode] + extra;
    }
    if (col == 0xE) {  // LDY (A side) / LDS (B side)
        uint16_t m = ld16(operand16());
        if (accB) setS(m); else y_ = m;
        return kLd[mode] + extra;
    }
    if (col == 0xF && mode != 0) {  // STY / STS
        write16(bus, eaFor(bus, mode, extra), ld16(accB ? s_ : y_));
        return kLd[mode] + extra;
    }
    return 2;
}

// Page 3 (after an 11 prefix): SWI3, CMPU, CMPS.
uint32_t Cpu6809::page3(Bus& bus, uint8_t op) {
    if (op == 0x3F) return swi(bus, 0xFFF2, false, 20);  // SWI3
    int col = op & 0x0F;
    if (op < 0x80 || (op & 0x40) || (col != 0x3 && col != 0xC)) return 2;

    int mode = (op >> 4) & 3;
    uint32_t extra = 0;
    static const uint32_t kCp[4] = {5, 7, 7, 8};
    uint16_t m = mode == 0 ? fetch16(bus) : read16(bus, eaFor(bus, mode, extra));
    sub16(col == 0x3 ? u_ : s_, m);
    return kCp[mode] + extra;
}

// ---------------------------------------------------------------------------
// Page 1. An opcode the manual does not list is an inert 2-cycle step: the byte is
// consumed, matching the disassembler's one-byte skip, and nothing else changes.
// ---------------------------------------------------------------------------
uint32_t Cpu6809::page1(Bus& bus, uint8_t op) {
    if (op >= 0x80) return aluBlock(bus, op);

    // The single-operand group: 00-0F direct, 40-4F A, 50-5F B, 60-6F indexed,
    // 70-7F extended. Columns 1, 2, 5 and B are undefined everywhere, E (JMP) only
    // has memory forms.
    uint8_t hiNib = op >> 4;
    if (hiNib == 0x0 || (hiNib >= 0x4 && hiNib <= 0x7)) {
        uint8_t col = op & 0x0F;
        if (col == 0x1 || col == 0x2 || col == 0x5 || col == 0xB) return 2;

        if (hiNib == 0x4 || hiNib == 0x5) {
            if (col == 0xE) return 2;
            uint8_t& acc = hiNib == 0x4 ? a_ : b_;
            uint8_t r = rmw(col, acc);
            if (col != 0xD) acc = r;
            return 2;
        }

        int mode = hiNib == 0x0 ? 1 : hiNib == 0x6 ? 2 : 3;
        uint32_t extra = 0;
        uint16_t ea = eaFor(bus, mode, extra);
        if (col == 0xE) {  // JMP: direct 3, indexed 3+, extended 4
            pc_ = ea;
            return (mode == 3 ? 4 : 3) + extra;
        }
        uint32_t t = (mode == 3 ? 7 : 6) + extra;
        if (col == 0xF) {  // CLR writes without reading
            bus.memWrite(ea, rmw(col, 0));
            return t;
        }
        uint8_t v = bus.memRead(ea);
        uint8_t r = rmw(col, v);
        if (col != 0xD) bus.memWrite(ea, r);  // TST writes nothing back
        return t;
    }

    if (hiNib == 0x2) {  // short branches: 3 cycles, taken or not
        int8_t off = (int8_t)fetch(bus);
        if (cond(op)) pc_ = (uint16_t)(pc_ + off);
        return 3;
    }

    switch (op) {
    case 0x10: return page2(bus, fetch(bus));
    case 0x11: return page3(bus, fetch(bus));
    case 0x12: return 2;  // NOP
    case 0x13:            // SYNC -- see step() for how it ends
        syncing_ = true;
        return 2;
    case 0x16: {  // LBRA
        uint16_t off = fetch16(bus);
        pc_ = (uint16_t)(pc_ + off);
        return 5;
    }
    case 0x17: {  // LBSR
        uint16_t off = fetch16(bus);
        push16(bus, s_, pc_);
        pc_ = (uint16_t)(pc_ + off);
        return 9;
    }
    case 0x19: daa(); return 2;
    case 0x1A: setCc((uint8_t)(cc() | fetch(bus))); return 3;  // ORCC
    case 0x1C: setCc((uint8_t)(cc() & fetch(bus))); return 3;  // ANDCC
    case 0x1D:  // SEX: B's sign into A; N and Z from D, V cleared
        a_ = (b_ & 0x80) ? 0xFF : 0x00;
        setNZ16(d());
        vf_ = false;
        return 2;
    case 0x1E: return tfrExg(fetch(bus), true);   // EXG
    case 0x1F: return tfrExg(fetch(bus), false);  // TFR

    case 0x30: case 0x31: case 0x32: case 0x33: {  // LEAX/LEAY/LEAS/LEAU
        uint32_t extra = 0;
        uint16_t ea = indexedEa(bus, extra);
        switch (op) {
        case 0x30: x_ = ea; zf_ = (ea == 0); break;  // only LEAX and LEAY touch Z
        case 0x31: y_ = ea; zf_ = (ea == 0); break;
        case 0x32: setS(ea); break;
        default:   u_ = ea; break;
        }
        return 4 + extra;
    }
    case 0x34: return pushRegs(bus, fetch(bus), false);  // PSHS
    case 0x35: return pullRegs(bus, fetch(bus), false);  // PULS
    case 0x36: return pushRegs(bus, fetch(bus), true);   // PSHU
    case 0x37: return pullRegs(bus, fetch(bus), true);   // PULU
    case 0x39: pc_ = pull16(bus, s_); return 5;           // RTS
    case 0x3A: x_ = (uint16_t)(x_ + b_); return 3;        // ABX (B unsigned)
    case 0x3B:  // RTI: the recovered E says how much was stacked
        setCc(pull8(bus, s_));
        if (ef_) {
            a_ = pull8(bus, s_);
            b_ = pull8(bus, s_);
            dp_ = pull8(bus, s_);
            x_ = pull16(bus, s_);
            y_ = pull16(bus, s_);
            u_ = pull16(bus, s_);
            pc_ = pull16(bus, s_);
            return 15;
        }
        pc_ = pull16(bus, s_);
        return 6;
    case 0x3C: {  // CWAI: AND the mask into CC, set E, stack everything, then wait.
        // The manual gives CWAI 20 cycles at least, as a whole. 17 are charged here
        // and 3 when an interrupt ends the wait (takeInterrupt), so a wait that ends
        // at once costs exactly 20.
        setCc((uint8_t)(cc() & fetch(bus)));
        ef_ = true;
        pushEntire(bus);
        waiting_ = true;
        return 17;
    }
    case 0x3D: {  // MUL: unsigned A*B into D; Z from D, C = bit 7 of the result
        uint16_t r = (uint16_t)(a_ * b_);
        setD(r);
        zf_ = (r == 0);
        cf_ = (r & 0x80) != 0;
        return 11;
    }
    case 0x3F: return swi(bus, 0xFFFA, true, 19);  // SWI
    default: return 2;
    }
}

// ---------------------------------------------------------------------------
// One instruction.
// ---------------------------------------------------------------------------
StepResult Cpu6809::step(Bus& bus) {
    if (fetchResetVector_) {
        fetchResetVector_ = false;
        pc_ = read16(bus, 0xFFFE);
    }

    bool irq = bus.intPending();

    // SYNC ends when ANY interrupt line is asserted, masked or not. A masked one
    // resumes at the next instruction (2 more cycles, so SYNC costs its listed 4); an
    // enabled one is taken below.
    if (syncing_) {
        if (!nmiPending_ && !firqLine_ && !irq) return {1, RunStatus::Halted};
        syncing_ = false;
        bool taken = nmiPending_ || (firqLine_ && !ff_) || (irq && !if_);
        if (!taken) return {2, RunStatus::Ok};
    }

    // Interrupts, at the instruction boundary, in the manual's priority order.
    if (nmiPending_) {
        nmiPending_ = false;
        return {takeInterrupt(bus, 0xFFFC, true, true), RunStatus::Ok};
    }
    if (firqLine_ && !ff_) return {takeInterrupt(bus, 0xFFF6, false, true), RunStatus::Ok};
    if (irq && !if_) return {takeInterrupt(bus, 0xFFF8, true, false), RunStatus::Ok};

    // CWAI with nothing to take: the frame is stacked, so burn a cycle and wait.
    if (waiting_) return {1, RunStatus::Halted};

    uint32_t t = page1(bus, fetch(bus));
    return {t, halted() ? RunStatus::Halted : RunStatus::Ok};
}

// ---------------------------------------------------------------------------
// SNAPSHOT/RESTORE (DESIGN.md 13): every register, then the hidden state that
// registers() does not show.
// ---------------------------------------------------------------------------
void Cpu6809::serialize(StateWriter& w) const {
    w.u8(a_);
    w.u8(b_);
    w.u8(dp_);
    w.u16(x_);
    w.u16(y_);
    w.u16(u_);
    w.u16(s_);
    w.u16(pc_);
    w.u8(cc());
    w.boolean(waiting_);
    w.boolean(syncing_);
    w.boolean(fetchResetVector_);
    w.boolean(nmiArmed_);
    w.boolean(nmiPending_);
    w.boolean(firqLine_);
}

void Cpu6809::deserialize(StateReader& r) {
    a_ = r.u8();
    b_ = r.u8();
    dp_ = r.u8();
    x_ = r.u16();
    y_ = r.u16();
    u_ = r.u16();
    s_ = r.u16();
    pc_ = r.u16();
    setCc(r.u8());
    waiting_ = r.boolean();
    syncing_ = r.boolean();
    fetchResetVector_ = r.boolean();
    nmiArmed_ = r.boolean();
    nmiPending_ = r.boolean();
    firqLine_ = r.boolean();
}

} // namespace swtpc
