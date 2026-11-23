// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "infra/seat.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/link_op.h"

namespace mister::app {

class RtcCadence final {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit RtcCadence(const os::IClock& clock) noexcept : clock_(clock) {}

    void arm(std::chrono::seconds period) noexcept;
    void disarm() noexcept;

    void restart() noexcept;

    [[nodiscard]] std::optional<proto::LinkOp::SendRtc> due(
        std::chrono::system_clock::time_point wall
    ) const noexcept;

    void sent() noexcept;

private:
    const os::IClock& clock_;
    std::chrono::seconds period_{};
    os::Deadline next_{os::Deadline::immediate()};
};

}
