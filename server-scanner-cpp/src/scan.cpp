#include "scanner/scan.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include <curl/curl.h>

#include <map>

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

static void print_progress(const Stats& st, const AdaptiveLimiter& lim) {
    std::cout << "\r" << CYAN << "[checked " << st.checked << "]" << RESET << " "
              << GREEN << st.matches << " responded" << RESET << " "
              << MAGENTA << "| concurrency=" << lim.limit << RESET << "   "
              << std::flush;
}

static void finalize_host(HostJob* job, CURLcode res, Stats& st, Window& win,
                          OutputWriter& out, const AdaptiveLimiter& lim,
                          const Config& cfg, const CidrDb& cdn, CdnWriter& cdnw) {
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
        st.matches++;
        // Tab-separated text line so the file carries the details, not just
        // the host (host \t port \t status \t server).
        std::string line = job->url + "\t" + pstr + "\t" + sstr + "\t" + server;
        out.row(line, {job->url, pstr, sstr, server});

        // CDN classification is additive: it writes to <provider>.txt only and
        // never touches the user's own results file (`out`) above.
        std::string cdn_note;
        if (!cdn.empty() && !job->ip.empty()) {
            auto prov = cdn.classify(job->ip);
            if (prov) {
                cdnw.write(*prov, job->url);
                st.cdn_counts[*prov]++;
                cdn_note = "  " + std::string(CYAN) + "(" + *prov + ")" + RESET;
            }
        }

        std::cout << "\r" << GREEN << "[OPEN] " << job->url
                  << "  :" << job->port << "  [" << job->status << "]  Server: "
                  << server << RESET << cdn_note << "\n";
    } else if (cfg.verbose) {
        std::cout << "\r" << RED << "[" << st.checked << "] " << job->url
                  << " -> closed/no HTTP: " << curl_easy_strerror(res) << RESET << "\n";
    }

    if (!cfg.verbose) print_progress(st, lim);
}

Stats run_scan(const Config& cfg, int initial_concurrency, bool adaptive,
               const CidrDb& cdn) {
    // When threads are pinned, clamp floor==ceiling==limit so nothing moves it.
    AdaptiveLimiter lim = adaptive
        ? AdaptiveLimiter(initial_concurrency)
        : AdaptiveLimiter(initial_concurrency, initial_concurrency, initial_concurrency);
    Window win;
    Stats st;

    std::ifstream in(cfg.input);

    // ----- resume checkpoint -----
    // A ".cache" file lists every host already checked. While a scan is
    // running (unfinished) it persists progress; on clean completion it is
    // deleted. On resume, already-checked hosts are skipped.
    std::string cache_path = cfg.output + ".cache";
    bool resuming = (cfg.resume == 1);
    std::set<std::string> done;
    if (resuming) {
        std::ifstream cf(cache_path);
        std::string h;
        while (std::getline(cf, h)) { h = trim(h); if (!h.empty()) done.insert(h); }
        if (done.empty()) resuming = false;
    }

    OutputWriter out;
    out.fmt = cfg.format;
    out.cols = {"host", "port", "status", "server"};
    // Text appends (results accumulate across runs); structured formats
    // overwrite — unless we are resuming, in which case output is appended so
    // the prior run's rows survive.
    bool append_out = (cfg.format == Format::Text) || resuming;
    // A resumed CSV appends onto its existing header, so don't write it again.
    if (resuming && cfg.format == Format::Csv) {
        std::ifstream probe(cfg.output);
        if (probe.peek() != std::ifstream::traits_type::eof()) out.suppress_header = true;
    }
    out.f.open(cfg.output, append_out ? (std::ios::out | std::ios::app) : std::ios::out);
    out.begin();

    // Progress checkpoint stream: append when resuming, else start fresh.
    std::ofstream cache(cache_path, resuming ? (std::ios::out | std::ios::app)
                                             : std::ios::out);

    // Per-CDN output. Mirrors the checkpoint's resume behavior: fresh runs
    // truncate each provider file on first hit, resumed runs append.
    CdnWriter cdnw;
    cdnw.dir = cfg.cdn_out_dir;
    cdnw.resuming = resuming;

    CURLM* multi = curl_multi_init();
    int active = 0;
    bool eof = false;

    long long skipped = 0;
    auto next_host = [&]() -> std::optional<std::string> {
        std::string raw;
        while (std::getline(in, raw)) {
            std::string h = normalize_host(raw);
            if (h.empty()) continue;
            if (!done.empty() && done.count(h)) { skipped++; continue; }  // resume: already checked
            return h;
        }
        return std::nullopt;
    };

    auto last_adjust = std::chrono::steady_clock::now();

    while (!g_stop) {
        // Fill up to the current in-flight cap.
        while (!eof && active < lim.limit) {
            auto h = next_host();
            if (!h) { eof = true; break; }
            auto* job = new HostJob{};
            job->url = *h;
            setup_easy(job, cfg);
            curl_multi_add_handle(multi, job->easy);
            active++;
        }

        if (eof && active == 0) break;

        int still_running = 0;
        curl_multi_perform(multi, &still_running);
        curl_multi_poll(multi, nullptr, 0, 100, nullptr);

        // Reap completed transfers.
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
                job->ip = ipbuf;  // valid until cleanup; copied into the job now
            curl_multi_remove_handle(multi, e);
            curl_easy_cleanup(e);
            job->easy = nullptr;

            // Transport error -> retry up to cfg.retries times.
            if (res != CURLE_OK && job->attempts < cfg.retries) {
                job->attempts++;
                job->headers.clear();
                job->ip.clear();
                setup_easy(job, cfg);
                curl_multi_add_handle(multi, job->easy);
                continue;
            }

            finalize_host(job, res, st, win, out, lim, cfg, cdn, cdnw);
            cache << job->url << "\n";  // checkpoint: this host is done
            cache.flush();
            delete job;
            active--;
        }

        // Mid-scan network-loss guard: if the whole recent window failed and
        // DNS is genuinely down, pause (don't burn through the rest of the list
        // as failures) and wait until connectivity returns.
        if (!cfg.no_pause && win.samples.size() >= win.maxlen) {
            bool all_failed = true;
            for (auto& s : win.samples) if (s.first) { all_failed = false; break; }
            if (all_failed && !network_up()) {
                std::cout << "\n" << YELLOW << "Network appears down — pausing scan…"
                          << RESET << std::endl;
                while (!g_stop && !network_up())
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                if (!g_stop)
                    std::cout << GREEN << "Network restored — resuming." << RESET << std::endl;
                win.samples.clear();  // fresh slate so we don't immediately re-trigger
            }
        }

        // Adaptive concurrency monitor (every ~2.5s); skipped when threads pinned.
        if (adaptive) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_adjust).count() >= 2.5) {
                maybe_adjust(lim, win);
                last_adjust = now;
            }
        }
    }

    out.end();
    curl_multi_cleanup(multi);
    cache.close();
    st.skipped = skipped;
    // Finished cleanly -> drop the checkpoint. Interrupted (Ctrl+C) -> keep it
    // so the next run resumes.
    if (!g_stop) std::remove(cache_path.c_str());
    std::cout << "\n";
    return st;
}

}  // namespace scanner
