// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/message_sum.h"
#include "svc/nic_control.h"

namespace mister::svc {

template <class S>
concept NicControlSink = requires(S& s, const NicControl& m) {
    s.on(std::declval<const NicControl::Start&>());
    s.on(std::declval<const NicControl::Stop&>());
    s.misrouted(m);
};

using NicControlRoutes = infra::AllRouted;

}
