#include "isa/isa.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>

namespace swtpc {
namespace {

// ---------------------------------------------------------------------------
// The Motorola MC6809 instruction set (DESIGN.md 3.0.2).
//
// Source: reference/Motorola MC6809-MC6809E Programming Manual.md -- the opcode
// tables, the page-2 (10) and page-3 (11) prefixes, the TFR/EXG and PSH/PUL
// post-bytes, and the indexed post-byte (Table F-2).
//
// THE SPELLING is Motorola's: `LDA`, `ORB`, `ASLA`, `LDX #F000`. Operands are bare
// hex like the 6800's, plus the 6809's markers:
//
//     <nn        direct (DP:nn). Always marked, because the disassembler has no DP
//                to know what page it is.
//     nnnn       extended. `>nnnn` when the address is below 0100, so it does not
//                read back as direct.
//     n,R        indexed; R is X Y U S. 5-bit, 8-bit or 16-bit offset, the shortest
//                that holds it. `<n,R` forces 8 bits, `>n,R` forces 16, and the
//                disassembler writes the marker only where the shortest form was NOT
//                used, so its text assembles back to the same bytes.
//     nnnn,PCR   PC-relative, shown as the TARGET address, which is what a symbol
//                names.
//     [...]      indirect; `[nnnn]` alone is extended indirect.
//
// Those rules are why the assembler below can reverse the disassembler exactly, and
// tests/test_isa6809.cpp checks that it does, over every opcode and post-byte.
// ---------------------------------------------------------------------------
enum class Mode {
    Inh,      // NOP, ASLA, RTS
    Imm8,     // #nn -- also ANDCC/ORCC/CWAI
    Imm16,    // #nnnn
    Dir,      // <nn
    Idx,      // post-byte (+ 0..2 bytes)
    Ext,      // nnnn
    Rel8,     // short branch
    Rel16,    // long branch
    Pair,     // TFR/EXG register pair
    ListS,    // PSHS/PULS register list (U in bit 6)
    ListU,    // PSHU/PULU register list (S in bit 6)
    Ill,
    Count
};

struct Op {
    std::string mnem;
    Mode mode = Mode::Ill;
};

using Page = std::array<Op, 256>;

struct Tables {
    Page p1, p2, p3;

    Tables() {
        // The single-operand group: direct 0x, accumulator 4x/5x, indexed 6x, extended 7x.
        static const std::pair<const char*, int> rmw[] = {
            {"NEG", 0x0}, {"COM", 0x3}, {"LSR", 0x4}, {"ROR", 0x6}, {"ASR", 0x7}, {"ASL", 0x8},
            {"ROL", 0x9}, {"DEC", 0xA}, {"INC", 0xC}, {"TST", 0xD}, {"CLR", 0xF},
        };
        for (const auto& [m, c] : rmw) {
            set(p1, 0x00 + c, m, Mode::Dir);
            set(p1, 0x40 + c, std::string(m) + "A", Mode::Inh);
            set(p1, 0x50 + c, std::string(m) + "B", Mode::Inh);
            set(p1, 0x60 + c, m, Mode::Idx);
            set(p1, 0x70 + c, m, Mode::Ext);
        }
        set(p1, 0x0E, "JMP", Mode::Dir);
        set(p1, 0x6E, "JMP", Mode::Idx);
        set(p1, 0x7E, "JMP", Mode::Ext);

        set(p1, 0x12, "NOP", Mode::Inh);
        set(p1, 0x13, "SYNC", Mode::Inh);
        set(p1, 0x16, "LBRA", Mode::Rel16);
        set(p1, 0x17, "LBSR", Mode::Rel16);
        set(p1, 0x19, "DAA", Mode::Inh);
        set(p1, 0x1A, "ORCC", Mode::Imm8);
        set(p1, 0x1C, "ANDCC", Mode::Imm8);
        set(p1, 0x1D, "SEX", Mode::Inh);
        set(p1, 0x1E, "EXG", Mode::Pair);
        set(p1, 0x1F, "TFR", Mode::Pair);

        static const char* br[16] = {"BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
                                     "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE"};
        for (int i = 0; i < 16; ++i) {
            set(p1, 0x20 + i, br[i], Mode::Rel8);
            if (i > 0) set(p2, 0x20 + i, std::string("L") + br[i], Mode::Rel16);
        }

        set(p1, 0x30, "LEAX", Mode::Idx);
        set(p1, 0x31, "LEAY", Mode::Idx);
        set(p1, 0x32, "LEAS", Mode::Idx);
        set(p1, 0x33, "LEAU", Mode::Idx);
        set(p1, 0x34, "PSHS", Mode::ListS);
        set(p1, 0x35, "PULS", Mode::ListS);
        set(p1, 0x36, "PSHU", Mode::ListU);
        set(p1, 0x37, "PULU", Mode::ListU);
        set(p1, 0x39, "RTS", Mode::Inh);
        set(p1, 0x3A, "ABX", Mode::Inh);
        set(p1, 0x3B, "RTI", Mode::Inh);
        set(p1, 0x3C, "CWAI", Mode::Imm8);
        set(p1, 0x3D, "MUL", Mode::Inh);
        set(p1, 0x3F, "SWI", Mode::Inh);

        // 80-FF: immediate / direct / indexed / extended at +00 / +10 / +20 / +30.
        static const char* aSide[16] = {"SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",
                                        "EORA", "ADCA", "ORA",  "ADDA", "CMPX", "JSR",  "LDX", "STX"};
        static const char* bSide[16] = {"SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",
                                        "EORB", "ADCB", "ORB",  "ADDB", "LDD",  "STD",  "LDU", "STU"};
        for (int c = 0; c < 16; ++c) {
            bool wide = c == 0x3 || c == 0xC || c == 0xE;
            bool store = c == 0x7 || c == 0xD || c == 0xF;
            quad(p1, 0x80 + c, aSide[c], store ? Mode::Ill : wide ? Mode::Imm16 : Mode::Imm8);
            quad(p1, 0xC0 + c, bSide[c], store ? Mode::Ill : wide ? Mode::Imm16 : Mode::Imm8);
        }
        set(p1, 0x8D, "BSR", Mode::Rel8);  // the A-side immediate slot of JSR

        set(p2, 0x3F, "SWI2", Mode::Inh);
        quad(p2, 0x83, "CMPD", Mode::Imm16);
        quad(p2, 0x8C, "CMPY", Mode::Imm16);
        quad(p2, 0x8E, "LDY", Mode::Imm16);
        quad(p2, 0x8F, "STY", Mode::Ill);
        quad(p2, 0xCE, "LDS", Mode::Imm16);
        quad(p2, 0xCF, "STS", Mode::Ill);

        set(p3, 0x3F, "SWI3", Mode::Inh);
        quad(p3, 0x83, "CMPU", Mode::Imm16);
        quad(p3, 0x8C, "CMPS", Mode::Imm16);
    }

    static void set(Page& p, int op, const std::string& m, Mode mode) { p[op] = {m, mode}; }

    // One column of the 80-FF block: the immediate slot takes `imm` (Ill for a store),
    // then direct, indexed and extended.
    static void quad(Page& p, int immOp, const char* m, Mode imm) {
        if (imm != Mode::Ill) set(p, immOp, m, imm);
        set(p, immOp + 0x10, m, Mode::Dir);
        set(p, immOp + 0x20, m, Mode::Idx);
        set(p, immOp + 0x30, m, Mode::Ext);
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

// An operand in the requested base (the same rule as the 6800 decoder): `digits` is
// the hex width; octal renders a byte as three digits and a word as split octal.
std::string fmtNum(unsigned v, int digits, int base) {
    char b[16];
    if (base == 8) {
        if (digits <= 2)
            std::snprintf(b, sizeof b, "%03o", v & 0xFF);
        else
            std::snprintf(b, sizeof b, "%03o %03o", (v >> 8) & 0xFF, v & 0xFF);
        return b;
    }
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (int i = digits - 1; i >= 0; --i) s += d[(v >> (i * 4)) & 0xF];
    return s;
}

// A signed offset: a minus sign and the magnitude.
std::string fmtSigned(int v, int digits, int base) {
    return v < 0 ? "-" + fmtNum((unsigned)-v, digits, base) : fmtNum((unsigned)v, digits, base);
}

bool fits5(int v) { return v >= -16 && v <= 15; }
bool fits8(int v) { return v >= -128 && v <= 127; }

const char* kIdxReg[4] = {"X", "Y", "U", "S"};

// TFR/EXG register codes (the manual, Figure 2-1A). Empty = undefined.
const char* kPairReg[16] = {"D", "X", "Y", "U", "S", "PC", "", "", "A", "B", "CC", "DP", "", "", "", ""};

// PSH/PUL bits, low to high (the manual, Figure 2-1B). Bit 6 is the OTHER stack.
const char* kListReg[8] = {"CC", "A", "B", "DP", "X", "Y", "?", "PC"};

class Isa6809 : public Disassembler {
public:
    const char* name() const override { return "6809"; }
    int maxLen() const override { return 5; }

    Insn at(uint16_t addr, const PeekFn& peek, int base) const override {
        const Tables& t = tables();
        Insn in;
        uint16_t p = addr;
        uint8_t opc = peek(p++);
        const Page* page = &t.p1;
        if (opc == 0x10 || opc == 0x11) {
            page = opc == 0x10 ? &t.p2 : &t.p3;
            uint8_t op2 = peek(p++);
            if ((*page)[op2].mode == Mode::Ill) {
                // A prefix and an opcode it does not list: the core consumes both.
                in.undocumented = true;
                in.len = 2;
                in.text = "?\?= " + fmtNum(opc, 2, base) + " " + fmtNum(op2, 2, base);
                return in;
            }
            opc = op2;
        }
        const Op& op = (*page)[opc];
        if (op.mode == Mode::Ill) {
            in.undocumented = true;
            in.len = 1;
            in.text = "?\?= " + fmtNum(opc, 2, base);
            return in;
        }

        auto word = [&](uint16_t a) { return (uint16_t)((peek(a) << 8) | peek((uint16_t)(a + 1))); };
        auto target = [&](uint16_t v) {
            in.operand = v;
            in.operandBits = 16;
        };

        std::string s;
        switch (op.mode) {
        case Mode::Inh: break;
        case Mode::Imm8: s = "#" + fmtNum(peek(p++), 2, base); break;
        case Mode::Imm16: {
            uint16_t v = word(p);
            p += 2;
            target(v);
            s = "#" + fmtNum(v, 4, base);
            break;
        }
        case Mode::Dir: s = "<" + fmtNum(peek(p++), 2, base); break;
        case Mode::Ext: {
            uint16_t v = word(p);
            p += 2;
            target(v);
            s = (v <= 0xFF ? ">" : "") + fmtNum(v, 4, base);
            break;
        }
        case Mode::Rel8: {
            int8_t off = (int8_t)peek(p++);
            uint16_t tgt = (uint16_t)(p + off);
            target(tgt);
            s = fmtNum(tgt, 4, base);
            break;
        }
        case Mode::Rel16: {
            uint16_t off = word(p);
            p += 2;
            uint16_t tgt = (uint16_t)(p + off);
            target(tgt);
            s = fmtNum(tgt, 4, base);
            break;
        }
        case Mode::Pair: {
            uint8_t post = peek(p++);
            const char* r1 = kPairReg[post >> 4];
            const char* r2 = kPairReg[post & 0x0F];
            s = (*r1 && *r2) ? std::string(r1) + "," + r2 : "#" + fmtNum(post, 2, base);
            break;
        }
        case Mode::ListS:
        case Mode::ListU: {
            uint8_t post = peek(p++);
            for (int b = 0; b < 8; ++b) {
                if (!(post & (1 << b))) continue;
                if (!s.empty()) s += ",";
                s += b == 6 ? (op.mode == Mode::ListS ? "U" : "S") : kListReg[b];
            }
            if (s.empty()) s = "#" + fmtNum(post, 2, base);
            break;
        }
        case Mode::Idx:
            if (!indexed(peek, p, base, s, in)) {
                in.undocumented = true;
                s = "?\?= " + fmtNum(peek((uint16_t)(p - 1)), 2, base);
            }
            break;
        case Mode::Ill:
        case Mode::Count:
            break;
        }

        in.len = (uint8_t)(uint16_t)(p - addr);
        in.text = s.empty() ? op.mnem : op.mnem + " " + s;
        return in;
    }

private:
    // Table F-2. Advances `p` past the post-byte and its offset. False for a
    // post-byte the table does not list (the core then consumes the post-byte only).
    static bool indexed(const PeekFn& peek, uint16_t& p, int base, std::string& s, Insn& in) {
        uint8_t post = peek(p++);
        std::string r = kIdxReg[(post >> 5) & 3];
        if (!(post & 0x80)) {
            int off = post & 0x1F;
            if (off & 0x10) off -= 32;
            s = fmtSigned(off, 2, base) + "," + r;
            return true;
        }
        bool ind = (post & 0x10) != 0;
        std::string body;
        switch (post & 0x0F) {
        case 0x0: if (ind) return false; body = "," + r + "+"; break;
        case 0x1: body = "," + r + "++"; break;
        case 0x2: if (ind) return false; body = ",-" + r; break;
        case 0x3: body = ",--" + r; break;
        case 0x4: body = "," + r; break;
        case 0x5: body = "B," + r; break;
        case 0x6: body = "A," + r; break;
        case 0x8: {
            int off = (int8_t)peek(p++);
            body = (!ind && fits5(off) ? "<" : "") + fmtSigned(off, 2, base) + "," + r;
            break;
        }
        case 0x9: {
            int off = (int16_t)((peek(p) << 8) | peek((uint16_t)(p + 1)));
            p += 2;
            body = (fits8(off) ? ">" : "") + fmtSigned(off, 4, base) + "," + r;
            break;
        }
        case 0xB: body = "D," + r; break;
        case 0xC: {
            int off = (int8_t)peek(p++);
            uint16_t tgt = (uint16_t)(p + off);
            in.operand = tgt;
            in.operandBits = 16;
            body = fmtNum(tgt, 4, base) + ",PCR";
            break;
        }
        case 0xD: {
            uint16_t off = (uint16_t)((peek(p) << 8) | peek((uint16_t)(p + 1)));
            p += 2;
            uint16_t tgt = (uint16_t)(p + off);
            in.operand = tgt;
            in.operandBits = 16;
            // The assembler would have used 8 bits (one byte shorter) if it reached.
            bool short8 = fits8((int)tgt - (int)(uint16_t)(p - 1)) ||
                          fits8((int)tgt - (int)(uint16_t)(p - 1) - 0x10000) ||
                          fits8((int)tgt - (int)(uint16_t)(p - 1) + 0x10000);
            body = (short8 ? ">" : "") + fmtNum(tgt, 4, base) + ",PCR";
            break;
        }
        case 0xF: {
            if (post != 0x9F) return false;  // Table F-2 lists 9F and no other
            uint16_t v = (uint16_t)((peek(p) << 8) | peek((uint16_t)(p + 1)));
            p += 2;
            in.operand = v;
            in.operandBits = 16;
            s = "[" + fmtNum(v, 4, base) + "]";
            return true;
        }
        default: return false;  // 7, A, E
        }
        s = ind ? "[" + body + "]" : body;
        return true;
    }
};

const Isa6809 k6809;

// ---------------------------------------------------------------------------
// The assembler: the tables above, reversed. The mode comes from the operand's
// syntax, by the rules in the header comment.
// ---------------------------------------------------------------------------

// Uppercase; drop blanks next to a comma; collapse the rest to one space.
std::string normalize(const std::string& s) {
    std::string out;
    bool pendingSpace = false;
    for (char c0 : s) {
        char c = (char)std::toupper((unsigned char)c0);
        if (c == ' ' || c == '\t') {
            if (!out.empty()) pendingSpace = true;
            continue;
        }
        if (c == ',') {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out += ',';
            pendingSpace = false;
            continue;
        }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += c;
    }
    return out;
}

// A number in `base` unless it carries its own radix ($ or 0x or a trailing H hex,
// a trailing Q/O octal). An optional leading '-' negates it. At most 16 bits.
bool parseNum(std::string s, int base, int& out) {
    bool neg = false;
    if (!s.empty() && s[0] == '-') { neg = true; s = s.substr(1); }
    if (s.empty()) return false;
    if (s[0] == '$') { base = 16; s = s.substr(1); }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'X' || s[1] == 'x')) { base = 16; s = s.substr(2); }
    else {
        char last = (char)std::toupper((unsigned char)s.back());
        if (last == 'H') { base = 16; s.pop_back(); }
        else if (last == 'Q' || last == 'O') { base = 8; s.pop_back(); }
    }
    if (s.empty()) return false;
    long v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        if (d >= base) return false;
        v = v * base + d;
        if (v > 0xFFFF) return false;
    }
    out = neg ? (int)-v : (int)v;
    return true;
}

// A 16-bit value as a signed offset: 8000-FFFF read as negative, so `FFF0,X` and
// `-10,X` are the same offset.
int asSigned16(int v) { return (v >= 0x8000) ? v - 0x10000 : v; }

class Isa6809Assembler : public Assembler {
public:
    Isa6809Assembler() {
        const Tables& t = tables();
        const Page* pages[3] = {&t.p1, &t.p2, &t.p3};
        const uint8_t prefix[3] = {0, 0x10, 0x11};
        for (int pg = 0; pg < 3; ++pg)
            for (int i = 0; i < 256; ++i) {
                const Op& op = (*pages[pg])[i];
                if (op.mode == Mode::Ill) continue;
                Enc& e = forms_[op.mnem][(int)op.mode];
                e.prefix = prefix[pg];
                e.op = (uint8_t)i;
                e.present = true;
            }
    }

    const char* name() const override { return "6809"; }

    AsmResult assemble(uint16_t addr, const std::string& line, int base) const override {
        size_t b0 = line.find_first_not_of(" \t");
        if (b0 == std::string::npos) return fail("empty");
        size_t e0 = line.find_first_of(" \t", b0);
        std::string mnem;
        for (size_t i = b0; i < (e0 == std::string::npos ? line.size() : e0); ++i)
            mnem += (char)std::toupper((unsigned char)line[i]);
        std::string opd = e0 == std::string::npos ? "" : normalize(line.substr(e0));

        // The manual's alternate names for the same opcodes.
        static const std::map<std::string, std::string> alias = {
            {"LSL", "ASL"},   {"LSLA", "ASLA"}, {"LSLB", "ASLB"}, {"BHS", "BCC"},
            {"BLO", "BCS"},   {"LBHS", "LBCC"}, {"LBLO", "LBCS"},
        };
        if (auto a = alias.find(mnem); a != alias.end()) mnem = a->second;

        auto it = forms_.find(mnem);
        if (it == forms_.end()) return fail("unknown instruction: " + mnem);
        const Forms& f = it->second;
        auto has = [&](Mode m) { return f[(int)m].present; };

        if (opd.empty()) {
            if (has(Mode::Inh)) return emit(f[(int)Mode::Inh], {});
            return fail(mnem + " needs an operand");
        }

        if (has(Mode::Pair)) return pair(f[(int)Mode::Pair], opd, base);
        if (has(Mode::ListS)) return list(f[(int)Mode::ListS], opd, base, "U");
        if (has(Mode::ListU)) return list(f[(int)Mode::ListU], opd, base, "S");

        if (opd[0] == '#') {
            int v;
            if (!parseNum(opd.substr(1), base, v)) return fail("bad operand: " + opd);
            v &= 0xFFFF;
            if (has(Mode::Imm16)) return emit(f[(int)Mode::Imm16], {(uint8_t)(v >> 8), (uint8_t)v});
            if (has(Mode::Imm8)) {
                if (v > 0xFF && v < 0xFF80) return fail("operand too large for an 8-bit immediate");
                return emit(f[(int)Mode::Imm8], {(uint8_t)v});
            }
            return fail(mnem + " has no immediate form");
        }

        if (has(Mode::Rel8) || has(Mode::Rel16)) {
            int tgt;
            if (!parseNum(opd, base, tgt)) return fail("bad branch target: " + opd);
            if (has(Mode::Rel8)) {
                int d = tgt - ((int)addr + 2);
                if (!fits8(d) && !fits8(d - 0x10000) && !fits8(d + 0x10000))
                    return fail("branch out of range (-128..+127)");
                return emit(f[(int)Mode::Rel8], {(uint8_t)d});
            }
            const Enc& e = f[(int)Mode::Rel16];
            int d = tgt - ((int)addr + (e.prefix ? 4 : 3));
            return emit(e, {(uint8_t)(d >> 8), (uint8_t)d});
        }

        // Indexed (anything with a comma, or in brackets).
        if (opd[0] == '[' || opd.find(',') != std::string::npos) {
            if (!has(Mode::Idx)) return fail(mnem + " has no indexed form");
            const Enc& e = f[(int)Mode::Idx];
            std::vector<uint8_t> post;
            std::string err;
            int lenBefore = (e.prefix ? 2 : 1);
            if (!indexed(addr, lenBefore, opd, base, post, err)) return fail(err);
            return emit(e, post);
        }

        // Direct or extended.
        bool forceDir = opd[0] == '<', forceExt = opd[0] == '>';
        std::string num = (forceDir || forceExt) ? opd.substr(1) : opd;
        int v;
        if (!parseNum(num, base, v)) return fail("bad operand: " + opd);
        v &= 0xFFFF;
        if (!forceExt && has(Mode::Dir) && (forceDir || v <= 0xFF)) {
            if (v > 0xFF) return fail("a direct address is 00..FF");
            return emit(f[(int)Mode::Dir], {(uint8_t)v});
        }
        if (has(Mode::Ext)) return emit(f[(int)Mode::Ext], {(uint8_t)(v >> 8), (uint8_t)v});
        return fail(mnem + " takes no memory operand");
    }

private:
    struct Enc {
        uint8_t prefix = 0;
        uint8_t op = 0;
        bool present = false;
    };
    using Forms = std::array<Enc, (int)Mode::Count>;

    static AsmResult ok(std::vector<uint8_t> b) { return {std::move(b), {}}; }
    static AsmResult fail(std::string e) { return {{}, std::move(e)}; }

    static AsmResult emit(const Enc& e, const std::vector<uint8_t>& rest) {
        std::vector<uint8_t> b;
        if (e.prefix) b.push_back(e.prefix);
        b.push_back(e.op);
        b.insert(b.end(), rest.begin(), rest.end());
        return ok(std::move(b));
    }

    static int pairCode(const std::string& r) {
        for (int i = 0; i < 16; ++i)
            if (*kPairReg[i] && r == kPairReg[i]) return i;
        return -1;
    }

    static AsmResult pair(const Enc& e, const std::string& opd, int base) {
        if (opd[0] == '#') {
            int v;
            if (!parseNum(opd.substr(1), base, v) || v < 0 || v > 0xFF) return fail("bad operand: " + opd);
            return emit(e, {(uint8_t)v});
        }
        size_t c = opd.find(',');
        if (c == std::string::npos) return fail("needs two registers: R1,R2");
        int r1 = pairCode(opd.substr(0, c)), r2 = pairCode(opd.substr(c + 1));
        if (r1 < 0 || r2 < 0) return fail("registers are D X Y U S PC A B CC DP");
        return emit(e, {(uint8_t)((r1 << 4) | r2)});
    }

    // `other` is the stack pointer that sits in bit 6 for this instruction.
    static AsmResult list(const Enc& e, const std::string& opd, int base, const char* other) {
        if (opd[0] == '#') {
            int v;
            if (!parseNum(opd.substr(1), base, v) || v < 0 || v > 0xFF) return fail("bad operand: " + opd);
            return emit(e, {(uint8_t)v});
        }
        uint8_t mask = 0;
        size_t start = 0;
        while (start <= opd.size()) {
            size_t c = opd.find(',', start);
            std::string r = opd.substr(start, c == std::string::npos ? std::string::npos : c - start);
            int bit = -1;
            if (r == other) bit = 6;
            else if (r == "D") mask |= 0x06;
            else
                for (int b = 0; b < 8; ++b)
                    if (b != 6 && r == kListReg[b]) bit = b;
            if (bit < 0 && r != "D") return fail("cannot stack " + r + " here");
            if (bit >= 0) mask |= (uint8_t)(1 << bit);
            if (c == std::string::npos) break;
            start = c + 1;
        }
        return emit(e, {mask});
    }

    // Table F-2, reversed. `lenBefore` is the prefix and opcode; the post-byte and any
    // offset follow. PC-relative needs the instruction's end, so it needs both.
    static bool indexed(uint16_t addr, int lenBefore, std::string s, int base,
                        std::vector<uint8_t>& out, std::string& err) {
        bool ind = false;
        if (s[0] == '[') {
            if (s.back() != ']') { err = "missing ]"; return false; }
            ind = true;
            s = s.substr(1, s.size() - 2);
            if (s.find(',') == std::string::npos) {  // [nnnn] -- extended indirect
                int v;
                if (!parseNum(s, base, v)) { err = "bad address: " + s; return false; }
                out = {0x9F, (uint8_t)(v >> 8), (uint8_t)v};
                return true;
            }
        }
        uint8_t indBit = ind ? 0x10 : 0x00;

        size_t c = s.rfind(',');
        std::string left = s.substr(0, c), right = s.substr(c + 1);

        // Auto increment / decrement.
        int code = -1;
        std::string rname = right;
        if (right.size() > 2 && right.compare(right.size() - 2, 2, "++") == 0) { code = 1; rname = right.substr(0, right.size() - 2); }
        else if (right.size() > 1 && right.back() == '+') { code = 0; rname = right.substr(0, right.size() - 1); }
        else if (right.rfind("--", 0) == 0) { code = 3; rname = right.substr(2); }
        else if (right.rfind("-", 0) == 0) { code = 2; rname = right.substr(1); }

        if (rname == "PCR" || rname == "PC") {
            if (code >= 0) { err = "PC cannot auto-increment"; return false; }
            bool f8 = !left.empty() && left[0] == '<', f16 = !left.empty() && left[0] == '>';
            int tgt;
            if (!parseNum((f8 || f16) ? left.substr(1) : left, base, tgt)) {
                err = "bad PC-relative target: " + left;
                return false;
            }
            int d8 = asSigned16((tgt - ((int)addr + lenBefore + 2)) & 0xFFFF);
            if (!f16 && fits8(d8)) {
                out = {(uint8_t)(0x8C | indBit), (uint8_t)d8};
                return true;
            }
            if (f8) { err = "offset out of 8-bit range"; return false; }
            int d16 = (tgt - ((int)addr + lenBefore + 3)) & 0xFFFF;
            out = {(uint8_t)(0x8D | indBit), (uint8_t)(d16 >> 8), (uint8_t)d16};
            return true;
        }

        int rr = -1;
        for (int i = 0; i < 4; ++i)
            if (rname == kIdxReg[i]) rr = i;
        if (rr < 0) { err = "index register is X Y U S or PCR"; return false; }
        uint8_t reg = (uint8_t)(rr << 5);

        if (code >= 0) {
            if (!left.empty()) { err = "auto increment/decrement takes no offset"; return false; }
            if (ind && (code == 0 || code == 2)) { err = "indirect needs ,R++ or ,--R"; return false; }
            out = {(uint8_t)(0x80 | reg | indBit | code)};
            return true;
        }
        if (left.empty()) { out = {(uint8_t)(0x84 | reg | indBit)}; return true; }
        if (left == "A") { out = {(uint8_t)(0x86 | reg | indBit)}; return true; }
        if (left == "B") { out = {(uint8_t)(0x85 | reg | indBit)}; return true; }
        if (left == "D") { out = {(uint8_t)(0x8B | reg | indBit)}; return true; }

        bool f8 = left[0] == '<', f16 = left[0] == '>';
        int v;
        if (!parseNum((f8 || f16) ? left.substr(1) : left, base, v)) { err = "bad offset: " + left; return false; }
        v = asSigned16(v & 0xFFFF);
        if (!f8 && !f16 && !ind && fits5(v)) { out = {(uint8_t)(reg | (v & 0x1F))}; return true; }
        if (!f16 && fits8(v)) { out = {(uint8_t)(0x88 | reg | indBit), (uint8_t)v}; return true; }
        if (f8) { err = "offset out of 8-bit range"; return false; }
        out = {(uint8_t)(0x89 | reg | indBit), (uint8_t)(v >> 8), (uint8_t)v};
        return true;
    }

    std::map<std::string, Forms> forms_;
};

const Isa6809Assembler k6809asm;

} // namespace

const Disassembler* mc6809Disassembler() { return &k6809; }
const Assembler*    mc6809Assembler() { return &k6809asm; }

} // namespace swtpc
