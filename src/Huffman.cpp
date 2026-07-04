#include "lumina/Huffman.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <queue>
#include <vector>

#include "lumina/BitIO.hpp"
#include "lumina/Crc32.hpp"

namespace lumina {
namespace {

using FreqTable = std::array<u64, kSymbolCount>;
using LenTable  = std::array<u8, kSymbolCount>;  // code length per symbol, 0 = absent

// -------------------------------------------------------------------------
// Pass 1: stream the file, accumulating byte frequencies, total size, and CRC.
// -------------------------------------------------------------------------
struct ScanResult {
    FreqTable freq{};
    u64       total = 0;
    u32       crc   = 0;
};

ScanResult scanFile(const std::string& path, bool computeCrc) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw LuminaError("cannot open input file: " + path);
    }
    ScanResult r;
    Crc32 crc;
    std::vector<char> buf(kChunkSize);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        const auto n = static_cast<std::size_t>(got);
        const auto* p = reinterpret_cast<const u8*>(buf.data());
        for (std::size_t i = 0; i < n; ++i) {
            ++r.freq[p[i]];
        }
        if (computeCrc) crc.update(buf.data(), n);
        r.total += n;
    }
    if (in.bad()) {
        throw LuminaError("I/O error while scanning input file: " + path);
    }
    r.crc = crc.value();
    return r;
}

// -------------------------------------------------------------------------
// Build per-symbol code lengths from frequencies via a standard Huffman tree,
// then reshape to guarantee max length <= kMaxCodeLength (zlib-style fixup).
// -------------------------------------------------------------------------

// Node for the priority-queue tree build. We only need depth (code length),
// never the tree shape, since codes are assigned canonically afterwards.
struct Node {
    u64 weight;
    int left;   // index into node pool, or -1 for a leaf
    int right;
    int symbol; // valid only for leaves, else -1
};

// Assign lengths by frequency to match a target per-length distribution.
// `counts[len]` = how many symbols must receive code length `len`. Symbols are
// handed the shortest codes first, in order of decreasing frequency, so the
// most common bytes get the shortest codes.
void assignLengthsFromCounts(const FreqTable& freq,
                             const std::vector<int>& present,
                             const std::array<u32, kMaxCodeLength + 1>& counts,
                             LenTable& outLen) {
    std::vector<int> order(present);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (freq[static_cast<std::size_t>(a)] != freq[static_cast<std::size_t>(b)])
            return freq[static_cast<std::size_t>(a)] > freq[static_cast<std::size_t>(b)];
        return a < b;  // stable, deterministic tie-break
    });
    std::size_t idx = 0;
    for (unsigned len = 1; len <= kMaxCodeLength; ++len) {
        for (u32 k = 0; k < counts[len]; ++k) {
            outLen[static_cast<std::size_t>(order[idx++])] = static_cast<u8>(len);
        }
    }
}

// Produce the code-length table for the given frequencies.
LenTable buildCodeLengths(const FreqTable& freq) {
    LenTable lengths{};  // all zero

    std::vector<int> present;
    present.reserve(kSymbolCount);
    for (int s = 0; s < kSymbolCount; ++s) {
        if (freq[static_cast<std::size_t>(s)] > 0) present.push_back(s);
    }

    if (present.empty()) {
        return lengths;  // empty file: no codes
    }
    if (present.size() == 1) {
        // A prefix code needs at least one bit even for a lone symbol.
        lengths[static_cast<std::size_t>(present[0])] = 1;
        return lengths;
    }

    // --- Build a Huffman tree with a min-heap keyed on weight. ---
    std::vector<Node> pool;
    pool.reserve(present.size() * 2);
    auto cmp = [&](int a, int b) {
        if (pool[static_cast<std::size_t>(a)].weight !=
            pool[static_cast<std::size_t>(b)].weight)
            return pool[static_cast<std::size_t>(a)].weight >
                   pool[static_cast<std::size_t>(b)].weight;  // min-heap
        return a > b;  // deterministic tie-break by insertion order
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (int s : present) {
        pool.push_back({freq[static_cast<std::size_t>(s)], -1, -1, s});
        pq.push(static_cast<int>(pool.size()) - 1);
    }
    while (pq.size() > 1) {
        const int a = pq.top(); pq.pop();
        const int b = pq.top(); pq.pop();
        pool.push_back({pool[static_cast<std::size_t>(a)].weight +
                            pool[static_cast<std::size_t>(b)].weight,
                        a, b, -1});
        pq.push(static_cast<int>(pool.size()) - 1);
    }
    const int root = pq.top();

    // --- Compute natural depths via an explicit stack (no recursion limits). ---
    std::vector<u32> rawLen(kSymbolCount, 0);
    unsigned maxLen = 0;
    std::vector<std::pair<int, u32>> stack;  // (node, depth)
    stack.emplace_back(root, 0u);
    while (!stack.empty()) {
        const auto [node, depth] = stack.back();
        stack.pop_back();
        const Node& nd = pool[static_cast<std::size_t>(node)];
        if (nd.symbol >= 0) {
            const u32 d = depth == 0 ? 1u : depth;  // guard degenerate single-node
            rawLen[static_cast<std::size_t>(nd.symbol)] = d;
            maxLen = std::max(maxLen, d);
        } else {
            stack.emplace_back(nd.left, depth + 1);
            stack.emplace_back(nd.right, depth + 1);
        }
    }

    // --- Histogram of natural code lengths. ---
    // Natural depth can exceed kMaxCodeLength only for pathological inputs; the
    // limiter below reshapes the histogram so no code exceeds the cap while
    // preserving the Kraft equality sum(2^-len) == 1.
    std::vector<u32> counts(maxLen + 1, 0);
    for (int s : present) counts[rawLen[static_cast<std::size_t>(s)]]++;

    if (maxLen > kMaxCodeLength) {
        // zlib/JPEG-style bit-length adjustment: repeatedly pull an over-long
        // code up to the cap by rebalancing a shorter sibling pair.
        for (unsigned len = maxLen; len > kMaxCodeLength; --len) {
            while (counts[len] > 0) {
                unsigned j = len - 2;
                while (counts[j] == 0) --j;
                counts[len]     -= 2;         // move two leaves up a level...
                counts[len - 1] += 1;         // ...into one node at len-1
                counts[j + 1]   += 2;         // split a shorter node into two
                counts[j]       -= 1;
            }
        }
        maxLen = kMaxCodeLength;
    }

    std::array<u32, kMaxCodeLength + 1> capped{};
    for (unsigned len = 1; len <= maxLen; ++len) capped[len] = counts[len];
    assignLengthsFromCounts(freq, present, capped, lengths);
    return lengths;
}

// -------------------------------------------------------------------------
// Canonical code assignment shared by encoder and decoder: given code lengths,
// derive the first code word of each length. Deterministic on both sides.
// -------------------------------------------------------------------------
struct Canonical {
    std::array<u32, kMaxCodeLength + 2> firstCode{};  // first code word per length
    std::array<u32, kMaxCodeLength + 2> countPerLen{};
    unsigned maxLen = 0;
};

Canonical buildCanonical(const LenTable& lengths) {
    Canonical c;
    for (int s = 0; s < kSymbolCount; ++s) {
        const u8 len = lengths[static_cast<std::size_t>(s)];
        if (len > 0) {
            c.countPerLen[len]++;
            c.maxLen = std::max<unsigned>(c.maxLen, len);
        }
    }
    u32 code = 0;
    for (unsigned len = 1; len <= c.maxLen; ++len) {
        code = (code + c.countPerLen[len - 1]) << 1;
        c.firstCode[len] = code;
    }
    return c;
}

// Encoder-side: absolute code word for each symbol.
struct EncodeTable {
    std::array<u32, kSymbolCount> code{};
    std::array<u8, kSymbolCount>  len{};
};

EncodeTable buildEncodeTable(const LenTable& lengths, const Canonical& c) {
    EncodeTable t;
    t.len = lengths;
    std::array<u32, kMaxCodeLength + 2> next = c.firstCode;
    // Assign in canonical order: ascending length, ascending symbol.
    for (unsigned len = 1; len <= c.maxLen; ++len) {
        for (int s = 0; s < kSymbolCount; ++s) {
            if (lengths[static_cast<std::size_t>(s)] == len) {
                t.code[static_cast<std::size_t>(s)] = next[len]++;
            }
        }
    }
    return t;
}

// -------------------------------------------------------------------------
// Model (de)serialization: a compact list of (symbol, length) pairs.
// -------------------------------------------------------------------------
void writeModel(std::ostream& out, const LenTable& lengths) {
    u16 count = 0;
    for (u8 len : lengths) count = static_cast<u16>(count + (len > 0 ? 1 : 0));
    putU16(out, count);
    for (int s = 0; s < kSymbolCount; ++s) {
        const u8 len = lengths[static_cast<std::size_t>(s)];
        if (len > 0) {
            const u8 pair[2] = {static_cast<u8>(s), len};
            out.write(reinterpret_cast<const char*>(pair), 2);
        }
    }
    if (!out) throw LuminaError("failed to write Huffman model");
}

LenTable readModel(std::istream& in) {
    LenTable lengths{};
    const u16 count = getU16(in);
    if (count > kSymbolCount) {
        throw LuminaError("corrupt Huffman model: symbol count out of range");
    }
    for (u16 i = 0; i < count; ++i) {
        u8 pair[2];
        in.read(reinterpret_cast<char*>(pair), 2);
        if (in.gcount() != 2) throw LuminaError("truncated Huffman model");
        const u8 len = pair[1];
        if (len == 0 || len > kMaxCodeLength) {
            throw LuminaError("corrupt Huffman model: invalid code length");
        }
        lengths[pair[0]] = len;
    }
    return lengths;
}

}  // namespace

// ============================ Public API ===================================

void huffmanCompress(const std::string& inputPath, std::ostream& out,
                     bool computeCrc) {
    const ScanResult scan = scanFile(inputPath, computeCrc);
    const LenTable   lengths = buildCodeLengths(scan.freq);

    FileHeader header;
    header.algorithm    = Algorithm::Huffman;
    header.originalSize = scan.total;
    if (computeCrc) header.setCrc(scan.crc);
    writeHeader(out, header);
    writeModel(out, lengths);

    if (scan.total == 0) {
        return;  // empty input: header + empty model, no payload
    }

    const Canonical   canon = buildCanonical(lengths);
    const EncodeTable enc   = buildEncodeTable(lengths, canon);

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw LuminaError("cannot reopen input file: " + inputPath);

    BitWriter bw(out);
    std::vector<char> buf(kChunkSize);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        const auto* p = reinterpret_cast<const u8*>(buf.data());
        for (std::size_t i = 0; i < static_cast<std::size_t>(got); ++i) {
            const u8 sym = p[i];
            bw.writeBits(enc.code[sym], enc.len[sym]);
        }
    }
    if (in.bad()) throw LuminaError("I/O error while encoding: " + inputPath);
    bw.finish();
}

void huffmanDecompress(std::istream& in, const FileHeader& header,
                       std::ostream& out) {
    const LenTable  lengths = readModel(in);
    const Canonical canon   = buildCanonical(lengths);

    // Canonical decode support: symbols sorted by (length, symbol) and, per
    // length, the index where that length's block begins in `sorted`.
    std::vector<int> sorted;
    sorted.reserve(kSymbolCount);
    std::array<u32, kMaxCodeLength + 2> firstIndex{};
    u32 running = 0;
    for (unsigned len = 1; len <= canon.maxLen; ++len) {
        firstIndex[len] = running;
        running += canon.countPerLen[len];
        for (int s = 0; s < kSymbolCount; ++s) {
            if (lengths[static_cast<std::size_t>(s)] == len) sorted.push_back(s);
        }
    }

    Crc32 crc;
    const bool verify = header.hasCrc();
    std::vector<u8> outBuf;
    outBuf.reserve(kChunkSize);
    auto flush = [&]() {
        if (!outBuf.empty()) {
            out.write(reinterpret_cast<const char*>(outBuf.data()),
                      static_cast<std::streamsize>(outBuf.size()));
            if (!out) throw LuminaError("failed to write decompressed output");
            if (verify) crc.update(outBuf.data(), outBuf.size());
            outBuf.clear();
        }
    };

    if (header.originalSize == 0) {
        // Nothing to decode. (An all-one-symbol file still has originalSize>0.)
    } else if (canon.maxLen == 0) {
        throw LuminaError("corrupt archive: non-empty payload with empty model");
    } else {
        BitReader br(in);
        for (u64 produced = 0; produced < header.originalSize; ++produced) {
            u32 code = 0;
            unsigned len = 0;
            int symbol = -1;
            // Walk bits until the accumulated code falls inside a length block.
            while (len < canon.maxLen) {
                code = (code << 1) | br.readBit();
                ++len;
                const u32 count = canon.countPerLen[len];
                if (count > 0) {
                    const u32 offset = code - canon.firstCode[len];
                    if (offset < count) {
                        symbol = sorted[firstIndex[len] + offset];
                        break;
                    }
                }
            }
            if (symbol < 0) {
                throw LuminaError("corrupt archive: invalid Huffman code");
            }
            outBuf.push_back(static_cast<u8>(symbol));
            if (outBuf.size() >= outBuf.capacity()) flush();
        }
    }
    flush();
    out.flush();

    if (verify && crc.value() != header.crc32) {
        throw LuminaError("integrity check failed: CRC-32 mismatch "
                          "(archive may be corrupt)");
    }
}

}  // namespace lumina
