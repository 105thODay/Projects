//! The NET-002 rolling ledger with the Network ICD's admission rules (section 7).
//!
//! Every frame is charged at its start, payload plus 56 B (UDP/IPv4 and authentication),
//! and the sum of charges with start times in (t - 1 s, t] stays at or below the limit at
//! every instant. Admission keeps the urgent reserve for urgent frames only; state and
//! summaries may not spend the repair share; repair may spend room the others leave unused.

use std::collections::VecDeque;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Class {
    Urgent,
    State,
    Control,
    Repair,
}

impl Class {
    pub fn name(self) -> &'static str {
        match self {
            Class::Urgent => "urgent",
            Class::State => "state",
            Class::Control => "control",
            Class::Repair => "repair",
        }
    }
}

pub struct Ledger {
    limit_bits: u64,
    urgent_reserve_bits: u64,
    repair_share_bits: u64,
    window_ns: u64,
    entries: VecDeque<(u64, u64, Class)>,
    total: u64,
    urgent: u64,
    repair: u64,
    pub max_total: u64,
    pub violations: u64,
}

impl Ledger {
    pub fn new(limit_bits: u64, urgent_reserve_bits: u64, repair_share_bits: u64) -> Ledger {
        Ledger {
            limit_bits,
            urgent_reserve_bits,
            repair_share_bits,
            window_ns: 1_000_000_000,
            entries: VecDeque::new(),
            total: 0,
            urgent: 0,
            repair: 0,
            max_total: 0,
            violations: 0,
        }
    }

    fn expire(&mut self, now_ns: u64) {
        while let Some(&(start, bits, class)) = self.entries.front() {
            if start + self.window_ns > now_ns {
                break;
            }
            self.entries.pop_front();
            self.total -= bits;
            match class {
                Class::Urgent => self.urgent -= bits,
                Class::Repair => self.repair -= bits,
                _ => {}
            }
        }
    }

    /// Whether a frame of `bits` in `class` may start now.
    pub fn admits(&mut self, now_ns: u64, bits: u64, class: Class) -> bool {
        self.expire(now_ns);
        if self.total + bits > self.limit_bits {
            return false;
        }
        if class == Class::Urgent {
            return true;
        }
        let non_urgent = self.total - self.urgent;
        if non_urgent + bits > self.limit_bits.saturating_sub(self.urgent_reserve_bits) {
            return false;
        }
        if class != Class::Repair {
            let others = non_urgent - self.repair;
            let room = self
                .limit_bits
                .saturating_sub(self.urgent_reserve_bits)
                .saturating_sub(self.repair_share_bits);
            if others + bits > room {
                return false;
            }
        }
        true
    }

    /// Charges a frame that started now.
    pub fn charge(&mut self, now_ns: u64, bits: u64, class: Class) {
        self.expire(now_ns);
        self.entries.push_back((now_ns, bits, class));
        self.total += bits;
        match class {
            Class::Urgent => self.urgent += bits,
            Class::Repair => self.repair += bits,
            _ => {}
        }
        self.max_total = self.max_total.max(self.total);
        if self.total > self.limit_bits {
            self.violations += 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn state_cannot_spend_the_urgent_reserve_or_the_repair_share() {
        let mut ledger = Ledger::new(200_000, 86_184, 32_000);
        let frame = 412 * 8;
        let mut sent = 0;
        while ledger.admits(0, frame, Class::State) {
            ledger.charge(0, frame, Class::State);
            sent += frame;
        }
        assert!(sent <= 200_000 - 86_184 - 32_000);
        assert!(ledger.admits(0, frame, Class::Repair));
        assert!(ledger.admits(0, frame, Class::Urgent));
    }

    #[test]
    fn the_window_rolls() {
        let mut ledger = Ledger::new(10_000, 0, 0);
        ledger.charge(0, 10_000, Class::State);
        assert!(!ledger.admits(999_999_999, 1, Class::Urgent));
        assert!(ledger.admits(1_000_000_000, 10_000, Class::State));
    }
}
