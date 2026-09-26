# Way Server Scanner (Rust)

A fast HTTP(S) **port/server scanner** with **built-in CDN classification**. It's
a Rust rewrite of the C++ tool: prompt-driven by default, scriptable with flags,
and it knows which CDN a host sits behind from a hardcoded IP-range table — so a
host is identified even when it returns no `Server` header.

## What it does

- **Server scan** (default): sends an HTTP `HEAD` to each host, reports the
  status and `Server` header, and classifies the connected IP by CDN. **If a
  host responds without a `Server` header, the CDN column still identifies it**
  from which provider range its IP falls in.
- **DNS-only** (`--resolve-only`): resolves each host to an IP and classifies it
  by CDN with no HTTP — the fastest way to map a list to providers.

The CDN ranges (Cloudflare, Imperva, Fastly, CloudFront, Bunny, Gcore, Azure,
Edgio, Akamai — 160 IPv4 ranges) are **compiled into the binary** (`src/cdn.rs`);
there's no external file to ship or point at.

## Build & install

Needs Rust (via [rustup](https://rustup.rs) or `pkg install rust` on Termux).

```sh
cd server-scanner-rs
cargo build --release
```

The binary is `target/release/njz`. Install it so `njz` works anywhere:

```sh
# Termux:
cp target/release/njz $PREFIX/bin/njz
# Linux:
install -m755 target/release/njz ~/.local/bin/njz   # or /usr/local/bin with sudo
```

## Usage

**Guided (prompt-based).** Run it with no `-i` and answer the prompts (tool,
hosts file, output, threads, per-CDN dir); press Enter to accept each default:

```sh
njz
```

**Scripted (flags).** Passing `-i` skips every prompt:

```sh
njz -i hosts.txt -o servers.txt            # server scan
njz -i hosts.txt --resolve-only -f ndjson  # DNS-only, ndjson output
cat hosts.txt | njz -i -                    # read hosts from stdin
```

| Flag | Meaning |
| --- | --- |
| `-i, --input FILE` | hosts list (or `-` for stdin; prompted if omitted) |
| `-o, --output FILE` | results file (default `servers.txt`) |
| `-t, --timeout SECS` | per-request timeout (default 8) |
| `-T, --threads N` | worker threads (default: 64 scan / 32 resolve) |
| `--resolve-only` | DNS mode: resolve + classify by IP, no HTTP |
| `--limit N` | stop after N hosts |
| `--dedup` | skip duplicate host lines |
| `--cdn-out-dir DIR` | where to write per-CDN `<provider>.txt` (default `.`) |
| `--cdn-only` | only record hosts that matched a CDN |
| `--verify-tls` | verify TLS certs (off by default) |
| `-A, --user-agent STR` | override the `User-Agent` |
| `-f, --format FMT` | `text` \| `csv` \| `json` \| `ndjson` (default `text`) |
| `--resume` / `--no-resume` | force / skip resume from `<output>.cache` |
| `--no-color` | disable colored output |
| `-v, --verbose` | print every host |
| `-h, --help` | help |

## Output

- **Server scan** columns: `host, ip, status, server, cdn`.
- **DNS-only** columns: `host, ip, cdn`.
- `text` (tab-separated) and `ndjson` append and are resumable; `csv`/`json`
  overwrite (json can't be resumed).
- Every host whose IP matches a provider is also appended to
  `<cdn-out-dir>/<provider>.txt`, and a per-provider tally prints at the end.

## Resume

Progress is checkpointed to `<output>.cache` as it runs. If interrupted, the
next run resumes and skips already-checked hosts (auto in scripted runs; prompted
in the guided flow). A clean finish removes the checkpoint.

## Notes

- TLS verification is **off by default** so odd-cert hosts are still probed; use
  `--verify-tls` to turn it on.
- CDN classification needs the real remote IP. In server-scan mode behind an
  HTTP(S) proxy the observed IP may be the proxy's, so classification only fires
  in DNS-only mode there; on a direct connection both modes classify.
- Only scan hosts you are authorized to test.
