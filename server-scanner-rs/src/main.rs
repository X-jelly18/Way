//! njz — fast HTTP(S) port/server scanner with built-in CDN classification.
//!
//! Prompt-based by default: run it with no -i and it walks you through the
//! essentials; any flag skips its prompt so it also scripts cleanly. CDN ranges
//! are baked in (see cdn.rs), so every host is classified by which provider owns
//! its IP — which identifies a host's CDN even when it returns no Server header.

mod cdn;
mod config;
mod output;
mod scan;

use std::io::IsTerminal;
use std::path::Path;

use config::{parse_args, prompt, Colors, Config, Format};

fn cache_has_entries(path: &str) -> bool {
    match std::fs::read_to_string(path) {
        Ok(s) => s.lines().any(|l| !l.trim().is_empty()),
        Err(_) => false,
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut cfg: Config = match parse_args(&args) {
        Some(c) => c,
        None => return, // --help was shown
    };

    let colors = Colors {
        on: std::io::stdout().is_terminal() && !cfg.no_color,
    };

    println!("{}", colors.bold(&colors.magenta("=== Way Server Scanner (Rust) ===")));
    println!();

    let interactive = cfg.input.is_empty();

    // ----- guided prompt flow (only when no -i; flags skip it) -----
    if interactive {
        if !cfg.resolve_only {
            let m = prompt(
                &colors,
                "Select a tool:\n  1) Server scan (HEAD probe, show Server header)\n  2) DNS-only (resolve each host and classify by CDN, no HTTP)\nChoice",
                "1",
            );
            let ml = m.to_ascii_lowercase();
            if m == "2" || ml == "dns" || ml == "resolve" {
                cfg.resolve_only = true;
            }
        }
        if cfg.resolve_only && cfg.output == "servers.txt" {
            cfg.output = "resolved.txt".into();
        }

        cfg.input = prompt(&colors, "Path to hosts .txt file (or - for stdin)", "");
        while cfg.input.is_empty() {
            cfg.input = prompt(&colors, "Please enter a valid path to hosts .txt file (or -)", "");
        }

        cfg.output = prompt(&colors, "Output file for results", &cfg.output.clone());

        let tstr = prompt(
            &colors,
            if cfg.resolve_only {
                "Resolver threads (number, or blank for 32)"
            } else {
                "Threads (number, or blank for 64)"
            },
            "",
        );
        if let Ok(t) = tstr.parse::<usize>() {
            if t > 0 {
                cfg.threads = t;
            }
        }

        cfg.cdn_out_dir = prompt(&colors, "Directory for per-CDN <provider>.txt files", &cfg.cdn_out_dir.clone());
    }

    // Validate input (skip for stdin).
    if cfg.input != "-" && !Path::new(&cfg.input).exists() {
        eprintln!("{}", colors.red(&format!("Can't open {}", cfg.input)));
        std::process::exit(1);
    }

    // ----- resume decision -----
    let cache_path = format!("{}.cache", cfg.output);
    let resuming = if cache_has_entries(&cache_path) {
        if cfg.format == Format::Json {
            if cfg.resume == 1 {
                println!("{}", colors.yellow("JSON output can't be resumed — starting fresh."));
            }
            false
        } else {
            match cfg.resume {
                1 => true,
                2 => false,
                _ => {
                    if interactive {
                        let a = prompt(&colors, "Found a checkpoint. Resume? (Y/n)", "y").to_ascii_lowercase();
                        !(a == "n" || a == "no")
                    } else {
                        true // auto-resume unattended
                    }
                }
            }
        }
    } else {
        false
    };

    let db = cdn::CdnDb::builtin();
    println!(
        "{}",
        colors.dim(&format!(
            "CDN classifier: {} ranges across {} providers (built in)",
            db.range_count(),
            db.provider_count()
        ))
    );
    let mode = if cfg.resolve_only { "DNS resolve" } else { "server scan" };
    println!(
        "{}",
        colors.dim(&format!(
            "Starting {} of {} -> {}",
            mode, cfg.input, cfg.output
        ))
    );
    println!();

    if let Err(e) = scan::run(&cfg, colors, resuming) {
        eprintln!("{}", colors.red(&format!("Error: {e}")));
        std::process::exit(1);
    }
}
