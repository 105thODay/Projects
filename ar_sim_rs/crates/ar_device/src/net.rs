//! L4 live traffic per the Network Layer ICD, over UDP multicast between phones, with L5
//! sessions on top when asked.
//!
//! Each device runs one process. It sends its own avatar state, network summaries,
//! calibration and urgent events (three copies on consecutive send opportunities), places and
//! deletes markers, and answers repair requests for its events. Every frame passes the
//! rolling ledger first. The receiver thread decodes everything it hears, finds gaps from
//! summaries and asks for repair.
//!
//! Two modes:
//!
//!   fixed     `id=<i> n=<n>`: every device holds its index from the start; no L5 messages.
//!             The preflight and the qualification multicast check use it.
//!   session   `role=leader|member uid=<8 hex> session=<8 hex>`: L5 per `session.rs`. Members
//!             join (at `join_at_s`), are admitted, catch up and then send; one may leave and
//!             rejoin (`leave_at_s`, `rejoin_at_s`); the leader closes at `close_at_s`.
//!
//! Two CSV logs per device, for offline analysis on the host (clock offsets between phones
//! are estimated there from matched send and receive times, so no clock sync is needed):
//!
//!   tx_<id>.csv   S,t_ns,class,kind,seq,copy,bytes,encode_ns,hash   a frame sent
//!                 E,t_ns,seq,encode_ns                               an event's input onset
//!                 D,t_ns,class,kind,seq                              a frame the ledger deferred or dropped
//!                 F,t_ns,error                                       a send that failed
//!                 L,t_ns,what,a,b,c                                  an L5 occurrence (session.rs)
//!   rx_<id>.csv   R,t_ns,sender,owner,kind,seq,bytes,decode_ns,ok,hash,disposition
//!                 B,t_ns,bytes                                       dropped by an emulated blackout
//!
//! t_ns counts from the process start on the monotonic clock. `kind` is the codec's integer
//! code (0 to 5) or a draft kind (10 record, 11 join request, 12 barrier, 13 catch-up request,
//! 14 marker delete). Disposition: 1 applied, 2 heard before admission, 3 sender not a member.
//! In session mode the log files are named by the session's `log_id`, not the roster index.

use std::collections::{HashMap, HashSet, VecDeque};
use std::fs::File;
use std::io::{BufWriter, Write};
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU64, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use cot_codec::{CotCodec, CotMessage, MessageKind, Operation, WireForm, ENTITY_STRIDE};
use cot_draft::{Draft, ItemId};
use socket2::{Domain, Protocol, Socket, Type};

use crate::ledger::{Class, Ledger};
use crate::session::{Outgoing, Session, SessionConfig, MARKER_LOCAL_BASE};
use crate::Args;

const MAX_REPAIR_LIST: usize = 32;
const RESEND_HOLDOFF_NS: u64 = 100_000_000;
/// Roster indices a session can issue; indices are never reused (R16).
const SESSION_INDEX_SPACE: usize = 32;

struct SessionArgs {
    leader: bool,
    uid: String,
    session: u32,
    log_id: i32,
    capacity: u32,
    marker_cap: usize,
    join_at_ns: u64,
    leave_at_ns: Option<u64>,
    rejoin_at_ns: Option<u64>,
    close_at_ns: Option<u64>,
    catchup_window_ns: u64,
}

enum Mode {
    Fixed { id: i32, n: usize },
    Session(SessionArgs),
}

struct Config {
    mode: Mode,
    group: SocketAddrV4,
    iface: Ipv4Addr,
    multicast_loop: bool,
    duration_ns: u64,
    warmup_ns: u64,
    state_hz: f64,
    summary_hz: f64,
    opportunity_hz: f64,
    input_hz: f64,
    marker_hz: f64,
    delete_hz: f64,
    copies: u32,
    calibration_period_ns: u64,
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

fn optional_ns(seconds: f64) -> Option<u64> {
    (seconds >= 0.0).then(|| seconds_ns(seconds))
}

fn hex8(text: &str, what: &str) -> Result<u32, String> {
    if text.len() != 8 {
        return Err(format!("{what} must be 8 hex digits"));
    }
    u32::from_str_radix(text, 16).map_err(|_| format!("{what} must be 8 hex digits"))
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
        let role = args.text("role", "");
        let duration_ns = seconds_ns(args.number("duration_s", 60.0)?);
        let mode = if role.is_empty() {
            let id: i32 = args.number("id", -1)?;
            let n: usize = args.number("n", 0)?;
            if n < 2 || id < 0 || id as usize >= n {
                return Err(
                    "fixed mode needs n >= 2 and 0 <= id < n (or give role= for a session)".into(),
                );
            }
            Mode::Fixed { id, n }
        } else {
            if role != "leader" && role != "member" {
                return Err("role must be leader or member".into());
            }
            let uid = args.text("uid", "");
            hex8(&uid, "uid")?;
            let session = hex8(&args.text("session", ""), "session")?;
            let close_at = args.number("close_at_s", -1.0)?;
            Mode::Session(SessionArgs {
                leader: role == "leader",
                uid,
                session,
                log_id: args.number("log_id", 0)?,
                capacity: args.number("capacity", 8)?,
                marker_cap: args.number("marker_cap", 50)?,
                join_at_ns: seconds_ns(args.number("join_at_s", 0.0)?),
                leave_at_ns: optional_ns(args.number("leave_at_s", -1.0)?),
                rejoin_at_ns: optional_ns(args.number("rejoin_at_s", -1.0)?),
                close_at_ns: optional_ns(close_at),
                catchup_window_ns: seconds_ns(args.number("catchup_window_s", 10.0)?),
            })
        };
        let config = Config {
            mode,
            group,
            iface,
            multicast_loop: args.number::<u8>("loop", 0)? != 0,
            duration_ns,
            warmup_ns: seconds_ns(args.number("warmup_s", 5.0)?),
            state_hz: args.number("state_hz", 15.0)?,
            summary_hz: args.number("summary_hz", 3.0)?,
            opportunity_hz: args.number("opportunity_hz", 90.0)?,
            input_hz: args.number("input_hz", 1.0)?,
            marker_hz: args.number("marker_hz", 0.0)?,
            delete_hz: args.number("delete_hz", 0.0)?,
            copies: args.number("copies", 3)?,
            calibration_period_ns: seconds_ns(args.number("calibration_period_s", 10.0)?),
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
        for (name, value) in [
            ("state_hz", config.state_hz),
            ("summary_hz", config.summary_hz),
            ("opportunity_hz", config.opportunity_hz),
        ] {
            if value <= 0.0 {
                return Err(format!("{name} must be positive"));
            }
        }
        if config.copies == 0 || config.catchup_bps <= 0.0 {
            return Err("copies and catchup_kbps must be positive".into());
        }
        if config.input_hz < 0.0 || config.marker_hz < 0.0 || config.delete_hz < 0.0 {
            return Err("input_hz, marker_hz and delete_hz must not be negative".into());
        }
        Ok(config)
    }

    /// The number the log files carry: the fixed id, or the session's log id.
    fn log_id(&self) -> i32 {
        match &self.mode {
            Mode::Fixed { id, .. } => *id,
            Mode::Session(s) => s.log_id,
        }
    }

    /// Roster size the codec is built for.
    fn participants(&self) -> usize {
        match &self.mode {
            Mode::Fixed { n, .. } => *n,
            Mode::Session(_) => SESSION_INDEX_SPACE,
        }
    }
}

/// splitmix64: a small, seedable generator for arrival times and choices.
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
        if rate_hz <= 0.0 {
            return u64::MAX / 4;
        }
        let unit = ((self.next() >> 11) as f64 + 0.5) / (1u64 << 53) as f64;
        seconds_ns(-unit.ln() / rate_hz)
    }
}

fn fnv1a(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325u64, |hash, byte| {
        (hash ^ u64::from(*byte)).wrapping_mul(0x0000_0100_0000_01b3)
    })
}

/// What the receiver hands the main loop.
enum Action {
    RequestRepair {
        owner: i32,
        missing: Vec<i64>,
    },
    Resend {
        sequences: Vec<i64>,
    },
    Draft {
        draft: Draft,
        wire: Vec<u8>,
        t_ns: u64,
    },
    Marker {
        item: ItemId,
        wire: Vec<u8>,
    },
    Summary {
        sender: i32,
        epoch: u32,
    },
    DropTrack {
        sender: i32,
        t_ns: u64,
    },
}

/// What the main loop tells the receiver about membership, so it can file each frame.
struct Gate {
    my_index: AtomicI32,
    present: AtomicU64,
    roster: AtomicU64,
}

impl Gate {
    fn bit(mask: u64, index: i32) -> bool {
        (0..64).contains(&index) && mask & (1u64 << index) != 0
    }
}

/// Builds every message this device sends, with real field values.
struct Builder {
    id: i32,
    n: i32,
    epoch: i32,
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

    /// This member's own track withdrawn (HICD leave_session): stale before start.
    fn drop_track(&self, sequence: i64, t_ns: u64) -> CotMessage {
        let mut m = self.state(sequence, t_ns);
        m.stale_us = m.time_us - 1000;
        m
    }

    fn summary(&self, highest: i64, ack_owner: i32, ack_sequence: i64, t_ns: u64) -> CotMessage {
        let mut m = self.base(MessageKind::Control, t_ns);
        m.highest_sequence = highest;
        m.clock_offset_us = 0;
        m.membership_epoch = self.epoch;
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

fn draft_sequence(draft: &Draft) -> (i32, i64) {
    match draft {
        Draft::Record(m) => (-2, m.revision as i64),
        Draft::Join(_) => (-2, 0),
        Draft::Barrier(m) => (-2, (m.markers.len() + m.tombstones.len()) as i64),
        Draft::CatchupRequest(m) => (-2, m.missing.len() as i64),
        Draft::Delete(m) => (m.deleter, m.target.sequence),
    }
}

/// Whether an event creates a marker (a retained item) rather than an input's effect.
fn is_marker(message: &CotMessage) -> bool {
    message.kind == MessageKind::Event
        && message.entity.rem_euclid(ENTITY_STRIDE) >= MARKER_LOCAL_BASE
}

struct ReceiveReport {
    frames: u64,
    decode_failures: u64,
    blackout_drops: u64,
    own_dropped: u64,
    before_admission: u64,
    not_member: u64,
}

struct Receiver {
    socket: UdpSocket,
    codec: CotCodec,
    uid: String,
    participants: usize,
    start: Instant,
    blackout: (u64, u64, u64),
    stop: Arc<AtomicBool>,
    gate: Arc<Gate>,
    actions: mpsc::Sender<Action>,
    log: BufWriter<File>,
}

impl Receiver {
    fn run(mut self) -> std::io::Result<ReceiveReport> {
        let (blackout_ns, every_ns, warmup_ns) = self.blackout;
        let mut buffer = vec![0u8; 2048];
        let mut received: Vec<HashSet<i64>> = vec![HashSet::new(); self.participants];
        let mut report = ReceiveReport {
            frames: 0,
            decode_failures: 0,
            blackout_drops: 0,
            own_dropped: 0,
            before_admission: 0,
            not_member: 0,
        };
        while !self.stop.load(Ordering::Relaxed) {
            let length = match self.socket.recv_from(&mut buffer) {
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
            let t_ns = self.start.elapsed().as_nanos() as u64;
            if blackout_ns > 0 && t_ns >= warmup_ns && (t_ns - warmup_ns) % every_ns < blackout_ns {
                report.blackout_drops += 1;
                writeln!(self.log, "B,{t_ns},{length}")?;
                continue;
            }
            let wire = &buffer[..length];
            let hash = fnv1a(wire);
            let my_index = self.gate.my_index.load(Ordering::Relaxed);
            let decode_start = Instant::now();
            // Draft kinds first; anything else goes to the reference codec.
            match cot_draft::decode(wire) {
                Ok(draft) => {
                    let decode_ns = decode_start.elapsed().as_nanos();
                    let own = match &draft {
                        Draft::Join(m) => m.candidate == self.uid,
                        Draft::CatchupRequest(m) => m.requester == self.uid,
                        Draft::Record(m) => m.leader == self.uid,
                        Draft::Barrier(m) => m.sender == self.uid,
                        Draft::Delete(m) => m.deleter == my_index && my_index >= 0,
                    };
                    if own {
                        report.own_dropped += 1;
                        continue;
                    }
                    report.frames += 1;
                    let (sender, sequence) = draft_sequence(&draft);
                    writeln!(
                        self.log,
                        "R,{t_ns},{sender},{sender},{},{sequence},{length},{decode_ns},1,{hash:016x},1",
                        draft.kind_code()
                    )?;
                    let _ = self.actions.send(Action::Draft {
                        draft,
                        wire: wire.to_vec(),
                        t_ns,
                    });
                    continue;
                }
                Err(cot_draft::DecodeError::Malformed(_)) => {
                    report.decode_failures += 1;
                    let decode_ns = decode_start.elapsed().as_nanos();
                    writeln!(
                        self.log,
                        "R,{t_ns},-1,-1,-1,0,{length},{decode_ns},0,{hash:016x},0"
                    )?;
                    continue;
                }
                Err(cot_draft::DecodeError::NotDraft) => {}
            }
            let decoded = self.codec.decode(wire);
            let decode_ns = decode_start.elapsed().as_nanos();
            let message = match decoded {
                Ok(message) => message,
                Err(_) => {
                    report.decode_failures += 1;
                    writeln!(
                        self.log,
                        "R,{t_ns},-1,-1,-1,0,{length},{decode_ns},0,{hash:016x},0"
                    )?;
                    continue;
                }
            };
            if message.sender == my_index && my_index >= 0 {
                report.own_dropped += 1; // our own frames, heard only with multicast loopback on
                continue;
            }
            report.frames += 1;
            let present = self.gate.present.load(Ordering::Relaxed);
            let roster = self.gate.roster.load(Ordering::Relaxed);
            let marker = is_marker(&message);
            // A relayed marker keeps its creator as sender, who may have left (R18).
            let disposition = if my_index < 0 {
                2
            } else if Gate::bit(present, message.sender)
                || (marker && Gate::bit(roster, message.sender))
            {
                1
            } else {
                3
            };
            let sequence = sequence_of(&message);
            writeln!(
                self.log,
                "R,{t_ns},{},{},{},{sequence},{length},{decode_ns},1,{hash:016x},{disposition}",
                message.sender, message.owner, message.kind as i32
            )?;
            match disposition {
                2 => {
                    report.before_admission += 1;
                    continue;
                }
                3 => {
                    report.not_member += 1;
                    continue;
                }
                _ => {}
            }
            let owner = message.owner;
            if owner < 0 || owner as usize >= self.participants {
                continue;
            }
            match message.kind {
                MessageKind::Event => {
                    received[owner as usize].insert(message.sequence);
                    if marker {
                        let _ = self.actions.send(Action::Marker {
                            item: ItemId {
                                owner,
                                sequence: message.sequence,
                            },
                            wire: wire.to_vec(),
                        });
                    }
                }
                MessageKind::State if message.stale_us < message.time_us => {
                    let _ = self.actions.send(Action::DropTrack {
                        sender: message.sender,
                        t_ns,
                    });
                }
                MessageKind::Control => {
                    let _ = self.actions.send(Action::Summary {
                        sender: message.sender,
                        epoch: message.membership_epoch.max(0) as u32,
                    });
                    // The summary names the owner's newest event whose copies have all gone out;
                    // anything at or below it that has not arrived is lost, so ask for it.
                    let highest = message.highest_sequence;
                    let have = &received[owner as usize];
                    let missing: Vec<i64> = ((highest - 512).max(1)..=highest)
                        .filter(|sequence| !have.contains(sequence))
                        .take(MAX_REPAIR_LIST)
                        .collect();
                    if !missing.is_empty() {
                        let _ = self.actions.send(Action::RequestRepair { owner, missing });
                    }
                }
                MessageKind::RepairRequest if message.request_owner == my_index => {
                    let _ = self.actions.send(Action::Resend {
                        sequences: message.missing.clone(),
                    });
                }
                _ => {}
            }
        }
        self.log.flush()?;
        Ok(report)
    }
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
        kind: i32,
        sequence: i64,
        copy: u32,
        wire: &[u8],
        encode_ns: u128,
    ) -> std::io::Result<bool> {
        let bits = self.bits(wire);
        if !self.ledger.admits(t_ns, bits, class) {
            *self.deferred.entry(class.name()).or_default() += 1;
            writeln!(self.log, "D,{t_ns},{},{kind},{sequence}", class.name())?;
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
            "S,{t_ns},{},{kind},{sequence},{copy},{},{encode_ns},{:016x}",
            class.name(),
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

/// One copy of an urgent frame waiting for its send opportunity.
struct Copy {
    due_tick: u64,
    wire: Arc<Vec<u8>>,
    kind: i32,
    sequence: i64,
    copy: u32,
    /// The last copy of one of this device's events: its sequence then counts as fully sent.
    completes_event: bool,
}

fn queue_urgent(
    queue: &mut VecDeque<Copy>,
    tick: u64,
    copies: u32,
    wire: Arc<Vec<u8>>,
    kind: i32,
    sequence: i64,
    event: bool,
) {
    for copy in 0..copies {
        queue.push_back(Copy {
            due_tick: tick + u64::from(copy),
            wire: wire.clone(),
            kind,
            sequence,
            copy,
            completes_event: event && copy + 1 == copies,
        });
    }
}

pub fn run(args: &Args) -> Result<(), String> {
    let config = Config::from_args(args)?;
    std::fs::create_dir_all(&config.out).map_err(|e| format!("{}: {e}", config.out.display()))?;
    let log_id = config.log_id();
    let open_log = |name: String| -> Result<BufWriter<File>, String> {
        let path = config.out.join(name);
        File::create(&path)
            .map(|file| BufWriter::with_capacity(1 << 20, file))
            .map_err(|e| format!("{}: {e}", path.display()))
    };
    let tx_log = open_log(format!("tx_{log_id}.csv"))?;
    let rx_log = open_log(format!("rx_{log_id}.csv"))?;

    let socket = open_socket(&config)?;
    let receive_socket = socket
        .try_clone()
        .map_err(|e| format!("socket clone: {e}"))?;
    let participants = config.participants();
    let codec = CotCodec::new(WireForm::TakLean, participants, config.session_seed);
    let start = Instant::now();
    let utc_base_us = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as i64)
        .unwrap_or(0);

    let mut session = match &config.mode {
        Mode::Fixed { .. } => None,
        Mode::Session(s) => Some(Session::new(
            SessionConfig {
                session: s.session,
                uid: s.uid.clone(),
                leader: s.leader,
                capacity: s.capacity,
                marker_cap: s.marker_cap,
                catchup_bps: config.catchup_bps,
                catchup_window_ns: s.catchup_window_ns,
                join_at_ns: s.join_at_ns,
                leave_at_ns: s.leave_at_ns,
                rejoin_at_ns: s.rejoin_at_ns,
                close_at_ns: s.close_at_ns,
                end_us: utc_base_us + (config.duration_ns / 1000) as i64,
            },
            utc_base_us,
        )),
    };
    let (fixed_id, uid) = match &config.mode {
        Mode::Fixed { id, .. } => (*id, String::new()),
        Mode::Session(s) => (-1, s.uid.clone()),
    };
    println!(
        "{}",
        serde_json::json!({
            "what": "net_start",
            "mode": if session.is_some() { "session" } else { "fixed" },
            "log_id": log_id,
            "uid": uid,
            "group": config.group.to_string(),
            "iface": config.iface.to_string(),
            "utc_start_us": utc_base_us,
            "state_hz": config.state_hz,
            "summary_hz": config.summary_hz,
            "input_hz": config.input_hz,
            "marker_hz": config.marker_hz,
            "delete_hz": config.delete_hz,
            "duration_s": config.duration_ns as f64 * 1e-9,
        })
    );

    let gate = Arc::new(Gate {
        my_index: AtomicI32::new(fixed_id),
        present: AtomicU64::new(if session.is_some() {
            0
        } else {
            (1u64 << participants.min(63)) - 1
        }),
        roster: AtomicU64::new(if session.is_some() {
            0
        } else {
            (1u64 << participants.min(63)) - 1
        }),
    });
    let stop = Arc::new(AtomicBool::new(false));
    let (action_tx, action_rx) = mpsc::channel();
    let receiver = Receiver {
        socket: receive_socket,
        codec: codec.clone(),
        uid: uid.clone(),
        participants,
        start,
        blackout: (
            config.blackout_ns,
            config.blackout_every_ns.max(1),
            config.warmup_ns,
        ),
        stop: stop.clone(),
        gate: gate.clone(),
        actions: action_tx,
        log: rx_log,
    };
    let receiver = std::thread::spawn(move || receiver.run());

    let mut builder = Builder {
        id: fixed_id.max(0),
        n: participants as i32,
        epoch: 1,
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

    let seed =
        config.seed ^ (u64::from(fnv1a(uid.as_bytes()) as u32) << 32) ^ (log_id as u64) ^ 0x5EED;
    let mut rng = Rng(seed);
    let mut choice_rng = Rng(seed ^ 0xC401CE);
    let state_period = 1e9 / config.state_hz;
    let summary_period = 1e9 / config.summary_hz;
    let mut next_event_ns = config.warmup_ns + rng.exponential_ns(config.input_hz);
    let mut next_marker_ns = config.warmup_ns + rng.exponential_ns(config.marker_hz);
    let mut next_delete_ns = config.warmup_ns + rng.exponential_ns(config.delete_hz);
    let mut next_state_ns = 0f64;
    let mut next_summary_ns = 0f64;
    let mut next_calibration_ns = config.calibration_period_ns / 2;
    let mut event_sequence: i64 = 0;
    let mut state_sequence: i64 = 0;
    let mut highest_complete: i64 = 0;
    let mut recent_onsets: VecDeque<u64> = VecDeque::new();
    let mut events: HashMap<i64, Arc<Vec<u8>>> = HashMap::new();
    let mut copies: VecDeque<Copy> = VecDeque::new();
    let mut repair_requests: VecDeque<CotMessage> = VecDeque::new();
    let mut resends: VecDeque<i64> = VecDeque::new();
    let mut last_resend: HashMap<i64, u64> = HashMap::new();
    let mut events_created = 0u64;
    let mut inputs_delayed = 0u64;
    let mut input_waiting = false;
    let mut inputs_skipped = 0u64;
    let mut l5_out: Vec<Outgoing> = Vec::new();
    let mut last_index: Option<i32> = None;
    let mut was_leaving = false;

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

        // What the receiver heard.
        while let Ok(action) = action_rx.try_recv() {
            match action {
                Action::Draft { draft, wire, t_ns } => {
                    if let Some(s) = session.as_mut() {
                        s.on_draft(draft, &wire, t_ns, &mut l5_out);
                    }
                }
                Action::Marker { item, wire } => {
                    if let Some(s) = session.as_mut() {
                        s.on_marker(item, &wire);
                    }
                }
                Action::Summary {
                    sender: from,
                    epoch,
                } => {
                    if let Some(s) = session.as_mut() {
                        s.on_summary(from, epoch);
                    }
                }
                Action::DropTrack { sender: from, t_ns } => {
                    if let Some(s) = session.as_mut() {
                        s.on_drop_track(from, t_ns, &mut l5_out);
                    }
                }
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

        // L5: advance the session and publish membership to the receiver.
        let ready = match session.as_mut() {
            None => true,
            Some(s) => {
                s.tick(now, &mut l5_out);
                let leaving = s.leaving_now();
                if leaving && !was_leaving {
                    // HICD leave_session: this member's own track goes out with stale before start.
                    if let Some(index) = last_index {
                        builder.id = index;
                        state_sequence += 1;
                        let (wire, _) = sender.encode(&builder.drop_track(state_sequence, now));
                        queue_urgent(
                            &mut copies,
                            tick,
                            config.copies,
                            Arc::new(wire),
                            MessageKind::State as i32,
                            state_sequence,
                            false,
                        );
                    }
                }
                was_leaving = leaving;
                last_index = s.index().or(if leaving { last_index } else { None });
                gate.my_index
                    .store(s.index().unwrap_or(-1), Ordering::Relaxed);
                gate.present.store(s.present_mask(), Ordering::Relaxed);
                gate.roster.store(s.roster_mask(), Ordering::Relaxed);
                if let Some(index) = s.index() {
                    builder.id = index;
                }
                builder.epoch = s.epoch() as i32;
                for note in s.take_notes() {
                    writeln!(
                        sender.log,
                        "L,{now},{},{},{},{}",
                        note.what, note.a, note.b, note.c
                    )
                    .map_err(io)?;
                }
                s.ready()
            }
        };

        // L5 frames: urgent ones on three opportunities, the rest once (they retry on their own).
        for frame in l5_out.drain(..) {
            if frame.urgent {
                queue_urgent(
                    &mut copies,
                    tick,
                    config.copies,
                    frame.wire,
                    frame.kind,
                    frame.sequence,
                    false,
                );
            } else {
                sender
                    .send(
                        now,
                        frame.class,
                        frame.kind,
                        frame.sequence,
                        0,
                        &frame.wire,
                        0,
                    )
                    .map_err(io)?;
            }
        }
        if let Some(s) = session.as_mut() {
            while let Some(frame) = s.lane_next(now) {
                let bits = sender.bits(&frame.wire);
                if frame.urgent {
                    queue_urgent(
                        &mut copies,
                        tick,
                        config.copies,
                        frame.wire,
                        frame.kind,
                        frame.sequence,
                        false,
                    );
                } else if !sender
                    .send(
                        now,
                        frame.class,
                        frame.kind,
                        frame.sequence,
                        0,
                        &frame.wire,
                        0,
                    )
                    .map_err(io)?
                {
                    break;
                }
                s.lane_sent(now, bits);
            }
        }

        if ready {
            // Inputs: a Poisson stream, held to the urgent reserve's events per rolling second.
            while next_event_ns <= now {
                while recent_onsets
                    .front()
                    .is_some_and(|&t| t + 1_000_000_000 <= now)
                {
                    recent_onsets.pop_front();
                }
                if recent_onsets.len() >= config.max_events_per_s {
                    // The urgent reserve holds 7 events a second; this input waits its turn.
                    if !input_waiting {
                        inputs_delayed += 1;
                        input_waiting = true;
                    }
                    break;
                }
                input_waiting = false;
                // The input happened at its arrival time, between opportunities; it waits for
                // this one, and that wait counts against its deadline as it does in the simulator.
                let onset = next_event_ns;
                event_sequence += 1;
                events_created += 1;
                recent_onsets.push_back(now);
                let message = builder.event(event_sequence, onset, now, event_sequence);
                let (wire, encode_ns) = sender.encode(&message);
                writeln!(sender.log, "E,{onset},{event_sequence},{encode_ns}").map_err(io)?;
                let wire = Arc::new(wire);
                events.insert(event_sequence, wire.clone());
                queue_urgent(
                    &mut copies,
                    tick,
                    config.copies,
                    wire,
                    MessageKind::Event as i32,
                    event_sequence,
                    true,
                );
                next_event_ns += rng.exponential_ns(config.input_hz);
            }
            // Markers (L7, retained) and deletes, when a session holds them.
            if let Some(s) = session.as_mut() {
                while next_marker_ns <= now {
                    let onset = next_marker_ns;
                    next_marker_ns += rng.exponential_ns(config.marker_hz);
                    if !s.can_place() {
                        s.refused_marker();
                        writeln!(
                            sender.log,
                            "L,{now},marker_refused,{},0,0",
                            s.retained_count()
                        )
                        .map_err(io)?;
                        continue;
                    }
                    event_sequence += 1;
                    let message = builder.event(
                        event_sequence,
                        onset,
                        now,
                        MARKER_LOCAL_BASE + event_sequence,
                    );
                    let (wire, encode_ns) = sender.encode(&message);
                    writeln!(sender.log, "E,{onset},{event_sequence},{encode_ns}").map_err(io)?;
                    let item = ItemId {
                        owner: builder.id,
                        sequence: event_sequence,
                    };
                    s.placed(item, &wire);
                    writeln!(
                        sender.log,
                        "L,{now},marker_placed,{},{},{}",
                        item.owner,
                        item.sequence,
                        s.retained_count()
                    )
                    .map_err(io)?;
                    let wire = Arc::new(wire);
                    events.insert(event_sequence, wire.clone());
                    queue_urgent(
                        &mut copies,
                        tick,
                        config.copies,
                        wire,
                        MessageKind::Event as i32,
                        event_sequence,
                        true,
                    );
                }
                while next_delete_ns <= now {
                    next_delete_ns += rng.exponential_ns(config.delete_hz);
                    let live = s.live_markers();
                    if live.is_empty() {
                        continue;
                    }
                    let target = live[(choice_rng.next() % live.len() as u64) as usize];
                    if let Some(frame) = s.delete(target, now) {
                        writeln!(
                            sender.log,
                            "L,{now},delete_sent,{},{},{}",
                            target.owner,
                            target.sequence,
                            s.retained_count()
                        )
                        .map_err(io)?;
                        queue_urgent(
                            &mut copies,
                            tick,
                            config.copies,
                            frame.wire,
                            frame.kind,
                            frame.sequence,
                            false,
                        );
                    }
                }
            }
        } else {
            // Not sending yet: inputs that would have happened are skipped, not saved up.
            for (next, rate) in [
                (&mut next_event_ns, config.input_hz),
                (&mut next_marker_ns, config.marker_hz),
                (&mut next_delete_ns, config.delete_hz),
            ] {
                while *next <= now {
                    *next += rng.exponential_ns(rate);
                    inputs_skipped += 1;
                }
            }
        }

        // Urgent copies due by this opportunity, oldest first; a refused copy waits a tick.
        let mut waiting = VecDeque::new();
        while let Some(copy) = copies.pop_front() {
            if copy.due_tick > tick {
                waiting.push_back(copy);
                continue;
            }
            if sender
                .send(
                    now,
                    Class::Urgent,
                    copy.kind,
                    copy.sequence,
                    copy.copy,
                    &copy.wire,
                    0,
                )
                .map_err(io)?
            {
                if copy.completes_event {
                    highest_complete = highest_complete.max(copy.sequence);
                }
            } else {
                waiting.push_back(copy);
            }
        }
        copies = waiting;

        if ready {
            // Network summary.
            if now as f64 >= next_summary_ns {
                let message = builder.summary(
                    highest_complete,
                    (builder.id + 1) % builder.n,
                    highest_complete,
                    now,
                );
                let (wire, encode_ns) = sender.encode(&message);
                if sender
                    .send(
                        now,
                        Class::Control,
                        MessageKind::Control as i32,
                        highest_complete,
                        0,
                        &wire,
                        encode_ns,
                    )
                    .map_err(io)?
                {
                    next_summary_ns += summary_period;
                    if (now as f64) > next_summary_ns + summary_period {
                        next_summary_ns = now as f64 + summary_period;
                    }
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
                        MessageKind::Calibration as i32,
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
                        MessageKind::State as i32,
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
            // Repair: requests for what others lost, resends of what we own.
            while let Some(request) = repair_requests.front() {
                let (wire, encode_ns) = sender.encode(request);
                let owner = i64::from(request.request_owner);
                if !sender
                    .send(
                        now,
                        Class::Control,
                        MessageKind::RepairRequest as i32,
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
                        MessageKind::Event as i32,
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
        } else {
            repair_requests.clear();
            resends.clear();
        }

        tick += 1;
    }

    stop.store(true, Ordering::Relaxed);
    if let Some(s) = session.as_mut() {
        for note in s.take_notes() {
            writeln!(
                sender.log,
                "L,{},{},{},{},{}",
                config.duration_ns, note.what, note.a, note.b, note.c
            )
            .map_err(io)?;
        }
    }
    sender.log.flush().map_err(io)?;
    let report = receiver
        .join()
        .map_err(|_| "receiver thread panicked".to_string())?
        .map_err(|e| format!("receiver: {e}"))?;
    println!(
        "{}",
        serde_json::json!({
            "what": "net_done",
            "log_id": log_id,
            "index": session.as_ref().map_or(Some(fixed_id), |s| s.index()),
            "ticks": tick,
            "events_created": events_created,
            "inputs_delayed": inputs_delayed,
            "inputs_skipped": inputs_skipped,
            "sent": sender.sent,
            "deferred": sender.deferred,
            "send_failures": sender.send_failures,
            "ledger_max_rolling_bits": sender.ledger.max_total,
            "ledger_violations": sender.ledger.violations,
            "frames_received": report.frames,
            "decode_failures": report.decode_failures,
            "blackout_drops": report.blackout_drops,
            "own_frames_dropped": report.own_dropped,
            "heard_before_admission": report.before_admission,
            "from_non_members": report.not_member,
            "session": session.as_ref().map(|s| s.summary()),
        })
    );
    Ok(())
}
