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

void setup_easy(HostJob* job, const Config& cfg) {
    CURL* e = curl_easy_init();
    job->easy = e;
    job->start = std::chrono::steady_clock::now();

    curl_easy_setopt(e, CURLOPT_URL, job->url.c_str());
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(e, CURLOPT_TIMEOUT, static_cast<long>(cfg.timeout));
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg.timeout));
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, job);
    curl_easy_setopt(e, CURLOPT_USERAGENT, "Mozilla/5.0 (server-scanner)");
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_PRIVATE, job);

    // Server scan needs only headers: probe 443 with HEAD, no port-80 fallback.
    curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
}

}  // namespace scanner
