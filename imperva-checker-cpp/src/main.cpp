// imperva_checker — C++ port of imperva_checker.py
//
// Async HTTPS CDN fingerprinter. Streams a list of hosts and saves the ones
// whose response carries an Imperva/Incapsula marker (X-CDN: Imperva header,
// X-Iinfo header, visid_incap/incap_ses cookies, or an "Imperva" body match).
//
// Design notes:
//   - Single-threaded libcurl multi-handle event loop, the C++ analogue of the
//     original's asyncio/aiohttp design: many transfers in flight, one thread,
//     no thread-per-request explosion.
//   - Streams the input file line-by-line (safe for multi-GB host lists).
//   - Adaptive concurrency: an in-flight cap that a lightweight monitor raises
//     or lowers based on the live error rate and latency.
//   - Silent network sanity check + latency-seeded starting concurrency.
//   - Optional HEAD-first mode, per-host retries, and text/CSV/JSON output.
//
// Build: see CMakeLists.txt (requires libcurl).

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// ---------- colors ----------

namespace col {
bool enabled = true;  // set from isatty() in main
const char* wrap(const char* code) { return enabled ? code : ""; }
#define RESET   col::wrap("\033[0m")
#define DIM     col::wrap("\033[2m")
#define RED     col::wrap("\033[91m")
#define GREEN   col::wrap("\033[92m")
#define YELLOW  col::wrap("\033[93m")
#define CYAN    col::wrap("\033[96m")
#define MAGENTA col::wrap("\033[95m")
#define BOLD    col::wrap("\033[1m")
}  // namespace col

// ---------- signal handling ----------

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

// ---------- string helpers ----------

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string normalize_host(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return "";
    if (line.find("://") == std::string::npos) line = "https://" + line;
    return line;
}

// ---------- marker detection ----------

// Returns a human-readable description of the first Imperva marker found, or
// nullopt. Scans the accumulated header block (case-insensitive) then the body.
static std::optional<std::string> find_imperva_marker(const std::string& headers,
                                                      const std::string& body) {
    std::istringstream hs(headers);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));
        std::string lk = to_lower(key);
        std::string lv = to_lower(val);

        if (lk == "x-cdn" && lv.find("imperva") != std::string::npos)
            return "header X-CDN: " + val;
        if (lk == "x-iinfo")
            return "header X-Iinfo (Imperva): " + val;
        if (lk == "set-cookie" &&
            (lv.find("visid_incap") != std::string::npos ||
             lv.find("incap_ses") != std::string::npos))
            return "Imperva/Incapsula cookie in Set-Cookie";
        if (lk.find("visid_incap") != std::string::npos ||
            lk.find("incap_ses") != std::string::npos)
            return "cookie/header hint: " + key;
    }

    if (to_lower(body).find("imperva") != std::string::npos)
        return "found 'Imperva' string in response body";

    return std::nullopt;
}

// Returns the value of a header by (case-insensitive) name. When the header
// appears more than once (e.g. across redirects), the last occurrence wins so
// the value reflects the final response.
static std::string find_header(const std::string& headers, const std::string& name) {
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

// ---------- config ----------

enum class Format { Text, Csv, Json };

// Which scanner to run.
//   Imperva — fingerprint hosts sitting behind Imperva/Incapsula.
//   Server  — probe port 443 (then 80) and report the Server header.
enum class Mode { Imperva, Server };

struct Config {
    std::string input;
    std::string output = "imperva_hosts.txt";
    int timeout = 8;
    bool verbose = false;
    int concurrency = 0;  // adaptive seed; 0 => auto-seed from latency probe
    int threads = 0;      // fixed concurrency (adaptive off); 0 => adaptive
    int retries = 0;      // per-host transport-error retries
    bool head_mode = false;
    Format format = Format::Text;
    Mode mode = Mode::Imperva;
    bool mode_explicit = false;  // mode chosen via flag (skip the menu)
};

// ---------- per-request state ----------

struct HostJob {
    std::string url;
    std::string headers;
    std::string body;
    size_t body_cap = 65536;
    std::chrono::steady_clock::time_point start;
    long status = 0;
    long port = 0;         // port actually connected to (server-scan)
    int attempts = 0;      // transport-error retries used so far
    bool force_get = false;    // HEAD-mode host that fell back to GET
    CURL* easy = nullptr;
};

static size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* job = static_cast<HostJob*>(userdata);
    job->headers.append(buffer, total);
    return total;
}

static size_t body_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* job = static_cast<HostJob*>(userdata);
    if (job->body.size() < job->body_cap) {
        size_t take = std::min(total, job->body_cap - job->body.size());
        job->body.append(ptr, take);
    }
    return total;  // consume everything so the transfer isn't aborted mid-stream
}

static void setup_easy(HostJob* job, const Config& cfg) {
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
    curl_easy_setopt(e, CURLOPT_USERAGENT, "Mozilla/5.0 (imperva-checker)");
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_PRIVATE, job);

    // Server scan needs only headers (probes 443 only, no port-80 fallback).
    // Imperva HEAD-first mode also fetches
    // headers only until a host proves it needs a GET fallback; otherwise pull
    // (and decode) the body so the body fingerprint can run.
    bool head_only = (cfg.mode == Mode::Server) || (cfg.head_mode && !job->force_get);
    if (head_only) {
        curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
    } else {
        curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, body_cb);
        curl_easy_setopt(e, CURLOPT_WRITEDATA, job);
        curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");  // decode gzip for body search
    }
}

// ---------- output writer ----------

static std::string csv_field(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

static std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

// Generic, column-based writer shared by both scanners. `cols` names the
// columns (used for the CSV header and JSON keys); each row supplies a
// pre-formatted `text_line` for text output plus one string per column.
struct OutputWriter {
    std::ofstream f;
    Format fmt;
    bool json_first = true;
    std::vector<std::string> cols;

    void begin() {
        if (fmt == Format::Csv) {
            for (size_t i = 0; i < cols.size(); i++) { if (i) f << ","; f << cols[i]; }
            f << "\n";
        } else if (fmt == Format::Json) {
            f << "[\n";
        }
        f.flush();
    }

    void row(const std::string& text_line, const std::vector<std::string>& vals) {
        if (fmt == Format::Text) {
            f << text_line << "\n";
        } else if (fmt == Format::Csv) {
            for (size_t i = 0; i < vals.size(); i++) { if (i) f << ","; f << csv_field(vals[i]); }
            f << "\n";
        } else {  // Json
            if (!json_first) f << ",\n";
            json_first = false;
            f << "  {";
            for (size_t i = 0; i < cols.size() && i < vals.size(); i++) {
                if (i) f << ",";
                f << json_str(cols[i]) << ":" << json_str(vals[i]);
            }
            f << "}";
        }
        f.flush();
    }

    void end() {
        if (fmt == Format::Json) f << (json_first ? "]\n" : "\n]\n");
        f.flush();
    }
};

// ---------- adaptive concurrency ----------

struct AdaptiveLimiter {
    int limit;
    int floor_v;
    int ceiling_v;

    AdaptiveLimiter(int initial, int fl = 5, int ceil = 500)
        : limit(initial), floor_v(fl), ceiling_v(ceil) {}

    void adjust(int new_limit) {
        limit = std::max(floor_v, std::min(ceiling_v, new_limit));
    }
};

// Sliding window of recent (success, latency_seconds) samples.
struct Window {
    std::deque<std::pair<bool, double>> samples;
    size_t maxlen = 50;
    void push(bool ok, double lat) {
        samples.emplace_back(ok, lat);
        if (samples.size() > maxlen) samples.pop_front();
    }
};

// Mirrors concurrency_monitor(): nudge the limit up or down from recent stats.
static void maybe_adjust(AdaptiveLimiter& lim, const Window& win) {
    if (win.samples.size() < 10) return;

    int errors = 0;
    double lat_sum = 0.0;
    for (auto& s : win.samples) {
        if (!s.first) errors++;
        lat_sum += s.second;
    }
    double error_rate = static_cast<double>(errors) / win.samples.size();
    double avg_latency = lat_sum / win.samples.size();

    if (error_rate > 0.25 || avg_latency > 6.0)
        lim.adjust(static_cast<int>(lim.limit * 0.6));       // struggling -> back off hard
    else if (error_rate > 0.10 || avg_latency > 3.0)
        lim.adjust(static_cast<int>(lim.limit * 0.85));      // some strain -> ease off
    else if (error_rate < 0.03 && avg_latency < 1.5)
        lim.adjust(lim.limit + std::max(2, static_cast<int>(lim.limit * 0.15)));  // happy -> ramp up
    // else: steady state
}

// ---------- network probes ----------

static bool network_up() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo("m.google.com", nullptr, &hints, &res);
    if (rc != 0) return false;
    freeaddrinfo(res);
    return true;
}

// Quick TCP-connect latency probe to seed a sane starting concurrency (silent).
static int seed_initial_concurrency() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("m.google.com", "443", &hints, &res) != 0 || !res)
        return 15;

    auto start = std::chrono::steady_clock::now();
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool ok = false;
    if (fd >= 0) {
        struct timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ok = (::connect(fd, res->ai_addr, res->ai_addrlen) == 0);
        ::close(fd);
    }
    freeaddrinfo(res);
    if (!ok) return 15;

    double rtt = std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start).count();
    if (rtt < 0.15) return 80;
    if (rtt < 0.40) return 40;
    return 15;
}

// ---------- CLI ----------

static std::string prompt(const std::string& msg, const std::string& def = "") {
    std::string suffix = def.empty() ? "" : " [" + def + "]";
    std::cout << CYAN << msg << suffix << ": " << RESET;
    std::string val;
    std::getline(std::cin, val);
    val = trim(val);
    return val.empty() ? def : val;
}

static void usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "      --mode M          scanner: imperva | server (menu if omitted)\n"
              << "                        imperva = CDN fingerprint; server = 443/80 + Server\n"
              << "      --server          shorthand for --mode server\n"
              << "  -i, --input FILE      hosts list (prompted if omitted)\n"
              << "  -o, --output FILE     matches output file (default imperva_hosts.txt)\n"
              << "  -t, --timeout SECS    per-request timeout (default 8)\n"
              << "  -c, --concurrency N   adaptive starting concurrency (default: auto-seed)\n"
              << "  -T, --threads N       fixed concurrency (disables auto-tuning)\n"
              << "  -r, --retries N       retry a host N times on transport error (default 0)\n"
              << "      --head            HEAD-first mode: fetch headers only, GET-fallback\n"
              << "                        when a host rejects HEAD (skips body fingerprint)\n"
              << "  -f, --format FMT      output format: text | csv | json (default text)\n"
              << "  -v, --verbose         print status for every host\n"
              << "  -h, --help            show this help\n";
}

static Format parse_format(const std::string& s) {
    std::string f = to_lower(s);
    if (f == "text") return Format::Text;
    if (f == "csv") return Format::Csv;
    if (f == "json") return Format::Json;
    std::cerr << "Invalid --format: " << s << " (use text|csv|json)\n";
    std::exit(2);
}

static bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--mode") {
            std::string m = to_lower(next("--mode"));
            if (m == "server") cfg.mode = Mode::Server;
            else if (m == "imperva") cfg.mode = Mode::Imperva;
            else { std::cerr << "Invalid --mode: " << m << " (use imperva|server)\n"; std::exit(2); }
            cfg.mode_explicit = true;
        }
        else if (a == "--server") { cfg.mode = Mode::Server; cfg.mode_explicit = true; }
        else if (a == "--imperva") { cfg.mode = Mode::Imperva; cfg.mode_explicit = true; }
        else if (a == "-i" || a == "--input") cfg.input = next("--input");
        else if (a == "-o" || a == "--output") cfg.output = next("--output");
        else if (a == "-t" || a == "--timeout") cfg.timeout = std::stoi(next("--timeout"));
        else if (a == "-c" || a == "--concurrency") cfg.concurrency = std::stoi(next("--concurrency"));
        else if (a == "-T" || a == "--threads") cfg.threads = std::stoi(next("--threads"));
        else if (a == "-r" || a == "--retries") cfg.retries = std::stoi(next("--retries"));
        else if (a == "--head") cfg.head_mode = true;
        else if (a == "-f" || a == "--format") cfg.format = parse_format(next("--format"));
        else if (a == "-v" || a == "--verbose") cfg.verbose = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); return false; }
        else { std::cerr << "Unknown argument: " << a << "\n"; usage(argv[0]); std::exit(2); }
    }
    if (cfg.retries < 0) cfg.retries = 0;
    if (cfg.threads < 0) cfg.threads = 0;
    return true;
}

// ---------- core scan ----------

struct Stats {
    long long checked = 0;
    long long matches = 0;
};

static void print_progress(const Stats& st, const AdaptiveLimiter& lim) {
    std::cout << "\r" << CYAN << "[checked " << st.checked << "]" << RESET << " "
              << GREEN << st.matches << " matches" << RESET << " "
              << MAGENTA << "| concurrency=" << lim.limit << RESET << "   "
              << std::flush;
}

static void finalize_host(HostJob* job, CURLcode res, Stats& st, Window& win,
                          OutputWriter& out, const AdaptiveLimiter& lim,
                          const Config& cfg) {
    double latency = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - job->start).count();
    bool ok = (res == CURLE_OK);
    win.push(ok, latency);
    st.checked++;

    if (cfg.mode == Mode::Server) {
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
            std::cout << "\r" << GREEN << "[OPEN] " << job->url
                      << "  :" << job->port << "  [" << job->status << "]  Server: "
                      << server << RESET << "\n";
        } else if (cfg.verbose) {
            std::cout << "\r" << RED << "[" << st.checked << "] " << job->url
                      << " -> closed/no HTTP: " << curl_easy_strerror(res) << RESET << "\n";
        }
    } else {  // Imperva
        if (ok) {
            auto marker = find_imperva_marker(job->headers, job->body);
            if (marker) {
                st.matches++;
                out.row(job->url, {job->url, std::to_string(job->status), *marker});
                std::cout << "\r" << GREEN << "[MATCH] " << job->url
                          << "  [" << job->status << "]  " << *marker << RESET << "\n";
            } else if (cfg.verbose) {
                std::cout << "\r" << DIM << "[" << st.checked << "] " << job->url
                          << " -> " << job->status << " (no match)" << RESET << "\n";
            }
        } else if (cfg.verbose) {
            std::cout << "\r" << RED << "[" << st.checked << "] " << job->url
                      << " -> error: " << curl_easy_strerror(res) << RESET << "\n";
        }
    }

    if (!cfg.verbose) print_progress(st, lim);
}

static Stats run_scan(const Config& cfg, int initial_concurrency, bool adaptive) {
    // When threads are pinned, clamp floor==ceiling==limit so nothing moves it.
    AdaptiveLimiter lim = adaptive
        ? AdaptiveLimiter(initial_concurrency)
        : AdaptiveLimiter(initial_concurrency, initial_concurrency, initial_concurrency);
    Window win;
    Stats st;

    std::ifstream in(cfg.input);
    OutputWriter out;
    out.fmt = cfg.format;
    out.cols = (cfg.mode == Mode::Server)
                   ? std::vector<std::string>{"host", "port", "status", "server"}
                   : std::vector<std::string>{"url", "status", "marker"};
    // Text appends (matches accumulate across runs); structured formats need a
    // single well-formed document, so they overwrite.
    out.f.open(cfg.output, cfg.format == Format::Text
                               ? (std::ios::out | std::ios::app)
                               : std::ios::out);
    out.begin();

    CURLM* multi = curl_multi_init();
    int active = 0;
    bool eof = false;

    auto next_host = [&]() -> std::optional<std::string> {
        std::string raw;
        while (std::getline(in, raw)) {
            std::string h = normalize_host(raw);
            if (!h.empty()) return h;
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
            curl_multi_remove_handle(multi, e);
            curl_easy_cleanup(e);
            job->easy = nullptr;

            // HEAD rejected by origin (405/501) -> retry this host once as GET.
            if (cfg.head_mode && !job->force_get && res == CURLE_OK &&
                (job->status == 405 || job->status == 501)) {
                job->force_get = true;
                job->headers.clear();
                job->body.clear();
                setup_easy(job, cfg);
                curl_multi_add_handle(multi, job->easy);
                continue;  // slot stays occupied; do not touch stats/active
            }

            // Transport error -> retry up to cfg.retries times.
            if (res != CURLE_OK && job->attempts < cfg.retries) {
                job->attempts++;
                job->headers.clear();
                job->body.clear();
                setup_easy(job, cfg);
                curl_multi_add_handle(multi, job->easy);
                continue;
            }

            finalize_host(job, res, st, win, out, lim, cfg);
            delete job;
            active--;
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
    std::cout << "\n";
    return st;
}

// ---------- menu ----------

static Mode prompt_menu() {
    std::cout << BOLD << CYAN << "Select a tool:" << RESET << "\n"
              << "  " << GREEN << "1)" << RESET << " Imperva CDN checker\n"
              << "  " << GREEN << "2)" << RESET << " Port / server scanner (probe 443, show Server)\n"
              << "  " << GREEN << "0)" << RESET << " Exit\n";
    std::string c = prompt("Choice", "1");
    if (c == "0") { std::cout << "Bye.\n"; std::exit(0); }
    if (c == "2") return Mode::Server;
    return Mode::Imperva;
}

// ---------- main ----------

int main(int argc, char** argv) {
    col::enabled = isatty(fileno(stdout));
    std::signal(SIGINT, on_sigint);

    Config cfg;
    if (!parse_args(argc, argv, cfg)) return 0;

    // Interactive when no input file was passed on the command line.
    bool interactive = cfg.input.empty();

    std::cout << BOLD << MAGENTA << "=== Way Scanner Toolkit (C++) ===\n\n" << RESET;

    // Menu picks the scanner unless one was chosen with a flag.
    if (interactive && !cfg.mode_explicit) cfg.mode = prompt_menu();

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << RED << "Failed to initialize libcurl." << RESET << "\n";
        return 1;
    }

    // Silent network sanity check.
    if (!network_up()) {
        std::cout << YELLOW
                  << "Could not resolve m.google.com — looks like a DNS/network issue."
                  << RESET << "\n";
        std::cout << YELLOW << "Continue anyway? (y/N): " << RESET;
        std::string ans;
        std::getline(std::cin, ans);
        if (to_lower(trim(ans)) != "y") {
            std::cout << RED << "Aborted. Fix your network/DNS and try again." << RESET << "\n";
            curl_global_cleanup();
            return 1;
        }
    }

    // Resolve input/output interactively when not passed as args.
    if (cfg.input.empty()) {
        cfg.input = prompt("Enter path to hosts .txt file");
        while (cfg.input.empty())
            cfg.input = prompt("Please enter a valid path to hosts .txt file");
    }
    {
        std::ifstream test(cfg.input);
        if (!test) {
            std::cerr << RED << "Can't open " << cfg.input << RESET << "\n";
            curl_global_cleanup();
            return 1;
        }
    }
    if (interactive) {  // also prompt for output and threads
        // Sensible default output name per tool.
        if (cfg.mode == Mode::Server && cfg.output == "imperva_hosts.txt")
            cfg.output = "servers.txt";
        cfg.output = prompt("Enter output file name for results", cfg.output);
        std::string tstr = prompt("Threads (a number, or blank for auto)");
        if (!tstr.empty()) {
            try {
                int t = std::stoi(tstr);
                if (t > 0) cfg.threads = t;
            } catch (...) {
                std::cout << YELLOW << "Not a number — using auto-concurrency." << RESET << "\n";
            }
        }
    }

    bool adaptive = (cfg.threads <= 0);
    int initial = adaptive
        ? (cfg.concurrency > 0 ? cfg.concurrency : seed_initial_concurrency())
        : cfg.threads;

    // Timestamp banner.
    std::time_t tt = std::time(nullptr);
    char ts[16];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&tt));
    const char* reqmode = cfg.mode == Mode::Server ? "HEAD 443"
                          : (cfg.head_mode ? "HEAD-first" : "GET");
    const char* tool = cfg.mode == Mode::Server ? "server scan" : "Imperva scan";
    const char* fmt = cfg.format == Format::Csv ? "csv"
                      : cfg.format == Format::Json ? "json" : "text";
    std::string conc = adaptive
        ? "auto-concurrency, seeded at " + std::to_string(initial)
        : std::to_string(initial) + " threads (fixed)";
    std::cout << "\n" << DIM << "[" << ts << "]" << RESET
              << " Starting " << tool << " of " << CYAN << cfg.input << RESET
              << " (" << conc << ", " << reqmode
              << ", retries=" << cfg.retries << "), output=" << CYAN << cfg.output
              << RESET << " [" << fmt << "]\n\n";

    Stats st = run_scan(cfg, initial, adaptive);

    const char* found = cfg.mode == Mode::Server ? " responded (Server shown)."
                                                 : " matched Imperva marker.";
    std::cout << "\n" << BOLD << GREEN << "Done." << RESET << " "
              << st.checked << " hosts checked, "
              << GREEN << st.matches << RESET << found << "\n";
    std::cout << "Saved to " << CYAN << cfg.output << RESET << "\n";

    curl_global_cleanup();
    return 0;
}
