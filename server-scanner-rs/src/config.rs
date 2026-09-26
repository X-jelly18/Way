//! Configuration, CLI parsing, colors, and interactive prompts.

use std::io::{self, Write};

#[derive(Clone, Copy, PartialEq)]
pub enum Format {
    Text,
    Csv,
    Json,
    Ndjson,
}

pub struct Config {
    pub input: String,
    pub output: String,
    pub timeout: u64, // seconds
    pub threads: usize, // 0 => auto default
    pub limit: u64, // 0 => no limit
    pub dedup: bool,
    pub resolve_only: bool, // DNS-only mode
    pub cdn_out_dir: String,
    pub cdn_only: bool, // only record hosts that matched a CDN
    pub format: Format,
    pub resume: u8, // 0 auto/ask, 1 force resume, 2 force fresh
    pub verify_tls: bool,
    pub user_agent: String,
    pub no_color: bool,
    pub verbose: bool,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            input: String::new(),
            output: "servers.txt".into(),
            timeout: 8,
            threads: 0,
            limit: 0,
            dedup: false,
            resolve_only: false,
            cdn_out_dir: ".".into(),
            cdn_only: false,
            format: Format::Text,
            resume: 0,
            verify_tls: false,
            user_agent: String::new(),
            no_color: false,
            verbose: false,
        }
    }
}

/// ANSI color helper; disabled when not a TTY or --no-color.
#[derive(Clone, Copy)]
pub struct Colors {
    pub on: bool,
}
impl Colors {
    pub fn wrap(&self, code: &str, s: &str) -> String {
        if self.on {
            format!("\x1b[{}m{}\x1b[0m", code, s)
        } else {
            s.to_string()
        }
    }
    pub fn green(&self, s: &str) -> String { self.wrap("92", s) }
    pub fn red(&self, s: &str) -> String { self.wrap("91", s) }
    pub fn yellow(&self, s: &str) -> String { self.wrap("93", s) }
    pub fn cyan(&self, s: &str) -> String { self.wrap("96", s) }
    pub fn magenta(&self, s: &str) -> String { self.wrap("95", s) }
    pub fn dim(&self, s: &str) -> String { self.wrap("2", s) }
    pub fn bold(&self, s: &str) -> String { self.wrap("1", s) }
}

pub fn usage(prog: &str) {
    println!(
        "Usage: {prog} [options]\n\
\x20 Port/server scanner with built-in CDN classification.\n\
\x20 Run with no -i for a guided prompt flow; any flag skips its prompt.\n\
\x20 -i, --input FILE      hosts list (or - for stdin; prompted if omitted)\n\
\x20 -o, --output FILE     results output file (default servers.txt)\n\
\x20 -t, --timeout SECS    per-request timeout (default 8)\n\
\x20 -T, --threads N       worker threads (default: auto)\n\
\x20     --resolve-only    DNS mode: resolve each host and classify by IP (no HTTP)\n\
\x20     --limit N         stop after N hosts\n\
\x20     --dedup           skip duplicate host lines\n\
\x20     --cdn-out-dir DIR where to write per-CDN <provider>.txt (default .)\n\
\x20     --cdn-only        only record hosts that matched a CDN\n\
\x20     --verify-tls      verify TLS certs (off by default)\n\
\x20 -A, --user-agent STR  override the User-Agent header\n\
\x20 -f, --format FMT      output: text | csv | json | ndjson (default text)\n\
\x20     --resume          resume from <output>.cache\n\
\x20     --no-resume       ignore any checkpoint\n\
\x20     --no-color        disable colored output\n\
\x20 -v, --verbose         print status for every host\n\
\x20 -h, --help            show this help"
    );
}

fn parse_format(s: &str) -> Format {
    match s.to_ascii_lowercase().as_str() {
        "text" => Format::Text,
        "csv" => Format::Csv,
        "json" => Format::Json,
        "ndjson" | "jsonl" => Format::Ndjson,
        other => {
            eprintln!("Invalid --format: {other} (use text|csv|json|ndjson)");
            std::process::exit(2);
        }
    }
}

fn num<T: std::str::FromStr>(v: &str, name: &str) -> T {
    match v.parse::<T>() {
        Ok(n) => n,
        Err(_) => {
            eprintln!("Invalid number for {name}: {v}");
            std::process::exit(2);
        }
    }
}

/// Parse argv. Returns None when --help was shown (caller exits 0).
pub fn parse_args(args: &[String]) -> Option<Config> {
    let mut cfg = Config::default();
    let mut i = 1;
    let prog = args.get(0).map(|s| s.as_str()).unwrap_or("jz");
    let next = |i: &mut usize, name: &str| -> String {
        *i += 1;
        match args.get(*i) {
            Some(v) => v.clone(),
            None => {
                eprintln!("Missing value for {name}");
                std::process::exit(2);
            }
        }
    };
    while i < args.len() {
        let a = args[i].as_str();
        match a {
            "-i" | "--input" => cfg.input = next(&mut i, "--input"),
            "-o" | "--output" => cfg.output = next(&mut i, "--output"),
            "-t" | "--timeout" => cfg.timeout = num(&next(&mut i, "--timeout"), "--timeout"),
            "-T" | "--threads" => cfg.threads = num(&next(&mut i, "--threads"), "--threads"),
            "--limit" => cfg.limit = num(&next(&mut i, "--limit"), "--limit"),
            "--dedup" => cfg.dedup = true,
            "--resolve-only" | "--dns" => cfg.resolve_only = true,
            "--cdn-out-dir" => cfg.cdn_out_dir = next(&mut i, "--cdn-out-dir"),
            "--cdn-only" => cfg.cdn_only = true,
            "--verify-tls" => cfg.verify_tls = true,
            "-A" | "--user-agent" => cfg.user_agent = next(&mut i, "--user-agent"),
            "-f" | "--format" => cfg.format = parse_format(&next(&mut i, "--format")),
            "--resume" => cfg.resume = 1,
            "--no-resume" => cfg.resume = 2,
            "--no-color" => cfg.no_color = true,
            "-v" | "--verbose" => cfg.verbose = true,
            "-h" | "--help" => {
                usage(prog);
                return None;
            }
            other => {
                eprintln!("Unknown argument: {other}");
                usage(prog);
                std::process::exit(2);
            }
        }
        i += 1;
    }
    if cfg.timeout < 1 {
        cfg.timeout = 1;
    }
    Some(cfg)
}

/// Prompt for a line of input, showing an optional default.
pub fn prompt(c: &Colors, msg: &str, default: &str) -> String {
    let suffix = if default.is_empty() {
        String::new()
    } else {
        format!(" [{default}]")
    };
    print!("{}", c.cyan(&format!("{msg}{suffix}: ")));
    let _ = io::stdout().flush();
    let mut line = String::new();
    if io::stdin().read_line(&mut line).is_err() {
        return default.to_string();
    }
    let t = line.trim();
    if t.is_empty() {
        default.to_string()
    } else {
        t.to_string()
    }
}
