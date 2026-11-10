// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/fixed_ring.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "reactor/round_timing.h"
#include "reactor/round_trace.h"
#include "reactor/types.h"

namespace mister::reactor {

class RoundRecorder {
    TASTY_SEAT_RESIDENT(RT);

public:
    RoundRecorder() = default;
    RoundRecorder(const RoundRecorder&) = delete;
    RoundRecorder& operator=(const RoundRecorder&) = delete;

    void begin(MonoInstant start) noexcept {
        seq_ = seq_.next();
        start_ = start;
        wake_ = RoundSpan::zero();
        marks_lost_ = false;
        marks_.clear();
    }

    void wake(RoundSpan latency) noexcept { wake_ = latency; }

    void mark(RoundTiming::Blame at, RoundSpan took) noexcept {
        if (!marks_.push(RoundTrace::Mark{at, took}))
            marks_lost_ = true;
    }

    [[gnu::cold]] void record(MonoInstant finish, RoundTiming::Blame blame) noexcept;

    void end(MonoInstant finish, RoundTiming::Blame blame) noexcept {
        const RoundSpan took = round_span(finish - start_);
        lead_[seq_.lead_slot<RoundTrace::kLeadUp>()] = RoundTrace::LeadUp{start_, took, blame};
    }

    [[nodiscard]] RoundTraceLane& lane() noexcept { return lane_; }

private:
    template <class A>
    void push_(MonoInstant finish, RoundTiming::Blame blame) noexcept;

    RoundTraceLane lane_{};
    RoundTraceLane::Producer tx_ = lane_.producer();
    infra::IndexedArray<RoundTrace::LeadUp, RoundTrace::kLeadUp, LeadUpSlot> lead_{};
    infra::FixedRing<RoundTrace::Mark, RoundTrace::kMarks> marks_{};
    MonoInstant start_{};
    RoundSeq seq_{};
    RoundSpan wake_{};
    bool marks_lost_ = false;
};

}
