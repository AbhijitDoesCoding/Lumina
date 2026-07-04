#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "lumina/Cli.hpp"
#include "lumina/Common.hpp"
#include "lumina/Format.hpp"
#include "lumina/Huffman.hpp"
#include "lumina/Lzw.hpp"

namespace {

using namespace lumina;

const char* algoName(Algorithm a) {
    switch (a) {
        case Algorithm::Huffman: return "huffman";
        case Algorithm::Lzw:     return "lzw";
    }
    return "?";
}

void reportStats(const Options& opt, u64 inBytes, u64 outBytes,
                 double seconds, const char* algo) {
    const double ratio = inBytes > 0
        ? static_cast<double>(outBytes) / static_cast<double>(inBytes)
        : 0.0;
    const double mbps = seconds > 0
        ? (static_cast<double>(inBytes) / (1024.0 * 1024.0)) / seconds
        : 0.0;
    std::fprintf(stderr,
        "  algorithm : %s\n"
        "  input     : %llu bytes\n"
        "  output    : %llu bytes\n"
        "  ratio     : %.2f%% of original%s\n"
        "  throughput: %.1f MiB/s (%.3fs)\n",
        algo,
        static_cast<unsigned long long>(inBytes),
        static_cast<unsigned long long>(outBytes),
        ratio * 100.0,
        (opt.command == Command::Compress) ? " (lower is better)" : "",
        mbps, seconds);
}

void runCompress(const Options& opt) {
    std::ofstream out(opt.output, std::ios::binary | std::ios::trunc);
    if (!out) throw LuminaError("cannot open output file: " + opt.output);

    switch (opt.algorithm) {
        case Algorithm::Huffman:
            huffmanCompress(opt.input, out, opt.verify);
            break;
        case Algorithm::Lzw:
            lzwCompress(opt.input, out, opt.verify);
            break;
    }
    out.flush();
    if (!out) throw LuminaError("error finalizing output file: " + opt.output);
}

void runDecompress(const Options& opt) {
    std::ifstream in(opt.input, std::ios::binary);
    if (!in) throw LuminaError("cannot open input file: " + opt.input);

    const FileHeader header = readHeader(in);

    std::ofstream out(opt.output, std::ios::binary | std::ios::trunc);
    if (!out) throw LuminaError("cannot open output file: " + opt.output);

    switch (header.algorithm) {
        case Algorithm::Huffman:
            huffmanDecompress(in, header, out);
            break;
        case Algorithm::Lzw:
            lzwDecompress(in, header, out);
            break;
    }
    out.flush();
    if (!out) throw LuminaError("error finalizing output file: " + opt.output);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    try {
        const Options opt = parseArgs(args);

        switch (opt.command) {
            case Command::Help:
                std::cout << usageText();
                return 0;
            case Command::Version:
                std::cout << "Lumina " << versionText() << "\n";
                return 0;
            case Command::None:
                std::cout << usageText();
                return 0;
            default:
                break;
        }

        namespace fs = std::filesystem;
        if (!fs::exists(opt.input)) {
            throw LuminaError("input file does not exist: " + opt.input);
        }
        if (fs::exists(opt.output) && !opt.force) {
            throw LuminaError("output file already exists: " + opt.output +
                              " (use -f/--force to overwrite)");
        }
        if (fs::exists(opt.input) && fs::exists(opt.output) &&
            fs::equivalent(opt.input, opt.output)) {
            throw LuminaError("input and output refer to the same file");
        }

        const auto t0 = std::chrono::steady_clock::now();
        if (opt.command == Command::Compress) {
            runCompress(opt);
        } else {
            runDecompress(opt);
        }
        const auto t1 = std::chrono::steady_clock::now();

        if (opt.verbose) {
            const double seconds =
                std::chrono::duration<double>(t1 - t0).count();
            const u64 inBytes  = static_cast<u64>(fs::file_size(opt.input));
            const u64 outBytes = static_cast<u64>(fs::file_size(opt.output));

            Algorithm usedAlgo = opt.algorithm;
            if (opt.command == Command::Decompress) {
                std::ifstream probe(opt.input, std::ios::binary);
                usedAlgo = readHeader(probe).algorithm;  // header dictates codec
            }
            std::fprintf(stderr, "%s: %s -> %s\n",
                         opt.command == Command::Compress ? "compressed" : "decompressed",
                         opt.input.c_str(), opt.output.c_str());
            reportStats(opt, inBytes, outBytes, seconds, algoName(usedAlgo));
        }
        return 0;
    } catch (const LuminaError& e) {
        std::fprintf(stderr, "lumina: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "lumina: unexpected error: %s\n", e.what());
        return 2;
    }
}
