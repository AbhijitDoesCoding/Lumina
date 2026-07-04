#include "lumina/BitIO.hpp"

namespace lumina {

// ============================ BitWriter ====================================

BitWriter::BitWriter(std::ostream& out, std::size_t bufferBytes) : out_(out) {
    buffer_.reserve(bufferBytes == 0 ? 1 : bufferBytes);
}

void BitWriter::flushBuffer() {
    if (!buffer_.empty()) {
        out_.write(reinterpret_cast<const char*>(buffer_.data()),
                   static_cast<std::streamsize>(buffer_.size()));
        if (!out_) {
            throw LuminaError("BitWriter: failed to write to output stream");
        }
        buffer_.clear();
    }
}

void BitWriter::writeBit(unsigned bit) {
    cur_ = static_cast<u8>((cur_ << 1) | (bit & 1u));
    ++nbits_;
    ++totalBits_;
    if (nbits_ == 8) {
        buffer_.push_back(cur_);
        cur_   = 0;
        nbits_ = 0;
        if (buffer_.size() >= buffer_.capacity()) {
            flushBuffer();
        }
    }
}

void BitWriter::writeBits(u64 bits, unsigned count) {
    // Emit most-significant of the low `count` bits first.
    for (unsigned i = count; i-- > 0;) {
        writeBit(static_cast<unsigned>((bits >> i) & 1u));
    }
}

u64 BitWriter::finish() {
    if (finished_) {
        return totalBits_;
    }
    // Zero-pad the trailing partial byte, if any.
    if (nbits_ > 0) {
        cur_ = static_cast<u8>(cur_ << (8 - nbits_));
        buffer_.push_back(cur_);
        cur_   = 0;
        nbits_ = 0;
    }
    flushBuffer();
    out_.flush();
    finished_ = true;
    return totalBits_;
}

BitWriter::~BitWriter() {
    // Defensive flush: if the caller forgot finish() we still emit buffered
    // data. Exceptions must not escape a destructor, so we swallow them.
    try {
        if (!finished_) {
            finish();
        }
    } catch (...) {
        // Nothing safe to do here.
    }
}

// ============================ BitReader ====================================

BitReader::BitReader(std::istream& in, std::size_t bufferBytes) : in_(in) {
    buffer_.resize(bufferBytes == 0 ? 1 : bufferBytes);
}

bool BitReader::refill() {
    if (exhausted_) {
        return false;
    }
    in_.read(reinterpret_cast<char*>(buffer_.data()),
             static_cast<std::streamsize>(buffer_.size()));
    const std::streamsize got = in_.gcount();
    if (got <= 0) {
        exhausted_ = true;
        return false;
    }
    pos_ = 0;
    len_ = static_cast<std::size_t>(got);
    if (in_.eof()) {
        // Last physical block; further refills will yield nothing.
        exhausted_ = true;
    }
    return true;
}

unsigned BitReader::readBit() {
    if (nbits_ == 0) {
        if (pos_ >= len_ && !refill()) {
            throw LuminaError("BitReader: unexpected end of bit stream");
        }
        cur_   = buffer_[pos_++];
        nbits_ = 8;
    }
    --nbits_;
    return (cur_ >> nbits_) & 1u;
}

u64 BitReader::readBits(unsigned count) {
    u64 v = 0;
    for (unsigned i = 0; i < count; ++i) {
        v = (v << 1) | readBit();
    }
    return v;
}

}  // namespace lumina
