#include "lumina/Lzw.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "lumina/BitIO.hpp"
#include "lumina/Crc32.hpp"

namespace lumina {
namespace {

constexpr u32 kMaxEntries = 1u << kLzwMaxBits;  // dictionary ceiling (65536)

// Grow the code width once the dictionary has filled the current width, capping
// at kLzwMaxBits. `bump` selects the encoder rule (grow at 2^width) or the
// decoder's "early change" (grow one entry sooner, since the decoder learns
// each new string one code later than the encoder assigns it).
inline void maybeGrow(u32 nextCode, unsigned& width, bool earlyChange) {
    if (width >= kLzwMaxBits) return;
    const u32 threshold = (1u << width) - (earlyChange ? 1u : 0u);
    if (nextCode >= threshold) ++width;
}

}  // namespace

// ============================ Compression ==================================

void lzwCompress(const std::string& inputPath, std::ostream& out,
                 bool computeCrc) {
    std::error_code ec;
    const auto fsize = std::filesystem::file_size(inputPath, ec);
    if (ec) throw LuminaError("cannot stat input file: " + inputPath);

    FileHeader header;
    header.algorithm    = Algorithm::Lzw;
    header.originalSize  = static_cast<u64>(fsize);
    if (computeCrc) header.setCrc(0);  // real value patched in after streaming
    writeHeader(out, header);

    if (fsize == 0) return;  // empty input: header only

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw LuminaError("cannot open input file: " + inputPath);

    // Dictionary maps a (prefixCode, nextByte) pair to its assigned code.
    std::unordered_map<u32, u32> dict;
    dict.reserve(kMaxEntries * 2);

    Crc32     crc;
    BitWriter bw(out);
    unsigned  width    = kLzwMinBits;
    u32       nextCode = kLzwFirst;

    std::vector<char> buf(kChunkSize);
    bool  haveCurrent = false;
    u32   current     = 0;

    auto readChunk = [&](std::size_t& n) -> const u8* {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        n = static_cast<std::size_t>(in.gcount());
        return reinterpret_cast<const u8*>(buf.data());
    };

    std::size_t n = 0;
    const u8*   p = readChunk(n);
    std::size_t i = 0;
    while (true) {
        if (i >= n) {
            if (computeCrc && n > 0) crc.update(buf.data(), n);
            if (in.bad()) throw LuminaError("I/O error while encoding: " + inputPath);
            p = readChunk(n);
            i = 0;
            if (n == 0) break;  // end of input
        }
        const u8 c = p[i++];
        if (!haveCurrent) {
            current     = c;      // seed with the first literal
            haveCurrent = true;
            continue;
        }
        const u32 key = (current << 8) | c;
        auto it = dict.find(key);
        if (it != dict.end()) {
            current = it->second;                 // extend the current match
        } else {
            bw.writeBits(current, width);          // emit the matched code
            if (nextCode < kMaxEntries) {
                dict.emplace(key, nextCode++);
                maybeGrow(nextCode, width, /*earlyChange=*/false);
            } else {
                bw.writeBits(kLzwClear, width);    // dictionary full: reset
                dict.clear();
                nextCode = kLzwFirst;
                width    = kLzwMinBits;
            }
            current = c;
        }
    }
    // Flush the trailing match and the end marker.
    if (haveCurrent) bw.writeBits(current, width);
    bw.writeBits(kLzwEnd, width);
    bw.finish();

    if (computeCrc) {
        // Patch the CRC field (offset 16) now that we have streamed the source.
        const std::streampos end = out.tellp();
        out.seekp(16, std::ios::beg);
        putU32(out, crc.value());
        out.seekp(end);
        if (!out) throw LuminaError("failed to patch CRC into LZW header");
    }
}

// ============================ Decompression ================================

void lzwDecompress(std::istream& in, const FileHeader& header,
                   std::ostream& out) {
    Crc32      crc;
    const bool verify = header.hasCrc();

    std::vector<u8> outBuf;
    outBuf.reserve(kChunkSize);
    u64 produced = 0;
    auto flush = [&]() {
        if (!outBuf.empty()) {
            out.write(reinterpret_cast<const char*>(outBuf.data()),
                      static_cast<std::streamsize>(outBuf.size()));
            if (!out) throw LuminaError("failed to write decompressed output");
            if (verify) crc.update(outBuf.data(), outBuf.size());
            produced += outBuf.size();
            outBuf.clear();
        }
    };
    auto emit = [&](u8 b) {
        outBuf.push_back(b);
        if (outBuf.size() >= outBuf.capacity()) flush();
    };

    if (header.originalSize == 0) {
        flush();
        out.flush();
        if (verify && crc.value() != header.crc32)
            throw LuminaError("integrity check failed: CRC-32 mismatch");
        return;
    }

    // String table stored as prefix/suffix chains so any code expands in time
    // proportional to its length, with no per-entry string copies.
    std::vector<int> prefix(kMaxEntries, -1);
    std::vector<u8>  suffix(kMaxEntries, 0);
    for (int b = 0; b < kSymbolCount; ++b) suffix[static_cast<std::size_t>(b)] = static_cast<u8>(b);

    std::vector<u8> stack;
    stack.reserve(kMaxEntries);

    // Expand `code` into the output stream; returns the first byte of the string
    // (needed both for KwKwK handling and for the next dictionary entry).
    auto outputCode = [&](u32 code) -> u8 {
        stack.clear();
        int c = static_cast<int>(code);
        while (c >= 0) {
            stack.push_back(suffix[static_cast<std::size_t>(c)]);
            c = prefix[static_cast<std::size_t>(c)];
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) emit(*it);
        return stack.back();  // last pushed == first byte of the string
    };

    BitReader br(in);
    unsigned  width    = kLzwMinBits;
    u32       nextCode = kLzwFirst;
    int       prevCode = -1;

    while (true) {
        const u32 code = static_cast<u32>(br.readBits(width));
        if (code == kLzwEnd) break;
        if (code == kLzwClear) {
            nextCode = kLzwFirst;
            width    = kLzwMinBits;
            prevCode = -1;
            continue;
        }

        if (prevCode < 0) {
            // First code after start or CLEAR: always a literal, and it creates
            // no dictionary entry (the encoder only adds one per matched pair).
            if (code >= kLzwFirst)
                throw LuminaError("corrupt LZW stream: bad initial code");
            outputCode(code);
        } else {
            u8 first;
            if (code < nextCode) {
                first = outputCode(code);
            } else if (code == nextCode) {
                // KwKwK: the code refers to the entry we are about to create.
                const u8 prevFirst = outputCode(static_cast<u32>(prevCode));
                emit(prevFirst);
                first = prevFirst;
            } else {
                throw LuminaError("corrupt LZW stream: code out of range");
            }
            // Add prevString + first as the next dictionary entry.
            if (nextCode < kMaxEntries) {
                prefix[nextCode] = prevCode;
                suffix[nextCode] = first;
                ++nextCode;
                maybeGrow(nextCode, width, /*earlyChange=*/true);
            }
        }
        prevCode = static_cast<int>(code);
    }

    flush();
    out.flush();

    if (produced != header.originalSize)
        throw LuminaError("corrupt LZW stream: output size mismatch");
    if (verify && crc.value() != header.crc32)
        throw LuminaError("integrity check failed: CRC-32 mismatch");
}

}  // namespace lumina
