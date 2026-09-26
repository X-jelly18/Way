#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scanner {

// A provider IP-range database loaded from a directory of CIDR lists. Each file
// in the directory is one provider; the provider name is the file's basename
// with a trailing "_plain"/"_plain_ipv4"/"_ipv4" and the ".txt" extension
// stripped (so "cloudflare_plain.txt" -> "cloudflare"). Every non-blank,
// non-comment line is parsed as a CIDR (a bare IP is treated as a host route:
// /32 for IPv4, /128 for IPv6).
//
// classify() returns the provider whose range contains an IP, or nullopt. When
// ranges from different providers overlap, the provider whose name sorts first
// wins (deterministic).
class CidrDb {
public:
    // Load every "*.txt" file under `dir` (non-recursive and one level of
    // subdirectories, so it accepts both a flat folder and the upstream
    // per-provider layout). Returns false if the directory can't be opened.
    // `providers_out`/`ranges_out`, when non-null, receive load counts.
    bool load(const std::string& dir, int* providers_out = nullptr,
              long long* ranges_out = nullptr);

    bool empty() const { return v4_.empty() && v6_.empty(); }

    // Provider name whose range contains `ip` (an IPv4 or IPv6 literal), or
    // nullopt when none match or `ip` doesn't parse.
    std::optional<std::string> classify(const std::string& ip) const;

private:
    struct Entry4 {
        uint32_t network;   // host byte order, masked to `prefix` bits
        uint32_t mask;
        int provider;       // index into names_
    };
    struct Entry6 {
        std::array<uint8_t, 16> network;  // masked to `prefix` bits
        int prefix;
        int provider;
    };

    std::vector<std::string> names_;
    // Bucketed by first octet (0..255) so a lookup scans only its bucket.
    std::array<std::vector<Entry4>, 256> v4_;
    std::array<std::vector<Entry6>, 256> v6_;

    int intern(const std::string& name);
    void add_line(const std::string& line, int provider);
};

}  // namespace scanner
