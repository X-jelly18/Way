#include "scanner/config.hpp"

#include <cstdlib>
#include <iostream>

#include "scanner/color.hpp"
#include "scanner/strutil.hpp"

namespace scanner {

std::string prompt(const std::string& msg, const std::string& def) {
    std::string suffix = def.empty() ? "" : " [" + def + "]";
    std::cout << CYAN << msg << suffix << ": " << RESET;
    std::string val;
    std::getline(std::cin, val);
    val = trim(val);
    return val.empty() ? def : val;
}

void usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  Port / server scanner: probe port 443 and report the Server header.\n"
              << "  -i, --input FILE      hosts list (prompted if omitted)\n"
              << "  -o, --output FILE     results output file (default servers.txt)\n"
              << "  -t, --timeout SECS    per-request timeout (default 8)\n"
              << "  -c, --concurrency N   adaptive starting concurrency (default: auto-seed)\n"
              << "  -T, --threads N       fixed concurrency (disables auto-tuning)\n"
              << "  -r, --retries N       retry a host N times on transport error (default 0)\n"
              << "      --cidr-dir DIR    classify each responder by CDN using CIDR lists in DIR;\n"
              << "                        appends the host to <cdn-out-dir>/<provider>.txt\n"
              << "      --cdn-out-dir DIR where to write the per-CDN files (default .)\n"
              << "  -f, --format FMT      output format: text | csv | json (default text)\n"
              << "      --resume          resume from <output>.cache, skipping checked hosts\n"
              << "      --no-resume       ignore any checkpoint and scan from the top\n"
              << "      --no-pause        don't pause mid-scan when the network drops\n"
              << "  -v, --verbose         print status for every host\n"
              << "  -h, --help            show this help\n";
}

Format parse_format(const std::string& s) {
    std::string f = to_lower(s);
    if (f == "text") return Format::Text;
    if (f == "csv") return Format::Csv;
    if (f == "json") return Format::Json;
    std::cerr << "Invalid --format: " << s << " (use text|csv|json)\n";
    std::exit(2);
}

bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-i" || a == "--input") cfg.input = next("--input");
        else if (a == "-o" || a == "--output") cfg.output = next("--output");
        else if (a == "-t" || a == "--timeout") cfg.timeout = std::stoi(next("--timeout"));
        else if (a == "-c" || a == "--concurrency") cfg.concurrency = std::stoi(next("--concurrency"));
        else if (a == "-T" || a == "--threads") cfg.threads = std::stoi(next("--threads"));
        else if (a == "-r" || a == "--retries") cfg.retries = std::stoi(next("--retries"));
        else if (a == "--cidr-dir") cfg.cidr_dir = next("--cidr-dir");
        else if (a == "--cdn-out-dir") cfg.cdn_out_dir = next("--cdn-out-dir");
        else if (a == "--resume") cfg.resume = 1;
        else if (a == "--no-resume") cfg.resume = 2;
        else if (a == "--no-pause") cfg.no_pause = true;
        else if (a == "-f" || a == "--format") cfg.format = parse_format(next("--format"));
        else if (a == "-v" || a == "--verbose") cfg.verbose = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); return false; }
        else { std::cerr << "Unknown argument: " << a << "\n"; usage(argv[0]); std::exit(2); }
    }
    if (cfg.retries < 0) cfg.retries = 0;
    if (cfg.threads < 0) cfg.threads = 0;
    return true;
}

}  // namespace scanner
