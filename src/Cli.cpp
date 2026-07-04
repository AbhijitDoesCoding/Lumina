#include "lumina/Cli.hpp"

#include <sstream>

namespace lumina {

std::string usageText() {
    std::ostringstream os;
    os <<
"Lumina " << versionText() << " - high-performance file compressor\n"
"\n"
"USAGE:\n"
"    lumina <command> [options] <input> [output]\n"
"\n"
"COMMANDS:\n"
"    compress, c      Compress <input> into <output>\n"
"    decompress, d    Decompress <input> into <output>\n"
"    help, -h         Show this help\n"
"    version          Show version\n"
"\n"
"OPTIONS:\n"
"    -a, --algo <huffman|lzw>   Algorithm for compression (default: huffman)\n"
"    -o, --output <file>        Explicit output path\n"
"    -f, --force                Overwrite output file if it exists\n"
"        --no-verify            Skip embedding/checking the CRC-32 integrity code\n"
"    -v, --verbose              Print statistics (sizes, ratio, timing)\n"
"\n"
"OUTPUT NAMING (when <output> / -o is omitted):\n"
"    compress    -> <input>.lum\n"
"    decompress  -> <input> without its .lum suffix (else <input>.orig)\n"
"\n"
"EXAMPLES:\n"
"    lumina compress bigfile.tar\n"
"    lumina c -a lzw -o out.lum server.log\n"
"    lumina decompress bigfile.tar.lum -o restored.tar\n";
    return os.str();
}

std::string versionText() {
    return "v1.0.0";
}

namespace {

std::string defaultOutput(Command cmd, const std::string& input) {
    if (cmd == Command::Compress) {
        return input + ".lum";
    }
    // Decompress: strip a trailing ".lum" if present, else add ".orig".
    const std::string suffix = ".lum";
    if (input.size() > suffix.size() &&
        input.compare(input.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return input.substr(0, input.size() - suffix.size());
    }
    return input + ".orig";
}

[[noreturn]] void fail(const std::string& msg) {
    throw LuminaError(msg + "\nRun 'lumina help' for usage.");
}

}  // namespace

Options parseArgs(const std::vector<std::string>& args) {
    Options opt;
    if (args.empty()) {
        opt.command = Command::Help;
        return opt;
    }

    // First non-option token is the command.
    const std::string& cmd = args[0];
    if (cmd == "compress" || cmd == "c") {
        opt.command = Command::Compress;
    } else if (cmd == "decompress" || cmd == "d" || cmd == "x") {
        opt.command = Command::Decompress;
    } else if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        opt.command = Command::Help;
        return opt;
    } else if (cmd == "version" || cmd == "--version") {
        opt.command = Command::Version;
        return opt;
    } else {
        fail("unknown command: '" + cmd + "'");
    }

    std::vector<std::string> positionals;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto needValue = [&](const char* name) -> const std::string& {
            if (i + 1 >= args.size()) fail(std::string("option ") + name + " requires a value");
            return args[++i];
        };

        if (a == "-a" || a == "--algo") {
            const std::string& v = needValue("--algo");
            if (v == "huffman" || v == "h") opt.algorithm = Algorithm::Huffman;
            else if (v == "lzw" || v == "l") opt.algorithm = Algorithm::Lzw;
            else fail("unknown algorithm: '" + v + "' (expected huffman|lzw)");
        } else if (a == "-o" || a == "--output") {
            opt.output = needValue("--output");
        } else if (a == "-f" || a == "--force") {
            opt.force = true;
        } else if (a == "--no-verify") {
            opt.verify = false;
        } else if (a == "-v" || a == "--verbose") {
            opt.verbose = true;
        } else if (a == "-h" || a == "--help") {
            opt.command = Command::Help;
            return opt;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            fail("unknown option: '" + a + "'");
        } else {
            positionals.push_back(a);
        }
    }

    if (opt.command == Command::Decompress &&
        opt.algorithm != Algorithm::Huffman) {
        // --algo is meaningless on decompress; the header dictates the codec.
    }

    if (positionals.empty()) fail("missing input file");
    if (positionals.size() > 2) fail("too many positional arguments");
    opt.input = positionals[0];
    if (positionals.size() == 2) {
        if (!opt.output.empty()) fail("output specified twice (positional and -o)");
        opt.output = positionals[1];
    }
    if (opt.output.empty()) {
        opt.output = defaultOutput(opt.command, opt.input);
    }
    return opt;
}

}  // namespace lumina
