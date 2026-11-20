//! L5, the session layer, per the Network ICD (sections 8.6, 8.7, 9) and the HICD.
//!
//! One leader creates the session, admits candidates, readmits a returning member at its old
//! index, marks a member departed when it drops its own track, relays the retained set to every
//! newcomer (tombstones first, then markers, then a snapshot barrier) on the catch-up lane, and
//! closes the session. Every member keeps the retained set: live markers and tombstones, at most
//! `marker_cap` items together; a tombstone outranks any create for its marker. A newcomer stays
//! silent until it holds every item the barrier lists and has heard a summary at its admission
//! epoch from every present member with a lower roster index.
//!
//! That last rule refines the R17 proposal ("a summary from every member"), which deadlocks when
//! two newcomers are admitted together: each would wait for the other's first summary. Ordering
//! the wait by roster index cannot form a cycle, since index 0, the leader, is always ready. A
//! newcomer that still waits a full catch-up window after its catch-up completes becomes ready
//! anyway and says so, so one silent member cannot hold it forever.
//!
//! The session talks to the rest of the device through plain calls: the main loop feeds it what
//! the receiver heard, asks it each tick for the frames it wants sent, and reads its state to
//! decide whether this device may send L7 traffic.

use std::collections::{BTreeMap, BTreeSet, HashMap, VecDeque};
use std::sync::Arc;

use cot_draft::{
    Barrier, CatchupRequest, Draft, ItemId, JoinRequest, MarkerDelete, Phase, SessionRecord,
    AR_EXTENSION_ID,
};

use crate::ledger::Class;

/// Marker entities use local numbers from here up, inside the codec's entity stride.
pub const MARKER_LOCAL_BASE: i64 = 500_000_000;
const JOIN_RETRY_NS: u64 = 1_000_000_000;
const RECORD_RESEND_HOLDOFF_NS: u64 = 500_000_000;
const CATCHUP_REQUEST_RETRY_NS: u64 = 1_000_000_000;
const ITEM_RESEND_HOLDOFF_NS: u64 = 1_000_000_000;
const MAX_REQUEST_ITEMS: usize = 40;
const LEADER_ABSENT_NS: u64 = 10_000_000_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    /// Before this member's join time.
    Waiting,
    /// Asking to join; holds no index.
    Candidate,
    /// Holds an index; catching up and waiting on earlier members; sends nothing of its own.
    Admitted,
    /// Full member.
    Ready,
    /// Left the session on purpose (it may rejoin).
    Left,
    /// The leader closed the session.
    Closed,
}

impl State {
    pub fn name(self) -> &'static str {
        match self {
            State::Waiting => "waiting",
            State::Candidate => "candidate",
            State::Admitted => "admitted",
            State::Ready => "ready",
            State::Left => "left",
            State::Closed => "closed",
        }
    }
}

pub struct SessionConfig {
    pub session: u32,
    pub uid: String,
    pub leader: bool,
    pub capacity: u32,
    pub marker_cap: usize,
    pub catchup_bps: f64,
    pub catchup_window_ns: u64,
    pub join_at_ns: u64,
    pub leave_at_ns: Option<u64>,
    pub rejoin_at_ns: Option<u64>,
    pub close_at_ns: Option<u64>,
    pub end_us: i64,
}

/// A frame the session wants sent.
pub struct Outgoing {
    pub wire: Arc<Vec<u8>>,
    pub class: Class,
    pub kind: i32,
    pub sequence: i64,
    /// Send on three consecutive opportunities (urgent service); otherwise once.
    pub urgent: bool,
}

/// One L5 occurrence, for the device's log: what happened and up to three numbers.
pub struct Note {
    pub what: &'static str,
    pub a: i64,
    pub b: i64,
    pub c: i64,
}

enum LaneItem {
    Retained(ItemId),
    Tombstone(ItemId),
    Barrier,
}

#[derive(Default)]
pub struct Counters {
    pub admissions: u64,
    pub readmissions: u64,
    pub refused_joins: u64,
    pub departures_marked: u64,
    pub catchup_items_sent: u64,
    pub catchup_requests: u64,
    pub markers_placed: u64,
    pub markers_refused: u64,
    pub deletes_sent: u64,
    pub records_rejected: u64,
    pub creates_after_tombstone: u64,
}

pub struct Session {
    config: SessionConfig,
    pub state: State,
    pub counters: Counters,
    notes: Vec<Note>,
    // the latest record applied
    leader_uid: Option<String>,
    revision: u64,
    epoch: u32,
    phase: Phase,
    roster: Vec<String>,
    departed: BTreeSet<u32>,
    my_index: Option<i32>,
    // retained set
    markers: BTreeMap<ItemId, Arc<Vec<u8>>>,
    tombstones: BTreeMap<ItemId, Arc<Vec<u8>>>,
    // leader
    lane: VecDeque<LaneItem>,
    lane_next_ns: u64,
    record_resent: HashMap<String, u64>,
    item_resent: HashMap<ItemId, u64>,
    close_sent: bool,
    // newcomer
    admitted_at_ns: u64,
    admitted_epoch: u32,
    barrier: Option<Barrier>,
    catchup_complete_ns: Option<u64>,
    heard_at_epoch: BTreeSet<i32>,
    next_join_ns: u64,
    first_join_ns: Option<u64>,
    next_catchup_request_ns: u64,
    delete_sequence: i64,
    utc_base_us: i64,
}

fn arc(wire: Vec<u8>) -> Arc<Vec<u8>> {
    Arc::new(wire)
}

impl Session {
    pub fn new(config: SessionConfig, utc_base_us: i64) -> Session {
        let leader = config.leader;
        let mut session = Session {
            state: if leader { State::Ready } else { State::Waiting },
            counters: Counters::default(),
            notes: Vec::new(),
            leader_uid: None,
            revision: 0,
            epoch: 0,
            phase: Phase::Match,
            roster: Vec::new(),
            departed: BTreeSet::new(),
            my_index: None,
            markers: BTreeMap::new(),
            tombstones: BTreeMap::new(),
            lane: VecDeque::new(),
            lane_next_ns: 0,
            record_resent: HashMap::new(),
            item_resent: HashMap::new(),
            close_sent: false,
            admitted_at_ns: 0,
            admitted_epoch: 0,
            barrier: None,
            catchup_complete_ns: None,
            heard_at_epoch: BTreeSet::new(),
            next_join_ns: config.join_at_ns,
            first_join_ns: None,
            next_catchup_request_ns: 0,
            delete_sequence: 0,
            utc_base_us,
            config,
        };
        if leader {
            session.leader_uid = Some(session.config.uid.clone());
            session.roster.push(session.config.uid.clone());
            session.my_index = Some(0);
            session.revision = 1;
            session.epoch = 1;
        }
        session
    }

    fn note(&mut self, what: &'static str, a: i64, b: i64, c: i64) {
        self.notes.push(Note { what, a, b, c });
    }

    pub fn take_notes(&mut self) -> Vec<Note> {
        std::mem::take(&mut self.notes)
    }

    fn utc(&self, t_ns: u64) -> i64 {
        self.utc_base_us + (t_ns / 1000) as i64
    }

    pub fn index(&self) -> Option<i32> {
        match self.state {
            State::Admitted | State::Ready => self.my_index,
            _ => None,
        }
    }

    pub fn ready(&self) -> bool {
        self.state == State::Ready && self.phase != Phase::Closed
    }

    pub fn epoch(&self) -> u32 {
        self.epoch
    }

    /// Indices of members present now (admitted and not departed).
    pub fn present_mask(&self) -> u64 {
        (0..self.roster.len() as u32)
            .filter(|i| !self.departed.contains(i) && *i < 64)
            .fold(0u64, |mask, i| mask | (1u64 << i))
    }

    /// Indices ever admitted; retained items from any of them are accepted (R18).
    pub fn roster_mask(&self) -> u64 {
        (0..self.roster.len().min(64) as u32).fold(0u64, |mask, i| mask | (1u64 << i))
    }

    pub fn retained_count(&self) -> usize {
        self.markers.len() + self.tombstones.len()
    }

    pub fn live_markers(&self) -> Vec<ItemId> {
        self.markers.keys().copied().collect()
    }

    /// A digest of the retained set, so the analysis can check every member agrees.
    pub fn digest(&self) -> String {
        let mut hash = 0xcbf2_9ce4_8422_2325u64;
        let mut feed = |text: String| {
            for byte in text.bytes() {
                hash = (hash ^ u64::from(byte)).wrapping_mul(0x0000_0100_0000_01b3);
            }
        };
        for item in self.markers.keys() {
            feed(format!("m{item};"));
        }
        for item in self.tombstones.keys() {
            feed(format!("t{item};"));
        }
        format!("{hash:016x}")
    }

    pub fn summary(&self) -> serde_json::Value {
        serde_json::json!({
            "role": if self.config.leader { "leader" } else { "member" },
            "uid": self.config.uid,
            "state": self.state.name(),
            "index": self.my_index,
            "epoch": self.epoch,
            "revision": self.revision,
            "roster": self.roster,
            "departed": self.departed,
            "live_markers": self.markers.len(),
            "tombstones": self.tombstones.len(),
            "retained_digest": self.digest(),
            "admissions": self.counters.admissions,
            "readmissions": self.counters.readmissions,
            "refused_joins": self.counters.refused_joins,
            "departures_marked": self.counters.departures_marked,
            "catchup_items_sent": self.counters.catchup_items_sent,
            "catchup_requests": self.counters.catchup_requests,
            "markers_placed": self.counters.markers_placed,
            "markers_refused": self.counters.markers_refused,
            "deletes_sent": self.counters.deletes_sent,
            "records_rejected": self.counters.records_rejected,
            "creates_after_tombstone": self.counters.creates_after_tombstone,
        })
    }

    // ------------------------------------------------------------------------- leader

    fn record(&self, t_ns: u64) -> Draft {
        Draft::Record(SessionRecord {
            session: self.config.session,
            leader: self.config.uid.clone(),
            revision: self.revision,
            epoch: self.epoch,
            phase: self.phase,
            end_us: self.config.end_us,
            capacity: self.config.capacity,
            marker_cap: self.config.marker_cap as u32,
            roster: self.roster.clone(),
            departed: self.departed.iter().copied().collect(),
            time_us: self.utc(t_ns),
        })
    }

    fn record_out(&mut self, t_ns: u64) -> Outgoing {
        self.note(
            "record_sent",
            self.revision as i64,
            i64::from(self.epoch),
            self.present_mask().count_ones() as i64,
        );
        Outgoing {
            wire: arc(cot_draft::encode(&self.record(t_ns))),
            class: Class::Urgent,
            kind: 10,
            sequence: self.revision as i64,
            urgent: true,
        }
    }

    /// Queues a catch-up for newcomers: tombstones, then markers, then the barrier.
    fn start_catchup(&mut self, t_ns: u64) {
        if !self.lane.is_empty() {
            return; // one run serves everyone; a later newcomer asks for what it missed
        }
        self.lane.extend(
            self.tombstones
                .keys()
                .map(|item| LaneItem::Tombstone(*item)),
        );
        self.lane
            .extend(self.markers.keys().map(|item| LaneItem::Retained(*item)));
        self.lane.push_back(LaneItem::Barrier);
        self.lane_next_ns = self.lane_next_ns.max(t_ns);
        self.note(
            "catchup_started",
            self.tombstones.len() as i64,
            self.markers.len() as i64,
            0,
        );
    }

    fn present_count(&self) -> u32 {
        self.roster.len() as u32 - self.departed.len() as u32
    }

    fn on_join(&mut self, request: JoinRequest, t_ns: u64, out: &mut Vec<Outgoing>) {
        if self.phase == Phase::Closed {
            return;
        }
        if request.tak_version < 1 || !request.extensions.contains(&AR_EXTENSION_ID) {
            self.counters.refused_joins += 1;
            self.note("refused_join", 1, 0, 0);
            return;
        }
        let known = self.roster.iter().position(|uid| *uid == request.candidate);
        match known {
            Some(index) if self.departed.contains(&(index as u32)) => {
                if self.present_count() >= self.config.capacity {
                    self.counters.refused_joins += 1;
                    self.note("refused_join", 2, index as i64, 0);
                    return;
                }
                self.departed.remove(&(index as u32));
                self.revision += 1;
                self.epoch += 1;
                self.counters.readmissions += 1;
                self.note("readmitted", index as i64, i64::from(self.epoch), 0);
                out.push(self.record_out(t_ns));
                self.start_catchup(t_ns);
            }
            Some(_) => {
                // Already present: it missed the record; resend it, at most twice a second.
                let last = self
                    .record_resent
                    .get(&request.candidate)
                    .copied()
                    .unwrap_or(0);
                if t_ns >= last + RECORD_RESEND_HOLDOFF_NS || last == 0 {
                    self.record_resent.insert(request.candidate.clone(), t_ns);
                    out.push(self.record_out(t_ns));
                }
            }
            None => {
                if self.present_count() >= self.config.capacity {
                    self.counters.refused_joins += 1;
                    self.note("refused_join", 3, 0, 0);
                    return;
                }
                self.roster.push(request.candidate.clone());
                self.revision += 1;
                self.epoch += 1;
                self.counters.admissions += 1;
                self.note(
                    "admitted_member",
                    self.roster.len() as i64 - 1,
                    i64::from(self.epoch),
                    0,
                );
                out.push(self.record_out(t_ns));
                self.start_catchup(t_ns);
            }
        }
    }

    /// A member dropped its own track (HICD leave_session): mark it departed.
    pub fn on_drop_track(&mut self, index: i32, t_ns: u64, out: &mut Vec<Outgoing>) {
        if !self.config.leader || index <= 0 || index as usize >= self.roster.len() {
            return;
        }
        if self.departed.insert(index as u32) {
            self.revision += 1;
            self.epoch += 1;
            self.counters.departures_marked += 1;
            self.note(
                "departure_marked",
                i64::from(index),
                i64::from(self.epoch),
                0,
            );
            out.push(self.record_out(t_ns));
        }
    }

    fn on_catchup_request(&mut self, request: CatchupRequest, t_ns: u64) {
        if !self.config.leader {
            return;
        }
        if request.missing.is_empty() {
            self.start_catchup(t_ns);
            return;
        }
        for item in request.missing {
            let recent = self
                .item_resent
                .get(&item)
                .is_some_and(|&t| t_ns < t + ITEM_RESEND_HOLDOFF_NS);
            if recent {
                continue;
            }
            if self.tombstones.contains_key(&item) {
                self.lane.push_front(LaneItem::Tombstone(item));
            } else if self.markers.contains_key(&item) {
                self.lane.push_front(LaneItem::Retained(item));
            } else {
                continue;
            }
            self.item_resent.insert(item, t_ns);
        }
        self.lane_next_ns = self.lane_next_ns.max(t_ns.saturating_sub(1));
    }

    /// The next catch-up frame due on the lane, if any; call `lane_sent` once it goes out.
    pub fn lane_next(&mut self, t_ns: u64) -> Option<Outgoing> {
        if !self.config.leader || t_ns < self.lane_next_ns {
            return None;
        }
        loop {
            match self.lane.front()? {
                LaneItem::Tombstone(item) => match self.tombstones.get(item) {
                    Some(wire) => {
                        return Some(Outgoing {
                            wire: wire.clone(),
                            class: Class::Repair,
                            kind: 14,
                            sequence: item.sequence,
                            urgent: false,
                        })
                    }
                    None => {
                        self.lane.pop_front();
                    }
                },
                LaneItem::Retained(item) => match self.markers.get(item) {
                    Some(wire) => {
                        return Some(Outgoing {
                            wire: wire.clone(),
                            class: Class::Repair,
                            kind: 0,
                            sequence: item.sequence,
                            urgent: false,
                        })
                    }
                    None => {
                        self.lane.pop_front(); // deleted since; its tombstone covers it
                    }
                },
                LaneItem::Barrier => {
                    let barrier = Draft::Barrier(Barrier {
                        session: self.config.session,
                        sender: self.config.uid.clone(),
                        revision: self.revision,
                        markers: self.markers.keys().copied().collect(),
                        tombstones: self.tombstones.keys().copied().collect(),
                        time_us: self.utc(t_ns),
                    });
                    return Some(Outgoing {
                        wire: arc(cot_draft::encode(&barrier)),
                        class: Class::Urgent,
                        kind: 12,
                        sequence: (self.markers.len() + self.tombstones.len()) as i64,
                        urgent: true,
                    });
                }
            }
        }
    }

    pub fn lane_sent(&mut self, t_ns: u64, bits: u64) {
        match self.lane.pop_front() {
            Some(LaneItem::Barrier) => self.note(
                "barrier_sent",
                self.markers.len() as i64,
                self.tombstones.len() as i64,
                self.revision as i64,
            ),
            Some(_) => {
                self.counters.catchup_items_sent += 1;
                self.lane_next_ns = t_ns + (bits as f64 / self.config.catchup_bps * 1e9) as u64;
            }
            None => {}
        }
    }

    // ------------------------------------------------------------------------- everyone

    fn apply_record(&mut self, record: SessionRecord, t_ns: u64) {
        if record.session != self.config.session {
            return;
        }
        match &self.leader_uid {
            Some(leader) if *leader != record.leader => {
                self.counters.records_rejected += 1;
                return;
            }
            None => self.leader_uid = Some(record.leader.clone()),
            _ => {}
        }
        if self.config.leader || record.revision <= self.revision {
            return;
        }
        self.revision = record.revision;
        self.epoch = record.epoch;
        self.phase = record.phase;
        self.roster = record.roster.clone();
        self.departed = record.departed.iter().copied().collect();
        self.note(
            "record_applied",
            self.revision as i64,
            i64::from(self.epoch),
            self.present_mask().count_ones() as i64,
        );
        if self.phase == Phase::Closed && !matches!(self.state, State::Closed) {
            self.state = State::Closed;
            self.note("closed", self.revision as i64, 0, 0);
            return;
        }
        let mine = self.roster.iter().position(|uid| *uid == self.config.uid);
        let present = mine.is_some_and(|i| !self.departed.contains(&(i as u32)));
        if self.state == State::Candidate && present {
            let index = mine.unwrap_or(0) as i32;
            self.my_index = Some(index);
            self.state = State::Admitted;
            self.admitted_at_ns = t_ns;
            self.admitted_epoch = self.epoch;
            self.barrier = None;
            self.catchup_complete_ns = None;
            self.heard_at_epoch.clear();
            self.next_catchup_request_ns = t_ns + self.config.catchup_window_ns + 2_000_000_000;
            self.note(
                "admitted",
                i64::from(index),
                i64::from(self.epoch),
                self.first_join_ns.map(|t| (t_ns - t) as i64).unwrap_or(-1),
            );
        }
    }

    /// A marker create heard on the air (first copy, repair or catch-up).
    pub fn on_marker(&mut self, item: ItemId, wire: &[u8]) {
        if self.tombstones.contains_key(&item) {
            self.counters.creates_after_tombstone += 1;
            return;
        }
        self.markers
            .entry(item)
            .or_insert_with(|| arc(wire.to_vec()));
    }

    fn apply_delete(&mut self, delete: &MarkerDelete, wire: &[u8]) {
        self.markers.remove(&delete.target);
        self.tombstones
            .entry(delete.target)
            .or_insert_with(|| arc(wire.to_vec()));
    }

    /// A network summary from a member, with the roster epoch it holds.
    pub fn on_summary(&mut self, sender: i32, epoch: u32) {
        if self.state == State::Admitted && epoch >= self.admitted_epoch {
            self.heard_at_epoch.insert(sender);
        }
    }

    pub fn on_draft(&mut self, draft: Draft, wire: &[u8], t_ns: u64, out: &mut Vec<Outgoing>) {
        if draft.session() != self.config.session
            || matches!(self.state, State::Waiting | State::Left)
        {
            return;
        }
        match draft {
            Draft::Record(record) => self.apply_record(record, t_ns),
            Draft::Join(request) => {
                if self.config.leader && request.candidate != self.config.uid {
                    self.on_join(request, t_ns, out);
                }
            }
            Draft::Barrier(barrier) => {
                if self.state == State::Admitted && self.barrier.is_none() {
                    self.note(
                        "barrier_heard",
                        barrier.markers.len() as i64,
                        barrier.tombstones.len() as i64,
                        (t_ns - self.admitted_at_ns) as i64,
                    );
                    self.barrier = Some(barrier);
                    self.next_catchup_request_ns = t_ns;
                }
            }
            Draft::CatchupRequest(request) => self.on_catchup_request(request, t_ns),
            Draft::Delete(delete) => self.apply_delete(&delete, wire),
        }
    }

    /// Items the barrier lists that this member does not hold yet.
    fn missing(&self) -> Vec<ItemId> {
        let Some(barrier) = &self.barrier else {
            return Vec::new();
        };
        let held =
            |item: &ItemId| self.markers.contains_key(item) || self.tombstones.contains_key(item);
        barrier
            .tombstones
            .iter()
            .filter(|item| !self.tombstones.contains_key(item))
            .chain(barrier.markers.iter().filter(|item| !held(item)))
            .copied()
            .collect()
    }

    /// Advances the session; returns the frames it wants sent now.
    pub fn tick(&mut self, t_ns: u64, out: &mut Vec<Outgoing>) {
        // Leader: close at the end.
        if self.config.leader {
            if let Some(close_at) = self.config.close_at_ns {
                if t_ns >= close_at && !self.close_sent {
                    self.close_sent = true;
                    self.phase = Phase::Closed;
                    self.revision += 1;
                    out.push(self.record_out(t_ns));
                    self.note("close_sent", self.revision as i64, 0, 0);
                    self.state = State::Closed;
                }
            }
            if self.revision == 1
                && self.epoch == 1
                && !self.close_sent
                && self.counters.admissions == 0
            {
                // Announce the session once at start (and again if nobody has joined yet).
                let last = self.record_resent.get("").copied();
                if last.is_none_or(|t| t_ns >= t + JOIN_RETRY_NS) {
                    self.record_resent.insert(String::new(), t_ns);
                    out.push(self.record_out(t_ns));
                }
            }
            return;
        }
        // Member: leave and rejoin on schedule.
        if let Some(leave_at) = self.config.leave_at_ns {
            if t_ns >= leave_at && matches!(self.state, State::Ready | State::Admitted) {
                self.state = State::Left;
                self.note(
                    "left",
                    i64::from(self.my_index.unwrap_or(-1)),
                    i64::from(self.epoch),
                    0,
                );
                self.config.leave_at_ns = None;
                return;
            }
        }
        if let Some(rejoin_at) = self.config.rejoin_at_ns {
            if t_ns >= rejoin_at && self.state == State::Left {
                self.state = State::Candidate;
                self.next_join_ns = t_ns;
                self.first_join_ns = None;
                self.config.rejoin_at_ns = None;
                self.note("rejoining", 0, 0, 0);
            }
        }
        match self.state {
            State::Waiting if t_ns >= self.config.join_at_ns => {
                self.state = State::Candidate;
                self.next_join_ns = t_ns;
            }
            State::Candidate if t_ns >= self.next_join_ns => {
                let request = Draft::Join(JoinRequest {
                    session: self.config.session,
                    candidate: self.config.uid.clone(),
                    tak_version: 1,
                    extensions: vec![AR_EXTENSION_ID],
                    time_us: self.utc(t_ns),
                });
                out.push(Outgoing {
                    wire: arc(cot_draft::encode(&request)),
                    class: Class::Control,
                    kind: 11,
                    sequence: 0,
                    urgent: false,
                });
                let first = *self.first_join_ns.get_or_insert(t_ns);
                if t_ns - first > LEADER_ABSENT_NS
                    && (t_ns - first) % LEADER_ABSENT_NS < JOIN_RETRY_NS
                {
                    self.note("leader_absent", ((t_ns - first) / 1_000_000) as i64, 0, 0);
                }
                self.note("join_sent", 0, 0, 0);
                self.next_join_ns = t_ns + JOIN_RETRY_NS;
            }
            State::Admitted => self.admitted_tick(t_ns, out),
            _ => {}
        }
    }

    fn admitted_tick(&mut self, t_ns: u64, out: &mut Vec<Outgoing>) {
        let me = self.my_index.unwrap_or(0);
        let missing = self.missing();
        if self.barrier.is_some() && missing.is_empty() && self.catchup_complete_ns.is_none() {
            self.catchup_complete_ns = Some(t_ns);
            self.note(
                "catchup_complete",
                (t_ns - self.admitted_at_ns) as i64,
                self.markers.len() as i64,
                self.tombstones.len() as i64,
            );
        }
        if t_ns >= self.next_catchup_request_ns && self.catchup_complete_ns.is_none() {
            // After the barrier, ask for what it lists and we lack; with no barrier in time, ask for everything.
            let wanted: Vec<ItemId> = missing.into_iter().take(MAX_REQUEST_ITEMS).collect();
            if self.barrier.is_none() || !wanted.is_empty() {
                let request = Draft::CatchupRequest(CatchupRequest {
                    session: self.config.session,
                    requester: self.config.uid.clone(),
                    missing: wanted.clone(),
                    time_us: self.utc(t_ns),
                });
                out.push(Outgoing {
                    wire: arc(cot_draft::encode(&request)),
                    class: Class::Control,
                    kind: 13,
                    sequence: wanted.len() as i64,
                    urgent: false,
                });
                self.counters.catchup_requests += 1;
                self.note(
                    "catchup_request_sent",
                    wanted.len() as i64,
                    i64::from(self.barrier.is_some()),
                    0,
                );
            }
            self.next_catchup_request_ns = t_ns + CATCHUP_REQUEST_RETRY_NS;
        }
        let Some(complete) = self.catchup_complete_ns else {
            return;
        };
        let earlier: Vec<i32> = (0..me)
            .filter(|i| !self.departed.contains(&(*i as u32)))
            .collect();
        let all_heard = earlier.iter().all(|i| self.heard_at_epoch.contains(i));
        let timed_out = t_ns >= complete + self.config.catchup_window_ns;
        if all_heard || timed_out {
            self.state = State::Ready;
            self.note(
                if all_heard {
                    "ready"
                } else {
                    "ready_by_timeout"
                },
                (t_ns - self.admitted_at_ns) as i64,
                earlier.len() as i64,
                self.heard_at_epoch.len() as i64,
            );
        }
    }

    // ------------------------------------------------------------------------- markers (L7)

    pub fn can_place(&self) -> bool {
        self.ready() && self.retained_count() < self.config.marker_cap
    }

    pub fn placed(&mut self, item: ItemId, wire: &[u8]) {
        self.counters.markers_placed += 1;
        self.on_marker(item, wire);
    }

    pub fn refused_marker(&mut self) {
        self.counters.markers_refused += 1;
    }

    /// Builds this member's delete of `target`, applies it locally, and returns it to send.
    pub fn delete(&mut self, target: ItemId, t_ns: u64) -> Option<Outgoing> {
        if !self.ready() || !self.markers.contains_key(&target) {
            return None;
        }
        self.delete_sequence += 1;
        let delete = MarkerDelete {
            session: self.config.session,
            deleter: self.my_index?,
            sequence: self.delete_sequence,
            target,
            time_us: self.utc(t_ns),
        };
        let wire = cot_draft::encode(&Draft::Delete(delete.clone()));
        self.apply_delete(&delete, &wire);
        self.counters.deletes_sent += 1;
        Some(Outgoing {
            wire: arc(wire),
            class: Class::Urgent,
            kind: 14,
            sequence: target.sequence,
            urgent: true,
        })
    }

    pub fn leaving_now(&self) -> bool {
        self.state == State::Left
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config(uid: &str, leader: bool) -> SessionConfig {
        SessionConfig {
            session: 0x1234_5678,
            uid: uid.into(),
            leader,
            capacity: 8,
            marker_cap: 50,
            catchup_bps: 24_000.0,
            catchup_window_ns: 10_000_000_000,
            join_at_ns: 0,
            leave_at_ns: None,
            rejoin_at_ns: None,
            close_at_ns: None,
            end_us: 0,
        }
    }

    /// Delivers every frame to every other session, as a lossless multicast group would.
    fn deliver(
        sessions: &mut [Session],
        from: usize,
        frames: Vec<Outgoing>,
        t: u64,
    ) -> Vec<(usize, Outgoing)> {
        let mut replies = Vec::new();
        for frame in frames {
            for (index, session) in sessions.iter_mut().enumerate() {
                if index == from {
                    continue;
                }
                if let Ok(draft) = cot_draft::decode(&frame.wire) {
                    let mut out = Vec::new();
                    session.on_draft(draft, &frame.wire, t, &mut out);
                    replies.extend(out.into_iter().map(|o| (index, o)));
                }
            }
        }
        replies
    }

    #[test]
    fn two_newcomers_admitted_together_both_become_ready() {
        let mut sessions = vec![
            Session::new(config("00000001", true), 0),
            Session::new(config("00000002", false), 0),
            Session::new(config("00000003", false), 0),
        ];
        let ms = 1_000_000;
        for step in 0..3000u64 {
            let t = step * ms;
            for index in 0..sessions.len() {
                let mut out = Vec::new();
                sessions[index].tick(t, &mut out);
                if index == 0 {
                    while let Some(frame) = sessions[0].lane_next(t) {
                        sessions[0].lane_sent(t, (frame.wire.len() as u64 + 56) * 8);
                        out.push(frame);
                    }
                }
                let mut pending = vec![(index, out)];
                while let Some((from, frames)) = pending.pop() {
                    for (by, reply) in deliver(&mut sessions, from, frames, t) {
                        pending.push((by, vec![reply]));
                    }
                }
            }
            // Ready members send summaries every 300 ms at their epoch.
            if step % 300 == 0 {
                let senders: Vec<(i32, u32)> = sessions
                    .iter()
                    .filter(|s| s.ready())
                    .filter_map(|s| s.index().map(|i| (i, s.epoch())))
                    .collect();
                for session in sessions.iter_mut() {
                    for (index, epoch) in &senders {
                        if session.index() != Some(*index) {
                            session.on_summary(*index, *epoch);
                        }
                    }
                }
            }
        }
        for session in &sessions {
            assert_eq!(session.state, State::Ready, "{}", session.config.uid);
        }
        assert_eq!(sessions[1].index(), Some(1));
        assert_eq!(sessions[2].index(), Some(2));
    }

    #[test]
    fn a_tombstone_outranks_a_late_create() {
        let mut session = Session::new(config("00000001", true), 0);
        let item = ItemId {
            owner: 1,
            sequence: 5,
        };
        let delete = MarkerDelete {
            session: 0x1234_5678,
            deleter: 2,
            sequence: 1,
            target: item,
            time_us: 0,
        };
        session.apply_delete(&delete, b"delete");
        session.on_marker(item, b"create");
        assert_eq!(session.live_markers(), Vec::<ItemId>::new());
        assert_eq!(session.retained_count(), 1);
        assert_eq!(session.counters.creates_after_tombstone, 1);
    }
}
