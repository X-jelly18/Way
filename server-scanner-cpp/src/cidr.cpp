#include "scanner/cidr.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

#include "scanner/strutil.hpp"

namespace scanner {

// Derive a provider name from a file basename: drop ".txt", then a trailing
// "_ipv4"/"_ipv6" and a trailing "_plain".
static std::string provider_from_filename(const std::string& fname) {
    std::string s = fname;
    auto strip_suffix = [&](const char* suf) {
        size_t n = std::strlen(suf);
        if (s.size() >= n && s.compare(s.size() - n, n, suf) == 0)
            s.erase(s.size() - n);
    };
    strip_suffix(".txt");
    strip_suffix("_ipv4");
    strip_suffix("_ipv6");
    strip_suffix("_plain");
    return s.empty() ? fname : s;
}

int CidrDb::intern(const std::string& name) {
    for (size_t i = 0; i < names_.size(); i++)
        if (names_[i] == name) return static_cast<int>(i);
    names_.push_back(name);
    return static_cast<int>(names_.size() - 1);
}

void CidrDb::add_line(const std::string& raw, int provider) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return;

    // Split "addr/prefix"; a bare address is a host route.
    std::string addr = line;
    int prefix = -1;
    size_t slash = line.find('/');
    if (slash != std::string::npos) {
        addr = line.substr(0, slash);
        try {
            prefix = std::stoi(line.substr(slash + 1));
        } catch (...) {
            return;
        }
    }

    if (addr.find(':') != std::string::npos) {
        // IPv6
        std::array<uint8_t, 16> bytes{};
        if (inet_pton(AF_INET6, addr.c_str(), bytes.data()) != 1) return;
        if (prefix < 0) prefix = 128;
        if (prefix > 128) return;
        // Mask to `prefix` bits.
        for (int i = 0; i < 16; i++) {
            int bits = prefix - i * 8;
            uint8_t m = bits <= 0 ? 0 : (bits >= 8 ? 0xFF : (0xFF << (8 - bits)));
            bytes[i] &= m;
        }
        Entry6 e{bytes, prefix, provider};
        v6_[bytes[0]].push_back(e);
    } else {
        // IPv4
        struct in_addr a{};
        if (inet_pton(AF_INET, addr.c_str(), &a) != 1) return;
        if (prefix < 0) prefix = 32;
        if (prefix > 32) return;
        uint32_t ip = ntohl(a.s_addr);
        uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
        Entry4 e{ip & mask, mask, provider};
        v4_[(e.network >> 24) & 0xFF].push_back(e);
    }
}

// Load every line of one file under `provider`.
static long long load_one(CidrDb& db, const std::string& path, int provider,
                          void (CidrDb::*add)(const std::string&, int)) {
    std::ifstream f(path);
    if (!f) return 0;
    long long n = 0;
    std::string line;
    while (std::getline(f, line)) {
        (db.*add)(line, provider);
        n++;
    }
    return n;
}

bool CidrDb::load(const std::string& dir, int* providers_out,
                  long long* ranges_out) {
    long long total_lines = 0;

    auto scan_dir = [&](const std::string& d, bool descend, auto&& self) -> void {
        DIR* dp = opendir(d.c_str());
        if (!dp) return;
        struct dirent* de;
        while ((de = readdir(dp)) != nullptr) {
            std::string name = de->d_name;
            if (name == "." || name == "..") continue;
            std::string path = d + "/" + name;
            struct stat st{};
            if (stat(path.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (descend) self(path, false, self);  // one level deep
                continue;
            }
            if (S_ISREG(st.st_mode) && name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".txt") == 0) {
                int p = intern(provider_from_filename(name));
                total_lines += load_one(*this, path, p, &CidrDb::add_line);
            }
        }
        closedir(dp);
    };

    DIR* probe = opendir(dir.c_str());
    if (!probe) return false;
    closedir(probe);

    scan_dir(dir, true, scan_dir);

    // Deterministic ordering: sort each bucket by provider name so classify()
    // returns the lexicographically-first provider on an overlap.
    auto by_name4 = [&](const Entry4& a, const Entry4& b) {
        return names_[a.provider] < names_[b.provider];
    };
    auto by_name6 = [&](const Entry6& a, const Entry6& b) {
        return names_[a.provider] < names_[b.provider];
    };
    for (auto& b : v4_) std::stable_sort(b.begin(), b.end(), by_name4);
    for (auto& b : v6_) std::stable_sort(b.begin(), b.end(), by_name6);

    if (providers_out) *providers_out = static_cast<int>(names_.size());
    if (ranges_out) *ranges_out = total_lines;
    return true;
}

std::optional<std::string> CidrDb::classify(const std::string& ip) const {
    if (ip.find(':') != std::string::npos) {
        std::array<uint8_t, 16> b{};
        if (inet_pton(AF_INET6, ip.c_str(), b.data()) != 1) return std::nullopt;
        const auto& bucket = v6_[b[0]];
        for (const auto& e : bucket) {
            bool match = true;
            for (int i = 0; i < 16 && match; i++) {
                int bits = e.prefix - i * 8;
                uint8_t m = bits <= 0 ? 0 : (bits >= 8 ? 0xFF : (0xFF << (8 - bits)));
                if ((b[i] & m) != e.network[i]) match = false;
            }
            if (match) return names_[e.provider];  // bucket is name-sorted
        }
        return std::nullopt;
    }

    struct in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return std::nullopt;
    uint32_t v = ntohl(a.s_addr);
    const auto& bucket = v4_[(v >> 24) & 0xFF];
    for (const auto& e : bucket) {
        if ((v & e.mask) == e.network) return names_[e.provider];
    }
    return std::nullopt;
}

}  // namespace scanner
