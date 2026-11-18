//! `ar_device`: the on-phone half of the Android test harness for the networking model (OSI layers 4 to 7).
//!
//! One static binary, pushed to /data/local/tmp over adb and run from `adb shell`:
//!
//!   ar_device info                         device, CPU and clock facts as one JSON line
//!   ar_device gate <corpus_dir>            L6 conformance: the golden-corpus gate (messages, sim_messages, flips)
//!   ar_device bench [key=value ...]        L6 performance: codec encode and decode speed per wire form and kind
//!   ar_device net id=<i> n=<n> ...         L4 live traffic per the Network Layer ICD over UDP multicast
//!
//! Every subcommand prints JSON lines on stdout, so the host controller (`android/harness.py`)
//! can parse them; `net` also writes a per-device event log for offline analysis.

#![forbid(unsafe_code)]

mod bench;
mod gate;
mod ledger;
mod net;
mod samples;

use std::collections::BTreeMap;
use std::process::ExitCode;

/// `key=value` arguments, with typed lookups and a check that every key was used.
pub struct Args {
    values: BTreeMap<String, String>,
    used: std::cell::RefCell<Vec<String>>,
}

impl Args {
    fn parse(words: &[String]) -> Result<Args, String> {
        let mut values = BTreeMap::new();
        for word in words {
            let (key, value) = word
                .split_once('=')
                .ok_or_else(|| format!("expected key=value, got {word:?}"))?;
            values.insert(key.to_string(), value.to_string());
        }
        Ok(Args {
            values,
            used: std::cell::RefCell::new(Vec::new()),
        })
    }

    pub fn text(&self, key: &str, default: &str) -> String {
        self.used.borrow_mut().push(key.to_string());
        self.values
            .get(key)
            .cloned()
            .unwrap_or_else(|| default.to_string())
    }

    pub fn number<T: std::str::FromStr>(&self, key: &str, default: T) -> Result<T, String> {
        self.used.borrow_mut().push(key.to_string());
        match self.values.get(key) {
            None => Ok(default),
            Some(text) => text
                .parse()
                .map_err(|_| format!("{key}={text} is not a valid number")),
        }
    }

    /// Fails on any key no lookup asked for, so a typo never passes silently.
    pub fn finish(&self) -> Result<(), String> {
        let used = self.used.borrow();
        let unknown: Vec<&String> = self
            .values
            .keys()
            .filter(|key| !used.contains(key))
            .collect();
        if unknown.is_empty() {
            Ok(())
        } else {
            Err(format!("unknown arguments: {unknown:?}"))
        }
    }
}

fn info() -> serde_json::Value {
    let cpuinfo = std::fs::read_to_string("/proc/cpuinfo").unwrap_or_default();
    let field = |name: &str| {
        cpuinfo
            .lines()
            .filter_map(|line| line.split_once(':'))
            .filter(|(key, _)| key.trim() == name)
            .map(|(_, value)| value.trim().to_string())
            .next()
            .unwrap_or_default()
    };
    // The smallest nonzero step the monotonic clock shows.
    let mut resolution_ns = u128::MAX;
    for _ in 0..1000 {
        let start = std::time::Instant::now();
        let mut now = std::time::Instant::now();
        while now == start {
            now = std::time::Instant::now();
        }
        resolution_ns = resolution_ns.min((now - start).as_nanos());
    }
    serde_json::json!({
        "what": "info",
        "version": env!("CARGO_PKG_VERSION"),
        "arch": std::env::consts::ARCH,
        "os": std::env::consts::OS,
        "cpus": std::thread::available_parallelism().map(|n| n.get()).unwrap_or(0),
        "hardware": field("Hardware"),
        "model_name": field("model name"),
        "processor": field("Processor"),
        "kernel": std::fs::read_to_string("/proc/version").unwrap_or_default().trim(),
        "clock_resolution_ns": resolution_ns,
    })
}

fn run(words: &[String]) -> Result<(), String> {
    let command = words.first().map(String::as_str).unwrap_or("");
    match command {
        "info" => {
            println!("{}", info());
            Ok(())
        }
        "gate" => {
            let dir = words
                .get(1)
                .ok_or("usage: ar_device gate <corpus_dir>")?;
            gate::run(std::path::Path::new(dir))
        }
        "bench" => {
            let args = Args::parse(&words[1..])?;
            bench::run(&args)
        }
        "net" => {
            let args = Args::parse(&words[1..])?;
            net::run(&args)
        }
        _ => Err("usage: ar_device info | gate <corpus_dir> | bench [key=value ...] | net id=<i> n=<n> [key=value ...]".into()),
    }
}

fn main() -> ExitCode {
    let words: Vec<String> = std::env::args().skip(1).collect();
    match run(&words) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            println!(
                "{}",
                serde_json::json!({ "what": "error", "message": message })
            );
            ExitCode::FAILURE
        }
    }
}
