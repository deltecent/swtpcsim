#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace swtpc {
uint32_t crc32(std::span<const uint8_t> data);
}
