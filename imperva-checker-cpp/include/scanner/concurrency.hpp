#pragma once

#include <algorithm>
#include <deque>
#include <utility>

namespace scanner {

// An in-flight request cap that a monitor can raise or lower, clamped between a
// floor and a ceiling. Pinning threads means floor == ceiling == limit.
struct AdaptiveLimiter {
    int limit;
    int floor_v;
    int ceiling_v;

    AdaptiveLimiter(int initial, int fl = 5, int ceil = 500)
        : limit(initial), floor_v(fl), ceiling_v(ceil) {}

    void adjust(int new_limit) {
        limit = std::max(floor_v, std::min(ceiling_v, new_limit));
    }
};

// Sliding window of recent (success, latency_seconds) samples.
struct Window {
    std::deque<std::pair<bool, double>> samples;
    size_t maxlen = 50;
    void push(bool ok, double lat) {
        samples.emplace_back(ok, lat);
        if (samples.size() > maxlen) samples.pop_front();
    }
};

// Nudge the limit up or down from recent error-rate and latency stats.
void maybe_adjust(AdaptiveLimiter& lim, const Window& win);

}  // namespace scanner
