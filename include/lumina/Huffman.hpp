#ifndef LUMINA_HUFFMAN_HPP
#define LUMINA_HUFFMAN_HPP

#include <istream>
#include <ostream>
#include <string>

#include "lumina/Common.hpp"
#include "lumina/Format.hpp"

namespace lumina {

// Hard cap on canonical code length. Length-limiting the Huffman codes to this
// many bits keeps every code word inside a u32 and enables a compact,
// table-driven canonical decoder. 32 comfortably exceeds the natural Huffman
// depth of any realistic input (a 4 GiB file peaks near 47 bits only under a
// Fibonacci-frequency adversary, which the limiter reshapes down to <= 32).
inline constexpr unsigned kMaxCodeLength = 32;

// ---------------------------------------------------------------------------
// Streaming Huffman codec.
//
// Compression is two-pass over the *file on disk* so the resident set stays
// flat no matter how large the input is:
//   pass 1  — scan bytes to build the frequency model (and CRC/size);
//   pass 2  — re-scan and emit canonical codes through a BitWriter.
// The model persisted to the header is just the 256 code lengths; both sides
// reconstruct identical canonical codes from those lengths alone.
// ---------------------------------------------------------------------------

// Compress `inputPath` into `out` (which must already be positioned at 0 and be
// binary). Writes the full container: header + model + payload. When
// `computeCrc` is set, a CRC-32 of the source is embedded for later verifying.
void huffmanCompress(const std::string& inputPath, std::ostream& out,
                     bool computeCrc);

// Decompress a Huffman payload. `in` must be positioned immediately after the
// 24-byte header (i.e. at the model); `header` is the already-parsed preamble.
// If the header carries a CRC, the reconstructed output is verified against it
// and a mismatch throws LuminaError.
void huffmanDecompress(std::istream& in, const FileHeader& header,
                       std::ostream& out);

}  // namespace lumina

#endif  // LUMINA_HUFFMAN_HPP
