//! Result writers: the main results file (text/csv/json/ndjson) and the
//! lazily-opened per-CDN `<provider>.txt` files. All writing happens on a single
//! thread, so no locking is needed.

use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::{self, BufWriter, Write};

use crate::config::Format;

fn csv_field(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for ch in s.chars() {
        if ch == '"' {
            out.push('"');
        }
        out.push(ch);
    }
    out.push('"');
    out
}

fn json_str(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

pub struct OutputWriter {
    f: BufWriter<File>,
    fmt: Format,
    cols: Vec<String>,
    json_first: bool,
    suppress_header: bool,
}

impl OutputWriter {
    pub fn open(
        path: &str,
        fmt: Format,
        cols: Vec<String>,
        append: bool,
        suppress_header: bool,
    ) -> io::Result<Self> {
        let f = OpenOptions::new()
            .create(true)
            .write(true)
            .append(append)
            .truncate(!append)
            .open(path)?;
        let mut w = OutputWriter {
            f: BufWriter::new(f),
            fmt,
            cols,
            json_first: true,
            suppress_header,
        };
        w.begin()?;
        Ok(w)
    }

    fn begin(&mut self) -> io::Result<()> {
        match self.fmt {
            Format::Csv if !self.suppress_header => {
                writeln!(self.f, "{}", self.cols.join(","))?;
            }
            Format::Json => {
                write!(self.f, "[\n")?;
            }
            _ => {}
        }
        Ok(())
    }

    /// Write one row. `vals` aligns with `cols`.
    pub fn row(&mut self, vals: &[String]) -> io::Result<()> {
        match self.fmt {
            Format::Text => {
                writeln!(self.f, "{}", vals.join("\t"))?;
            }
            Format::Csv => {
                let line: Vec<String> = vals.iter().map(|v| csv_field(v)).collect();
                writeln!(self.f, "{}", line.join(","))?;
            }
            Format::Ndjson => {
                writeln!(self.f, "{}", self.json_object(vals))?;
            }
            Format::Json => {
                if !self.json_first {
                    write!(self.f, ",\n")?;
                }
                self.json_first = false;
                write!(self.f, "  {}", self.json_object(vals))?;
            }
        }
        Ok(())
    }

    fn json_object(&self, vals: &[String]) -> String {
        let parts: Vec<String> = self
            .cols
            .iter()
            .zip(vals.iter())
            .map(|(k, v)| format!("{}:{}", json_str(k), json_str(v)))
            .collect();
        format!("{{{}}}", parts.join(","))
    }

    pub fn flush(&mut self) {
        let _ = self.f.flush();
    }

    pub fn end(&mut self) {
        if let Format::Json = self.fmt {
            if self.json_first {
                let _ = write!(self.f, "]\n");
            } else {
                let _ = write!(self.f, "\n]\n");
            }
        }
        let _ = self.f.flush();
    }
}

/// Lazily-opened per-provider files. Fresh runs truncate on first hit; resumed
/// runs append. Separate from the main results file.
pub struct CdnWriter {
    dir: String,
    resuming: bool,
    streams: HashMap<String, BufWriter<File>>,
}

impl CdnWriter {
    pub fn new(dir: &str, resuming: bool) -> Self {
        CdnWriter {
            dir: dir.to_string(),
            resuming,
            streams: HashMap::new(),
        }
    }

    pub fn write(&mut self, provider: &str, host: &str) {
        let resuming = self.resuming;
        let dir = self.dir.clone();
        let w = self.streams.entry(provider.to_string()).or_insert_with(|| {
            let path = if dir.is_empty() {
                format!("{provider}.txt")
            } else {
                format!("{dir}/{provider}.txt")
            };
            let f = OpenOptions::new()
                .create(true)
                .write(true)
                .append(resuming)
                .truncate(!resuming)
                .open(&path)
                .unwrap_or_else(|e| {
                    eprintln!("Can't open {path}: {e}");
                    std::process::exit(1);
                });
            BufWriter::new(f)
        });
        let _ = writeln!(w, "{host}");
    }

    pub fn flush(&mut self) {
        for w in self.streams.values_mut() {
            let _ = w.flush();
        }
    }
}
