#include "scanner/scan.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "scanner/cidr.hpp"
#include "scanner/color.hpp"
#include "scanner/concurrency.hpp"
#include "scanner/http.hpp"
#include "scanner/netprobe.hpp"
#include "scanner/output.hpp"
#include "scanner/strutil.hpp"

namespace scanner {

volatile std::sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// ---------- per-CDN output ----------

// Lazily-opened per-provider output. Each provider's file is opened on its first
// hit and kept open for the rest of the run; a fresh run truncates it, a resumed
// run appends. Kept entirely separate from the user's own results file.
struct CdnWriter {
    std::string dir;
    bool resuming = false;
    std::map<std::string, std::ofstream> streams;

    void write(const std::string& provider, const std::string& host) {
        auto it = streams.find(provider);
        if (it == streams.end()) {
            std::ofstream& f = streams[provider];
            std::string path = dir.empty() ? provider + ".txt"
                                           : dir + "/" + provider + ".txt";
            f.open(path, resuming ? (std::ios::out | std::ios::app)
                                  : (std::ios::out | std::ios::trunc));
            it = streams.find(provider);
        }
        it->second << host << "\n";
        it->second.flush();
    }
};

// ---------- rate limiter ----------

// Token bucket: take(want) returns how many of `want` new requests may start now
// without blocking, so the poll loop keeps reaping while we throttle dispatch.
struct RateLimiter {
    double rate = 0.0;      // tokens/sec; <=0 disables limiting
    double allowance = 0.0;
    std::chrono::steady_clock::time_point last;
    bool started = false;

    int take(int want) {
        if (rate <= 0.0 || want <= 0) return want;
        auto now = std::chrono::steady_clock::now();
        if (!started) { last = now; allowance = rate; started = true; }
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        allowance += dt * rate;
        if (allowance > rate) allowance = rate;  // burst capped at ~1s
        int n = static_cast<int>(allowance);
        if (n > want) n = want;
        allowance -= n;
        return n;
    }
};

// ---------- shared helpers ----------

// Count non-blank, non-comment lines so progress can show a total + ETA. Returns
// -1 for stdin (unknown total).
static long long count_hosts(const std::string& path) {
    if (path == "-") return -1;
    std::ifstream f(path);
    if (!f) return -1;
    long long n = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (!t.empty() && t[0] != '#') n++;
    }
    return n;
}

// Open the results writer with the correct append/overwrite + header behavior.
static void open_output(const Config& cfg, bool resuming, OutputWriter& out,
                        const std::vector<std::string>& cols) {
    out.fmt = cfg.format;
    out.cols = cols;
    // Text and ndjson stream and accumulate; csv/json are single documents that
    // overwrite — unless resuming, when we append so prior rows survive.
    bool append_out = (cfg.format == Format::Text) ||
                      (cfg.format == Format::Ndjson) || resuming;
    if (resuming && cfg.format == Format::Csv) {
        std::ifstream probe(cfg.output);
        if (probe.peek() != std::ifstream::traits_type::eof()) out.suppress_header = true;
    }
    out.f.open(cfg.output, append_out ? (std::ios::out | std::ios::app) : std::ios::out);
    out.begin();
}

// Load the resume checkpoint (set of completed keys).
static std::set<std::string> load_done(const std::string& cache_path, bool& resuming) {
    std::set<std::string> done;
    if (resuming) {
        std::ifstream cf(cache_path);
        std::string h;
        while (std::getline(cf, h)) { h = trim(h); if (!h.empty()) done.insert(h); }
        if (done.empty()) resuming = false;
    }
    return done;
}

// Classify an IP and, on a match, write the host to <provider>.txt and bump the
// per-provider counter. Returns the provider name (or nullopt).
static std::optional<std::string> classify_hit(const CidrDb& cdn, CdnWriter& cdnw,
                                               Stats& st, const std::string& ip,
                                               const std::string& host) {
    if (cdn.empty() || ip.empty()) return std::nullopt;
    auto prov = cdn.classify(ip);
    if (prov) {
        cdnw.write(*prov, host);
        st.cdn_counts[*prov]++;
    }
    return prov;
}

static void print_progress(const char* verb, long long done, long long total,
                           long long resp, const char* resp_word,
                           long long cdnm, double per_s, const char* tail) {
    std::cout << "\r" << CYAN << "[" << verb << " " << done;
    if (total >= 0) std::cout << "/" << total;
    std::cout << "]" << RESET << " " << GREEN << resp << " " << resp_word << RESET;
    if (cdnm > 0) std::cout << " " << MAGENTA << "cdn=" << cdnm << RESET;
    if (per_s > 0) std::cout << " " << DIM << (long long)per_s << "/s" << RESET;
    if (total > 0 && per_s > 0 && done < total) {
        long long eta = (long long)((total - done) / per_s);
        std::cout << " " << DIM << "ETA " << (eta / 60) << "m" << (eta % 60) << "s" << RESET;
    }
    if (tail && *tail) std::cout << " " << MAGENTA << tail << RESET;
    std::cout << "   " << std::flush;
}

// ---------- HTTP scan (multi-handle) ----------

static Stats run_http(const Config& cfg, int initial_concurrency, bool adaptive,
                      const CidrDb& cdn, long long total) {
    AdaptiveLimiter lim = adaptive
        ? AdaptiveLimiter(initial_concurrency)
        : AdaptiveLimiter(initial_concurrency, initial_concurrency, initial_concurrency);
    Window win;
    Stats st;

    std::ifstream fin;
    if (cfg.input != "-") fin.open(cfg.input);
    std::istream& in = (cfg.input == "-") ? std::cin : static_cast<std::istream&>(fin);

    std::string cache_path = cfg.output + ".cache";
    bool resuming = (cfg.resume == 1);
    std::set<std::string> done = load_done(cache_path, resuming);

    OutputWriter out;
    open_output(cfg, resuming, out, {"host", "port", "status", "server"});
    std::ofstream cache(cache_path, resuming ? (std::ios::out | std::ios::app)
                                             : std::ios::out);
    CdnWriter cdnw;
    cdnw.dir = cfg.cdn_out_dir;
    cdnw.resuming = resuming;

    curl_slist* extra_headers = build_header_list(cfg);
    RateLimiter limiter;
    limiter.rate = cfg.rate;

    // Object + handle pools to avoid per-request new/delete and easy init/cleanup.
    std::vector<HostJob*> job_pool;
    std::vector<CURL*> easy_pool;
    const size_t max_pool = std::max<size_t>(256, static_cast<size_t>(initial_concurrency) * 2);
    auto acquire_job = [&]() -> HostJob* {
        HostJob* j = job_pool.empty() ? new HostJob{} : job_pool.back();
        if (!job_pool.empty()) job_pool.pop_back();
        CURL* reuse = nullptr;
        if (!easy_pool.empty()) { reuse = easy_pool.back(); easy_pool.pop_back(); }
        *j = HostJob{};
        j->easy = reuse;  // setup_easy resets it; nullptr => it inits a new one
        return j;
    };
    auto release_job = [&](HostJob* j) {
        if (j->easy) {
            if (easy_pool.size() < max_pool) { curl_easy_reset(j->easy); easy_pool.push_back(j->easy); }
            else curl_easy_cleanup(j->easy);
            j->easy = nullptr;
        }
        if (job_pool.size() < max_pool) job_pool.push_back(j);
        else delete j;
    };

    // Host -> per-port job expansion, honoring dedup / resume-skip / limit.
    const std::vector<int> ports = cfg.ports.empty() ? std::vector<int>{0} : cfg.ports;
    const bool single_default = (ports.size() == 1 && ports[0] == 0);
    std::set<std::string> seen;
    long long dispatched_hosts = 0, skipped = 0;
    std::deque<HostJob*> pending;

    auto next_job = [&]() -> HostJob* {
        while (pending.empty()) {
            std::string raw;
            if (!std::getline(in, raw)) return nullptr;
            std::string h = normalize_host(raw);
            if (h.empty()) continue;
            if (cfg.dedup && !seen.insert(h).second) continue;
            if (cfg.limit && dispatched_hosts >= cfg.limit) return nullptr;
            dispatched_hosts++;
            for (int p : ports) {
                std::string key = single_default ? h : h + "|" + std::to_string(p);
                if (!done.empty() && done.count(key)) { skipped++; continue; }
                HostJob* job = acquire_job();
                job->url = h;
                job->port_req = p;
                job->key = key;
                pending.push_back(job);
            }
        }
        HostJob* job = pending.front();
        pending.pop_front();
        return job;
    };

    auto finalize = [&](HostJob* job, CURLcode res) {
        double latency = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - job->start).count();
        bool ok = (res == CURLE_OK);
        win.push(ok, latency);
        st.checked++;
        if (ok) {
            std::string server = find_header(job->headers, "server");
            if (server.empty()) server = "(unknown)";
            std::string pstr = std::to_string(job->port);
            std::string sstr = std::to_string(job->status);
            auto prov = classify_hit(cdn, cdnw, st, job->ip, job->url);
            if (!cfg.cdn_only || prov) {
                st.matches++;
                std::string line = job->url + "\t" + pstr + "\t" + sstr + "\t" + server;
                out.row(line, {job->url, pstr, sstr, server});
                std::string note = prov ? "  " + std::string(CYAN) + "(" + *prov + ")" + RESET : "";
                std::cout << "\r" << GREEN << "[OPEN] " << job->url
                          << "  :" << job->port << "  [" << job->status << "]  Server: "
                          << server << RESET << note << "\n";
            }
        } else if (cfg.verbose) {
            std::cout << "\r" << RED << "[" << st.checked << "] " << job->url
                      << " -> closed/no HTTP: " << curl_easy_strerror(res) << RESET << "\n";
        }
    };

    CURLM* multi = curl_multi_init();
    long maxconn = adaptive ? 500L : static_cast<long>(initial_concurrency);
    curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, maxconn);
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, maxconn);

    int active = 0;
    bool eof = false;
    auto scan_start = std::chrono::steady_clock::now();
    auto last_adjust = scan_start;

    while (!g_stop) {
        int budget = limiter.take(lim.limit - active);
        while (!eof && active < lim.limit && budget > 0) {
            HostJob* job = next_job();
            if (!job) { eof = true; break; }
            setup_easy(job, cfg, extra_headers);
            curl_multi_add_handle(multi, job->easy);
            active++;
            budget--;
        }

        if (eof && active == 0) break;

        int still_running = 0;
        curl_multi_perform(multi, &still_running);
        curl_multi_poll(multi, nullptr, 0, 100, nullptr);

        CURLMsg* msg;
        int msgs_left = 0;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg != CURLMSG_DONE) continue;
            CURL* e = msg->easy_handle;
            CURLcode res = msg->data.result;
            HostJob* job = nullptr;
            curl_easy_getinfo(e, CURLINFO_PRIVATE, &job);
            curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &job->status);
            curl_easy_getinfo(e, CURLINFO_PRIMARY_PORT, &job->port);
            char* ipbuf = nullptr;
            if (curl_easy_getinfo(e, CURLINFO_PRIMARY_IP, &ipbuf) == CURLE_OK && ipbuf)
                job->ip = ipbuf;
            curl_multi_remove_handle(multi, e);  // keep the handle for reuse

            if (res != CURLE_OK && job->attempts < cfg.retries) {
                job->attempts++;
                job->headers.clear();
                job->ip.clear();
                setup_easy(job, cfg, extra_headers);  // resets the same handle
                curl_multi_add_handle(multi, job->easy);
                continue;
            }

            finalize(job, res);
            cache << job->key << "\n";
            cache.flush();
            release_job(job);
            active--;
        }

        if (!cfg.verbose) {
            double el = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - scan_start).count();
            print_progress("checked", st.checked, total, st.matches, "responded",
                           0, el > 0 ? st.checked / el : 0,
                           ("concurrency=" + std::to_string(lim.limit)).c_str());
        }

        if (!cfg.no_pause && win.samples.size() >= win.maxlen) {
            bool all_failed = true;
            for (auto& s : win.samples) if (s.first) { all_failed = false; break; }
            if (all_failed && !network_up(cfg.probe_host)) {
                std::cout << "\n" << YELLOW << "Network appears down — pausing scan…"
                          << RESET << std::endl;
                while (!g_stop && !network_up(cfg.probe_host))
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                if (!g_stop)
                    std::cout << GREEN << "Network restored — resuming." << RESET << std::endl;
                win.samples.clear();
            }
        }

        if (adaptive) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_adjust).count() >= 2.5) {
                maybe_adjust(lim, win);
                last_adjust = now;
            }
        }
    }

    out.end();
    // Any jobs still queued (limit/g_stop) never ran: recycle them.
    for (HostJob* j : pending) release_job(j);
    curl_multi_cleanup(multi);
    for (CURL* e : easy_pool) curl_easy_cleanup(e);
    for (HostJob* j : job_pool) delete j;
    if (extra_headers) curl_slist_free_all(extra_headers);
    cache.close();
    st.skipped = skipped;
    if (!g_stop) std::remove(cache_path.c_str());
    std::cout << "\n";
    return st;
}

// ---------- DNS-only scan (thread pool) ----------

static Stats run_resolve(const Config& cfg, const CidrDb& cdn, long long total) {
    Stats st;

    std::ifstream fin;
    if (cfg.input != "-") fin.open(cfg.input);
    std::istream& in = (cfg.input == "-") ? std::cin : static_cast<std::istream&>(fin);

    std::string cache_path = cfg.output + ".cache";
    bool resuming = (cfg.resume == 1);
    std::set<std::string> done = load_done(cache_path, resuming);

    OutputWriter out;
    open_output(cfg, resuming, out, {"host", "ip", "cdn"});
    std::ofstream cache(cache_path, resuming ? (std::ios::out | std::ios::app)
                                             : std::ios::out);
    CdnWriter cdnw;
    cdnw.dir = cfg.cdn_out_dir;
    cdnw.resuming = resuming;

    RateLimiter limiter;
    limiter.rate = cfg.rate;

    std::mutex mtx;
    std::set<std::string> seen;
    long long dispatched = 0, skipped = 0;
    auto scan_start = std::chrono::steady_clock::now();

    // Pull the next host to resolve under the lock (dedup/resume/limit/rate).
    auto next_host = [&](std::string& out_host) -> bool {
        for (;;) {
            if (g_stop) return false;
            if (cfg.rate > 0 && limiter.take(1) == 0) return true;  // caller retries after a nap
            std::string raw;
            if (!std::getline(in, raw)) return false;
            std::string h = normalize_host(raw);
            if (h.empty()) continue;
            if (cfg.dedup && !seen.insert(h).second) continue;
            if (cfg.limit && dispatched >= cfg.limit) return false;
            if (!done.empty() && done.count(h)) { skipped++; continue; }
            dispatched++;
            out_host = h;
            return true;
        }
    };

    int nthreads = cfg.threads > 0 ? cfg.threads
                   : cfg.resolve_threads > 0 ? cfg.resolve_threads : 32;
    std::vector<std::thread> workers;
    for (int t = 0; t < nthreads; t++) {
        workers.emplace_back([&]() {
            for (;;) {
                std::string host;
                bool got;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    got = next_host(host);
                }
                if (!got) return;   // truly finished: EOF, --limit reached, or stopped
                if (host.empty()) {  // rate-limited this tick; back off and retry
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    continue;
                }
                std::string ip;
                auto r = resolve_ip(url_host(host), cfg.ip_family);
                if (r) ip = *r;

                std::lock_guard<std::mutex> lk(mtx);
                st.checked++;
                if (!ip.empty()) {
                    auto prov = classify_hit(cdn, cdnw, st, ip, host);
                    if (!cfg.cdn_only || prov) {
                        st.matches++;
                        std::string cdnname = prov ? *prov : "";
                        out.row(host + "\t" + ip + "\t" + cdnname, {host, ip, cdnname});
                        if (cfg.verbose)
                            std::cout << "\r" << GREEN << "[IP] " << host << " -> " << ip
                                      << RESET << (prov ? "  " + std::string(CYAN) + "(" + *prov + ")" + RESET : "")
                                      << "\n";
                    }
                } else if (cfg.verbose) {
                    std::cout << "\r" << RED << "[--] " << host << " -> no DNS" << RESET << "\n";
                }
                cache << host << "\n";
                cache.flush();
                if (!cfg.verbose) {
                    double el = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - scan_start).count();
                    long long cdnm = 0;
                    for (auto& kv : st.cdn_counts) cdnm += kv.second;
                    print_progress("resolved", st.checked, total, st.matches, "with-ip",
                                   cdnm, el > 0 ? st.checked / el : 0, nullptr);
                }
            }
        });
    }
    for (auto& w : workers) w.join();

    out.end();
    cache.close();
    st.skipped = skipped;
    if (!g_stop) std::remove(cache_path.c_str());
    std::cout << "\n";
    return st;
}

// ---------- dispatcher ----------

Stats run_scan(const Config& cfg, int initial_concurrency, bool adaptive,
               const CidrDb& cdn) {
    long long total = count_hosts(cfg.input);
    if (cfg.resolve_only) return run_resolve(cfg, cdn, total);
    return run_http(cfg, initial_concurrency, adaptive, cdn, total);
}

}  // namespace scanner
