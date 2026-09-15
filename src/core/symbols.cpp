#include "core/symbols.h"

#include <cctype>

namespace swtpc {

namespace {

std::string upcase(const std::string& s) {
    std::string u;
    u.reserve(s.size());
    for (char c : s) u += (char)std::toupper((unsigned char)c);
    return u;
}

int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

bool isHex4(const std::string& s, size_t at, uint32_t& out) {
    // Exactly four hex digits at [at, at+4). The .PRN address field is four wide.
    if (at + 4 > s.size()) return false;
    uint32_t v = 0;
    for (size_t i = at; i < at + 4; ++i) {
        int d = hexNib(s[i]);
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    out = v;
    return true;
}

bool isBlank(char c) { return c == ' ' || c == '\t'; }

// The payload of a text file, minus any trailing ^Z (0x1A) an old editor may have left
// on it. Harmless on a listing that has none.
std::span<const uint8_t> untilEof(std::span<const uint8_t> d) {
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i] == 0x1A) return d.subspan(0, i);
    return d;
}

// The Motorola assembler (as0/as9, and the SWTPC/Altair-680 ROM listings it produced)
// opens each line with a four-DIGIT line-number column and a blank. That is what tells
// this geometry apart from a bare disassembly listing, which opens with the address in
// column 1. Digits, not hex: a line number counts in decimal, so `f002` in column 1 is
// an address, never a line number.
bool hasLineNumber(const std::string& s) {
    if (s.size() < 5 || s[4] != ' ') return false;
    for (int i = 0; i < 4; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

size_t firstNonBlank(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i)
        if (!isBlank(s[i])) return i;
    return std::string::npos;
}

// Skip past the address field and any run of "HH " object-byte groups that follow it, to
// where the ORIGINAL SOURCE begins. A label sits exactly here; a mnemonic on a label-less
// line sits further right; so the minimum of this over the file is the label column.
size_t sourceStart(const std::string& s, size_t addrCol) {
    size_t i = addrCol;
    uint32_t v;
    if (isHex4(s, i, v)) i += 4;           // the address/value field
    while (i + 2 < s.size() && s[i] == ' ' &&
           hexNib(s[i + 1]) >= 0 && hexNib(s[i + 2]) >= 0 &&
           (i + 3 >= s.size() || s[i + 3] == ' '))
        i += 3;                            // one " HH" object-byte group
    while (i < s.size() && isBlank(s[i])) ++i;
    return i;
}

// The two columns a Motorola listing is read by: where the 4-hex address/value field
// begins, and where the ORIGINAL SOURCE (a label, or a `*` comment) begins. Both vary by
// assembler and options -- as0's HELLO.LST labels at column 32, the SWIMON ROM listing at
// 29, a Mike-Douglas disassembly (KCACR) at 24 with the address in column 1 -- so both are
// DETECTED per file rather than assumed.
struct ListingShape {
    size_t addrCol = 5;      // 5 with a leading line-number column, else 0
    size_t labelCol = 0;     // detected: the leftmost source column in the file
};

}  // namespace

bool SymbolTable::lookup(const std::string& name, uint32_t& out) const {
    auto it = byName.find(upcase(name));
    if (it == byName.end()) return false;
    out = it->second.value;
    return true;
}

void SymbolTable::clear() {
    byName.clear();
    byAddr.clear();
    loadOrder.clear();
}

std::vector<std::string> SymbolTable::labelsAt(uint32_t addr) const {
    std::vector<std::string> out;
    auto range = byAddr.equal_range(addr & 0xFFFF);
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    return out;
}

std::string SymbolTable::operandName(uint32_t value) const {
    value &= 0xFFFF;

    // A real label wins -- byAddr is labels only, so a hit here is unambiguous.
    auto range = byAddr.equal_range(value);
    if (range.first != range.second) return range.first->second;

    // No label at this address, but a symbol may still name the value -- an EQU that is
    // really an address (ACIAS EQU $8004). byName is ordered by name, so the choice is
    // deterministic when two symbols share a value.
    for (const auto& [name, s] : byName)
        if ((s.value & 0xFFFF) == value) return name;
    return "";
}

void SymbolTable::put(const std::string& rawName, uint32_t value, bool isAddr,
                      const std::string& source, LoadStats& st) {
    std::string name = upcase(rawName);
    if (name.empty()) return;

    auto it = byName.find(name);
    if (it != byName.end()) {
        st.redefined++;
        if (st.redefinedNames.size() < 8) st.redefinedNames.push_back(name);
        // Drop the old reverse entry so a redefined label does not linger at its old
        // address (equal_range because two names can share a value).
        if (it->second.isAddr) {
            auto range = byAddr.equal_range(it->second.value);
            for (auto r = range.first; r != range.second; ++r)
                if (r->second == name) { byAddr.erase(r); break; }
        }
    } else {
        st.added++;
    }

    byName[name] = Sym{value, isAddr, source};
    if (isAddr) byAddr.insert({value, name});
}

// One physical line, with the '\r' dropped and the trailing '\n' consumed. Returns false
// at end of input.
static bool nextLine(std::span<const uint8_t> d, size_t& i, std::string& line) {
    if (i >= d.size()) return false;
    line.clear();
    while (i < d.size() && d[i] != '\n') {
        if (d[i] != '\r') line += (char)d[i];
        ++i;
    }
    if (i < d.size()) ++i;  // consume the '\n'
    return true;
}

// A comment line -- the first source character is a '*' (Motorola) or a ';'. Such a line
// carries no symbol but it DOES sit at the label column, so it helps detect that column.
static bool isCommentAt(const std::string& s, size_t col) {
    return col < s.size() && (s[col] == '*' || s[col] == ';');
}

namespace {

// Read a whole listing to learn its two columns (see ListingShape). The address column is
// 5 when the file carries a leading line-number column (as0/as9, the ROM listings), else 0
// (a bare disassembly listing). The label column is the LEFTMOST place source text starts
// anywhere in the file -- a label or a `*` comment sits there, while a label-less line
// indents its mnemonic further right -- so the minimum over the file finds it.
ListingShape detectShape(std::span<const uint8_t> d) {
    ListingShape sh;
    size_t i = 0, lineNum = 0, nonBlank = 0;
    std::string line;
    while (nextLine(d, i, line)) {
        if (firstNonBlank(line, 0) == std::string::npos) continue;
        ++nonBlank;
        if (hasLineNumber(line)) ++lineNum;
    }
    sh.addrCol = (lineNum * 2 >= nonBlank && lineNum > 0) ? 5 : 0;

    size_t label = std::string::npos;
    i = 0;
    while (nextLine(d, i, line)) {
        size_t cand = std::string::npos;
        uint32_t v;
        if (isHex4(line, sh.addrCol, v))        // a value-bearing line: source after the object
            cand = sourceStart(line, sh.addrCol);
        else {                                  // a comment line: the '*'/';' sits at the label col
            size_t c = firstNonBlank(line, sh.addrCol == 5 ? 5 : 0);
            if (c != std::string::npos && (line[c] == '*' || line[c] == ';')) cand = c;
        }
        if (cand != std::string::npos && cand < label) label = cand;
    }
    sh.labelCol = label == std::string::npos ? 0 : label;
    return sh;
}

}  // namespace

// Merge a Motorola assembler LISTING (as0/as9, or a ROM disassembly in the same shape).
// Each line is `[line#] address [object bytes] LABEL OP OPERAND [comment]`; the address is
// four hex digits, an EQU puts its value in that same field. A label heads the source
// column; a label defined by EQU/SET/= is a CONSTANT (name->value only), any other label is
// a real address that also feeds the reverse map. Yields no symbols on an unrecognised file
// -- never a hard error, so `err` is unused and it always returns true.
bool loadPrn(std::span<const uint8_t> raw, const std::string& source, SymbolTable& t,
             bool replace, SymbolTable::LoadStats& st, std::string& err) {
    (void)err;
    if (replace) t.clear();

    std::span<const uint8_t> d = untilEof(raw);
    ListingShape sh = detectShape(d);

    size_t i = 0;
    std::string line;
    while (nextLine(d, i, line)) {
        // The address/value field. No hex there -- a comment, a continuation line, an ORG
        // with no label -- means no symbol on this line.
        uint32_t value;
        if (!isHex4(line, sh.addrCol, value)) continue;

        // A label is present only when the label column holds source that is not a comment;
        // a label-less line indents its mnemonic past this column, leaving it blank.
        if (sh.labelCol >= line.size() || isBlank(line[sh.labelCol])) continue;
        if (isCommentAt(line, sh.labelCol)) continue;

        // The label is the first whitespace-delimited token there, minus a trailing ':'.
        size_t e = sh.labelCol;
        while (e < line.size() && !isBlank(line[e])) ++e;
        std::string label = line.substr(sh.labelCol, e - sh.labelCol);
        if (!label.empty() && label.back() == ':') label.pop_back();
        if (label.empty()) continue;

        // The directive that follows the label decides label vs constant. EQU/SET/= define a
        // CONSTANT -- its value may be an I/O address as easily as a bit mask, and nothing
        // tells them apart, so it feeds name->value ONLY (never the reverse map). A label on
        // any other line -- code, FCC, FCB, RMB, or standing alone (which the assembler reads
        // as `LABEL EQU *`) -- names a real address.
        size_t ds = firstNonBlank(line, e);
        std::string dir;
        if (ds != std::string::npos) {
            size_t de = ds;
            while (de < line.size() && !isBlank(line[de])) ++de;
            dir = upcase(line.substr(ds, de - ds));
        }
        bool isEqu = dir == "EQU" || dir == "SET" || dir == "=";
        t.put(label, value, /*isAddr=*/!isEqu, source, st);
    }

    if (st.added > 0 || replace) t.loadOrder.push_back(source);
    return true;
}

}  // namespace swtpc
