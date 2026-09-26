#pragma once

#include <chrono>
#include <string>

#include <curl/curl.h>

#include "scanner/config.hpp"

namespace scanner {

// Per-request state carried through the libcurl multi loop.
struct HostJob {
    std::string url;
    std::string headers;
    std::string ip;        // remote IP actually connected to (for CDN lookup)
    std::string key;       // checkpoint/dedup key (url, plus port when multi-port)
    long port_req = 0;     // requested port (0 => scheme default)
    std::chrono::steady_clock::time_point start;
    long status = 0;
    long port = 0;         // port actually connected to
    int attempts = 0;      // transport-error retries used so far
    CURL* easy = nullptr;
};

// Return the value of a header by (case-insensitive) name. When the header
// appears more than once (e.g. across redirects), the last occurrence wins so
// the value reflects the final response.
std::string find_header(const std::string& headers, const std::string& name);

// Build a curl_slist of the extra request headers in cfg (or nullptr when there
// are none). Caller owns it and must curl_slist_free_all() it. The same list can
// be shared across every handle.
curl_slist* build_header_list(const Config& cfg);

// Configure `job->easy` for a HEAD probe of job->url. If job->easy is null a new
// handle is created; otherwise the existing handle is reset and reused (cheaper
// under high concurrency). `extra_headers` is the shared list from
// build_header_list() (may be nullptr).
void setup_easy(HostJob* job, const Config& cfg, curl_slist* extra_headers);

}  // namespace scanner
