#include "scanner/http.hpp"

#include <cctype>
#include <cstring>

#include "scanner/strutil.hpp"

namespace scanner {

// libcurl calls this once per response header line. We only ever need the
// Server header, so capture just that (last occurrence wins, preserving the
// across-redirects semantics) instead of buffering every header byte.
static size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* job = static_cast<HostJob*>(userdata);
    static const char kName[] = "server";  // 6 chars, then ':'
    if (total >= 7) {
        bool match = true;
        for (int i = 0; i < 6; i++) {
            if (std::tolower(static_cast<unsigned char>(buffer[i])) != kName[i]) { match = false; break; }
        }
        if (match && buffer[6] == ':')
            job->server = trim(std::string(buffer + 7, total - 7));
    }
    return total;
}

curl_slist* build_header_list(const Config& cfg) {
    curl_slist* list = nullptr;
    for (const auto& h : cfg.headers) list = curl_slist_append(list, h.c_str());
    return list;
}

bool setup_easy(HostJob* job, const Config& cfg, curl_slist* extra_headers) {
    // Reuse the handle if one is attached (from a pool); otherwise make a fresh
    // one. Reset clears prior options but keeps the connection cache.
    CURL* e = job->easy;
    if (e) curl_easy_reset(e);
    else e = curl_easy_init();
    if (!e) { job->easy = nullptr; return false; }
    job->easy = e;
    job->start = std::chrono::steady_clock::now();

    long tmo_ms = static_cast<long>(cfg.timeout) * 1000L;
    long conn_ms = static_cast<long>(cfg.connect_timeout > 0 ? cfg.connect_timeout
                                                             : cfg.timeout) * 1000L;

    curl_easy_setopt(e, CURLOPT_URL, job->url.c_str());
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, cfg.no_follow ? 0L : 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(e, CURLOPT_TIMEOUT_MS, tmo_ms);
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, conn_ms);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, cfg.verify_tls ? 1L : 0L);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, cfg.verify_tls ? 2L : 0L);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, job);
    curl_easy_setopt(e, CURLOPT_USERAGENT,
                     cfg.user_agent.empty() ? "Mozilla/5.0 (server-scanner)"
                                            : cfg.user_agent.c_str());
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_PRIVATE, job);

    // Server scan needs only headers: probe with HEAD, no port-80 fallback.
    curl_easy_setopt(e, CURLOPT_NOBODY, 1L);

    // Restrict both the primary request and any redirect to http/https so a
    // crafted or fat-fingered input line can't reach file://, gopher://, etc.
#if LIBCURL_VERSION_NUM >= 0x075500  /* 7.85.0: the string form supersedes the bitmask */
    curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(e, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(e, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

    // Each host is a distinct origin probed once, so HTTP/2 negotiation and
    // keepalive don't pay off; pin 1.1 and keep only a DNS cache + small buffer.
    curl_easy_setopt(e, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(e, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(e, CURLOPT_BUFFERSIZE, 4096L);

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
    return true;
}

}  // namespace scanner
