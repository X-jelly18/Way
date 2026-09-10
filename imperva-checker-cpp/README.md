# Imperva HTTPS Checker (C++)

A fast HTTPS CDN fingerprinter. It streams a list of hosts and saves the ones
whose response carries an **Imperva / Incapsula** marker.

This is a C++ port of the original `imperva_checker.py`, rebuilt on a
single-threaded [libcurl](https://curl.se/libcurl/) multi-handle event loop —
the direct analogue of the Python version's `asyncio` / `aiohttp` design. Many
transfers run in flight on one thread, so there is no thread-per-request
explosion.

## Detection

A host is flagged when any of the following is present in its response:

| Signal | Source |
| --- | --- |
| `X-CDN: Imperva` | response header |
| `X-Iinfo: …` | response header (Imperva-specific) |
| `visid_incap*` / `incap_ses*` | `Set-Cookie` (Incapsula session/visitor cookies) |
| `Imperva` | first 64 KB of the response body |

## Features

- **Streams** the input file line-by-line — safe for multi-GB host lists; the
  whole file is never loaded into memory.
- **Adaptive concurrency** — a starting cap is seeded from a quick latency
  probe, then raised or lowered while running based on the live error rate and
  latency.
- **Silent network sanity check** before scanning (only speaks up if it fails).
- **Colored output** (auto-disabled when stdout is not a TTY, so piped output
  stays clean).
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
| `-i, --input FILE` | hosts list (prompted if omitted) |
| `-o, --output FILE` | matches output file (default `imperva_hosts.txt`) |
| `-t, --timeout SECS` | per-request timeout (default 8) |
| `-c, --concurrency N` | starting concurrency (default: auto-seed) |
| `-v, --verbose` | print status for every host, not just matches |
| `-h, --help` | show help |

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
