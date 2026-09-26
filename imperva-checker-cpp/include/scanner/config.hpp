#pragma once

#include <string>

namespace scanner {

enum class Format { Text, Csv, Json };

struct Config {
    std::string input;
    std::string output = "servers.txt";
    int timeout = 8;
    bool verbose = false;
    int concurrency = 0;  // adaptive seed; 0 => auto-seed from latency probe
    int threads = 0;      // fixed concurrency (adaptive off); 0 => adaptive
    int retries = 0;      // per-host transport-error retries
    Format format = Format::Text;
    bool no_pause = false;  // disable mid-scan pause on network loss
    int resume = 0;         // 0 = auto/ask, 1 = force resume, 2 = force fresh
};

// Parse a --format value (text|csv|json); exits(2) on an invalid value.
Format parse_format(const std::string& s);

// Parse argv into cfg. Returns false when --help was shown (caller should exit
// 0); exits(2) on a malformed argument.
bool parse_args(int argc, char** argv, Config& cfg);

// Print CLI usage to stdout.
void usage(const char* prog);

// Prompt the user for a line of input, showing an optional default. Returns the
// trimmed input, or `def` when the input is empty.
std::string prompt(const std::string& msg, const std::string& def = "");

}  // namespace scanner
