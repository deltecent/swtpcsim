#include "test.h"

#include "isa/isa.h"

#include <algorithm>
#include <vector>

using namespace swtpc;

// THE 6809 DISASSEMBLER AND ASSEMBLER ARE STATELESS AND NEED NO CPU (DESIGN.md 3.0.2):
// bytes in, text out, and back. The encodings are the manual's opcode tables, the
// TFR/EXG and PSH/PUL post-bytes (Figure 2-1) and the indexed post-byte (Table F-2).

namespace {

Insn dis(const std::vector<uint8_t>& code, uint16_t at = 0x0100, int base = 16) {
    const Disassembler* d = disassemblerFor("6809");
    auto peek = [&](uint16_t a) -> uint8_t {
        uint16_t off = (uint16_t)(a - at);
        return off < code.size() ? code[off] : 0x12;  // NOP past the end
    };
    return d->at(at, peek, base);
}

bool disIs(const std::vector<uint8_t>& code, const char* text, int len, uint16_t at = 0x0100) {
    Insn in = dis(code, at);
    bool ok = in.text == text && in.len == len;
    if (!ok) std::printf("        got \"%s\" len %d\n", in.text.c_str(), in.len);
    return ok;
}

std::vector<uint8_t> as(const char* line, uint16_t at = 0x0100) {
    return assemblerFor("6809")->assemble(at, line).bytes;
}

std::string asErr(const char* line, uint16_t at = 0x0100) {
    return assemblerFor("6809")->assemble(at, line).error;
}

} // namespace

void test_isa6809() {
    SECTION("the 6809 instruction set -- its own decoder, five bytes at most");
    const Disassembler* d = disassemblerFor("6809");
    CHECK(d != nullptr && std::string(d->name()) == "6809", "we speak 6809");
    if (!d) return;
    CHECK(d->maxLen() == 5 && disassemblerFor("6800")->maxLen() == 3, "5 bytes at most, against the 6800's 3");
    CHECK(d != disassemblerFor("6800"), "a decoder of its own -- a 6809 runs no 6800 binary");
    std::vector<std::string> sets = instructionSets();
    CHECK(std::find(sets.begin(), sets.end(), "6809") != sets.end(), "and it is listed");

    SECTION("immediate, direct, extended");
    CHECK(disIs({0x86, 0x41}, "LDA #41", 2), "LDA #41");
    CHECK(disIs({0xCC, 0x12, 0x34}, "LDD #1234", 3), "LDD #1234");
    CHECK(disIs({0x96, 0x40}, "LDA <40", 2), "direct is marked <");
    CHECK(disIs({0xB6, 0x12, 0x34}, "LDA 1234", 3), "extended");
    CHECK(disIs({0xB6, 0x00, 0x40}, "LDA >0040", 3), "extended below 0100 is marked >");
    CHECK(dis({0xB6, 0x12, 0x34}).operand == 0x1234 && dis({0xB6, 0x12, 0x34}).operandBits == 16,
          "the extended address is handed up as a value, for a symbol");
    CHECK(disIs({0x10, 0x8E, 0x12, 0x34}, "LDY #1234", 4), "page 2: LDY #");
    CHECK(disIs({0x11, 0x83, 0x00, 0x01}, "CMPU #0001", 4), "page 3: CMPU #");
    CHECK(disIs({0x10, 0x3F}, "SWI2", 2), "SWI2");
    CHECK(disIs({0x11, 0x3F}, "SWI3", 2), "SWI3");
    CHECK(disIs({0x1C, 0xAF}, "ANDCC #AF", 2), "ANDCC");
    CHECK(disIs({0x3C, 0xEF}, "CWAI #EF", 2), "CWAI");
    CHECK(disIs({0x48}, "ASLA", 1), "ASLA");

    SECTION("indexed -- every Table F-2 form");
    CHECK(disIs({0xA6, 0x84}, "LDA ,X", 2), ",X");
    CHECK(disIs({0xA6, 0xA4}, "LDA ,Y", 2), ",Y");
    CHECK(disIs({0xA6, 0xC4}, "LDA ,U", 2), ",U");
    CHECK(disIs({0xA6, 0xE4}, "LDA ,S", 2), ",S");
    CHECK(disIs({0xA6, 0x1F}, "LDA -01,X", 2), "5-bit, negative");
    CHECK(disIs({0xA6, 0x05}, "LDA 05,X", 2), "5-bit, positive");
    CHECK(disIs({0xA6, 0x88, 0x40}, "LDA 40,X", 3), "8-bit");
    CHECK(disIs({0xA6, 0x88, 0xE0}, "LDA -20,X", 3), "8-bit, negative");
    CHECK(disIs({0xA6, 0x88, 0x05}, "LDA <05,X", 3), "an 8-bit offset that 5 bits would hold is marked <");
    CHECK(disIs({0xA6, 0x89, 0x12, 0x34}, "LDA 1234,X", 4), "16-bit");
    CHECK(disIs({0xA6, 0x89, 0x00, 0x05}, "LDA >0005,X", 4), "a 16-bit offset that 8 would hold is marked >");
    CHECK(disIs({0xA6, 0x86}, "LDA A,X", 2), "A,X");
    CHECK(disIs({0xA6, 0x85}, "LDA B,X", 2), "B,X");
    CHECK(disIs({0xA6, 0x8B}, "LDA D,X", 2), "D,X");
    CHECK(disIs({0xA6, 0x80}, "LDA ,X+", 2), ",X+");
    CHECK(disIs({0xA6, 0x81}, "LDA ,X++", 2), ",X++");
    CHECK(disIs({0xA6, 0x82}, "LDA ,-X", 2), ",-X");
    CHECK(disIs({0xA6, 0x83}, "LDA ,--X", 2), ",--X");
    CHECK(disIs({0xA6, 0x94}, "LDA [,X]", 2), "[,X]");
    CHECK(disIs({0xA6, 0x98, 0x05}, "LDA [05,X]", 3), "[n,X] -- no < needed, there is no 5-bit indirect");
    CHECK(disIs({0xA6, 0x91}, "LDA [,X++]", 2), "[,X++]");
    CHECK(disIs({0xA6, 0x9F, 0x12, 0x34}, "LDA [1234]", 4), "extended indirect");
    CHECK(disIs({0xA6, 0x8C, 0x10}, "LDA 0113,PCR", 3), "n,PCR shows the target");
    CHECK(dis({0xA6, 0x8C, 0x10}).operand == 0x0113, "and hands the target up");
    CHECK(disIs({0xA6, 0x8D, 0x01, 0x00}, "LDA 0204,PCR", 4), "16-bit PCR");
    CHECK(disIs({0xA6, 0x8D, 0x00, 0x05}, "LDA >0109,PCR", 4), "a 16-bit PCR that 8 would reach is marked >");
    CHECK(disIs({0x10, 0xAE, 0x89, 0x12, 0x34}, "LDY 1234,X", 5), "the five-byte instruction");
    Insn bad = dis({0xA6, 0x87});
    CHECK(bad.undocumented && bad.len == 2, "an unlisted post-byte is marked, and is two bytes");
    bad = dis({0xA6, 0x90});
    CHECK(bad.undocumented && bad.len == 2, "[,X+] is not allowed: marked");
    bad = dis({0xA6, 0xFF, 0x12, 0x34});
    CHECK(bad.undocumented && bad.len == 2, "extended indirect is 9F only -- FF is not listed");

    SECTION("branches, register pairs and register lists");
    CHECK(disIs({0x20, 0xFE}, "BRA 0100", 2), "BRA to itself");
    CHECK(disIs({0x10, 0x27, 0x00, 0x10}, "LBEQ 0114", 4), "LBEQ");
    CHECK(disIs({0x16, 0xFF, 0xFD}, "LBRA 0100", 3), "LBRA backward");
    CHECK(disIs({0x17, 0x00, 0x00}, "LBSR 0103", 3), "LBSR");
    CHECK(disIs({0x8D, 0x10}, "BSR 0112", 2), "BSR sits in JSR's immediate slot");
    CHECK(disIs({0x1F, 0x12}, "TFR X,Y", 2), "TFR X,Y");
    CHECK(disIs({0x1E, 0x89}, "EXG A,B", 2), "EXG A,B");
    CHECK(disIs({0x1F, 0x6E}, "TFR #6E", 2), "an undefined register code prints the raw post-byte");
    CHECK(disIs({0x34, 0x16}, "PSHS A,B,X", 2), "PSHS A,B,X");
    CHECK(disIs({0x34, 0x40}, "PSHS U", 2), "bit 6 is U on the S stack");
    CHECK(disIs({0x36, 0x40}, "PSHU S", 2), "and S on the U stack");
    CHECK(disIs({0x35, 0xFF}, "PULS CC,A,B,DP,X,Y,U,PC", 2), "everything");
    CHECK(disIs({0x34, 0x00}, "PSHS #00", 2), "an empty list");

    SECTION("undefined opcodes");
    bad = dis({0x01});
    CHECK(bad.undocumented && bad.len == 1 && bad.text == "?\?= 01", "01: one byte");
    bad = dis({0x10, 0x00});
    CHECK(bad.undocumented && bad.len == 2, "10 00: the prefix and the byte after it");
    bad = dis({0x11, 0x8E});
    CHECK(bad.undocumented && bad.len == 2, "11 8E: likewise");
    bad = dis({0x87});
    CHECK(bad.undocumented && bad.len == 1, "87: there is no STA immediate");

    SECTION("octal operands follow the console base");
    CHECK(dis({0x86, 0x41}, 0x0100, 8).text == "LDA #101", "LDA #101 in octal");

    SECTION("coverage -- every opcode on every page decodes to 1..5 bytes");
    bool lenOk = true;
    for (int pre : {-1, 0x10, 0x11})
        for (int op = 0; op < 256; ++op)
            for (int post : {0x00, 0x84, 0x89, 0x9F}) {
                std::vector<uint8_t> code;
                if (pre >= 0) code.push_back((uint8_t)pre);
                code.push_back((uint8_t)op);
                code.push_back((uint8_t)post);
                code.push_back(0x12);
                code.push_back(0x34);
                Insn in = dis(code);
                if (in.len < 1 || in.len > 5 || in.text.empty()) lenOk = false;
            }
    CHECK(lenOk, "no instruction is empty or longer than five bytes");
}

void test_asm6809() {
    SECTION("the 6809 assembler -- the mode comes from the operand");
    const Assembler* a = assemblerFor("6809");
    CHECK(a != nullptr && std::string(a->name()) == "6809", "we assemble 6809");
    if (!a) return;

    using B = std::vector<uint8_t>;
    CHECK(as("LDA #41") == B({0x86, 0x41}), "immediate");
    CHECK(as("LDX #F000") == B({0x8E, 0xF0, 0x00}), "16-bit immediate, high byte first");
    CHECK(as("LDA <40") == B({0x96, 0x40}), "< forces direct");
    CHECK(as("LDA 40") == B({0x96, 0x40}), "a bare address below 0100 is direct");
    CHECK(as("LDA >40") == B({0xB6, 0x00, 0x40}), "> forces extended");
    CHECK(as("LDA 1234") == B({0xB6, 0x12, 0x34}), "extended");
    CHECK(as("LDY 1234") == B({0x10, 0xBE, 0x12, 0x34}), "the page-2 prefix");
    CHECK(as("CMPS #5") == B({0x11, 0x8C, 0x00, 0x05}), "the page-3 prefix");
    CHECK(as("lsla") == B({0x48}), "LSLA is ASLA, and case does not matter");
    CHECK(as("BHS 0100") == B({0x24, 0xFE}), "BHS is BCC");

    SECTION("indexed");
    CHECK(as("LDA ,X") == B({0xA6, 0x84}), ",X");
    CHECK(as("LDA 0,X") == B({0xA6, 0x00}), "0,X is the 5-bit form");
    CHECK(as("LDA -1,X") == B({0xA6, 0x1F}), "5-bit negative");
    CHECK(as("LDA 10,X") == B({0xA6, 0x88, 0x10}), "10 does not fit 5 bits");
    CHECK(as("LDA <5,X") == B({0xA6, 0x88, 0x05}), "< forces 8 bits");
    CHECK(as("LDA >5,X") == B({0xA6, 0x89, 0x00, 0x05}), "> forces 16 bits");
    CHECK(as("LDA 1234,Y") == B({0xA6, 0xA9, 0x12, 0x34}), "16-bit on Y");
    CHECK(as("LDA A,S") == B({0xA6, 0xE6}), "A,S");
    CHECK(as("LDA ,--U") == B({0xA6, 0xC3}), ",--U");
    CHECK(as("LDA [,X++]") == B({0xA6, 0x91}), "[,X++]");
    CHECK(as("LDA [5,X]") == B({0xA6, 0x98, 0x05}), "an indirect offset is at least 8 bits");
    CHECK(as("JMP [FFFE]") == B({0x6E, 0x9F, 0xFF, 0xFE}), "extended indirect");
    CHECK(as("LEAX 0113,PCR") == B({0x30, 0x8C, 0x10}), "PCR: the operand is the target");
    CHECK(as("LEAX 2000,PCR") == B({0x30, 0x8D, 0x1E, 0xFC}), "a far PCR target takes 16 bits");
    CHECK(!asErr("LDA [,X+]").empty(), "[,X+] is refused");
    CHECK(!asErr("LDA 5,Q").empty(), "an unknown index register is refused");

    SECTION("branches, pairs, lists");
    CHECK(as("LBEQ 0114") == B({0x10, 0x27, 0x00, 0x10}), "LBEQ");
    CHECK(as("LBRA 0100") == B({0x16, 0xFF, 0xFD}), "LBRA backward");
    CHECK(as("BSR 0112") == B({0x8D, 0x10}), "BSR");
    CHECK(!asErr("BRA 0300").empty(), "a short branch out of range is refused");
    CHECK(as("TFR A,DP") == B({0x1F, 0x8B}), "TFR A,DP");
    CHECK(as("EXG D,X") == B({0x1E, 0x01}), "EXG D,X");
    CHECK(as("PSHS D,X") == B({0x34, 0x16}), "D in a list is A and B");
    CHECK(as("PULU S,PC") == B({0x37, 0xC0}), "S on the U stack");
    CHECK(!asErr("PSHS S").empty(), "S cannot go on its own stack");
    CHECK(!asErr("STA #5").empty(), "there is no immediate store");
    CHECK(!asErr("FOO").empty(), "an unknown mnemonic is refused");

    // Every opcode on every page, with a spread of operand bytes -- and for the
    // indexed forms every one of the 256 post-bytes -- must disassemble to text that
    // assembles, and what it assembles to must disassemble to the SAME text and
    // length. That holds the two directions to each other.
    //
    // The bytes themselves come back identical except where the manual gives two
    // encodings one meaning: a PC-relative post-byte ignores its register bits
    // (1XX0110x), so AC, CC and EC read exactly as 8C, and the assembler writes 8C.
    // Every other form must round-trip byte for byte, and that is checked too.
    SECTION("ROUND-TRIP: disassemble(assemble(disassemble(b))) is stable, over the whole map");
    int tried = 0, failed = 0;
    auto trip = [&](const std::vector<uint8_t>& code, uint16_t at) {
        Insn in = dis(code, at);
        if (in.undocumented) return;
        ++tried;
        std::vector<uint8_t> want(code.begin(), code.begin() + in.len);
        AsmResult r = a->assemble(at, in.text);
        Insn back = dis(r.bytes, at);
        size_t idx = (want[0] == 0x10 || want[0] == 0x11) ? 2 : 1;
        bool pcrAlias = want.size() > idx && (want[idx] & 0x8E) == 0x8C && (want[idx] & 0x60) != 0 &&
                        r.bytes.size() == want.size();
        bool same = back.text == in.text && back.len == in.len && (r.bytes == want || pcrAlias);
        if (!same && ++failed <= 10)
            std::printf("        \"%s\" at %04X: %s\n", in.text.c_str(), at, r.error.c_str());
    };
    for (int pre : {-1, 0x10, 0x11})
        for (int op = 0; op < 256; ++op) {
            std::vector<uint8_t> head;
            if (pre >= 0) head.push_back((uint8_t)pre);
            head.push_back((uint8_t)op);
            for (int post = 0; post < 256; ++post)
                for (std::vector<uint8_t> tail : {std::vector<uint8_t>{0x12, 0x34},
                                                  std::vector<uint8_t>{0x00, 0x05},
                                                  std::vector<uint8_t>{0xFF, 0xF0},
                                                  std::vector<uint8_t>{0x80, 0x00}}) {
                    std::vector<uint8_t> code = head;
                    code.push_back((uint8_t)post);
                    code.insert(code.end(), tail.begin(), tail.end());
                    trip(code, 0x1000);
                    trip(code, 0x0010);
                }
        }
    std::printf("        %d instructions round-tripped\n", tried);
    CHECK(tried > 100000 && failed == 0, "every decoded instruction assembles back to its bytes");
}
