# Way Server Scanner (C++)

A fast HTTP(S) **port / server scanner**: it probes port **443** for each host
and reports the host's `Server` header.

It runs on a single-threaded [libcurl](https://curl.se/libcurl/) multi-handle
event loop — many transfers in flight on one thread, so there is no
thread-per-request explosion.

## What it does

For each host it sends a `HEAD` request to `https://` (port 443). Every host
that responds is reported with the port it answered on, the HTTP status, and its
`Server` header; hosts that don't answer on 443 are reported as closed. There is
no automatic fallback to port 80 — a host listed explicitly as `http://…` is
still honored on 80, but bare hostnames are probed on 443 only. Output columns
are `host, port, status, server` (text output is tab-separated).

## Project layout

The code is split by responsibility. Headers live under `include/scanner/` and
their implementations under `src/`:

| Module | Header | Responsibility |
| --- | --- | --- |
| colors | `color.hpp` | TTY-aware color escape helpers |
| strings | `strutil.hpp` | trim / lowercase / host-line normalization |
| config | `config.hpp` | `Config`, CLI parsing, usage, interactive prompts |
| output | `output.hpp` | text / CSV / JSON result writer |
| concurrency | `concurrency.hpp` | adaptive in-flight limiter + sliding window |
| net probes | `netprobe.hpp` | connectivity check + latency-seeded concurrency |
| http | `http.hpp` | libcurl per-host setup + header lookup |
| scan | `scan.hpp` | the multi-handle scan loop + stats |
| entrypoint | `src/main.cpp` | thin orchestrator wiring the modules together |

## CDN classification (optional)

Point `--cidr-dir DIR` at a folder of CIDR lists and each **responding** host is
matched against those provider ranges by the IP curl actually connected to. When
a host's IP falls inside a provider's range, the host is appended to
`<cdn-out-dir>/<provider>.txt` (e.g. `cloudflare.txt`).

```sh
server_scanner -i hosts.txt -o servers.txt --cidr-dir ./cdn-ip-ranges --cdn-out-dir ./by-cdn
```

- **Your own results are never touched.** The per-CDN files are entirely
  separate from `--output`; that file is written exactly as it would be without
  this flag.
- **Provider name = filename.** Each `*.txt` under `DIR` (one level of
  subdirectories is scanned too, so the upstream per-provider layout works) is
  one provider; the name is the basename with a trailing `_plain`, `_ipv4`,
  `_ipv6`, and `.txt` stripped — `cloudflare_plain.txt` → `cloudflare`.
- **IPv4 and IPv6** ranges are both supported. A bare IP with no `/prefix` is
  treated as a host route (`/32` or `/128`). Blank lines and `#` comments are
  ignored.
- **Overlaps** resolve to the alphabetically-first provider (deterministic).
- **Fresh vs. resume.** Like the text output, each `<provider>.txt` is truncated
  on the first hit of a fresh run and appended to on a resumed run.
- A one-line summary at the end reports how many hosts landed in each provider.

> Requires the real remote IP (`CURLINFO_PRIMARY_IP`), which is available in a
> normal environment. Behind an HTTP(S) proxy that tunnels the connection, the
> remote IP may be hidden and classification will find no matches.

## Resume & caching

While a scan runs it writes a checkpoint next to the output file
(`<output>.cache`) listing every host it has finished. Rows are also flushed to
the output file the moment they're found. If the scan is interrupted (`Ctrl+C`
or killed), both survive.

On the next run with the same `--output`, the checkpoint is detected and the
scan **resumes** — already-checked hosts are skipped. Interactive runs ask first
(`Resume? (Y/n)`); unattended runs auto-resume. Force it either way with
`--resume` / `--no-resume`. Output is appended when resuming (CSV keeps its
single header). **JSON output is not resumable** — its single array can't be
appended cleanly, so a JSON run always starts fresh. On clean completion the
checkpoint is deleted.

## Network pause

If the whole recent result window fails **and** a DNS check confirms the
network is actually down, the scan **pauses** instead of burning through the
rest of the list as errors, then resumes automatically once connectivity is
back. Disable with `--no-pause`. (A one-shot DNS check also runs at startup.)

## Features

- **Resumable** — checkpoints progress to `<output>.cache`; an interrupted scan
  resumes and skips already-checked hosts.
- **Network-aware** — pauses mid-scan if the network drops and resumes when it
  returns.
- **Streams** the input file line-by-line — safe for multi-GB host lists; the
  whole file is never loaded into memory.
- **Adaptive concurrency** — a starting cap is seeded from a quick latency
  probe, then raised or lowered while running based on the live error rate and
  latency.
- **Silent network sanity check** before scanning (only speaks up if it fails).
- **Colored output** (auto-disabled when stdout is not a TTY, so piped output
  stays clean).
- **Per-host retries** (`--retries N`) on transport errors.
- **Text / CSV / JSON** output formats.
- Both **interactive prompts** and **command-line flags** for automation.
- Graceful `Ctrl+C` — finishes in-flight work and prints the summary.

## Build

Requires a C++17 compiler, CMake ≥ 3.16, and libcurl development headers.

```sh
# Debian/Ubuntu: sudo apt install build-essential cmake libcurl4-openssl-dev
cmake -S . -B build
cmake --build build -j
```

The binary is written to `build/server_scanner`.

## Usage

Interactive (prompts for input/output files):

```sh
./build/server_scanner
```

Non-interactive:

```sh
./build/server_scanner -i hosts.txt -o servers.txt -t 8 -v
```

| Flag | Meaning |
| --- | --- |
| `--resume` / `--no-resume` | force resume from / ignore the `<output>.cache` checkpoint |
| `--no-pause` | don't pause mid-scan when the network drops |
| `-i, --input FILE` | hosts list (prompted if omitted) |
| `-o, --output FILE` | results output file (default `servers.txt`) |
| `-t, --timeout SECS` | per-request timeout (default 8) |
| `-c, --concurrency N` | adaptive starting concurrency (default: auto-seed) |
| `-T, --threads N` | fixed concurrency — pins N in-flight requests, disables auto-tuning |
| `-r, --retries N` | retry a host N times on transport error (default 0) |
| `--cidr-dir DIR` | classify each responder by CDN using the CIDR lists in `DIR` |
| `--cdn-out-dir DIR` | where to write the per-CDN `<provider>.txt` files (default `.`) |
| `-f, --format FMT` | output format: `text` \| `csv` \| `json` (default `text`) |
| `-v, --verbose` | print status for every host, not just responders |
| `-h, --help` | show help |

### Output formats

- `text` — a tab-separated `host \t port \t status \t server` line per
  responding host. **Appends** to the output file, so results accumulate across
  runs.
- `csv` — `host,port,status,server` with a header row; fields are quoted/escaped.
- `json` — a JSON array of `{"host","port","status","server"}` objects.

`csv` and `json` write a single well-formed document, so they **overwrite** the
output file rather than appending.

### Concurrency

By default the scanner **auto-tunes** how many requests are in flight, seeded
from a quick latency probe and adjusted from the live error rate and latency.
Pass `-T/--threads N` to **pin** it to a fixed number instead (the monitor is
turned off). Use `-c/--concurrency N` to only change the adaptive *starting*
point while keeping auto-tuning on. A fully interactive run (no flags) also
**prompts** for a thread count — enter a number to pin it, or leave it blank
for auto.

```sh
server_scanner -i hosts.txt -T 100      # exactly 100 in flight, fixed
server_scanner -i hosts.txt -c 50       # start at 50, auto-tune from there
```

## Install as a global command (Termux)

To run it from anywhere by a name of your choice, copy the built binary onto
your `PATH`. On Termux, `$PREFIX/bin` is on the path:

```sh
cp build/server_scanner $PREFIX/bin/scan
chmod +x $PREFIX/bin/scan
```

Now `scan -i hosts.txt -T 100` works from any directory. To update it later,
rebuild and copy again. To remove it: `rm $PREFIX/bin/scan`.

### Input format

One host per line. Blank lines and lines starting with `#` are ignored. A
scheme is added automatically when missing:

```
example.com
https://another.example.org
# this is a comment
```

## Notes

- TLS certificate verification is disabled, so hosts with self-signed or
  mismatched certificates are still probed. Do not reuse this client for
  anything that requires authenticated TLS.
- Only scan hosts you are authorized to test.
