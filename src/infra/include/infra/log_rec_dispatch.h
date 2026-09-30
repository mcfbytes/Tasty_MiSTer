// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/log_rec.h"
#include "infra/message_sum.h"

namespace mister {

template <class S>
concept LogRecSink = requires(S& s, const LogRec& m, const LogRec::Head& h) {
    s.on(std::declval<const LogRec::DeadlineMiss&>(), h);
    s.on(std::declval<const LogRec::SessionTransition&>(), h);
    s.on(std::declval<const LogRec::StagingRung&>(), h);
    s.on(std::declval<const LogRec::CoreLoad&>(), h);
    s.on(std::declval<const LogRec::CoreUnload&>(), h);
    s.on(std::declval<const LogRec::Mount&>(), h);
    s.on(std::declval<const LogRec::Unmount&>(), h);
    s.on(std::declval<const LogRec::Refusal&>(), h);
    s.on(std::declval<const LogRec::RingLoss&>(), h);
    s.on(std::declval<const LogRec::DoorbellBound&>(), h);
    s.on(std::declval<const LogRec::DoorbellPolling&>(), h);
    s.misrouted(m);
};

using LogRecRoutes = infra::AllRouted;

}  // namespace mister
