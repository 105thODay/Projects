//! Live traffic per the Network Layer ICD, over UDP multicast between phones.
//!
//! Each device runs one process. It sends its own avatar state, network summaries,
//! calibration and urgent events (three copies on consecutive send opportunities), answers
//! repair requests for its events, and, as leader, can send a catch-up burst of retained
//! markers on the 24 kbit/s lane. Every frame passes the rolling ledger first. The receiver
//! thread decodes everything it hears, finds gaps from summaries and asks for repair.
//!
//! Two CSV logs per device, for offline analysis on the host (clock offsets between phones
//! are estimated there from matched send and receive times, so no clock sync is needed):
//!
//!   tx_<id>.csv   S,t_ns,class,kind,seq,copy,bytes,encode_ns,hash   a frame sent
//!                 E,t_ns,seq,encode_ns                               an event's input onset
//!                 D,t_ns,class,kind,seq                              a frame the ledger deferred or dropped
//!                 F,t_ns,error                                       a send that failed
//!   rx_<id>.csv   R,t_ns,sender,owner,kind,seq,bytes,decode_ns,ok,hash
//!                 B,t_ns,bytes                                       dropped by an emulated blackout
//!
//! t_ns counts from the process start on the monotonic clock. `kind` is the codec's
//! integer code; `seq` is the event or state sequence, the summary's highest sequence, or
//! the repair request's owner.

use std::collections::{HashMap, HashSet, VecDeque};
use std::fs::File;
use std::io::{BufWriter, Write};
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use cot_codec::{CotCodec, CotMessage, MessageKind, Operation, WireForm, ENTITY_STRIDE};
use socket2::{Domain, Protocol, Socket, Type};

use crate::ledger::{Class, Ledger};
use crate::Args;

/// Retained catch-up markers use sequences from here, outside the event gap tracking.
const CATCHUP_BASE: i64 = 1_000_000;
const MAX_REPAIR_LIST: usize = 32;
const RESEND_HOLDOFF_NS: u64 = 100_000_000;

struct Config {
    id: i32,
    n: usize,
    group: SocketAddrV4,
    iface: Ipv4Addr,
    multicast_loop: bool,
    duration_ns: u64,
    warmup_ns: u64,
    state_hz: f64,
    summary_hz: f64,
    opportunity_hz: f64,
    input_hz: f64,
    copies: u32,
    calibration_period_ns: u64,
    catchup_at_ns: Option<u64>,
    catchup_items: usize,
    catchup_bps: f64,
    blackout_ns: u64,
    blackout_every_ns: u64,
    limit_bits: u64,
    urgent_reserve_bits: u64,
    repair_share_bits: u64,
    overhead_bytes: u64,
    max_events_per_s: usize,
    seed: u64,
    session_seed: u64,
    out: std::path::PathBuf,
}

fn seconds_ns(seconds: f64) -> u64 {
    (seconds * 1e9).round().max(0.0) as u64
}

impl Config {
    fn from_args(args: &Args) -> Result<Config, String> {
        let group: SocketAddrV4 = args
            .text("group", "239.255.82.6:18206")
            .parse()
            .map_err(|_| "group must be a.b.c.d:port")?;
        if !group.ip().is_multicast() {
            return Err(format!("{} is not a multicast address", group.ip()));
        }
        let iface: Ipv4Addr = args
            .text("iface", "0.0.0.0")
            .parse()
            .map_err(|_| "iface must be an IPv4 address")?;
        let catchup_at_s: f64 = args.number("catchup_at_s", -1.0)?;
        let config = Config {
            id: args.number("id", -1)?,
            n: args.number("n", 0)?,
            group,
            iface,
            multicast_loop: args.number::<u8>("loop", 0)? != 0,
            duration_ns: seconds_ns(args.number("duration_s", 60.0)?),
            warmup_ns: seconds_ns(args.number("warmup_s", 5.0)?),
            state_hz: args.number("state_hz", 15.0)?,
            summary_hz: args.number("summary_hz", 3.0)?,
            opportunity_hz: args.number("opportunity_hz", 90.0)?,
            input_hz: args.number("input_hz", 1.0)?,
            copies: args.number("copies", 3)?,
            calibration_period_ns: seconds_ns(args.number("calibration_period_s", 10.0)?),
            catchup_at_ns: (catchup_at_s >= 0.0).then(|| seconds_ns(catchup_at_s)),
            catchup_items: args.number("catchup_items", 50)?,
            catchup_bps: args.number::<f64>("catchup_kbps", 24.0)? * 1000.0,
            blackout_ns: seconds_ns(args.number::<f64>("blackout_ms", 0.0)? / 1000.0),
            blackout_every_ns: seconds_ns(args.number("blackout_every_s", 10.0)?),
            limit_bits: args.number("limit_bits", 200_000)?,
            urgent_reserve_bits: args.number("urgent_reserve_bits", 86_184)?,
            repair_share_bits: args.number("repair_share_bits", 32_000)?,
            overhead_bytes: args.number("overhead_bytes", 56)?,
            max_events_per_s: args.number("max_events_per_s", 7)?,
            seed: args.number("seed", 1)?,
            session_seed: args.number("session_seed", 20_261_117)?,
            out: args.text("out", ".").into(),
        };
        args.finish()?;
        if config.n < 2 || config.id < 0 || config.id as usize >= config.n {
            return Err("need n >= 2 and 0 <= id < n".into());
        }
        for (name, value) in [
            ("state_hz", config.state_hz),
            ("summary_hz", config.summary_hz),
            ("opportunity_hz", config.opportunity_hz),
        ] {
            if value <= 0.0 {
                return Err(format!("{name} must be positive"));
            }
        }
        if config.copies == 0 || config.catchup_bps <= 0.0 || config.input_hz < 0.0 {
            return Err("copies and catchup_kbps must be positive, input_hz not negative".into());
        }
        Ok(config)
    }
}

/// splitmix64: a small, seedable generator for input arrival times.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Exponential gap in nanoseconds for a Poisson rate in events per second.
    fn exponential_ns(&mut self, rate_hz: f64) -> u64 {
        let unit = ((self.next() >> 11) as f64 + 0.5) / (1u64 << 53) as f64;
        seconds_ns(-unit.ln() / rate_hz)
    }
}

fn fnv1a(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325u64, |hash, byte| {
        (hash ^ u64::from(*byte)).wrapping_mul(0x0000_0100_0000_01b3)
    })
}

/// What the receiver asks the sender to do.
enum Action {
    RequestRepair { owner: i32, missing: Vec<i64> },
    Resend { sequences: Vec<i64> },
}

/// Builds every message this device sends, with real field values.
struct Builder {
    id: i32,
    n: i32,
    utc_base_us: i64,
}

impl Builder {
    fn utc(&self, t_ns: u64) -> i64 {
        self.utc_base_us + (t_ns / 1000) as i64
    }

    fn base(&self, kind: MessageKind, t_ns: u64) -> CotMessage {
        let utc = self.utc(t_ns);
        CotMessage {
            kind,
            time_us: utc,
            stale_us: utc + 10_000_000,
            owner: self.id,
            sender: self.id,
            clock_epoch: 1,
            clock_uncertainty_us: 1000,
            frame_epoch: 1,
            calibration_epoch: 4,
            ce: 9_999_999.0,
            le: 9_999_999.0,
            ..CotMessage::default()
        }
    }

    fn event(&self, sequence: i64, onset_ns: u64, t_ns: u64, local: i64) -> CotMessage {
        let mut m = self.base(MessageKind::Event, t_ns);
        let onset = self.utc(onset_ns);
        m.entity = i64::from(self.id) * ENTITY_STRIDE + local;
        m.sequence = sequence;
        m.authority_epoch = 1;
        m.revision = 1;
        m.sample_us = onset - 1200;
        m.onset_us = onset;
        m.deadline_us = onset + 100_000;
        m.pose[3] = 0.923_879_5;
        m.pose[4] = 0.0;
        m.pose[5] = 0.0;
        m.pose[6] = 0.382_683_4;
        m.operation = Operation::Create;
        m.expiry_us = onset + 30_000_000;
        m.value = sequence as u64;
        m.stale_us = m.expiry_us;
        m.lat = 35.012_481_1 + 1e-6 * f64::from(self.id);
        m.lon = -116.456_610_2;
        m.hae = 713.3;
        m.ce = 1.5;
        m.le = 2.0;
        m
    }

    fn state(&self, sequence: i64, t_ns: u64) -> CotMessage {
        let mut m = self.base(MessageKind::State, t_ns);
        let phase = t_ns as f64 * 1e-9;
        m.entity = i64::from(self.id) * ENTITY_STRIDE;
        m.sequence = sequence;
        m.authority_epoch = 1;
        m.revision = (sequence % 1_000_000_000) as i32;
        m.sample_us = m.time_us;
        m.pose = [
            10.0 + 2.0 * phase.sin() + f64::from(self.id),
            -7.6543 + phase.cos(),
            1.6123,
            0.965_925_8,
            0.012_345_7,
            -0.003_456_7,
            0.258_819_0,
        ];
        m.velocity = [2.0 * phase.cos(), -phase.sin(), 0.012];
        m.stale_us = m.time_us + 2_000_000;
        m.lat = 35.012_276_6;
        m.lon = -116.456_653_5;
        m.hae = 713.9;
        m.ce = 1.5;
        m.le = 2.0;
        m
    }

    fn summary(&self, highest: i64, ack_owner: i32, ack_sequence: i64, t_ns: u64) -> CotMessage {
        let mut m = self.base(MessageKind::Control, t_ns);
        m.highest_sequence = highest;
        m.clock_offset_us = 0;
        m.membership_epoch = 1;
        m.ack_owner = ack_owner;
        m.ack_sequence = ack_sequence;
        m.calibration_delta = [0.0712, -0.0333, 0.0041, 0.001_234];
        m
    }

    fn calibration(&self, t_ns: u64) -> CotMessage {
        let mut m = self.base(MessageKind::Calibration, t_ns);
        m.sample_us = m.time_us;
        m.calibration_epoch = 5;
        m.calibration_source = 1;
        m.calibration_reference = (self.id + 1) % self.n;
        m.calibration_delta = [0.0812, -0.0333, 0.0041, 0.001_834];
        m
    }

    fn repair_request(&self, owner: i32, missing: Vec<i64>, t_ns: u64) -> CotMessage {
        let mut m = self.base(MessageKind::RepairRequest, t_ns);
        m.request_owner = owner;
        m.missing = missing;
        m
    }
}

fn open_socket(config: &Config) -> Result<UdpSocket, String> {
    let fail = |what: &str, error: std::io::Error| format!("{what}: {error}");
    let socket = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))
        .map_err(|e| fail("socket", e))?;
    socket
        .set_reuse_address(true)
        .map_err(|e| fail("SO_REUSEADDR", e))?;
    #[cfg(unix)]
    socket
        .set_reuse_port(true)
        .map_err(|e| fail("SO_REUSEPORT", e))?;
    socket
        .set_recv_buffer_size(1 << 20)
        .map_err(|e| fail("SO_RCVBUF", e))?;
    let bind = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, config.group.port());
    socket.bind(&bind.into()).map_err(|e| fail("bind", e))?;
    socket
        .join_multicast_v4(config.group.ip(), &config.iface)
        .map_err(|e| fail("join group", e))?;
    if !config.iface.is_unspecified() {
        socket
            .set_multicast_if_v4(&config.iface)
            .map_err(|e| fail("IP_MULTICAST_IF", e))?;
    }
    socket
        .set_multicast_loop_v4(config.multicast_loop)
        .map_err(|e| fail("IP_MULTICAST_LOOP", e))?;
    socket.set_multicast_ttl_v4(1).map_err(|e| fail("TTL", e))?;
    socket
        .set_read_timeout(Some(Duration::from_millis(100)))
        .map_err(|e| fail("SO_RCVTIMEO", e))?;
    Ok(socket.into())
}

fn sequence_of(message: &CotMessage) -> i64 {
    match message.kind {
        MessageKind::Event | MessageKind::State => message.sequence,
        MessageKind::Control | MessageKind::RepairStatus => message.highest_sequence,
        MessageKind::RepairRequest => i64::from(message.request_owner),
        MessageKind::Calibration => message.sample_us,
    }
}

struct ReceiveReport {
    frames: u64,
    decode_failures: u64,
    blackout_drops: u64,
    own_dropped: u64,
}

#[allow(clippy::too_many_arguments)]
fn receive(
    socket: UdpSocket,
    codec: CotCodec,
    id: i32,
    n: usize,
    start: Instant,
    blackout: (u64, u64, u64),
    stop: Arc<AtomicBool>,
    actions: mpsc::Sender<Action>,
    mut log: BufWriter<File>,
) -> std::io::Result<ReceiveReport> {
    let (blackout_ns, every_ns, warmup_ns) = blackout;
    let mut buffer = vec![0u8; 2048];
    let mut received: Vec<HashSet<i64>> = vec![HashSet::new(); n];
    let mut report = ReceiveReport {
        frames: 0,
        decode_failures: 0,
        blackout_drops: 0,
        own_dropped: 0,
    };
    while !stop.load(Ordering::Relaxed) {
        let length = match socket.recv_from(&mut buffer) {
            Ok((length, _)) => length,
            Err(error)
                if matches!(
                    error.kind(),
                    std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut
                ) =>
            {
                continue
            }
            Err(error) => return Err(error),
        };
        let t_ns = start.elapsed().as_nanos() as u64;
        if blackout_ns > 0 && t_ns >= warmup_ns && (t_ns - warmup_ns) % every_ns < blackout_ns {
            report.blackout_drops += 1;
            writeln!(log, "B,{t_ns},{length}")?;
            continue;
        }
        let wire = &buffer[..length];
        let hash = fnv1a(wire);
        let decode_start = Instant::now();
        let decoded = codec.decode(wire);
        let decode_ns = decode_start.elapsed().as_nanos();
        let message = match decoded {
            Ok(message) => message,
            Err(_) => {
                report.decode_failures += 1;
                writeln!(
                    log,
                    "R,{t_ns},-1,-1,-1,0,{length},{decode_ns},0,{hash:016x}"
                )?;
                continue;
            }
        };
        if message.sender == id {
            report.own_dropped += 1; // our own frames, heard only with multicast loopback on
            continue;
        }
        report.frames += 1;
        let sequence = sequence_of(&message);
        writeln!(
            log,
            "R,{t_ns},{},{},{},{sequence},{length},{decode_ns},1,{hash:016x}",
            message.sender, message.owner, message.kind as i32
        )?;
        let owner = message.owner;
        if owner < 0 || owner as usize >= n {
            continue;
        }
        match message.kind {
            MessageKind::Event if message.sequence < CATCHUP_BASE => {
                received[owner as usize].insert(message.sequence);
            }
            MessageKind::Control => {
                // The summary names the owner's newest event whose copies have all gone out;
                // anything at or below it that has not arrived is lost, so ask for it.
                let highest = message.highest_sequence;
                let have = &received[owner as usize];
                let missing: Vec<i64> = ((highest - 512).max(1)..=highest)
                    .filter(|sequence| !have.contains(sequence))
                    .take(MAX_REPAIR_LIST)
                    .collect();
                if !missing.is_empty() {
                    let _ = actions.send(Action::RequestRepair { owner, missing });
                }
            }
            MessageKind::RepairRequest if message.request_owner == id => {
                let _ = actions.send(Action::Resend {
                    sequences: message.missing.clone(),
                });
            }
            _ => {}
        }
    }
    log.flush()?;
    Ok(report)
}

struct Sender {
    socket: UdpSocket,
    group: SocketAddr,
    codec: CotCodec,
    ledger: Ledger,
    overhead_bytes: u64,
    log: BufWriter<File>,
    sent: HashMap<&'static str, u64>,
    deferred: HashMap<&'static str, u64>,
    send_failures: u64,
}

impl Sender {
    fn bits(&self, wire: &[u8]) -> u64 {
        (wire.len() as u64 + self.overhead_bytes) * 8
    }

    /// Admits, charges and sends one frame; false if the ledger refused it.
    #[allow(clippy::too_many_arguments)]
    fn send(
        &mut self,
        t_ns: u64,
        class: Class,
        kind: MessageKind,
        sequence: i64,
        copy: u32,
        wire: &[u8],
        encode_ns: u128,
    ) -> std::io::Result<bool> {
        let bits = self.bits(wire);
        if !self.ledger.admits(t_ns, bits, class) {
            *self.deferred.entry(class.name()).or_default() += 1;
            writeln!(
                self.log,
                "D,{t_ns},{},{},{sequence}",
                class.name(),
                kind as i32
            )?;
            return Ok(false);
        }
        self.ledger.charge(t_ns, bits, class);
        if let Err(error) = self.socket.send_to(wire, self.group) {
            self.send_failures += 1;
            writeln!(self.log, "F,{t_ns},{error}")?;
        }
        *self.sent.entry(class.name()).or_default() += 1;
        writeln!(
            self.log,
            "S,{t_ns},{},{},{sequence},{copy},{},{encode_ns},{:016x}",
            class.name(),
            kind as i32,
            wire.len(),
            fnv1a(wire)
        )?;
        Ok(true)
    }

    fn encode(&self, message: &CotMessage) -> (Vec<u8>, u128) {
        let start = Instant::now();
        let wire = self.codec.encode(message);
        (wire, start.elapsed().as_nanos())
    }
}

pub fn run(args: &Args) -> Result<(), String> {
    let config = Config::from_args(args)?;
    std::fs::create_dir_all(&config.out).map_err(|e| format!("{}: {e}", config.out.display()))?;
    let open_log = |name: String| -> Result<BufWriter<File>, String> {
        let path = config.out.join(name);
        File::create(&path)
            .map(|file| BufWriter::with_capacity(1 << 20, file))
            .map_err(|e| format!("{}: {e}", path.display()))
    };
    let tx_log = open_log(format!("tx_{}.csv", config.id))?;
    let rx_log = open_log(format!("rx_{}.csv", config.id))?;

    let socket = open_socket(&config)?;
    let receive_socket = socket
        .try_clone()
        .map_err(|e| format!("socket clone: {e}"))?;
    let codec = CotCodec::new(WireForm::TakLean, config.n, config.session_seed);
    let start = Instant::now();
    let utc_base_us = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as i64)
        .unwrap_or(0);
    println!(
        "{}",
        serde_json::json!({
            "what": "net_start",
            "id": config.id,
            "n": config.n,
            "group": config.group.to_string(),
            "iface": config.iface.to_string(),
            "utc_start_us": utc_base_us,
            "state_hz": config.state_hz,
            "summary_hz": config.summary_hz,
            "input_hz": config.input_hz,
            "duration_s": config.duration_ns as f64 * 1e-9,
        })
    );

    let stop = Arc::new(AtomicBool::new(false));
    let (action_tx, action_rx) = mpsc::channel();
    let receiver = {
        let codec = codec.clone();
        let stop = stop.clone();
        let blackout = (
            config.blackout_ns,
            config.blackout_every_ns.max(1),
            config.warmup_ns,
        );
        let (id, n) = (config.id, config.n);
        std::thread::spawn(move || {
            receive(
                receive_socket,
                codec,
                id,
                n,
                start,
                blackout,
                stop,
                action_tx,
                rx_log,
            )
        })
    };

    let builder = Builder {
        id: config.id,
        n: config.n as i32,
        utc_base_us,
    };
    let mut sender = Sender {
        socket,
        group: SocketAddr::V4(config.group),
        codec,
        ledger: Ledger::new(
            config.limit_bits,
            config.urgent_reserve_bits,
            config.repair_share_bits,
        ),
        overhead_bytes: config.overhead_bytes,
        log: tx_log,
        sent: HashMap::new(),
        deferred: HashMap::new(),
        send_failures: 0,
    };
    let io = |error: std::io::Error| format!("log: {error}");

    let mut rng = Rng(config.seed ^ (u64::from(config.id as u32) << 32) ^ 0x5EED);
    let state_period = 1e9 / config.state_hz;
    let summary_period = 1e9 / config.summary_hz;
    let mut next_event_ns = if config.input_hz > 0.0 {
        config.warmup_ns + rng.exponential_ns(config.input_hz)
    } else {
        u64::MAX
    };
    let mut next_state_ns = 0f64;
    let mut next_summary_ns = 0f64;
    let mut next_calibration_ns = config.calibration_period_ns / 2;
    let mut event_sequence: i64 = 0;
    let mut state_sequence: i64 = 0;
    let mut highest_complete: i64 = 0;
    let mut recent_onsets: VecDeque<u64> = VecDeque::new();
    let mut events: HashMap<i64, Vec<u8>> = HashMap::new();
    // (due tick, sequence, copy)
    let mut copies: VecDeque<(u64, i64, u32)> = VecDeque::new();
    let mut repair_requests: VecDeque<CotMessage> = VecDeque::new();
    let mut resends: VecDeque<i64> = VecDeque::new();
    let mut last_resend: HashMap<i64, u64> = HashMap::new();
    let mut catchup_sent = 0usize;
    let mut next_catchup_ns = config.catchup_at_ns.unwrap_or(u64::MAX);
    let mut events_created = 0u64;
    let mut inputs_delayed = 0u64;

    let mut tick: u64 = 0;
    loop {
        let tick_ns = seconds_ns(tick as f64 / config.opportunity_hz);
        let elapsed = start.elapsed().as_nanos() as u64;
        if tick_ns > elapsed {
            std::thread::sleep(Duration::from_nanos(tick_ns - elapsed));
        }
        let now = start.elapsed().as_nanos() as u64;
        if now >= config.duration_ns {
            break;
        }

        // Inputs: a Poisson stream, held to the urgent reserve's events per rolling second.
        while next_event_ns <= now {
            while recent_onsets
                .front()
                .is_some_and(|&t| t + 1_000_000_000 <= now)
            {
                recent_onsets.pop_front();
            }
            if recent_onsets.len() >= config.max_events_per_s {
                inputs_delayed += 1;
                break;
            }
            // The input happened at its arrival time, between opportunities; it waits for
            // this one, and that wait counts against its deadline as it does in the simulator.
            let onset = next_event_ns;
            event_sequence += 1;
            events_created += 1;
            recent_onsets.push_back(now);
            let message = builder.event(event_sequence, onset, now, event_sequence);
            let (wire, encode_ns) = sender.encode(&message);
            writeln!(sender.log, "E,{onset},{event_sequence},{encode_ns}").map_err(io)?;
            events.insert(event_sequence, wire);
            for copy in 0..config.copies {
                copies.push_back((tick + u64::from(copy), event_sequence, copy));
            }
            next_event_ns += rng.exponential_ns(config.input_hz);
        }

        // Urgent copies due by this opportunity, oldest first; a refused copy waits a tick.
        let mut waiting = VecDeque::new();
        while let Some((due, sequence, copy)) = copies.pop_front() {
            if due > tick {
                waiting.push_back((due, sequence, copy));
                continue;
            }
            let wire = events[&sequence].clone();
            if sender
                .send(
                    now,
                    Class::Urgent,
                    MessageKind::Event,
                    sequence,
                    copy,
                    &wire,
                    0,
                )
                .map_err(io)?
            {
                if copy + 1 == config.copies {
                    highest_complete = highest_complete.max(sequence);
                }
            } else {
                waiting.push_back((due, sequence, copy));
            }
        }
        copies = waiting;

        // Network summary.
        if now as f64 >= next_summary_ns {
            let message = builder.summary(
                highest_complete,
                (config.id + 1) % config.n as i32,
                highest_complete,
                now,
            );
            let (wire, encode_ns) = sender.encode(&message);
            if sender
                .send(
                    now,
                    Class::Control,
                    MessageKind::Control,
                    highest_complete,
                    0,
                    &wire,
                    encode_ns,
                )
                .map_err(io)?
            {
                next_summary_ns += summary_period;
            }
        }

        // Calibration, now and then.
        if now >= next_calibration_ns {
            let message = builder.calibration(now);
            let (wire, encode_ns) = sender.encode(&message);
            if sender
                .send(
                    now,
                    Class::Control,
                    MessageKind::Calibration,
                    message.sample_us,
                    0,
                    &wire,
                    encode_ns,
                )
                .map_err(io)?
            {
                next_calibration_ns += config.calibration_period_ns;
            }
        }

        // Avatar state; a refused state is dropped, since the next one replaces it.
        if now as f64 >= next_state_ns {
            state_sequence += 1;
            let message = builder.state(state_sequence, now);
            let (wire, encode_ns) = sender.encode(&message);
            sender
                .send(
                    now,
                    Class::State,
                    MessageKind::State,
                    state_sequence,
                    0,
                    &wire,
                    encode_ns,
                )
                .map_err(io)?;
            next_state_ns += state_period;
            if (now as f64) > next_state_ns + state_period {
                next_state_ns = now as f64 + state_period; // never burst to catch up
            }
        }

        // Requests from the receiver.
        while let Ok(action) = action_rx.try_recv() {
            match action {
                Action::RequestRepair { owner, missing } => {
                    if repair_requests
                        .iter()
                        .all(|queued| queued.request_owner != owner)
                    {
                        repair_requests.push_back(builder.repair_request(owner, missing, now));
                    }
                }
                Action::Resend { sequences } => {
                    for sequence in sequences {
                        let recent = last_resend
                            .get(&sequence)
                            .is_some_and(|&t| now < t + RESEND_HOLDOFF_NS);
                        if events.contains_key(&sequence) && !recent && !resends.contains(&sequence)
                        {
                            resends.push_back(sequence);
                        }
                    }
                }
            }
        }
        while let Some(request) = repair_requests.front() {
            let (wire, encode_ns) = sender.encode(request);
            let owner = i64::from(request.request_owner);
            if !sender
                .send(
                    now,
                    Class::Control,
                    MessageKind::RepairRequest,
                    owner,
                    0,
                    &wire,
                    encode_ns,
                )
                .map_err(io)?
            {
                break;
            }
            repair_requests.pop_front();
        }
        while let Some(&sequence) = resends.front() {
            let wire = events[&sequence].clone();
            if !sender
                .send(
                    now,
                    Class::Repair,
                    MessageKind::Event,
                    sequence,
                    99,
                    &wire,
                    0,
                )
                .map_err(io)?
            {
                break;
            }
            last_resend.insert(sequence, now);
            resends.pop_front();
        }

        // Leader catch-up: retained markers paced on the catch-up lane.
        while config.id == 0 && catchup_sent < config.catchup_items && now >= next_catchup_ns {
            let sequence = CATCHUP_BASE + catchup_sent as i64;
            let message = builder.event(sequence, now, now, sequence);
            let (wire, encode_ns) = sender.encode(&message);
            let bits = sender.bits(&wire);
            if !sender
                .send(
                    now,
                    Class::Repair,
                    MessageKind::Event,
                    sequence,
                    0,
                    &wire,
                    encode_ns,
                )
                .map_err(io)?
            {
                break;
            }
            catchup_sent += 1;
            next_catchup_ns += seconds_ns(bits as f64 / config.catchup_bps);
        }

        tick += 1;
    }

    stop.store(true, Ordering::Relaxed);
    sender.log.flush().map_err(io)?;
    let report = receiver
        .join()
        .map_err(|_| "receiver thread panicked".to_string())?
        .map_err(|e| format!("receiver: {e}"))?;
    println!(
        "{}",
        serde_json::json!({
            "what": "net_done",
            "id": config.id,
            "ticks": tick,
            "events_created": events_created,
            "inputs_delayed": inputs_delayed,
            "sent": sender.sent,
            "deferred": sender.deferred,
            "send_failures": sender.send_failures,
            "ledger_max_rolling_bits": sender.ledger.max_total,
            "ledger_violations": sender.ledger.violations,
            "catchup_sent": catchup_sent,
            "frames_received": report.frames,
            "decode_failures": report.decode_failures,
            "blackout_drops": report.blackout_drops,
            "own_frames_dropped": report.own_dropped,
        })
    );
    Ok(())
}
