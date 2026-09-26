#pragma once

#include <optional>
#include <string>

namespace scanner {

// True when DNS resolves `host` (a cheap connectivity check). Used for the
// startup sanity check and the mid-scan network-loss guard.
bool network_up(const std::string& host = "m.google.com");

// Quick TCP-connect latency probe to `host` to seed a sane starting concurrency
// (silent).
int seed_initial_concurrency(const std::string& host = "m.google.com");

// Resolve `host` (a bare hostname or IP literal) to a single IP string.
// `family` is 0 (any), 4 (IPv4 only), or 6 (IPv6 only). Returns nullopt when the
// name doesn't resolve. Used by the DNS-only classification mode.
std::optional<std::string> resolve_ip(const std::string& host, int family);

}  // namespace scanner
