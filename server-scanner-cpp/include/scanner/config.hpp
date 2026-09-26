#pragma once

#include <string>
#include <vector>

namespace scanner {

// Text / Csv / Json are single-document formats; Ndjson is one JSON object per
// line, which (unlike Json) streams and can be appended, so it is resumable.
enum class Format { Text, Csv, Json, Ndjson };

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

    // Optional CDN classification. When cidr_dir is set, each responding host is
    // matched against the provider IP ranges loaded from it and appended to
    // <cdn_out_dir>/<provider>.txt. This is separate from `output` and never
    // touches the user's own results file.
    std::string cidr_dir;
    std::string cdn_out_dir = ".";

    // ----- advanced options -----
    bool resolve_only = false;      // DNS-only: classify by resolved IP, no HTTP
    int resolve_threads = 0;        // resolver pool size (0 => auto)
    std::vector<int> ports;         // ports to probe (empty => scheme default 443)
    double rate = 0.0;              // max new requests/sec (0 => unlimited)
    long long limit = 0;            // stop after N hosts dispatched (0 => all)
    bool dedup = false;             // skip duplicate host lines within a run
    std::string user_agent;         // override User-Agent (empty => built-in)
    std::vector<std::string> headers;  // extra request headers ("K: V")
    std::string proxy;              // proxy URL for all requests
    int ip_family = 0;              // 0 any, 4 => IPv4 only, 6 => IPv6 only
    bool cdn_only = false;          // only write responders that matched a CDN
    bool no_color = false;          // force-disable ANSI color
    bool no_follow = false;         // don't follow HTTP redirects
    bool no_probe = false;          // skip the startup connectivity probe/seed
    std::string probe_host = "m.google.com";  // host for the connectivity probe
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
