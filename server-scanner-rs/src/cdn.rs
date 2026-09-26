//! Built-in CDN IP-range database and classifier.
//!
//! The provider ranges are hardcoded here (IPv4), so the tool classifies a host
//! by which CDN owns its IP with no external file. This is what identifies a
//! host's CDN even when it returns no `Server` header.

use std::net::Ipv4Addr;

/// (CIDR, provider) rows, baked in at compile time.
const RANGES: &[(&str, &str)] = &[
    ("23.32.0.0/11", "akamai"),
    ("23.64.0.0/14", "akamai"),
    ("23.192.0.0/11", "akamai"),
    ("104.64.0.0/10", "akamai"),
    ("184.24.0.0/13", "akamai"),
    ("184.50.0.0/15", "akamai"),
    ("184.84.0.0/14", "akamai"),
    ("96.6.0.0/15", "akamai"),
    ("96.16.0.0/15", "akamai"),
    ("2.16.0.0/13", "akamai"),
    ("2.20.0.0/14", "akamai"),
    ("173.222.0.0/15", "akamai"),
    ("173.223.0.0/16", "akamai"),
    ("88.221.0.0/16", "akamai"),
    ("13.107.246.0/24", "azure"),
    ("13.107.247.0/24", "azure"),
    ("13.107.238.0/24", "azure"),
    ("13.107.239.0/24", "azure"),
    ("147.243.0.0/16", "azure"),
    ("150.171.0.0/16", "azure"),
    ("150.171.128.0/17", "azure"),
    ("199.117.100.0/22", "azure"),
    ("204.79.197.0/24", "azure"),
    ("169.150.196.0/22", "bunny"),
    ("169.150.200.0/22", "bunny"),
    ("169.150.204.0/22", "bunny"),
    ("169.150.208.0/22", "bunny"),
    ("169.150.212.0/22", "bunny"),
    ("169.150.216.0/22", "bunny"),
    ("169.150.220.0/22", "bunny"),
    ("169.150.224.0/22", "bunny"),
    ("169.150.228.0/22", "bunny"),
    ("169.150.232.0/22", "bunny"),
    ("169.150.236.0/22", "bunny"),
    ("169.150.240.0/22", "bunny"),
    ("169.150.244.0/22", "bunny"),
    ("169.150.248.0/22", "bunny"),
    ("169.150.252.0/22", "bunny"),
    ("185.180.12.0/22", "bunny"),
    ("185.180.14.0/23", "bunny"),
    ("185.180.16.0/22", "bunny"),
    ("185.180.20.0/22", "bunny"),
    ("185.180.24.0/22", "bunny"),
    ("185.180.28.0/22", "bunny"),
    ("185.180.32.0/22", "bunny"),
    ("185.180.36.0/22", "bunny"),
    ("185.180.40.0/22", "bunny"),
    ("185.180.44.0/22", "bunny"),
    ("173.245.48.0/20", "cloudflare"),
    ("103.21.244.0/22", "cloudflare"),
    ("103.22.200.0/22", "cloudflare"),
    ("103.31.4.0/22", "cloudflare"),
    ("141.101.64.0/18", "cloudflare"),
    ("108.162.192.0/18", "cloudflare"),
    ("190.93.240.0/20", "cloudflare"),
    ("188.114.96.0/20", "cloudflare"),
    ("197.234.240.0/22", "cloudflare"),
    ("198.41.128.0/17", "cloudflare"),
    ("162.158.0.0/15", "cloudflare"),
    ("104.16.0.0/13", "cloudflare"),
    ("104.24.0.0/14", "cloudflare"),
    ("172.64.0.0/13", "cloudflare"),
    ("131.0.72.0/22", "cloudflare"),
    ("13.32.0.0/15", "cloudfront"),
    ("13.35.0.0/16", "cloudfront"),
    ("13.224.0.0/14", "cloudfront"),
    ("13.249.0.0/16", "cloudfront"),
    ("18.64.0.0/14", "cloudfront"),
    ("18.68.0.0/15", "cloudfront"),
    ("18.160.0.0/15", "cloudfront"),
    ("18.164.0.0/15", "cloudfront"),
    ("18.172.0.0/15", "cloudfront"),
    ("18.238.0.0/15", "cloudfront"),
    ("18.244.0.0/15", "cloudfront"),
    ("52.84.0.0/15", "cloudfront"),
    ("52.222.128.0/17", "cloudfront"),
    ("54.182.0.0/16", "cloudfront"),
    ("54.192.0.0/16", "cloudfront"),
    ("54.230.0.0/16", "cloudfront"),
    ("54.239.128.0/18", "cloudfront"),
    ("54.239.192.0/19", "cloudfront"),
    ("54.240.128.0/18", "cloudfront"),
    ("64.252.64.0/18", "cloudfront"),
    ("65.8.0.0/16", "cloudfront"),
    ("65.9.0.0/17", "cloudfront"),
    ("70.132.0.0/18", "cloudfront"),
    ("99.84.0.0/16", "cloudfront"),
    ("99.86.0.0/16", "cloudfront"),
    ("204.246.164.0/22", "cloudfront"),
    ("204.246.168.0/22", "cloudfront"),
    ("205.251.192.0/19", "cloudfront"),
    ("205.251.249.0/24", "cloudfront"),
    ("192.229.128.0/17", "edgio"),
    ("192.229.0.0/17", "edgio"),
    ("192.16.64.0/18", "edgio"),
    ("152.199.0.0/16", "edgio"),
    ("152.195.0.0/16", "edgio"),
    ("93.184.216.0/24", "edgio"),
    ("93.184.220.0/22", "edgio"),
    ("72.21.80.0/20", "edgio"),
    ("72.21.96.0/20", "edgio"),
    ("68.232.32.0/20", "edgio"),
    ("46.22.64.0/20", "edgio"),
    ("117.18.232.0/21", "edgio"),
    ("117.18.240.0/21", "edgio"),
    ("158.85.224.0/19", "edgio"),
    ("23.235.32.0/20", "fastly"),
    ("43.249.72.0/22", "fastly"),
    ("103.244.50.0/24", "fastly"),
    ("103.245.222.0/23", "fastly"),
    ("103.245.224.0/24", "fastly"),
    ("104.156.80.0/20", "fastly"),
    ("146.75.0.0/16", "fastly"),
    ("151.101.0.0/16", "fastly"),
    ("157.52.64.0/18", "fastly"),
    ("167.82.0.0/17", "fastly"),
    ("167.82.128.0/20", "fastly"),
    ("167.82.160.0/20", "fastly"),
    ("167.82.224.0/20", "fastly"),
    ("172.111.64.0/18", "fastly"),
    ("185.31.16.0/22", "fastly"),
    ("199.27.72.0/21", "fastly"),
    ("199.232.0.0/16", "fastly"),
    ("92.223.64.0/22", "gcore"),
    ("92.223.80.0/22", "gcore"),
    ("92.223.84.0/22", "gcore"),
    ("92.223.88.0/22", "gcore"),
    ("92.223.92.0/22", "gcore"),
    ("92.223.96.0/22", "gcore"),
    ("92.223.100.0/22", "gcore"),
    ("92.223.104.0/22", "gcore"),
    ("92.223.108.0/22", "gcore"),
    ("92.223.112.0/22", "gcore"),
    ("92.223.116.0/22", "gcore"),
    ("92.223.120.0/22", "gcore"),
    ("92.223.124.0/22", "gcore"),
    ("5.188.118.0/23", "gcore"),
    ("5.188.120.0/23", "gcore"),
    ("194.38.20.0/24", "gcore"),
    ("194.38.21.0/24", "gcore"),
    ("194.38.22.0/24", "gcore"),
    ("194.38.23.0/24", "gcore"),
    ("5.42.228.0/22", "gcore"),
    ("5.42.232.0/22", "gcore"),
    ("5.42.236.0/22", "gcore"),
    ("5.42.240.0/22", "gcore"),
    ("146.185.240.0/22", "gcore"),
    ("146.185.244.0/22", "gcore"),
    ("146.185.248.0/22", "gcore"),
    ("146.185.252.0/22", "gcore"),
    ("199.83.128.0/21", "imperva"),
    ("198.143.32.0/19", "imperva"),
    ("149.126.72.0/21", "imperva"),
    ("103.28.248.0/22", "imperva"),
    ("185.11.124.0/22", "imperva"),
    ("192.230.64.0/18", "imperva"),
    ("107.154.0.0/16", "imperva"),
    ("45.60.0.0/16", "imperva"),
    ("45.223.0.0/16", "imperva"),
    ("131.125.128.0/17", "imperva"),
];

/// A parsed range: masked network + mask + provider index.
struct Entry {
    network: u32,
    mask: u32,
    provider: usize,
}

/// Compiled classifier: parses the hardcoded rows once.
pub struct CdnDb {
    entries: Vec<Entry>,
    providers: Vec<&'static str>,
}

impl CdnDb {
    /// Parse the built-in table. Rows are sorted by provider name so overlaps
    /// resolve to the alphabetically-first provider (deterministic).
    pub fn builtin() -> Self {
        let mut providers: Vec<&'static str> = Vec::new();
        let mut entries: Vec<Entry> = Vec::new();
        for (cidr, prov) in RANGES {
            let (net, mask) = match parse_cidr(cidr) {
                Some(v) => v,
                None => continue,
            };
            let pi = match providers.iter().position(|p| p == prov) {
                Some(i) => i,
                None => {
                    providers.push(prov);
                    providers.len() - 1
                }
            };
            entries.push(Entry { network: net, mask, provider: pi });
        }
        entries.sort_by(|a, b| providers[a.provider].cmp(providers[b.provider]));
        CdnDb { entries, providers }
    }

    pub fn provider_count(&self) -> usize {
        self.providers.len()
    }
    pub fn range_count(&self) -> usize {
        self.entries.len()
    }

    /// Return the provider owning `ip`, or None. Entries are provider-sorted so
    /// the first match is the alphabetically-first provider on an overlap.
    pub fn classify(&self, ip: Ipv4Addr) -> Option<&'static str> {
        let v = u32::from(ip);
        for e in &self.entries {
            if v & e.mask == e.network {
                return Some(self.providers[e.provider]);
            }
        }
        None
    }
}

/// Parse "a.b.c.d/prefix" into (masked network, mask). A bare IP => /32.
fn parse_cidr(s: &str) -> Option<(u32, u32)> {
    let (addr, prefix) = match s.split_once('/') {
        Some((a, p)) => (a, p.parse::<u32>().ok()?),
        None => (s, 32),
    };
    if prefix > 32 {
        return None;
    }
    let ip: Ipv4Addr = addr.parse().ok()?;
    let mask = if prefix == 0 { 0 } else { u32::MAX << (32 - prefix) };
    Some((u32::from(ip) & mask, mask))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ip(s: &str) -> Ipv4Addr {
        s.parse().unwrap()
    }

    #[test]
    fn classifies_known_ranges() {
        let db = CdnDb::builtin();
        assert_eq!(db.classify(ip("104.16.0.1")), Some("cloudflare"));
        assert_eq!(db.classify(ip("151.101.1.1")), Some("fastly"));
        assert_eq!(db.classify(ip("13.35.0.1")), Some("cloudfront"));
        assert_eq!(db.classify(ip("45.60.0.1")), Some("imperva"));
        assert_eq!(db.classify(ip("23.32.0.1")), Some("akamai"));
        assert_eq!(db.classify(ip("204.79.197.5")), Some("azure"));
        assert_eq!(db.classify(ip("169.150.196.9")), Some("bunny"));
        assert_eq!(db.classify(ip("92.223.64.9")), Some("gcore"));
        assert_eq!(db.classify(ip("152.199.0.9")), Some("edgio"));
    }

    #[test]
    fn boundaries_and_misses() {
        let db = CdnDb::builtin();
        // 104.16.0.0/13 covers 104.16.0.0 - 104.23.255.255; .15 is outside.
        assert_eq!(db.classify(ip("104.15.255.255")), None);
        assert_eq!(db.classify(ip("8.8.8.8")), None);
        assert_eq!(db.classify(ip("1.1.1.1")), None);
    }

    #[test]
    fn loads_all_rows() {
        let db = CdnDb::builtin();
        assert_eq!(db.range_count(), 160);
        assert_eq!(db.provider_count(), 9);
    }
}
