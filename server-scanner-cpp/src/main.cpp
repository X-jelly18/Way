// server-scanner — fast HTTPS port/server fingerprinter.
//
// Streams a list of hosts and, for each, sends a HEAD request to https:// (port
// 443). Every host that answers is reported with the port it responded on, the
// HTTP status, and its Server header.
//
// This translation unit is a thin orchestrator; the pieces live in:
//   scanner/color.hpp        terminal color helpers
//   scanner/strutil.hpp      string helpers (trim / lower / host normalize)
//   scanner/config.hpp       Config + CLI parsing + prompts
//   scanner/output.hpp       text / CSV / JSON writer
//   scanner/concurrency.hpp  adaptive in-flight limiter
//   scanner/netprobe.hpp     connectivity + latency probes
//   scanner/http.hpp         libcurl per-host setup + header lookup
//   scanner/scan.hpp         the multi-handle scan loop
//
// Build: see CMakeLists.txt (requires libcurl).

#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <unistd.h>

#include "scanner/color.hpp"
#include "scanner/config.hpp"
#include "scanner/netprobe.hpp"
#include "scanner/scan.hpp"
#include "scanner/strutil.hpp"

using namespace scanner;

int main(int argc, char** argv) {
    col::enabled = isatty(fileno(stdout));
    std::signal(SIGINT, on_sigint);

    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 0;

    // Interactive when no input file was passed on the command line.
    bool interactive = cfg.input.empty();

    std::cout << BOLD << MAGENTA << "=== Way Server Scanner (C++) ===\n\n" << RESET;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << RED << "Failed to initialize libcurl." << RESET << "\n";
        return 1;
    }

    // Silent network sanity check.
    if (!network_up()) {
        std::cout << YELLOW
                  << "Could not resolve m.google.com — looks like a DNS/network issue."
                  << RESET << "\n";
        std::cout << YELLOW << "Continue anyway? (y/N): " << RESET;
        std::string ans;
        std::getline(std::cin, ans);
        if (to_lower(trim(ans)) != "y") {
            std::cout << RED << "Aborted. Fix your network/DNS and try again." << RESET << "\n";
            curl_global_cleanup();
            return 1;
        }
    }

    // Resolve input/output interactively when not passed as args.
    if (cfg.input.empty()) {
        cfg.input = prompt("Enter path to hosts .txt file");
        while (cfg.input.empty())
            cfg.input = prompt("Please enter a valid path to hosts .txt file");
    }
    {
        std::ifstream test(cfg.input);
        if (!test) {
            std::cerr << RED << "Can't open " << cfg.input << RESET << "\n";
            curl_global_cleanup();
            return 1;
        }
    }
    if (interactive) {  // also prompt for output and threads
        cfg.output = prompt("Enter output file name for results", cfg.output);
        std::string tstr = prompt("Threads (a number, or blank for auto)");
        if (!tstr.empty()) {
            try {
                int t = std::stoi(tstr);
                if (t > 0) cfg.threads = t;
            } catch (...) {
                std::cout << YELLOW << "Not a number — using auto-concurrency." << RESET << "\n";
            }
        }
    }

    // ----- resume checkpoint decision -----
    {
        std::string cache_path = cfg.output + ".cache";
        long long cached = 0;
        {
            std::ifstream cf(cache_path);
            std::string l;
            while (std::getline(cf, l)) if (!trim(l).empty()) cached++;
        }
        if (cached > 0) {
            if (cfg.format == Format::Json) {
                if (cfg.resume == 1)
                    std::cout << YELLOW << "JSON output can't be resumed — starting fresh."
                              << RESET << "\n";
                cfg.resume = 2;  // json's single array can't be appended cleanly
            } else if (cfg.resume == 0) {
                if (interactive) {
                    std::string a = prompt("Found a checkpoint (" + std::to_string(cached) +
                                           " hosts checked). Resume? (Y/n)", "y");
                    a = to_lower(a);
                    cfg.resume = (a == "n" || a == "no") ? 2 : 1;
                } else {
                    cfg.resume = 1;  // auto-resume unattended runs
                }
            }
        }
    }

    bool adaptive = (cfg.threads <= 0);
    int initial = adaptive
        ? (cfg.concurrency > 0 ? cfg.concurrency : seed_initial_concurrency())
        : cfg.threads;

    // Timestamp banner.
    std::time_t tt = std::time(nullptr);
    char ts[16];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&tt));
    const char* fmt = cfg.format == Format::Csv ? "csv"
                      : cfg.format == Format::Json ? "json" : "text";
    std::string conc = adaptive
        ? "auto-concurrency, seeded at " + std::to_string(initial)
        : std::to_string(initial) + " threads (fixed)";
    std::cout << "\n" << DIM << "[" << ts << "]" << RESET
              << " Starting server scan of " << CYAN << cfg.input << RESET
              << " (" << conc << ", HEAD 443"
              << ", retries=" << cfg.retries << "), output=" << CYAN << cfg.output
              << RESET << " [" << fmt << "]\n\n";

    Stats st = run_scan(cfg, initial, adaptive);

    std::cout << "\n" << BOLD << GREEN << "Done." << RESET << " "
              << st.checked << " hosts checked, "
              << GREEN << st.matches << RESET << " responded (Server shown).\n";
    if (st.skipped)
        std::cout << DIM << "(resumed: skipped " << st.skipped
                  << " already-checked hosts)" << RESET << "\n";
    std::cout << "Saved to " << CYAN << cfg.output << RESET << "\n";

    curl_global_cleanup();
    return 0;
}
