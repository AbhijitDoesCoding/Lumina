#ifndef LUMINA_LZW_HPP
#define LUMINA_LZW_HPP

#include <istream>
#include <ostream>
#include <string>

#include "lumina/Common.hpp"
#include "lumina/Format.hpp"

namespace lumina {

// LZW code-width bounds. Codes start at 9 bits and grow up to 16; when the
// dictionary fills, a CLEAR code resets it and the width drops back to 9. This
// bounds the dictionary at 2^16 entries, keeping memory flat for huge inputs.
inline constexpr unsigned kLzwMinBits = 9;
inline constexpr unsigned kLzwMaxBits = 16;

// Reserved control codes (literals occupy 0..255).
inline constexpr u32 kLzwClear = 256;  // flush + reset dictionary
inline constexpr u32 kLzwEnd   = 257;  // end of stream
inline constexpr u32 kLzwFirst = 258;  // first assignable string code

// ---------------------------------------------------------------------------
// Streaming LZW codec. Effective on data with repeated byte sequences (text,
// logs, tabular data). Single pass in each direction; the dictionary is the
// only state and is capped at 2^kLzwMaxBits entries.
//
// `computeCrc` embeds a CRC-32 of the source. Because LZW encodes in a single
// pass, the CRC is folded in while streaming and then patched back into the
// header (which requires `out` to be a seekable stream).
// ---------------------------------------------------------------------------
void lzwCompress(const std::string& inputPath, std::ostream& out,
                 bool computeCrc);

void lzwDecompress(std::istream& in, const FileHeader& header,
                   std::ostream& out);

}  // namespace lumina

#endif  // LUMINA_LZW_HPP
