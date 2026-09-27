#pragma once
//
// The SS-30 I/O window -- where an SWTPC I/O board's slot can sit.
//
// The motherboard decodes the window, not the I/O board: eight slots of four
// addresses, slot N at window + 4*N. A 6800 system's MP-B/MP-B2 puts the window at
// $8000. A 6809 system's moves it to $E000 -- the MP-B3, or an MP-B with the
// modification the MP-09 manual gives ("I/O devices at 56K (E000 hex)"). Same board,
// same slot, a different motherboard, so an SS-30 board's `base` may lie in either.

#include <cstdint>

namespace swtpc {

inline bool ss30Slot(long long base) {
    long long window = base & ~0x1FLL;
    return (window == 0x8000 || window == 0xE000) && (base & 0x3) == 0;
}

} // namespace swtpc
