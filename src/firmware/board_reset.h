// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "app/board_ops.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::hal {
class IBridgeSequencer;
}

namespace mister::fw {

class BoardReset final : public app::IBoardReset {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::chrono::milliseconds kResetSettle{5000};

    static constexpr std::chrono::milliseconds kPollSlice{100};

    explicit BoardReset(
        hal::IBridgeSequencer& bridges,
        infra::OptRef<app::ISleeper> sleeper = {}
    ) noexcept
        : bridges_(bridges),
          sleeper_(sleeper) {}

    BoardReset(const BoardReset&) = delete;
    BoardReset& operator=(const BoardReset&) = delete;

    void board_reset() override;

private:
    hal::IBridgeSequencer& bridges_;
    infra::OptRef<app::ISleeper> sleeper_{};
};

}
