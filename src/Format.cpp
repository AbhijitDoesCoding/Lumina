#include "lumina/Format.hpp"

#include <array>

namespace lumina {

// --- Little-endian scalar helpers -------------------------------------------

void putU16(std::ostream& out, u16 v) {
    u8 b[2] = {static_cast<u8>(v), static_cast<u8>(v >> 8)};
    out.write(reinterpret_cast<const char*>(b), 2);
}

void putU32(std::ostream& out, u32 v) {
    u8 b[4] = {static_cast<u8>(v), static_cast<u8>(v >> 8),
               static_cast<u8>(v >> 16), static_cast<u8>(v >> 24)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

void putU64(std::ostream& out, u64 v) {
    u8 b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<u8>(v >> (8 * i));
    }
    out.write(reinterpret_cast<const char*>(b), 8);
}

namespace {
void readExact(std::istream& in, void* dst, std::size_t n, const char* ctx) {
    in.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    if (static_cast<std::size_t>(in.gcount()) != n) {
        throw LuminaError(std::string("truncated input while reading ") + ctx);
    }
}
}  // namespace

u16 getU16(std::istream& in) {
    u8 b[2];
    readExact(in, b, 2, "u16");
    return static_cast<u16>(b[0] | (b[1] << 8));
}

u32 getU32(std::istream& in) {
    u8 b[4];
    readExact(in, b, 4, "u32");
    return static_cast<u32>(b[0]) | (static_cast<u32>(b[1]) << 8) |
           (static_cast<u32>(b[2]) << 16) | (static_cast<u32>(b[3]) << 24);
}

u64 getU64(std::istream& in) {
    u8 b[8];
    readExact(in, b, 8, "u64");
    u64 v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<u64>(b[i]) << (8 * i);
    }
    return v;
}

// --- Header -----------------------------------------------------------------

void writeHeader(std::ostream& out, const FileHeader& header) {
    out.write(reinterpret_cast<const char*>(kMagic), 4);
    const u8 pre[4] = {kFormatVersion,
                       static_cast<u8>(header.algorithm),
                       header.flags,
                       0 /* reserved */};
    out.write(reinterpret_cast<const char*>(pre), 4);
    putU64(out, header.originalSize);
    putU32(out, header.crc32);
    putU32(out, 0);  // reserved2, pads to 24 bytes
    if (!out) {
        throw LuminaError("failed to write container header");
    }
}

FileHeader readHeader(std::istream& in) {
    std::array<u8, 8> pre{};
    readExact(in, pre.data(), pre.size(), "header");
    if (pre[0] != kMagic[0] || pre[1] != kMagic[1] ||
        pre[2] != kMagic[2] || pre[3] != kMagic[3]) {
        throw LuminaError("not a Lumina archive (bad magic bytes)");
    }
    if (pre[4] != kFormatVersion) {
        throw LuminaError("unsupported Lumina format version " +
                          std::to_string(static_cast<int>(pre[4])));
    }

    FileHeader h;
    switch (pre[5]) {
        case static_cast<u8>(Algorithm::Huffman): h.algorithm = Algorithm::Huffman; break;
        case static_cast<u8>(Algorithm::Lzw):     h.algorithm = Algorithm::Lzw;     break;
        default:
            throw LuminaError("unknown algorithm id " +
                              std::to_string(static_cast<int>(pre[5])));
    }
    h.flags        = pre[6];
    // pre[7] reserved
    h.originalSize = getU64(in);
    h.crc32        = getU32(in);
    (void)getU32(in);  // reserved2
    return h;
}

}  // namespace lumina
