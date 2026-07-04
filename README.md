# Lumina

[![CI](https://github.com/AbhijitDoesCoding/Lumina/actions/workflows/ci.yml/badge.svg)](https://github.com/AbhijitDoesCoding/Lumina/actions/workflows/ci.yml)

A high-performance command-line file compressor written in modern C++ (C++20),
using **only the C++ standard library** — no zlib, no third-party code. It
implements **Huffman coding** and **LZW** from scratch, streams data in fixed
chunks so it compresses arbitrarily large files (4 GB+) with a flat memory
footprint, and verifies every round-trip with an embedded CRC-32.

```
$ lumina compress bigfile.tar
$ lumina decompress bigfile.tar.lum -o restored.tar
```

---

## Highlights

- **Two codecs.** Huffman (great on skewed byte distributions) and LZW
  (great on repetitive text/logs). Pick with `-a huffman|lzw`.
- **Constant memory.** Everything streams in 1 MiB chunks. Huffman uses a
  two-pass scan of the file on disk; LZW keeps a dictionary bounded to 2^16
  entries. Resident memory does not grow with file size, so 4 GB+ inputs work
  without loading them into RAM.
- **Length-limited canonical Huffman.** Codes are capped at 32 bits via a
  zlib-style bit-length adjustment, so a code word always fits in a `uint32_t`
  even for adversarial (Fibonacci-frequency) inputs. The header stores only the
  256 code *lengths* — both sides reconstruct identical canonical codes.
- **Integrity checked.** A CRC-32 of the original bytes is embedded in the
  header and verified on decompression. `--no-verify` opts out.
- **Portable.** Little-endian on-disk format; builds with GCC, Clang, or MSVC on
  Linux and Windows.

---

## Building

Requires CMake ≥ 3.16 and a C++20 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # run the round-trip test suite
```

On Windows with MinGW:

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The `lumina` executable lands in `build/`.

---

## Usage

```
lumina <command> [options] <input> [output]

COMMANDS
  compress, c      Compress <input>
  decompress, d    Decompress <input>
  help, -h         Show help
  version          Show version

OPTIONS
  -a, --algo <huffman|lzw>   Compression algorithm (default: huffman)
  -o, --output <file>        Explicit output path
  -f, --force                Overwrite output if it exists
      --no-verify            Skip embedding/checking the CRC-32
  -v, --verbose              Print sizes, ratio, and throughput
```

Default output naming (when `-o`/`[output]` is omitted):

| Command    | Output              |
|------------|---------------------|
| compress   | `<input>.lum`       |
| decompress | `<input>` minus the `.lum` suffix (else `<input>.orig`) |

The decompressor selects the codec from the archive header — you never specify
the algorithm when decompressing.

---

## Architecture

```
include/lumina/            src/
  Common.hpp   types, LuminaError, chunk size, Algorithm enum
  Crc32.hpp      Crc32.cpp   incremental IEEE CRC-32 (integrity)
  BitIO.hpp      BitIO.cpp   BitWriter / BitReader — buffered, MSB-first bit I/O
  Format.hpp     Format.cpp  24-byte container header + LE scalar helpers
  Huffman.hpp    Huffman.cpp streaming two-pass canonical Huffman codec
  Lzw.hpp        Lzw.cpp     streaming variable-width LZW codec
  Cli.hpp        Cli.cpp     argument parsing
                 main.cpp    driver: file guards, dispatch, timing/stats
tests/
  roundtrip_tests.cpp        8 input profiles × 2 codecs, byte-identical checks
```

All codec logic lives in the `lumina_core` static library; `main.cpp` is a thin
driver, and the tests link the library directly.

### Bit-level I/O (`BitIO`)

`BitWriter` packs variable-length codes MSB-first into a byte and flushes
completed bytes through an in-memory buffer to the `std::ostream`, amortizing
write cost. `finish()` zero-pads the final partial byte. `BitReader` is the
inverse, refilling its buffer from the `std::istream` and serving one bit at a
time. MSB-first ordering pairs naturally with canonical Huffman codes stored
high-bit-first.

### Streaming & memory

- **Huffman** makes two passes over the *file path*: pass 1 counts byte
  frequencies (and folds the CRC); pass 2 re-reads and emits codes. Only a
  1 MiB chunk buffer plus 256-entry tables are ever resident.
- **LZW** makes a single pass; its dictionary is capped at 2^16 entries and
  reset via a CLEAR code when full, so memory is bounded regardless of input
  size. The original size is taken from the filesystem, and the CRC is patched
  into the header after the streaming pass.

Because sizes are stored as `u64` and nothing accumulates in RAM, files well
beyond 4 GB compress and decompress correctly.

---

## File format (`.lum`)

All integers little-endian. Fixed 24-byte preamble, then an algorithm-specific
model, then the bit-packed payload.

```
off  size  field
  0    4   magic  = 'L','M','N','A'
  4    1   version (currently 1)
  5    1   algorithm (1 = Huffman, 2 = LZW)
  6    1   flags (bit0 = CRC-32 present)
  7    1   reserved
  8    8   originalSize (u64) — exact source length in bytes
 16    4   crc32 (u32) — CRC-32 of the original bytes
 20    4   reserved
 24    -   model + payload
```

**Huffman model:** `u16 count`, then `count` × (`u8 symbol`, `u8 codeLength`).
The decoder rebuilds canonical codes from the lengths alone. The payload is the
concatenated canonical codes; the decoder stops after `originalSize` symbols, so
no pseudo-EOF symbol is needed.

**LZW model:** none. The payload is a stream of variable-width codes (9→16
bits) with reserved control codes `256 = CLEAR` and `257 = END`; string codes
start at 258.

---

## Design notes & limits

- The Huffman length limiter guarantees ≤ 32-bit codes for any input. Natural
  Huffman depth only approaches this under Fibonacci-distributed frequencies;
  the limiter reshapes the length histogram while preserving the Kraft equality.
- LZW encoder/decoder widths stay in lockstep via the classic "early change":
  the decoder, which learns each new string one code after the encoder assigns
  it, grows its code width one entry sooner.
- Incompressible input (e.g. already-compressed or random data) will not shrink;
  Huffman stays near 100% and LZW may expand slightly. Use the algorithm that
  fits the data — LZW for repetitive text, Huffman for skewed byte statistics.
- CRC-32 is an integrity checksum, not a cryptographic hash.

---

## License

MIT — see [LICENSE](LICENSE).
