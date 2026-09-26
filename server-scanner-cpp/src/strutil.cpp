#include "scanner/strutil.hpp"

#include <algorithm>
#include <cctype>

namespace scanner {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string normalize_host(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return "";
    if (line.find("://") == std::string::npos) line = "https://" + line;
    return line;
}

std::string url_host(const std::string& url) {
    std::string s = url;
    size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    // Strip path/query.
    size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    // Strip userinfo (user:pass@host).
    size_t at = s.find('@');
    if (at != std::string::npos) s = s.substr(at + 1);
    // Bracketed IPv6 literal: "[::1]:443" -> "::1".
    if (!s.empty() && s[0] == '[') {
        size_t close = s.find(']');
        if (close != std::string::npos) return s.substr(1, close - 1);
    }
    // Strip ":port" for a plain host (only when there's a single colon, so we
    // don't mangle an unbracketed IPv6 literal).
    size_t colon = s.find(':');
    if (colon != std::string::npos && s.find(':', colon + 1) == std::string::npos)
        s = s.substr(0, colon);
    return s;
}

}  // namespace scanner
