#ifndef LUMINA_BITIO_HPP
#define LUMINA_BITIO_HPP

#include <istream>
#include <ostream>
#include <vector>

#include "lumina/Common.hpp"

namespace lumina {

// ---------------------------------------------------------------------------
// BitWriter — packs variable-length bit codes into a byte stream, MSB-first.
//
// Bits accumulate into a single "current byte"; completed bytes are appended to
// an in-memory buffer that is flushed to the underlying std::ostream in blocks,
// so the syscall/formatting cost is amortized. MSB-first ordering means the
// first bit written lands in bit 7 of the first output byte, which is the
// natural pairing for canonical Huffman codes stored high-bit-first.
// ---------------------------------------------------------------------------
class BitWriter {
public:
    explicit BitWriter(std::ostream& out, std::size_t bufferBytes = kChunkSize);

    // Non-copyable: it owns a flush obligation tied to a specific stream.
    BitWriter(const BitWriter&)            = delete;
    BitWriter& operator=(const BitWriter&) = delete;

    // Write the low `count` bits of `bits`, most-significant of those first.
    // `count` must be in [0, 64]. Codes are stored high-bit-first, so a code
    // value V of length L is emitted as bits V[L-1], V[L-2], ..., V[0].
    void writeBits(u64 bits, unsigned count);

    // Write a single bit (0 or 1).
    void writeBit(unsigned bit);

    // Pad the final partial byte with zero bits and flush everything to the
    // stream. Returns the total number of *payload bits* written (not counting
    // the final zero padding). Must be called exactly once before destruction
    // if any bits were written; the destructor will also flush defensively.
    u64 finish();

    // Total payload bits written so far (excludes not-yet-emitted padding).
    u64 bitsWritten() const noexcept { return totalBits_; }

    ~BitWriter();

private:
    void flushBuffer();

    std::ostream&        out_;
    std::vector<u8>      buffer_;
    u8                   cur_      = 0;  // bits being assembled for current byte
    unsigned             nbits_    = 0;  // number of valid bits in `cur_` [0,8)
    u64                  totalBits_ = 0;
    bool                 finished_ = false;
};

// ---------------------------------------------------------------------------
// BitReader — the inverse: serves individual bits (MSB-first) from a byte
// stream, refilling an in-memory buffer from the underlying std::istream.
// ---------------------------------------------------------------------------
class BitReader {
public:
    explicit BitReader(std::istream& in, std::size_t bufferBytes = kChunkSize);

    BitReader(const BitReader&)            = delete;
    BitReader& operator=(const BitReader&) = delete;

    // Read a single bit. Returns 0 or 1. Throws LuminaError if the stream is
    // exhausted (callers that decode a known symbol count never hit this in a
    // well-formed file; it guards against truncated/corrupt input).
    unsigned readBit();

    // Read `count` bits (0..64), MSB-first, returning them right-aligned.
    u64 readBits(unsigned count);

    // True once no more bits can be produced (buffer drained and stream ended).
    bool eof() const noexcept {
        return exhausted_ && pos_ >= len_ && nbits_ == 0;
    }

private:
    bool refill();

    std::istream&   in_;
    std::vector<u8> buffer_;
    std::size_t     pos_       = 0;  // index of next byte in buffer_
    std::size_t     len_       = 0;  // valid bytes in buffer_
    u8              cur_       = 0;  // current byte being drained
    unsigned        nbits_     = 0;  // remaining valid bits in `cur_`
    bool            exhausted_ = false;
};

}  // namespace lumina

#endif  // LUMINA_BITIO_HPP
