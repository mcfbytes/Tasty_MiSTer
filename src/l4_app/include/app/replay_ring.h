// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "app/replay_msg.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

namespace mister::app {

class ReplayRing {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr std::size_t kSlots = 256;
    using Ring = xthread::SpscRing<ReplayMsg, kSlots>;
    using Sender = Ring::Producer;
    using Receiver = Ring::Consumer;

    [[nodiscard]] Sender sender() noexcept { return ring_.producer(); }

    [[nodiscard]] Receiver receiver() noexcept { return ring_.consumer(); }

    [[nodiscard]] Ring& ring_for_test() noexcept { return ring_; }

private:
    Ring ring_{};
};

}
