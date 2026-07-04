#ifndef LUMINA_CRC32_HPP
#define LUMINA_CRC32_HPP

#include <cstddef>

#include "lumina/Common.hpp"

namespace lumina {

// Incremental CRC-32 (IEEE 802.3 / zlib polynomial 0xEDB88320).
//
// Used only for end-to-end integrity verification of the *original* byte
// stream: the compressor folds each chunk in as it streams, stores the final
// value in the header, and the decompressor recomputes and compares. This is a
// checksum, not a cryptographic hash.
class Crc32 {
public:
    Crc32() = default;

    // Fold `len` bytes of `data` into the running checksum.
    void update(const void* data, std::size_t len) noexcept;

    // Current checksum value (post-conditioned; ready to store).
    u32 value() const noexcept { return state_ ^ 0xFFFFFFFFu; }

    void reset() noexcept { state_ = 0xFFFFFFFFu; }

private:
    u32 state_ = 0xFFFFFFFFu;
};

}  // namespace lumina

#endif  // LUMINA_CRC32_HPP
