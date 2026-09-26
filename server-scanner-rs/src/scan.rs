//! Scan engine: a worker pool feeds results to a single writer.
//!
//! Two modes share the same plumbing:
//!   * server scan  — HTTP HEAD on the host, report status + Server header, and
//!     classify the connected IP by CDN (so a host with no Server header is
//!     still identified by which range its IP falls in).
//!   * DNS-only     — resolve each host to an IP and classify it by CDN.

use std::collections::HashSet;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufRead, BufReader, Write};
use std::net::{IpAddr, ToSocketAddrs};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::cdn::CdnDb;
use crate::config::{Colors, Config, Format};
use crate::output::{CdnWriter, OutputWriter};

/// One processed host handed from a worker to the writer.
struct ScanResult {
    host: String,
    ip: String,
    status: u16,
    server: String,
    cdn: String,
    responded: bool, // got an HTTP response (server) / resolved (dns)
}

/// Normalize a raw host line to a URL, or "" to skip it.
pub fn normalize_host(raw: &str) -> String {
    let line = raw.trim();
    if line.is_empty() || line.starts_with('#') {
        return String::new();
    }
    if line.contains("://") {
        line.to_string()
    } else {
        format!("https://{line}")
    }
}

/// Extract the bare hostname from a URL (strip scheme, userinfo, port, path).
pub fn url_host(url: &str) -> String {
    let mut s = url;
    if let Some(pos) = s.find("://") {
        s = &s[pos + 3..];
    }
    if let Some(pos) = s.find('/') {
        s = &s[..pos];
    }
    if let Some(pos) = s.rfind('@') {
        s = &s[pos + 1..];
    }
    if s.starts_with('[') {
        if let Some(end) = s.find(']') {
            return s[1..end].to_string();
        }
    }
    // Strip ":port" only for a single-colon (non-IPv6) authority.
    if let Some(pos) = s.find(':') {
        if s[pos + 1..].find(':').is_none() {
            s = &s[..pos];
        }
    }
    s.to_string()
}

/// Shared, streaming source of the next host to scan.
struct Feeder {
    reader: Box<dyn BufRead + Send>,
    dedup: bool,
    seen: HashSet<String>,
    done: HashSet<String>,
    skipped: u64,
    limit: u64,
    dispatched: u64,
}

impl Feeder {
    fn next(&mut self) -> Option<String> {
        loop {
            if self.limit > 0 && self.dispatched >= self.limit {
                return None;
            }
            let mut line = String::new();
            match self.reader.read_line(&mut line) {
                Ok(0) | Err(_) => return None,
                Ok(_) => {}
            }
            let h = normalize_host(&line);
            if h.is_empty() {
                continue;
            }
            if self.dedup && !self.seen.insert(h.clone()) {
                continue;
            }
            if self.done.contains(&h) {
                self.skipped += 1;
                continue;
            }
            self.dispatched += 1;
            return Some(h);
        }
    }
}

fn load_done(cache_path: &str) -> HashSet<String> {
    let mut done = HashSet::new();
    if let Ok(f) = File::open(cache_path) {
        for line in BufReader::new(f).lines().map_while(Result::ok) {
            let t = line.trim();
            if !t.is_empty() {
                done.insert(t.to_string());
            }
        }
    }
    done
}

/// Run the scan described by `cfg`. `resuming` was decided by the caller.
pub fn run(cfg: &Config, c: Colors, resuming: bool) -> io::Result<()> {
    let db = Arc::new(CdnDb::builtin());

    // ----- input -----
    let reader: Box<dyn BufRead + Send> = if cfg.input == "-" {
        Box::new(BufReader::new(io::stdin()))
    } else {
        Box::new(BufReader::new(File::open(&cfg.input)?))
    };

    let cache_path = format!("{}.cache", cfg.output);
    let done = if resuming { load_done(&cache_path) } else { HashSet::new() };

    // ----- outputs -----
    let cols: Vec<String> = if cfg.resolve_only {
        vec!["host".into(), "ip".into(), "cdn".into()]
    } else {
        vec![
            "host".into(),
            "ip".into(),
            "status".into(),
            "server".into(),
            "cdn".into(),
        ]
    };
    let append_out = matches!(cfg.format, Format::Text | Format::Ndjson) || resuming;
    let suppress_header = resuming
        && cfg.format == Format::Csv
        && fs::metadata(&cfg.output).map(|m| m.len() > 0).unwrap_or(false);
    let mut out = OutputWriter::open(&cfg.output, cfg.format, cols, append_out, suppress_header)?;

    if cfg.cdn_out_dir != "." && !cfg.cdn_out_dir.is_empty() {
        let _ = fs::create_dir_all(&cfg.cdn_out_dir);
    }
    let mut cdnw = CdnWriter::new(&cfg.cdn_out_dir, resuming);

    let cache = OpenOptions::new()
        .create(true)
        .write(true)
        .append(resuming)
        .truncate(!resuming)
        .open(&cache_path)?;
    let mut cache = io::BufWriter::new(cache);

    let feeder = Arc::new(Mutex::new(Feeder {
        reader,
        dedup: cfg.dedup,
        seen: HashSet::new(),
        done,
        skipped: 0,
        limit: cfg.limit,
        dispatched: 0,
    }));

    let nthreads = if cfg.threads > 0 {
        cfg.threads
    } else if cfg.resolve_only {
        32
    } else {
        64
    };

    // Shared HTTP client for server mode.
    let client = if cfg.resolve_only {
        None
    } else {
        let ua = if cfg.user_agent.is_empty() {
            "Mozilla/5.0 (server-scanner)".to_string()
        } else {
            cfg.user_agent.clone()
        };
        Some(Arc::new(
            reqwest::blocking::Client::builder()
                .danger_accept_invalid_certs(!cfg.verify_tls)
                .timeout(Duration::from_secs(cfg.timeout))
                .connect_timeout(Duration::from_secs(cfg.timeout))
                .redirect(reqwest::redirect::Policy::none())
                .user_agent(ua)
                .build()
                .expect("failed to build HTTP client"),
        ))
    };

    let (tx, rx) = mpsc::channel::<ScanResult>();
    let resolve_only = cfg.resolve_only;

    let mut workers = Vec::with_capacity(nthreads);
    for _ in 0..nthreads {
        let feeder = Arc::clone(&feeder);
        let db = Arc::clone(&db);
        let tx = tx.clone();
        let client = client.clone();
        workers.push(std::thread::spawn(move || loop {
            let host = {
                let mut f = feeder.lock().unwrap();
                match f.next() {
                    Some(h) => h,
                    None => break,
                }
            };
            let res = if resolve_only {
                resolve_one(&host, &db)
            } else {
                probe_one(client.as_ref().unwrap(), &host, &db)
            };
            if tx.send(res).is_err() {
                break;
            }
        }));
    }
    drop(tx); // so rx ends once every worker's sender is dropped

    // ----- single writer (this thread) -----
    let mut checked: u64 = 0;
    let mut matches: u64 = 0;
    let mut cdn_counts: std::collections::BTreeMap<String, u64> = Default::default();
    let start = Instant::now();
    let mut last_flush = Instant::now();

    for r in rx {
        checked += 1;
        // Checkpoint every processed host (durable for resume).
        let _ = writeln!(cache, "{}", r.host);
        let _ = cache.flush();

        if !r.cdn.is_empty() {
            cdnw.write(&r.cdn, &r.host);
            *cdn_counts.entry(r.cdn.clone()).or_insert(0) += 1;
        }

        if r.responded {
            let record = !cfg.cdn_only || !r.cdn.is_empty();
            if record {
                matches += 1;
                if resolve_only {
                    out.row(&[r.host.clone(), r.ip.clone(), r.cdn.clone()])?;
                } else {
                    let server = if r.server.is_empty() {
                        "(unknown)".to_string()
                    } else {
                        r.server.clone()
                    };
                    out.row(&[
                        r.host.clone(),
                        r.ip.clone(),
                        r.status.to_string(),
                        server,
                        r.cdn.clone(),
                    ])?;
                }
                if cfg.verbose {
                    print_hit(&c, resolve_only, &r);
                }
            }
        } else if cfg.verbose {
            eprintln!("{}", c.red(&format!("[--] {} -> no response", r.host)));
        }

        if !cfg.verbose {
            let per_s = checked as f64 / start.elapsed().as_secs_f64().max(0.001);
            let cdnm: u64 = cdn_counts.values().sum();
            print!(
                "\r{} {} {} {}   ",
                c.cyan(&format!("[{} {}]", if resolve_only { "resolved" } else { "checked" }, checked)),
                c.green(&format!("{} {}", matches, if resolve_only { "with-ip" } else { "responded" })),
                if cdnm > 0 { c.magenta(&format!("cdn={cdnm}")) } else { String::new() },
                c.dim(&format!("{:.0}/s", per_s)),
            );
            let _ = io::stdout().flush();
        }

        if last_flush.elapsed().as_secs_f64() >= 1.0 {
            out.flush();
            cdnw.flush();
            last_flush = Instant::now();
        }
    }

    for w in workers {
        let _ = w.join();
    }

    out.end();
    cdnw.flush();
    let _ = cache.flush();
    drop(cache);
    // Completed cleanly -> drop the checkpoint.
    let _ = fs::remove_file(&cache_path);

    let skipped = feeder.lock().unwrap().skipped;
    println!();
    println!(
        "{} {} hosts {}, {} {}.",
        c.bold(&c.green("Done.")),
        checked,
        if resolve_only { "resolved" } else { "checked" },
        c.green(&matches.to_string()),
        if resolve_only { "with an IP" } else { "responded" }
    );
    if skipped > 0 {
        println!("{}", c.dim(&format!("(resumed: skipped {skipped} already-checked hosts)")));
    }
    println!("Saved to {}", c.cyan(&cfg.output));
    if !cdn_counts.is_empty() {
        let parts: Vec<String> = cdn_counts
            .iter()
            .map(|(k, v)| format!("{}={}", c.cyan(k), v))
            .collect();
        println!("CDN matches -> {}/: {}", cfg.cdn_out_dir, parts.join(", "));
    }
    Ok(())
}

fn print_hit(c: &Colors, resolve_only: bool, r: &ScanResult) {
    let note = if r.cdn.is_empty() {
        String::new()
    } else {
        format!("  {}", c.cyan(&format!("({})", r.cdn)))
    };
    if resolve_only {
        println!("{}{}", c.green(&format!("[IP] {} -> {}", r.host, r.ip)), note);
    } else {
        let server = if r.server.is_empty() { "(unknown)" } else { &r.server };
        println!(
            "{}{}",
            c.green(&format!("[OPEN] {}  [{}]  Server: {}", r.host, r.status, server)),
            note
        );
    }
}

/// First IPv4 in a list of addresses, else the first address of any family.
fn pick_ip(addrs: &[IpAddr]) -> Option<IpAddr> {
    addrs
        .iter()
        .find(|a| matches!(a, IpAddr::V4(_)))
        .or_else(|| addrs.first())
        .copied()
}

fn classify(db: &CdnDb, ip: Option<IpAddr>) -> String {
    match ip {
        Some(IpAddr::V4(a)) => db.classify(a).unwrap_or("").to_string(),
        _ => String::new(),
    }
}

fn probe_one(client: &reqwest::blocking::Client, host: &str, db: &CdnDb) -> ScanResult {
    match client.head(host).send() {
        Ok(resp) => {
            let status = resp.status().as_u16();
            let server = resp
                .headers()
                .get(reqwest::header::SERVER)
                .and_then(|v| v.to_str().ok())
                .unwrap_or("")
                .to_string();
            let ip = resp.remote_addr().map(|sa| sa.ip());
            let ip_str = ip.map(|i| i.to_string()).unwrap_or_default();
            let cdn = classify(db, ip);
            ScanResult {
                host: host.to_string(),
                ip: ip_str,
                status,
                server,
                cdn,
                responded: true,
            }
        }
        Err(_) => ScanResult {
            host: host.to_string(),
            ip: String::new(),
            status: 0,
            server: String::new(),
            cdn: String::new(),
            responded: false,
        },
    }
}

fn resolve_one(host: &str, db: &CdnDb) -> ScanResult {
    let name = url_host(host);
    let addrs: Vec<IpAddr> = match (name.as_str(), 443u16).to_socket_addrs() {
        Ok(it) => it.map(|sa| sa.ip()).collect(),
        Err(_) => Vec::new(),
    };
    let ip = pick_ip(&addrs);
    let ip_str = ip.map(|i| i.to_string()).unwrap_or_default();
    let cdn = classify(db, ip);
    ScanResult {
        host: host.to_string(),
        ip: ip_str,
        status: 0,
        server: String::new(),
        cdn,
        responded: ip.is_some(),
    }
}

/// Count non-blank, non-comment lines of a regular file (for nothing critical;
/// kept simple). Currently unused by the writer but handy for callers.
#[allow(dead_code)]
pub fn count_hosts(path: &str) -> Option<u64> {
    if path == "-" {
        return None;
    }
    let f = File::open(path).ok()?;
    let mut n = 0;
    for line in BufReader::new(f).lines().map_while(Result::ok) {
        let t = line.trim();
        if !t.is_empty() && !t.starts_with('#') {
            n += 1;
        }
    }
    Some(n)
}
