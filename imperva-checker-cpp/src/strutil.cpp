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

}  // namespace scanner
