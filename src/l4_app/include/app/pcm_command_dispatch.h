// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/pcm_command.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept PcmCommandSink = requires(S& s, const PcmCommand& m) {
    s.on(std::declval<const PcmCommand::Play&>());
    s.on(std::declval<const PcmCommand::Stop&>());
    s.on(std::declval<const PcmCommand::Resume&>());
    s.misrouted(m);
};

using PcmCommandRoutes = infra::AllRouted;

}  // namespace mister::app
