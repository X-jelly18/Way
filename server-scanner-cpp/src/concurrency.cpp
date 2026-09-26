#include "scanner/concurrency.hpp"

namespace scanner {

void maybe_adjust(AdaptiveLimiter& lim, const Window& win) {
    if (win.samples.size() < 10) return;

    int errors = 0;
    double lat_sum = 0.0;
    for (auto& s : win.samples) {
        if (!s.first) errors++;
        lat_sum += s.second;
    }
    double error_rate = static_cast<double>(errors) / win.samples.size();
    double avg_latency = lat_sum / win.samples.size();

    if (error_rate > 0.25 || avg_latency > 6.0)
        lim.adjust(static_cast<int>(lim.limit * 0.6));       // struggling -> back off hard
    else if (error_rate > 0.10 || avg_latency > 3.0)
        lim.adjust(static_cast<int>(lim.limit * 0.85));      // some strain -> ease off
    else if (error_rate < 0.03 && avg_latency < 1.5)
        lim.adjust(lim.limit + std::max(2, static_cast<int>(lim.limit * 0.15)));  // happy -> ramp up
    // else: steady state
}

}  // namespace scanner
