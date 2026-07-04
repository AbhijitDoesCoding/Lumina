# Contributing to Lumina

Thanks for your interest! Lumina is a small, dependency-free C++20 project, so
the workflow is deliberately simple.

## Ground rules

- **Standard library only.** No third-party libraries — that constraint is the
  point of the project. If you need a data structure or algorithm, implement it
  or use `<algorithm>`/`<vector>`/etc.
- **Keep it portable.** Code must build clean on GCC, Clang, and MSVC. Use the
  little-endian serialization helpers in `Format.hpp` rather than raw
  `reinterpret_cast` of multi-byte integers.
- **Warnings are errors in spirit.** The build enables
  `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`
  (and `/W4 /permissive-` on MSVC). Don't add warnings.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On Windows with MinGW, add `-G "MinGW Makefiles"` to the configure step and put
the toolchain `bin/` (e.g. `C:/mingw64/bin`) on `PATH` when running the
binaries.

## Before opening a PR

1. **Add a round-trip case.** Any codec or format change should be covered in
   `tests/roundtrip_tests.cpp`. New inputs must compress *and* decompress back
   byte-identically for both algorithms. Think about the corners: empty files,
   a single symbol, tiny alphabets, highly repetitive data (LZW dictionary
   fill + CLEAR), and random data (Huffman worst case).
2. **Preserve the on-disk format** — or bump `kFormatVersion` in `Format.hpp`
   and document the change in the README's format section. Old archives should
   still fail loudly (bad magic / unsupported version), never silently.
3. **Run the full suite** (`ctest`) and a manual large-file round-trip if you
   touched the streaming paths.
4. **Match the surrounding style:** 4-space indent, `lumina` namespace, headers
   in `include/lumina/`, one class/concern per translation unit, comments that
   explain *why* rather than restate the code.

## Commit messages

Short imperative subject line, then a body explaining the reasoning if the
change isn't obvious. Reference the affected component (e.g. `Huffman:`,
`LZW:`, `BitIO:`, `CLI:`).

## Reporting bugs

Open an issue with: the command you ran, the input characteristics (size, kind
of data), the expected vs. actual result, and your compiler/OS. A minimal file
that reproduces a round-trip mismatch is the most useful thing you can attach.
