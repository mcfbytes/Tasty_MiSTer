// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/event.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept EventUiSink = requires(S& s, const Event& m, const Event::Head& h) {
    s.on(std::declval<const Event::CoreLoaded&>(), h);
    s.on(std::declval<const Event::SessionFailed&>(), h);
    s.on(std::declval<const Event::SdActivity&>(), h);
    s.on(std::declval<const Event::InfoRequest&>(), h);
    s.on(std::declval<const Event::ProgressUpdate&>(), h);
    s.on(std::declval<const Event::DeadlineMiss&>(), h);
    s.on(std::declval<const Event::RequestRefused&>(), h);
    s.on(std::declval<const Event::ConfStrOnlySession&>(), h);
    s.on(std::declval<const Event::SessionAdvisory&>(), h);
    s.on(std::declval<const Event::SessionEnded&>(), h);
    s.misrouted(m);
};

using EventRoutes = infra::AllRouted;

}  // namespace mister::app
