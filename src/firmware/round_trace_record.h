// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>

#include "infra/diag_log.h"
#include "infra/json_name.h"
#include "infra/json_out.h"
#include "infra/message_sum.h"
#include "reactor/round_trace.h"
#include "reactor/types.h"

namespace mister::fw {

struct RoundTraceRecord {
    static constexpr infra::JsonName kKey{"rtflight"};
    using View = reactor::RoundTrace::Round;
};

constexpr void to_json(infra::JsonOut& o, const reactor::RoundTrace::Round& r) noexcept {
    o.field("rs", r.seq);
    o.field("t0", r.start.time_since_epoch().count());
    o.field("t1", r.end.time_since_epoch().count());
    o.field("wk", r.wake.count());
    o.field("sc", r.cause.seg);
    o.field("si", r.cause.index);
    o.field("lk", r.cause.lifecycle);
    o.field("ml", r.marks_lost);
    o.begin_array("m");
    for (const reactor::RoundTrace::Mark& m : r.marks) {
        if (m.at.seg == reactor::RoundSegment::None)
            break;
        o.elem(infra::ordinal(m.at.seg));
        o.elem(m.at.index);
        o.elem(m.took.count());
    }
    o.end_array();
    o.begin_array("p");
    for (const reactor::RoundTrace::LeadUp& l : r.lead) {
        if (l.start == reactor::MonoInstant{})
            continue;
        o.elem(l.start.time_since_epoch().count());
        o.elem(l.took.count());
        o.elem(infra::ordinal(l.cause.seg));
        o.elem(l.cause.index);
        o.elem(l.cause.lifecycle);
    }
    o.end_array();
}

[[nodiscard]] consteval bool round_trace_fits() {
    reactor::RoundTrace::Round full{};
    for (reactor::RoundTrace::Mark& m : full.marks)
        m.at.seg = reactor::RoundSegment::Tail;
    for (reactor::RoundTrace::LeadUp& l : full.lead)
        l.start = reactor::MonoInstant{std::chrono::nanoseconds{1}};
    infra::JsonOut o = infra::JsonOut::measuring();
    to_json(o, full);
    return infra::json_record_worst(RoundTraceRecord::kKey, o.need())
        <= xthread::DiagLog::kMaxLine - 1;
}

static_assert(round_trace_fits(), "the widest rtflight line must fit the xthread::DiagLog ceiling");

}
