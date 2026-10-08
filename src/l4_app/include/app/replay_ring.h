// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "app/replay_msg.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

namespace mister::app {

class ReplayRing {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr std::size_t kSlots = 256;

    [[nodiscard]] bool push(const ReplayMsg& msg) noexcept { return ring_.push(msg); }
    [[nodiscard]] std::optional<ReplayMsg> pop() noexcept { return ring_.pop(); }
    [[nodiscard]] std::size_t size() const noexcept { return ring_.size(); }

private:
    xthread::SpscRing<ReplayMsg, kSlots> ring_{};
};

}  // namespace mister::app
