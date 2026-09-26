#include "scanner/http.hpp"

#include <sstream>

#include "scanner/strutil.hpp"

namespace scanner {

static size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* job = static_cast<HostJob*>(userdata);
    job->headers.append(buffer, total);
    return total;
}

std::string find_header(const std::string& headers, const std::string& name) {
    std::string want = to_lower(name);
    std::istringstream hs(headers);
    std::string line, result;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        if (to_lower(trim(line.substr(0, pos))) == want)
            result = trim(line.substr(pos + 1));
    }
    return result;
}

curl_slist* build_header_list(const Config& cfg) {
    curl_slist* list = nullptr;
    for (const auto& h : cfg.headers) list = curl_slist_append(list, h.c_str());
    return list;
}

void setup_easy(HostJob* job, const Config& cfg, curl_slist* extra_headers) {
    // Reuse the handle if one is already attached (from a pool); otherwise make
    // a fresh one. Reset clears all prior options but keeps the connection cache.
    CURL* e = job->easy;
    if (e) curl_easy_reset(e);
    else e = curl_easy_init();
    job->easy = e;
    job->start = std::chrono::steady_clock::now();

    curl_easy_setopt(e, CURLOPT_URL, job->url.c_str());
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, cfg.no_follow ? 0L : 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 5L);
    // Only ever follow redirects to http/https, never to file:// gopher:// etc.
    // (reported Server/status/IP reflect the final hop when following.)
#if LIBCURL_VERSION_NUM >= 0x075500  /* 7.85.0: the string form supersedes the bitmask */
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(e, CURLOPT_TIMEOUT, static_cast<long>(cfg.timeout));
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg.timeout));
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, job);
    curl_easy_setopt(e, CURLOPT_USERAGENT,
                     cfg.user_agent.empty() ? "Mozilla/5.0 (server-scanner)"
                                            : cfg.user_agent.c_str());
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_PRIVATE, job);

    // Server scan needs only headers: probe with HEAD, no port-80 fallback.
    curl_easy_setopt(e, CURLOPT_NOBODY, 1L);

    // ----- performance tuning -----
    // Try HTTP/2 (falls back to 1.1); keepalive so cached connections survive;
    // a small DNS cache; a modest buffer since we only read headers.
    curl_easy_setopt(e, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(e, CURLOPT_PIPEWAIT, 1L);
    curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(e, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(e, CURLOPT_BUFFERSIZE, 16384L);

    // ----- optional per-request settings -----
    if (job->port_req > 0)
        curl_easy_setopt(e, CURLOPT_PORT, job->port_req);
    if (!cfg.proxy.empty())
        curl_easy_setopt(e, CURLOPT_PROXY, cfg.proxy.c_str());
    if (cfg.ip_family == 4)
        curl_easy_setopt(e, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    else if (cfg.ip_family == 6)
        curl_easy_setopt(e, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
    if (extra_headers)
        curl_easy_setopt(e, CURLOPT_HTTPHEADER, extra_headers);
}

}  // namespace scanner
