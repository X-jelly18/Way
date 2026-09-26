#pragma once

#include <string>

namespace scanner {

// Lowercase a copy of `s`.
std::string to_lower(std::string s);

// Strip leading/trailing whitespace (space, tab, CR, LF).
std::string trim(const std::string& s);

// Normalize a raw host-list line into a URL: trims it, drops blanks and
// comments (lines starting with '#'), and prefixes "https://" when no scheme is
// present. Returns "" for lines that should be skipped.
std::string normalize_host(const std::string& raw);

// Extract the bare hostname from a URL: strips the scheme, any userinfo, the
// port, and the path, and unwraps a bracketed IPv6 literal ("[::1]" -> "::1").
std::string url_host(const std::string& url);

}  // namespace scanner
