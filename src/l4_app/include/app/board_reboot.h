// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/board_ops.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::hal {
class IBootHandoff;
}

namespace mister::app {

class BoardReboot final {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr unsigned kSettleMs = 500;

    BoardReboot(infra::OptRef<hal::IBootHandoff> handoff, infra::OptRef<IBoardReset> reset,
                infra::OptRef<ISleeper> sleeper) noexcept
        : handoff_(handoff), reset_(reset), sleeper_(sleeper) {}

    [[nodiscard]] Ex<void> reboot(bool cold);

    [[nodiscard]] std::uint32_t reboots() const noexcept { return reboots_; }

private:
    infra::OptRef<hal::IBootHandoff> handoff_{};
    infra::OptRef<IBoardReset> reset_{};
    infra::OptRef<ISleeper> sleeper_{};
    std::uint32_t reboots_ = 0;
};

}  // namespace mister::app
