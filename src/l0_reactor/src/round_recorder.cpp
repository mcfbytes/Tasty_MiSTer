// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/round_recorder.h"

#include <optional>

namespace mister::reactor {

void RoundRecorder::record(MonoInstant finish, RoundTiming::Blame blame) noexcept {
    if (!blame.lifecycle_marks.empty())
        push_<RoundTrace::SlowLifecycle>(finish, blame);
    else
        push_<RoundTrace::SlowSteady>(finish, blame);
}

template <class A>
void RoundRecorder::push_(MonoInstant finish, RoundTiming::Blame blame) noexcept {
    if (!tx_.admit<A>())
        return;
    A a{.round = {
            .start = start_,
            .end = finish,
            .seq = seq_,
            .wake = wake_,
            .blame = blame,
            .marks_lost = marks_lost_
        }};
    for (RoundTrace::Mark& m : a.round.marks) {
        const std::optional<RoundTrace::Mark> kept = marks_.pop();
        if (!kept)
            break;
        m = *kept;
    }

    RoundSeq at = seq_;
    for (RoundTrace::LeadUp& l : a.round.lead) {
        l = lead_[at.lead_slot<RoundTrace::kLeadUp>()];
        at = at.next();
    }
    tx_.push(a);
}

}
