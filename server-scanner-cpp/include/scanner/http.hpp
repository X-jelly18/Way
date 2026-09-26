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

// Initialize a fresh easy handle on `job` for a HEAD probe of job->url (port
// 443, headers only, no port-80 fallback).
void setup_easy(HostJob* job, const Config& cfg);

}  // namespace scanner
