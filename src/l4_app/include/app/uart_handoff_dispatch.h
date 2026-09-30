// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/uart_mode.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept UartHandoffSink = requires(S& s, const UartHandoff& m) {
    s.on(std::declval<const UartHandoff::Mode&>());
    s.on(std::declval<const UartHandoff::MidiLink&>());
    s.misrouted(m);
};

using UartHandoffRoutes = infra::AllRouted;

}  // namespace mister::app
