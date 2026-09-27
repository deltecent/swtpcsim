#include "isa/isa.h"

#include <cctype>

namespace swtpc {

// Each instruction set's tables stay private to its own file; these are the only doors.
const Disassembler* mc6800Disassembler();
const Assembler*    mc6800Assembler();
const Disassembler* mc6809Disassembler();
const Assembler*    mc6809Assembler();

namespace {

std::string lower(const std::string& s) {
    std::string k;
    for (char c : s) k += (char)std::tolower((unsigned char)c);
    return k;
}

} // namespace

// The instruction-set registry -- the one map from an isa name to its
// disassembler/assembler. It answers for "6800" and "6809" and nulls everything
// else -- exactly the contract in isa.h. The caller reports a null and never
// guesses: disassembling one instruction set as another produces plausible,
// WRONG text, which is worse than an error. (A 6809 runs no 6800 binary, so the
// two are not interchangeable even where the mnemonics agree.)
const Disassembler* disassemblerFor(const std::string& isa) {
    std::string k = lower(isa);
    if (k == "6800") return mc6800Disassembler();
    if (k == "6809") return mc6809Disassembler();
    return nullptr;
}

const Assembler* assemblerFor(const std::string& isa) {
    std::string k = lower(isa);
    if (k == "6800") return mc6800Assembler();
    if (k == "6809") return mc6809Assembler();
    return nullptr;
}

std::vector<std::string> instructionSets() { return {"6800", "6809"}; }

} // namespace swtpc
