#ifndef LUMINA_CLI_HPP
#define LUMINA_CLI_HPP

#include <string>
#include <vector>

#include "lumina/Common.hpp"

namespace lumina {

enum class Command {
    None,
    Compress,
    Decompress,
    Help,
    Version,
};

// Fully-resolved invocation produced by parseArgs().
struct Options {
    Command     command    = Command::None;
    Algorithm   algorithm  = Algorithm::Huffman;  // compress only
    std::string input;
    std::string output;                            // resolved, never empty for (de)compress
    bool        force      = false;                // overwrite existing output
    bool        verify     = true;                 // embed/check CRC-32
    bool        verbose    = false;
};

// Parse argv (excluding program name is handled internally). Throws LuminaError
// with a usage-oriented message on malformed input.
Options parseArgs(const std::vector<std::string>& args);

// Human-readable help / version text.
std::string usageText();
std::string versionText();

}  // namespace lumina

#endif  // LUMINA_CLI_HPP
