// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/round_recorder.h"

#include <optional>

namespace mister::reactor {

void RoundRecorder::record(MonoInstant finish, RoundTiming::Cause cause) noexcept {
    if (cause.lifecycle != 0)
        push_<RoundTrace::SlowLifecycle>(finish, cause);
    else
        push_<RoundTrace::SlowSteady>(finish, cause);
}

template <class A>
void RoundRecorder::push_(MonoInstant finish, RoundTiming::Cause cause) noexcept {
    if (!tx_.admit<A>())
        return;
    A a{.round = {
            .start = start_,
            .end = finish,
            .seq = seq_,
            .wake = wake_,
            .cause = cause,
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
