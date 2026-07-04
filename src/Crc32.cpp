#include "lumina/Crc32.hpp"

#include <array>

namespace lumina {
namespace {

// Build the 256-entry lookup table once at static-init time.
std::array<u32, 256> makeTable() noexcept {
    std::array<u32, 256> table{};
    for (u32 n = 0; n < 256; ++n) {
        u32 c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[n] = c;
    }
    return table;
}

const std::array<u32, 256>& table() noexcept {
    static const std::array<u32, 256> t = makeTable();
    return t;
}

}  // namespace

void Crc32::update(const void* data, std::size_t len) noexcept {
    const auto* p = static_cast<const u8*>(data);
    const auto& t = table();
    u32 c = state_;
    for (std::size_t i = 0; i < len; ++i) {
        c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    state_ = c;
}

}  // namespace lumina
