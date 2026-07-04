#ifndef LUMINA_FORMAT_HPP
#define LUMINA_FORMAT_HPP

#include <istream>
#include <ostream>

#include "lumina/Common.hpp"

namespace lumina {

// ---------------------------------------------------------------------------
// Container header — the fixed 24-byte preamble every .lum file begins with.
//
// Layout (all multi-byte integers little-endian for cross-platform stability):
//
//   off  size  field
//   ---  ----  -----------------------------------------------------------
//    0    4    magic  = 'L','M','N','A'
//    4    1    version                    (kFormatVersion)
//    5    1    algorithm                  (Algorithm)
//    6    1    flags                      (bit0 = payload CRC present)
//    7    1    reserved                   (0)
//    8    8    originalSize   (u64)       exact size of the source in bytes
//   16    4    crc32          (u32)       CRC-32 of the original bytes
//   20    4    reserved2      (u32)       (0)  — pads header to 24 bytes
//
// The algorithm-specific model (e.g. the Huffman code-length table) is written
// immediately after this preamble by the codec itself.
// ---------------------------------------------------------------------------

inline constexpr u8 kMagic[4] = {'L', 'M', 'N', 'A'};
inline constexpr u8 kFormatVersion = 1;
inline constexpr std::size_t kHeaderSize = 24;

enum class HeaderFlag : u8 {
    HasCrc = 0x01,
};

struct FileHeader {
    Algorithm algorithm    = Algorithm::Huffman;
    u8        flags        = 0;
    u64       originalSize = 0;
    u32       crc32        = 0;

    bool hasCrc() const noexcept {
        return (flags & static_cast<u8>(HeaderFlag::HasCrc)) != 0;
    }
    void setCrc(u32 value) noexcept {
        crc32 = value;
        flags = static_cast<u8>(flags | static_cast<u8>(HeaderFlag::HasCrc));
    }
};

// Serialize/deserialize the 24-byte preamble. `readHeader` validates the magic
// and version and throws LuminaError on mismatch or short read.
void writeHeader(std::ostream& out, const FileHeader& header);
FileHeader readHeader(std::istream& in);

// --- Little-endian scalar helpers (also used by the codecs for their models) --
void putU16(std::ostream& out, u16 v);
void putU32(std::ostream& out, u32 v);
void putU64(std::ostream& out, u64 v);

u16 getU16(std::istream& in);
u32 getU32(std::istream& in);
u64 getU64(std::istream& in);

}  // namespace lumina

#endif  // LUMINA_FORMAT_HPP
