# Way Scanner Toolkit (C++)

A fast HTTP(S) host scanner with two tools, chosen from a menu (or a flag):

1. **Imperva CDN checker** — flags hosts sitting behind **Imperva / Incapsula**.
2. **Port / server scanner** — probes port **443** and reports each live host's
   `Server` header.

Both share one engine: a single-threaded [libcurl](https://curl.se/libcurl/)
multi-handle event loop — the direct analogue of the original
`imperva_checker.py`'s `asyncio` / `aiohttp` design. Many transfers run in
flight on one thread, so there is no thread-per-request explosion.

## Menu

Run with no arguments and pick a tool:

```
Select a tool:
  1) Imperva CDN checker
  2) Port / server scanner (probe 443, show Server)
  0) Exit
```

Skip the menu with `--mode imperva` / `--mode server` (or the `--server`
shorthand).

## Tool 1 — Imperva detection

A host is flagged when any of the following is present in its response:

| Signal | Source |
| --- | --- |
| `X-CDN: Imperva` | response header |
| `X-Iinfo: …` | response header (Imperva-specific) |
| `visid_incap*` / `incap_ses*` | `Set-Cookie` (Incapsula session/visitor cookies) |
| `Imperva` | first 64 KB of the response body |

## Tool 2 — Port / server scan

For each host it sends a `HEAD` request to `https://` (port 443). Every host
that responds is reported with the port it answered on, the HTTP status, and
its `Server` header; hosts that don't answer on 443 are reported as closed.
There is no automatic fallback to port 80 — a host listed explicitly as
`http://…` is still honored on 80, but bare hostnames are probed on 443 only.
Output columns are `host, port, status, server` (text output is tab-separated).

## Features

- **Two tools, one menu** — Imperva fingerprinting and a port/server scanner.
- **Streams** the input file line-by-line — safe for multi-GB host lists; the
  whole file is never loaded into memory.
- **Adaptive concurrency** — a starting cap is seeded from a quick latency
  probe, then raised or lowered while running based on the live error rate and
  latency.
- **Silent network sanity check** before scanning (only speaks up if it fails).
- **Colored output** (auto-disabled when stdout is not a TTY, so piped output
  stays clean).
- **HEAD-first mode** (`--head`) — fetches headers only to save bandwidth,
  and automatically falls back to `GET` for a host that rejects `HEAD`
  (405/501).
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

The binary is written to `build/imperva_checker`.

## Usage

Interactive (prompts for input/output files):

```sh
./build/imperva_checker
```

Non-interactive:

```sh
./build/imperva_checker -i hosts.txt -o imperva_hosts.txt -t 8 -v
```

| Flag | Meaning |
| --- | --- |
| `--mode M` | scanner: `imperva` \| `server` (shows the menu if omitted) |
| `--server` | shorthand for `--mode server` |
| `-i, --input FILE` | hosts list (prompted if omitted) |
| `-o, --output FILE` | matches output file (default `imperva_hosts.txt`) |
| `-t, --timeout SECS` | per-request timeout (default 8) |
| `-c, --concurrency N` | adaptive starting concurrency (default: auto-seed) |
| `-T, --threads N` | fixed concurrency — pins N in-flight requests, disables auto-tuning |
| `-r, --retries N` | retry a host N times on transport error (default 0) |
| `--head` | HEAD-first mode (headers only; GET-fallback on 405/501) |
| `-f, --format FMT` | output format: `text` \| `csv` \| `json` (default `text`) |
| `-v, --verbose` | print status for every host, not just matches |
| `-h, --help` | show help |

### Output formats

- `text` — one matching host URL per line. **Appends** to the output file, so
  matches accumulate across runs.
- `csv` — `url,status,marker` with a header row; fields are quoted/escaped.
- `json` — a JSON array of `{"url","status","marker"}` objects.

`csv` and `json` write a single well-formed document, so they **overwrite** the
output file rather than appending.

### HEAD-first mode

`--head` issues `HEAD` requests, which is enough to catch the header and cookie
signals while transferring no response body. Hosts that reject `HEAD` (status
`405`/`501`) are automatically re-tried as `GET`. Note that a pure-`HEAD` hit
never sees the body, so the weaker "`Imperva` in response body" fingerprint only
fires on the `GET`-fallback hosts.

### Concurrency

By default the scanner **auto-tunes** how many requests are in flight, seeded
from a quick latency probe and adjusted from the live error rate and latency.
Pass `-T/--threads N` to **pin** it to a fixed number instead (the monitor is
turned off). Use `-c/--concurrency N` to only change the adaptive *starting*
point while keeping auto-tuning on. A fully interactive run (no flags) also
**prompts** for a thread count — enter a number to pin it, or leave it blank
for auto.

```sh
imperva_checker -i hosts.txt -T 100      # exactly 100 in flight, fixed
imperva_checker -i hosts.txt -c 50       # start at 50, auto-tune from there
```

## Install as a global command (Termux)

To run it from anywhere by a name of your choice, copy the built binary onto
your `PATH`. On Termux, `$PREFIX/bin` is on the path:

```sh
cp build/imperva_checker $PREFIX/bin/gh
chmod +x $PREFIX/bin/gh
```

Now `gh -i hosts.txt -T 100` works from any directory. To update it later,
rebuild and copy again. To remove it: `rm $PREFIX/bin/gh`.

> Note: `gh` is also the name of GitHub's official CLI. If you install that
> later, one will shadow the other — pick a different name (e.g. `impv`) to
> avoid the clash.

### Input format

One host per line. Blank lines and lines starting with `#` are ignored. A
scheme is added automatically when missing:

```
example.com
https://another.example.org
# this is a comment
```

## Notes

- TLS certificate verification is disabled (matching the original), so hosts
  with self-signed or mismatched certificates are still fingerprinted. Do not
  reuse this client for anything that requires authenticated TLS.
- Only scan hosts you are authorized to test.
