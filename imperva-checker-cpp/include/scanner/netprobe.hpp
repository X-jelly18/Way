#pragma once

namespace scanner {

// True when DNS resolves (a cheap connectivity check). Used for the startup
// sanity check and the mid-scan network-loss guard.
bool network_up();

// Quick TCP-connect latency probe to seed a sane starting concurrency (silent).
int seed_initial_concurrency();

}  // namespace scanner
