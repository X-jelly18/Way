#pragma once

#include <csignal>
#include <map>
#include <string>

#include "scanner/cidr.hpp"
#include "scanner/config.hpp"

namespace scanner {

// Set to 1 by the SIGINT handler so the scan loop stops and the checkpoint is
// kept for a later resume.
extern volatile std::sig_atomic_t g_stop;
void on_sigint(int);

struct Stats {
    long long checked = 0;
    long long matches = 0;
    long long skipped = 0;  // hosts skipped because a resume checkpoint had them
    std::map<std::string, long long> cdn_counts;  // provider -> hosts classified
};

// Run the port/server scan described by `cfg`. `initial_concurrency` seeds the
// in-flight cap; `adaptive` toggles the auto-tuning monitor (off when threads
// are pinned). When `cdn` is non-empty, each responding host is classified by
// provider and appended to <cfg.cdn_out_dir>/<provider>.txt, separately from
// the user's own results file.
Stats run_scan(const Config& cfg, int initial_concurrency, bool adaptive,
               const CidrDb& cdn);

}  // namespace scanner
