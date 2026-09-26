#pragma once

#include <csignal>

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
};

// Run the port/server scan described by `cfg`. `initial_concurrency` seeds the
// in-flight cap; `adaptive` toggles the auto-tuning monitor (off when threads
// are pinned).
Stats run_scan(const Config& cfg, int initial_concurrency, bool adaptive);

}  // namespace scanner
