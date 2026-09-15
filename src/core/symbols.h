#pragma once
//
// Symbol tables for the debugger (DESIGN.md 10.3.2) -- ONE implementation, three
// front ends: the SYMBOLS command, the TOML `startup` that re-loads a file at boot,
// and the MCP symbols tool. A file on disk is parsed by the same code whichever door
// it comes through, so the three cannot drift. Mirrors core/hex.h on purpose.
//
// A symbol is HOST-SIDE STATE, like a breakpoint -- it survives RESET and POWER and
// belongs to no board, because it is the operator's view OF the address space, not a
// fact IN it (DESIGN.md 10.3.2, and machine.h's "no machine-level board state" rule
// does not reach it for exactly that reason).
//
// THE FILE IS A MOTOROLA ASSEMBLER LISTING (.LST or .PRN). It is what `as0`/`as9` write
// with `-l`, and the SWTPC/Altair-680 ROM listings in the tree are the same geometry:
//
//   [line#] address [object bytes]  LABEL  OP  OPERAND  [comment]
//
// The address is four hex digits; an EQU puts its value in that same field. A label heads
// the source column and a `*` (or `;`) begins a comment. loadPrn tells a program LABEL from
// a CONSTANT by the directive that follows the label -- EQU/SET/= is a constant, anything
// else names a real address -- and only real labels feed the reverse (address->name) map.
// The two columns (where the address starts, where the source starts) vary by assembler and
// options, so they are DETECTED per file rather than assumed.

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace swtpc {

struct SymbolTable {
    struct Sym {
        uint32_t value = 0;
        bool isAddr = false;    // a program label (feeds byAddr); false for an EQU
        std::string source;     // the file it came from -- SHOW SYMBOLS names it
    };

    // Forward: EVERY symbol, keyed by UPPERCASED name. This is what a reference resolves
    // through, so it is case-insensitive the way a register name is (expr.cpp upcases too).
    std::map<std::string, Sym> byName;

    // Reverse: address -> name, LABELS ONLY. A multimap because two source labels can land
    // on one address (a label standing just before another) and both should show. Read by
    // labelsAt() for a disassembly's leading `NAME:` lines; SHOW SYMBOLS reads byName.
    std::multimap<uint32_t, std::string> byAddr;

    // The files loaded, in load order (deduped). CONFIG SAVE re-emits one SYMBOLS LOAD per
    // entry, so it round-trips the FILENAME, not the parsed table (the builtin: rule).
    std::vector<std::string> loadOrder;

    bool lookup(const std::string& name, uint32_t& out) const;  // case-insensitive
    void clear();
    bool empty() const { return byName.empty(); }
    size_t size() const { return byName.size(); }

    // The program LABELS at an address, for a leading `NAME:` line in a disassembly.
    // byAddr is labels-only by design (an EQU never lands here, so a constant that
    // happens to equal a code address does not print a phantom header), which is
    // exactly what a listing header wants. Empty when nothing is labelled here.
    std::vector<std::string> labelsAt(uint32_t addr) const;

    // The best name for an address an instruction REFERENCES -- the target of a JMP, the
    // pointer in an LDX. A real program label wins outright; failing that, ANY symbol with
    // this value, which is what lets `LDAB ACIAS` read symbolically even though ACIAS is an
    // EQU (the reverse-map rule keeps EQUs out of byAddr, so this asks byName directly).
    // Empty when no symbol has this value. This is name-for-a-*referenced*-value,
    // deliberately more permissive than labelsAt's name-for-*here*.
    std::string operandName(uint32_t value) const;

    // Merge one symbol, maintaining byAddr and counting a redefinition. `isAddr` decides
    // whether it also enters the reverse map. Not usually called directly -- loadPrn does.
    struct LoadStats;
    void put(const std::string& name, uint32_t value, bool isAddr,
             const std::string& source, LoadStats& st);

    // What a load did, for the one-line operator summary.
    struct LoadStats {
        int added = 0;
        int redefined = 0;
        std::vector<std::string> redefinedNames;   // the first few, for the message
    };
};

// Merge a Motorola assembler LISTING into `t`. `source` is the name SHOW SYMBOLS prints.
// `replace` clears the table first. A listing this parser does not recognise simply yields
// no symbols (st.added == 0); the caller says so out loud. There is no hard-error path, so
// `err` is unused and it always returns true (the bool is kept for call-site symmetry).
bool loadPrn(std::span<const uint8_t> text, const std::string& source,
             SymbolTable& t, bool replace, SymbolTable::LoadStats& st, std::string& err);

} // namespace swtpc
