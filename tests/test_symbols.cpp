#include "test.h"

#include "core/symbols.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

using namespace swtpc;

static std::span<const uint8_t> sv(const std::string& s) {
    return std::span<const uint8_t>((const uint8_t*)s.data(), s.size());
}

// One line of a line-numbered Motorola listing (as0/as9, and the SWTPC/Altair-680 ROM
// listings): a 4-digit line number, the 4-hex address/value field (empty on a comment
// line), the object bytes, and the ORIGINAL SOURCE placed at the label column `lc`. A
// label-less line passes a `src` that begins with blanks, so its column `lc` is empty.
static std::string L(int lineNo, const std::string& addr, const std::string& obj,
                     const std::string& src, size_t lc) {
    std::ostringstream s;
    s << std::setw(4) << std::setfill('0') << lineNo << ' ';   // cols 0-4
    std::string body = (addr.empty() ? "    " : addr) + " " + obj;  // addr at 5, obj at 10
    while (5 + body.size() < lc) body += ' ';                  // pad to the label column
    s << body << src << "\r\n";
    return s.str();
}

// One line of a bare disassembly listing (the KCACR ROM's shape): the address in column 1,
// no line number, the source at column `lc`.
static std::string D(const std::string& addr, const std::string& obj,
                     const std::string& src, size_t lc) {
    std::string body = (addr.empty() ? std::string(4, ' ') : addr) + " " + obj;
    while (body.size() < lc) body += ' ';
    return body + src + "\r\n";
}

void test_symbols() {
    SECTION("symbol tables (DESIGN.md 10.3.2)");

    // ---- a Motorola listing: an EQU is a CONSTANT, a code/data label is an ADDRESS ----
    {
        // ACIAS/TDRE are EQUs (ACIAS's value is an I/O address, but it is still a constant);
        // START and MSG are real labels. The label column is 24 here -- deliberately not the
        // column another listing uses -- to prove the parser DETECTS it rather than assuming.
        const size_t LC = 24;
        std::string p =
            L(10, "8004", "",         "ACIAS   EQU     $8004", LC) +
            L(11, "0002", "",         "TDRE    EQU     $02",   LC) +
            L(16, "0100", "",         "        ORG     $0100", LC) +   // no label
            L(18, "0100", "8e 01 3c", "START   LDS     #STACK", LC) +
            L(19, "0103", "a6 00",    "        LDAA    0,X",   LC) +   // no label
            L(20, "011d", "48 45 4c", "MSG     FCC     /HEL/", LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.LST", t, true, st, err), "a good listing loads");
        CHECK(st.added == 4, "four symbols: two EQUs and two labels");
        uint32_t v = 0;
        CHECK(t.lookup("START", v) && v == 0x0100, "START -> 0100");
        CHECK(t.lookup("ACIAS", v) && v == 0x8004, "the EQU ACIAS -> 8004");
        CHECK(t.lookup("tdre", v) && v == 0x0002, "lookup is case-insensitive");
        CHECK(t.lookup("MSG", v) && v == 0x011D, "the data label MSG -> 011D");

        // The reverse map holds the LABELS and NOT the EQUs -- so 8004 never renders as ACIAS.
        CHECK(t.byAddr.count(0x0100) == 1, "the label START is in the reverse map");
        CHECK(t.byAddr.count(0x011D) == 1, "the label MSG is in the reverse map");
        CHECK(t.byAddr.count(0x8004) == 0, "the EQU ACIAS is NOT -- 8004 is not a label");
        CHECK(t.byAddr.count(0x0002) == 0, "nor is the bit mask TDRE");

        // labelsAt (headers) vs operandName (referenced values).
        CHECK(t.labelsAt(0x0100).size() == 1 && t.labelsAt(0x0100)[0] == "START",
              "labelsAt heads the line with a real label");
        CHECK(t.labelsAt(0x8004).empty(), "an EQU value is not a line header");

        // An operand names the value it points at. A real label wins; failing that, an EQU
        // that is really an address still names it -- what makes `LDAB ACIAS` read as a name.
        CHECK(t.operandName(0x0100) == "START", "an operand at a label reads as the label");
        CHECK(t.operandName(0x8004) == "ACIAS", "an operand at an EQU-address reads as the EQU");
        CHECK(t.operandName(0x9999).empty(), "an operand with no matching symbol stays a number");
    }

    // ---- operandName prefers a real label to an EQU that shares its value ----
    {
        const size_t LC = 24;
        std::string p =
            L(1, "0100", "8e 01 3c", "START   LDS     #STACK", LC) +
            L(2, "0100", "",         "PAGE1   EQU     $0100",  LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.LST", t, true, st, err), "loads");
        CHECK(t.operandName(0x0100) == "START", "a label beats a same-valued EQU");
    }

    // ---- a label standing alone is `LABEL EQU *`, an ADDRESS, and heads its line ----
    {
        // as0 reads a lone label as EQU * -- the current address -- so it is a real address.
        const size_t LC = 24;
        std::string p =
            L(40, "013c", "", "STACK", LC) +
            L(41, "013c", "3e", "DONE    WAI", LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.LST", t, true, st, err), "loads");
        CHECK(t.byAddr.count(0x013C) >= 1, "a lone label is a real address");
        uint32_t v = 0;
        CHECK(t.lookup("STACK", v) && v == 0x013C, "and resolves");
    }

    // ---- the column is detected per file: a different label column still parses ----
    {
        const size_t LC = 31;   // as0's actual default, not the 24 used above
        std::string p =
            L(18, "0100", "8e 01 3c", "START   LDS     #STACK", LC) +
            L(19, "0100", "",         "*  a comment sits at the label column too", LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "wide.LST", t, true, st, err), "loads at a wider label column");
        uint32_t v = 0;
        CHECK(t.lookup("START", v) && v == 0x0100, "START parses at column 31");
        CHECK(st.added == 1, "the comment line adds nothing");
    }

    // ---- the bare-disassembly shape (KCACR): address in column 1, no line number ----
    {
        const size_t LC = 24;
        std::string p =
            D("",     "",         "*  680 monitor equates", LC) +
            D("ff62", "",         "BADDR   equ     $FF62", LC) +
            D("ff81", "20 0a",    "OUTCH   bra     $FF8D", LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "kcacr.LST", t, true, st, err), "a disassembly listing loads");
        CHECK(st.added == 2, "the two labelled lines; the comment adds nothing");
        uint32_t v = 0;
        CHECK(t.lookup("BADDR", v) && v == 0xFF62, "the lowercase equ is a constant, and resolves");
        CHECK(t.byAddr.count(0xFF62) == 0, "the equ is not in the reverse map");
        CHECK(t.byAddr.count(0xFF81) == 1, "OUTCH is a real address");
    }

    // ---- a comment, an ORG, and a label-less line yield nothing ----
    {
        const size_t LC = 24;
        std::string p =
            L(1, "",     "", "*  a banner comment", LC) +
            L(2, "0100", "", "        ORG     $0100", LC) +
            L(3, "0100", "8e 01 3c", "        LDS     #STACK", LC);
        SymbolTable t;
        SymbolTable::LoadStats st;
        std::string err;
        CHECK(loadPrn(sv(p), "x.LST", t, true, st, err), "loads");
        CHECK(st.added == 0, "no label anywhere -> no symbol");
    }

    // ---- merge is the default; REPLACE and CLEAR ----
    {
        const size_t LC = 24;
        SymbolTable t;
        SymbolTable::LoadStats a;
        std::string err;
        loadPrn(sv(L(1, "e000", "fe a0 00", "MONIT   LDX     $A000", LC)), "rom.LST", t, true, a, err);
        CHECK(t.size() == 1 && t.loadOrder.size() == 1, "first file: one symbol, one source");

        // Merge a second file. A collision is COUNTED and the newest wins.
        SymbolTable::LoadStats b;
        std::string p2 = L(1, "0100", "8e 01 3c", "START   LDS     #STACK", LC) +
                         L(2, "e000", "01",       "MONIT   NOP", LC);
        loadPrn(sv(p2), "prog.LST", t, /*replace=*/false, b, err);
        uint32_t v = 0;
        CHECK(t.size() == 2, "merge added START, kept MONIT");
        CHECK(b.redefined == 1 && b.redefinedNames[0] == "MONIT", "the redefinition is reported");
        CHECK(t.lookup("MONIT", v) && v == 0xE000, "and the newest MONIT wins");
        CHECK(t.loadOrder.size() == 2, "both sources are remembered for CONFIG SAVE");

        // REPLACE clears first.
        SymbolTable::LoadStats c;
        loadPrn(sv(L(1, "0200", "39", "ONLY    RTS", LC)), "only.LST", t, /*replace=*/true, c, err);
        CHECK(t.size() == 1 && t.loadOrder.size() == 1, "REPLACE cleared the table and the sources");
        CHECK(!t.lookup("START", v), "the merged symbols are gone");
    }
}
