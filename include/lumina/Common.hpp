#ifndef LUMINA_COMMON_HPP
#define LUMINA_COMMON_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace lumina {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Number of distinct byte values a symbol alphabet can take.
inline constexpr int kSymbolCount = 256;

// I/O chunk size for streaming. Chosen large enough to amortize syscall cost
// but small enough to keep the resident set flat for arbitrarily large files.
inline constexpr std::size_t kChunkSize = 1u << 20;  // 1 MiB

// Selected compression algorithm, stored in the container header.
enum class Algorithm : u8 {
    Huffman = 1,
    Lzw     = 2,
};

// All recoverable failures in the codec/CLI paths throw this. main() maps it to
// a non-zero exit code and a human-readable message.
class LuminaError : public std::runtime_error {
public:
    explicit LuminaError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace lumina

#endif  // LUMINA_COMMON_HPP
