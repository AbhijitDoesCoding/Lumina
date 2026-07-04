// Self-contained round-trip test harness for the Lumina codecs.
//
// Exercises both algorithms across a spread of inputs that target the tricky
// corners: empty files, single-symbol files, tiny alphabets, highly repetitive
// data (LZW dictionary fill + CLEAR), and pseudo-random data (Huffman worst
// case). Each case compresses to a temp file, decompresses back, and asserts
// the bytes are identical and that reported sizes/CRC survive the trip.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "lumina/Common.hpp"
#include "lumina/Format.hpp"
#include "lumina/Huffman.hpp"
#include "lumina/Lzw.hpp"

namespace fs = std::filesystem;
using namespace lumina;

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool cond, const std::string& name) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "  [FAIL] %s\n", name.c_str());
    }
}

fs::path tmpDir() {
    fs::path d = fs::temp_directory_path() / "lumina_tests";
    fs::create_directories(d);
    return d;
}

void writeFile(const fs::path& p, const std::vector<u8>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

std::vector<u8> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<u8>(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
}

// Deterministic pseudo-random generator (xorshift) so tests are reproducible
// without depending on <random> defaults across platforms.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
    }
    u8 byte() { return static_cast<u8>(next() >> 24); }
};

bool roundTrip(Algorithm algo, const std::vector<u8>& original,
               const std::string& label) {
    const fs::path dir = tmpDir();
    const fs::path src = dir / (label + ".src");
    const fs::path arc = dir / (label + ".lum");
    const fs::path out = dir / (label + ".out");

    writeFile(src, original);

    {
        std::ofstream cf(arc, std::ios::binary | std::ios::trunc);
        if (algo == Algorithm::Huffman) huffmanCompress(src.string(), cf, true);
        else                            lzwCompress(src.string(), cf, true);
    }
    {
        std::ifstream af(arc, std::ios::binary);
        const FileHeader h = readHeader(af);
        std::ofstream of(out, std::ios::binary | std::ios::trunc);
        if (h.algorithm == Algorithm::Huffman) huffmanDecompress(af, h, of);
        else                                   lzwDecompress(af, h, of);
    }

    const std::vector<u8> restored = readFile(out);
    const bool ok = (restored == original);
    if (!ok) {
        std::fprintf(stderr, "    (%s/%s: %zu -> %zu bytes, mismatch)\n",
                     label.c_str(),
                     algo == Algorithm::Huffman ? "huffman" : "lzw",
                     original.size(), restored.size());
    }
    return ok;
}

void testInput(const std::vector<u8>& data, const std::string& name) {
    check(roundTrip(Algorithm::Huffman, data, name + "_huf"),
          "huffman/" + name);
    check(roundTrip(Algorithm::Lzw, data, name + "_lzw"),
          "lzw/" + name);
}

}  // namespace

int main() {
    // 1. Empty file.
    testInput({}, "empty");

    // 2. Single byte.
    testInput({0x42}, "single_byte");

    // 3. One symbol repeated (degenerate Huffman tree; LZW long match).
    testInput(std::vector<u8>(100000, 0xAB), "single_symbol");

    // 4. Two symbols.
    {
        std::vector<u8> d(50000);
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = (i % 3 == 0) ? 'A' : 'B';
        testInput(d, "two_symbols");
    }

    // 5. Highly repetitive text (great for LZW; forces dictionary growth/clear).
    {
        std::string unit = "the quick brown fox jumps over the lazy dog. ";
        std::vector<u8> d;
        for (int i = 0; i < 40000; ++i) d.insert(d.end(), unit.begin(), unit.end());
        testInput(d, "repetitive_text");
    }

    // 6. Pseudo-random data (Huffman ~ incompressible; exercises full alphabet).
    {
        Rng rng(12345);
        std::vector<u8> d(500000);
        for (auto& b : d) b = rng.byte();
        testInput(d, "random");
    }

    // 7. Skewed distribution (non-trivial Huffman code lengths).
    {
        Rng rng(999);
        std::vector<u8> d(300000);
        for (auto& b : d) {
            const std::uint64_t r = rng.next() % 100;
            b = (r < 70) ? 0 : (r < 90) ? 1 : (r < 97) ? 2 : static_cast<u8>(rng.byte());
        }
        testInput(d, "skewed");
    }

    // 8. All 256 byte values present, uneven counts.
    {
        std::vector<u8> d;
        for (int v = 0; v < 256; ++v)
            d.insert(d.end(), static_cast<std::size_t>(v + 1), static_cast<u8>(v));
        testInput(d, "all_symbols");
    }

    std::fprintf(stderr, "\n%d/%d checks passed\n",
                 g_checks - g_failures, g_checks);
    if (g_failures == 0) {
        std::fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d TEST(S) FAILED\n", g_failures);
    return 1;
}
